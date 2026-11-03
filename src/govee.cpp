#include "govee.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>

#include "secrets.h"

namespace govee {
namespace {

const char *CLOUD_BASE = "https://openapi.api.govee.com/router/api/v1";

const IPAddress MULTICAST_IP(239, 255, 255, 250);
const uint16_t SCAN_PORT = 4001;
const uint16_t RESPONSE_PORT = 4002;
const uint16_t CONTROL_PORT = 4003;

// A LAN light is reported offline after missing this many devStatus polls in a row.
const uint8_t OFFLINE_AFTER_MISSED_POLLS = 3;

// Models that accept a Kelvin value over LAN (and even echo it back in devStatus) without changing.
// The cloud API does change them, so color temperature goes through the cloud for these.
const char *const CLOUD_ONLY_COLOR_TEMP_SKUS[] = {"H6056"};

std::vector<Light> registry;
WiFiUDP udp;

int cloudRequest(const char *method, const String &path, const String &body, JsonDocument &response) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, String(CLOUD_BASE) + path);
  http.addHeader("Govee-API-Key", GOVEE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);

  const int status = http.sendRequest(method, body);
  const String text = http.getString();
  http.end();

  if (status != HTTP_CODE_OK) {
    Serial.printf("Govee cloud %s %s failed (HTTP %d): %s\n", method, path.c_str(), status, text.c_str());
    return status;
  }
  if (deserializeJson(response, text)) {
    Serial.printf("Govee cloud %s %s returned unparseable JSON\n", method, path.c_str());
    return -1;
  }
  return status;
}

bool cloudControl(const Light &light, const char *type, const char *instance, JsonVariantConst value) {
  JsonDocument req;
  req["requestId"] = String(millis());
  JsonObject payload = req["payload"].to<JsonObject>();
  payload["sku"] = light.sku;
  payload["device"] = light.id;
  JsonObject capability = payload["capability"].to<JsonObject>();
  capability["type"] = type;
  capability["instance"] = instance;
  capability["value"] = value;

  String body;
  serializeJson(req, body);

  JsonDocument res;
  if (cloudRequest("POST", "/device/control", body, res) != HTTP_CODE_OK) return false;
  return (res["code"] | 200) == 200 && strcmp(res["capability"]["state"]["status"] | "success", "success") == 0;
}

void cloudFetchState(Light &light) {
  JsonDocument req;
  req["requestId"] = String(millis());
  req["payload"]["sku"] = light.sku;
  req["payload"]["device"] = light.id;

  String body;
  serializeJson(req, body);

  JsonDocument res;
  if (cloudRequest("POST", "/device/state", body, res) != HTTP_CODE_OK) return;

  for (JsonObject cap : res["payload"]["capabilities"].as<JsonArray>()) {
    const String instance = cap["instance"] | "";
    JsonVariant value = cap["state"]["value"];
    if (instance == "online") {
      light.online = value.as<bool>();
    } else if (instance == "powerSwitch") {
      light.on = value.as<int>() == 1;
      light.stateKnown = true;
    } else if (instance == "brightness") {
      light.brightness = value.as<uint8_t>();
    } else if (instance == "colorRgb") {
      const uint32_t rgb = value.as<uint32_t>();
      light.r = (rgb >> 16) & 0xFF;
      light.g = (rgb >> 8) & 0xFF;
      light.b = rgb & 0xFF;
    } else if (instance == "colorTemperatureK") {
      light.colorTempK = value.as<uint16_t>();
    }
  }
}

// LAN control is UDP, so "success" means the packet left the ESP32; Govee lights don't acknowledge.
bool lanSend(const Light &light, const JsonDocument &msg) {
  String body;
  serializeJson(msg, body);
  if (!udp.beginPacket(light.ip, CONTROL_PORT)) return false;
  udp.print(body);
  return udp.endPacket() == 1;
}

bool lanCommand(const Light &light, const char *cmd, JsonVariantConst data, bool log = false) {
  JsonDocument msg;
  msg["msg"]["cmd"] = cmd;
  msg["msg"]["data"] = data;
  if (log) {
    String body;
    serializeJson(msg, body);
    Serial.printf("LAN -> %s %s\n", light.ip.toString().c_str(), body.c_str());
  }
  return lanSend(light, msg);
}

// Asks one LAN light for devStatus and logs the reply. Returns false if it didn't answer in time.
bool lanStatus(const Light &light, JsonDocument &status) {
  JsonDocument empty;
  empty.to<JsonObject>();
  lanCommand(light, "devStatus", empty, true);

  char buffer[768];
  const uint32_t start = millis();
  while (millis() - start < 500) {
    if (udp.parsePacket() <= 0 || udp.remoteIP() != light.ip) {
      delay(5);
      continue;
    }
    const int len = udp.read(buffer, sizeof(buffer) - 1);
    if (len <= 0) continue;
    buffer[len] = '\0';
    JsonDocument doc;
    if (deserializeJson(doc, buffer) || doc["msg"]["cmd"] != "devStatus") continue;
    Serial.printf("LAN <- %s %s\n", light.ip.toString().c_str(), buffer);
    status.set(doc["msg"]["data"]);
    return true;
  }
  Serial.printf("LAN <- %s (no devStatus reply)\n", light.ip.toString().c_str());
  return false;
}

// Approximate RGB of white light at a color temperature (Tanner Helland's curve fit).
void kelvinToRgb(uint16_t kelvin, uint8_t &r, uint8_t &g, uint8_t &b) {
  const float t = kelvin / 100.0f;
  float red, green, blue;
  if (t <= 66) {
    red = 255;
    green = 99.4708025861f * logf(t) - 161.1195681661f;
    blue = t <= 19 ? 0 : 138.5177312231f * logf(t - 10) - 305.0447927307f;
  } else {
    red = 329.698727446f * powf(t - 60, -0.1332047592f);
    green = 288.1221695283f * powf(t - 60, -0.0755148492f);
    blue = 255;
  }
  r = constrain(lroundf(red), 0, 255);
  g = constrain(lroundf(green), 0, 255);
  b = constrain(lroundf(blue), 0, 255);
}

bool nearColor(const Light &light, uint8_t r, uint8_t g, uint8_t b) {
  return abs(light.r - r) <= 3 && abs(light.g - g) <= 3 && abs(light.b - b) <= 3;
}

// Reads UDP replies on the response port for `durationMs`, passing each parsed one to `handler`.
template <typename Handler>
void collectReplies(uint32_t durationMs, Handler handler) {
  char buffer[768];
  const uint32_t start = millis();
  while (millis() - start < durationMs) {
    if (udp.parsePacket() <= 0) {
      delay(5);
      continue;
    }
    const IPAddress from = udp.remoteIP();
    const int len = udp.read(buffer, sizeof(buffer) - 1);
    if (len <= 0) continue;
    buffer[len] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buffer)) continue;
    handler(from, doc["msg"].as<JsonObject>());
  }
}

bool lanHandlesColorTemp(const String &sku) {
  for (const char *cloudOnly : CLOUD_ONLY_COLOR_TEMP_SKUS) {
    if (sku == cloudOnly) return false;
  }
  return true;
}

// Sends devStatus to each light (twice for ones that stay quiet) and returns which ones answered.
std::vector<bool> pingLan(const std::vector<Light *> &targets) {
  std::vector<bool> answered(targets.size(), false);
  JsonDocument empty;
  empty.to<JsonObject>();

  for (int attempt = 0; attempt < 2; attempt++) {
    bool anyWaiting = false;
    for (size_t i = 0; i < targets.size(); i++) {
      if (answered[i]) continue;
      lanCommand(*targets[i], "devStatus", empty);
      anyWaiting = true;
    }
    if (!anyWaiting) break;
    collectReplies(attempt == 0 ? 300 : 500, [&](const IPAddress &from, JsonObject msg) {
      if (msg["cmd"] != "devStatus") return;
      for (size_t i = 0; i < targets.size(); i++) {
        if (targets[i]->ip == from) answered[i] = true;
      }
    });
  }

  for (size_t i = 0; i < targets.size(); i++) {
    targets[i]->online = answered[i];
    targets[i]->missedStatusPolls = answered[i] ? 0 : OFFLINE_AFTER_MISSED_POLLS;
  }
  return answered;
}

void fetchCloudDevices() {
  JsonDocument res;
  if (cloudRequest("GET", "/user/devices", "", res) != HTTP_CODE_OK) return;

  for (JsonObject device : res["data"].as<JsonArray>()) {
    const String id = device["device"] | "";
    if (id.isEmpty() || find(id)) continue;

    Light light;
    light.id = id;
    light.sku = device["sku"] | "";
    light.name = device["deviceName"] | light.sku;
    light.inCloud = true;
    light.lanColorTemp = lanHandlesColorTemp(light.sku);
    light.supportsColorTemp = false;
    for (JsonObject cap : device["capabilities"].as<JsonArray>()) {
      if (cap["instance"] != "colorTemperatureK") continue;
      light.supportsColorTemp = true;
      light.minColorTempK = cap["parameters"]["range"]["min"] | light.minColorTempK;
      light.maxColorTempK = cap["parameters"]["range"]["max"] | light.maxColorTempK;
    }
    registry.push_back(light);
  }
}

}  // namespace

void begin() { udp.begin(RESPONSE_PORT); }

std::vector<Light> &lights() { return registry; }

Light *find(const String &id) {
  for (Light &light : registry) {
    if (light.id == id) return &light;
  }
  return nullptr;
}

void rescanLan() {
  udp.beginPacket(MULTICAST_IP, SCAN_PORT);
  udp.print(R"({"msg":{"cmd":"scan","data":{"account_topic":"reserve"}}})");
  udp.endPacket();

  collectReplies(2000, [](const IPAddress &, JsonObject msg) {
    if (msg["cmd"] != "scan") return;
    JsonObject data = msg["data"];
    const String id = data["device"] | "";
    if (id.isEmpty()) return;

    Light *light = find(id);
    if (!light) {
      Light added;
      added.id = id;
      added.sku = data["sku"] | "";
      added.name = added.sku;
      added.lanColorTemp = lanHandlesColorTemp(added.sku);
      added.supportsColorTemp = added.lanColorTemp;
      registry.push_back(added);
      light = &registry.back();
    }
    light->ip.fromString(data["ip"] | "");
    light->lan = true;
  });
}

void refreshLanState() {
  JsonDocument empty;
  empty.to<JsonObject>();
  for (const Light &light : registry) {
    if (light.lan) lanCommand(light, "devStatus", empty);
  }

  std::vector<bool> answered(registry.size(), false);
  collectReplies(400, [&answered](const IPAddress &from, JsonObject msg) {
    if (msg["cmd"] != "devStatus") return;
    for (size_t i = 0; i < registry.size(); i++) {
      Light &light = registry[i];
      if (!light.lan || light.ip != from) continue;
      answered[i] = true;
      JsonObject data = msg["data"];
      light.on = (data["onOff"] | 0) == 1;
      light.brightness = data["brightness"] | light.brightness;
      light.r = data["color"]["r"] | light.r;
      light.g = data["color"]["g"] | light.g;
      light.b = data["color"]["b"] | light.b;
      const uint16_t reportedK = data["colorTemInKelvin"] | 0;
      if (reportedK == 0 && light.emulatedColorTempK) {
        uint8_t r, g, b;
        kelvinToRgb(light.emulatedColorTempK, r, g, b);
        if (!nearColor(light, r, g, b)) light.emulatedColorTempK = 0;  // changed elsewhere, e.g. the Govee app
      } else {
        light.emulatedColorTempK = 0;
      }
      light.colorTempK = light.emulatedColorTempK ? light.emulatedColorTempK : reportedK;
      light.stateKnown = true;
    }
  });

  for (size_t i = 0; i < registry.size(); i++) {
    Light &light = registry[i];
    if (!light.lan) continue;
    if (answered[i]) {
      light.missedStatusPolls = 0;
      light.online = true;
    } else if (light.missedStatusPolls < OFFLINE_AFTER_MISSED_POLLS && ++light.missedStatusPolls == OFFLINE_AFTER_MISSED_POLLS) {
      light.online = false;
      Serial.printf("%s stopped answering on the LAN, reporting it offline\n", light.name.c_str());
    }
  }
}

void refreshCloudState() {
  for (Light &light : registry) {
    if (!light.lan && light.inCloud) cloudFetchState(light);
  }
}

void discover() {
  fetchCloudDevices();
  rescanLan();
  refreshCloudState();
  refreshLanState();

  Serial.printf("Managing %u light(s):\n", registry.size());
  for (const Light &light : registry) {
    Serial.printf("- %-24s %-8s %-15s%s\n", light.name.c_str(), light.sku.c_str(),
                  light.lan ? light.ip.toString().c_str() : "cloud", light.online ? "" : " (offline)");
  }
}

bool setPower(Light &light, bool on) {
  bool ok = true;
  if (light.lan) {
    JsonDocument data;
    data["value"] = on ? 1 : 0;
    ok = lanCommand(light, "turn", data);
  } else {
    JsonDocument value;
    value.set(on ? 1 : 0);
    ok = cloudControl(light, "devices.capabilities.on_off", "powerSwitch", value);
  }
  if (ok) {
    light.on = on;
    light.stateKnown = true;
  }
  return ok;
}

bool setBrightness(Light &light, uint8_t percent) {
  percent = constrain(percent, 1, 100);
  bool ok = true;
  if (light.lan) {
    JsonDocument data;
    data["value"] = percent;
    ok = lanCommand(light, "brightness", data);
  } else {
    JsonDocument value;
    value.set(percent);
    ok = cloudControl(light, "devices.capabilities.range", "brightness", value);
  }
  if (ok) light.brightness = percent;
  return ok;
}

static bool lanColorwc(const Light &light, uint8_t r, uint8_t g, uint8_t b, uint16_t kelvin, bool log = false) {
  JsonDocument data;
  data["color"]["r"] = r;
  data["color"]["g"] = g;
  data["color"]["b"] = b;
  data["colorTemInKelvin"] = kelvin;
  return lanCommand(light, "colorwc", data, log);
}

static bool cloudColorTemp(const Light &light, uint16_t kelvin) {
  JsonDocument value;
  value.set(kelvin);
  const bool ok = cloudControl(light, "devices.capabilities.color_setting", "colorTemperatureK", value);
  Serial.printf("Govee cloud -> %s colorTemperatureK %u: %s\n", light.name.c_str(), kelvin, ok ? "ok" : "failed");
  return ok;
}

static bool sendKelvinAsRgb(Light &light, uint16_t kelvin) {
  uint8_t r, g, b;
  kelvinToRgb(kelvin, r, g, b);
  if (!lanColorwc(light, r, g, b, 0, true)) return false;
  light.r = r;
  light.g = g;
  light.b = b;
  return true;
}

bool setColor(Light &light, uint8_t r, uint8_t g, uint8_t b) {
  bool ok = true;
  if (light.lan) {
    ok = lanColorwc(light, r, g, b, 0);
  } else {
    JsonDocument value;
    value.set((uint32_t(r) << 16) | (uint32_t(g) << 8) | b);
    ok = cloudControl(light, "devices.capabilities.color_setting", "colorRgb", value);
  }
  if (ok) {
    light.r = r;
    light.g = g;
    light.b = b;
    light.colorTempK = 0;
    light.emulatedColorTempK = 0;
  }
  return ok;
}

bool setColorTemp(Light &light, uint16_t kelvin) {
  if (!light.supportsColorTemp) return false;
  kelvin = constrain(kelvin, light.minColorTempK, light.maxColorTempK);
  bool ok = false;
  bool emulated = false;
  JsonDocument status;

  if (!light.lan) {
    ok = cloudColorTemp(light, kelvin);
  } else if (light.lanColorTemp) {
    ok = lanColorwc(light, 0, 0, 0, kelvin, true);
    if (ok && lanStatus(light, status) && (status["colorTemInKelvin"] | 0) == 0) {
      Serial.printf("%s ignored colorTemInKelvin; showing color temperature as RGB from now on\n", light.name.c_str());
      light.lanColorTemp = false;
      emulated = ok = sendKelvinAsRgb(light, kelvin);
    }
  } else {
    // Models in CLOUD_ONLY_COLOR_TEMP_SKUS echo the Kelvin value over LAN without changing, so the
    // devStatus reply can't detect it. The cloud changes them for real; RGB is the fallback.
    if (light.inCloud) ok = cloudColorTemp(light, kelvin);
    if (ok) {
      lanStatus(light, status);
    } else {
      Serial.printf("%s: showing %u K as RGB\n", light.name.c_str(), kelvin);
      emulated = ok = sendKelvinAsRgb(light, kelvin);
    }
  }

  if (ok) {
    light.colorTempK = kelvin;
    light.emulatedColorTempK = emulated ? kelvin : 0;
  }
  return ok;
}

bool applyCommand(JsonVariantConst ids, JsonObjectConst command, JsonArray failed, String &error) {
  const bool allLights = !ids.is<JsonArrayConst>();
  std::vector<Light *> targets;
  if (allLights) {
    for (Light &light : registry) targets.push_back(&light);
  } else {
    for (JsonVariantConst id : ids.as<JsonArrayConst>()) {
      if (Light *light = find(id.as<String>())) targets.push_back(light);
    }
  }
  if (targets.empty()) return false;

  auto fail = [&](const Light &light, const char *reason) {
    failed.add(light.id);
    if (!error.isEmpty()) error += "; ";
    error += light.name + ": " + reason;
  };

  JsonVariantConst kelvin = command["color_temp_k"].is<int>() ? command["color_temp_k"] : command["colorTempK"];
  const bool wantsColorTemp = kelvin.is<int>() && kelvin.as<int>() > 0;

  std::vector<Light *> sentOverLan;
  for (Light *light : targets) {
    // Cloud lights report online status; recheck before giving up on one that was named explicitly.
    if (!light->lan && !light->online && !allLights) cloudFetchState(*light);
    if (!light->lan && !light->online) {
      if (!allLights) fail(*light, "light offline");
      continue;
    }
    if (wantsColorTemp && !light->supportsColorTemp && !allLights) {
      fail(*light, "color temperature not supported");
      continue;
    }

    bool ok = true;
    if (command["power"].is<bool>()) ok &= setPower(*light, command["power"]);
    if (command["brightness"].is<int>()) ok &= setBrightness(*light, command["brightness"].as<int>());
    if (command["color"].is<JsonObjectConst>()) {
      JsonObjectConst color = command["color"];
      ok &= setColor(*light, color["r"] | 0, color["g"] | 0, color["b"] | 0);
    }
    if (wantsColorTemp && light->supportsColorTemp) ok &= setColorTemp(*light, kelvin.as<int>());

    if (light->lan) {
      if (ok) sentOverLan.push_back(light);
      else fail(*light, "couldn't send to light");
    } else if (!ok) {
      cloudFetchState(*light);
      fail(*light, light->online ? "Govee cloud rejected the command" : "light offline");
    }
  }

  // LAN lights never acknowledge commands, so check they're still there.
  const std::vector<bool> answered = pingLan(sentOverLan);
  for (size_t i = 0; i < sentOverLan.size(); i++) {
    if (!answered[i] && !allLights) fail(*sentOverLan[i], "light offline");
  }
  return true;
}

void allToJson(JsonArray out) {
  for (const Light &light : registry) toJson(light, out.add<JsonObject>());
}

void toJson(const Light &light, JsonObject out) {
  out["id"] = light.id;
  out["name"] = light.name;
  out["sku"] = light.sku;
  out["connection"] = light.lan ? "lan" : "cloud";
  if (light.lan) out["ip"] = light.ip.toString();
  out["online"] = light.online;
  out["stateKnown"] = light.stateKnown && light.online;
  out["on"] = light.on;
  out["brightness"] = light.brightness;
  JsonObject color = out["color"].to<JsonObject>();
  color["r"] = light.r;
  color["g"] = light.g;
  color["b"] = light.b;
  out["colorTempK"] = light.colorTempK;
  out["supportsColorTemp"] = light.supportsColorTemp;
  if (light.supportsColorTemp) {
    out["minColorTempK"] = light.minColorTempK;
    out["maxColorTempK"] = light.maxColorTempK;
  }
}

}  // namespace govee
