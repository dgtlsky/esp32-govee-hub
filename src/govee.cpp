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
  return cloudRequest("POST", "/device/control", body, res) == HTTP_CODE_OK;
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
    if (instance == "powerSwitch") {
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

bool lanCommand(const Light &light, const char *cmd, JsonVariantConst data) {
  JsonDocument msg;
  msg["msg"]["cmd"] = cmd;
  msg["msg"]["data"] = data;
  return lanSend(light, msg);
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
    for (JsonObject cap : device["capabilities"].as<JsonArray>()) {
      if (cap["instance"] != "colorTemperatureK") continue;
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

  collectReplies(400, [](const IPAddress &from, JsonObject msg) {
    if (msg["cmd"] != "devStatus") return;
    for (Light &light : registry) {
      if (!light.lan || light.ip != from) continue;
      JsonObject data = msg["data"];
      light.on = (data["onOff"] | 0) == 1;
      light.brightness = data["brightness"] | light.brightness;
      light.r = data["color"]["r"] | light.r;
      light.g = data["color"]["g"] | light.g;
      light.b = data["color"]["b"] | light.b;
      light.colorTempK = data["colorTemInKelvin"] | 0;
      light.stateKnown = true;
    }
  });
}

void discover() {
  fetchCloudDevices();
  rescanLan();

  for (Light &light : registry) {
    if (!light.lan) cloudFetchState(light);
  }
  refreshLanState();

  Serial.printf("Managing %u light(s):\n", registry.size());
  for (const Light &light : registry) {
    Serial.printf("- %-24s %-8s %s\n", light.name.c_str(), light.sku.c_str(),
                  light.lan ? light.ip.toString().c_str() : "cloud");
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

bool setColor(Light &light, uint8_t r, uint8_t g, uint8_t b) {
  bool ok = true;
  if (light.lan) {
    JsonDocument data;
    data["color"]["r"] = r;
    data["color"]["g"] = g;
    data["color"]["b"] = b;
    data["colorTemInKelvin"] = 0;
    ok = lanCommand(light, "colorwc", data);
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
  }
  return ok;
}

bool setColorTemp(Light &light, uint16_t kelvin) {
  kelvin = constrain(kelvin, light.minColorTempK, light.maxColorTempK);
  bool ok;
  if (light.lan) {
    JsonDocument data;
    data["color"]["r"] = 0;
    data["color"]["g"] = 0;
    data["color"]["b"] = 0;
    data["colorTemInKelvin"] = kelvin;
    ok = lanCommand(light, "colorwc", data);
  } else {
    JsonDocument value;
    value.set(kelvin);
    ok = cloudControl(light, "devices.capabilities.color_setting", "colorTemperatureK", value);
  }
  if (ok) light.colorTempK = kelvin;
  return ok;
}

bool applyCommand(JsonVariantConst ids, JsonObjectConst command, JsonArray failed) {
  std::vector<Light *> targets;
  if (ids.is<JsonArrayConst>()) {
    for (JsonVariantConst id : ids.as<JsonArrayConst>()) {
      if (Light *light = find(id.as<String>())) targets.push_back(light);
    }
  } else {
    for (Light &light : registry) targets.push_back(&light);
  }
  if (targets.empty()) return false;

  for (Light *light : targets) {
    bool ok = true;
    if (command["power"].is<bool>()) ok &= setPower(*light, command["power"]);
    if (command["brightness"].is<int>()) ok &= setBrightness(*light, command["brightness"].as<int>());
    if (command["color"].is<JsonObjectConst>()) {
      JsonObjectConst color = command["color"];
      ok &= setColor(*light, color["r"] | 0, color["g"] | 0, color["b"] | 0);
    }
    JsonVariantConst kelvin = command["color_temp_k"].is<int>() ? command["color_temp_k"] : command["colorTempK"];
    if (kelvin.is<int>() && kelvin.as<int>() > 0) ok &= setColorTemp(*light, kelvin.as<int>());
    if (!ok) failed.add(light->id);
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
  out["stateKnown"] = light.stateKnown;
  out["on"] = light.on;
  out["brightness"] = light.brightness;
  JsonObject color = out["color"].to<JsonObject>();
  color["r"] = light.r;
  color["g"] = light.g;
  color["b"] = light.b;
  out["colorTempK"] = light.colorTempK;
}

}  // namespace govee
