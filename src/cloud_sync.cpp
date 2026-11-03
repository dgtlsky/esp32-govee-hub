#include "cloud_sync.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "govee.h"
#include "secrets.h"

#ifndef SUPABASE_URL
#define SUPABASE_URL ""
#endif
#ifndef SUPABASE_KEY
#define SUPABASE_KEY ""
#endif
#ifndef DEVICE_TOKEN
#define DEVICE_TOKEN ""
#endif

namespace cloud_sync {
namespace {

const uint32_t POLL_INTERVAL_MS = 2000;
const uint32_t STATE_REFRESH_INTERVAL_MS = 15000;
const uint32_t STATE_HEARTBEAT_MS = 60000;
const uint32_t ERROR_BACKOFF_MS = 15000;

bool enabled = false;
WiFiClientSecure tls;
HTTPClient http;

uint32_t nextSyncAt = 0;
uint32_t lastStateRefresh = 0;
uint32_t lastStateSent = 0;
String lastStateJson;
JsonDocument pendingResults;

String currentStateJson() {
  JsonDocument doc;
  govee::allToJson(doc.to<JsonArray>());
  String out;
  serializeJson(doc, out);
  return out;
}

void runCommands(JsonArrayConst commands) {
  for (JsonObjectConst command : commands) {
    JsonDocument failedDoc;
    JsonArray failed = failedDoc.to<JsonArray>();
    String error;
    const bool matched = govee::applyCommand(command["light_ids"], command, failed, error);

    JsonObject result = pendingResults.add<JsonObject>();
    result["id"] = command["id"];
    result["ok"] = matched && failed.size() == 0;
    if (!matched) {
      result["error"] = "No matching lights";
    } else if (failed.size() > 0) {
      result["error"] = error;
    }
    String raw;
    serializeJson(command, raw);
    Serial.printf("Supabase command %s -> %s%s%s\n", raw.c_str(), result["ok"] ? "done" : "failed",
                  result["error"].is<const char *>() ? ": " : "", result["error"] | "");
  }
}

void sync() {
  const uint32_t now = millis();
  if (now - lastStateRefresh > STATE_REFRESH_INTERVAL_MS) {
    govee::refreshLanState();
    lastStateRefresh = now;
  }

  const String state = currentStateJson();
  const bool sendState = state != lastStateJson || now - lastStateSent > STATE_HEARTBEAT_MS;

  String body = "{\"p_token\":\"" DEVICE_TOKEN "\"";
  if (sendState) body += ",\"p_lights\":" + state;
  if (pendingResults.size() > 0) {
    String results;
    serializeJson(pendingResults, results);
    body += ",\"p_results\":" + results;
  }
  body += "}";

  http.begin(tls, String(SUPABASE_URL) + "/rest/v1/rpc/device_sync");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  const int status = http.POST(body);
  const String response = http.getString();
  http.end();

  if (status != HTTP_CODE_OK) {
    Serial.printf("Supabase sync failed (HTTP %d): %s\n", status, response.c_str());
    nextSyncAt = millis() + ERROR_BACKOFF_MS;
    return;
  }

  pendingResults.clear();
  if (sendState) {
    lastStateJson = state;
    lastStateSent = now;
  }

  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    nextSyncAt = millis() + POLL_INTERVAL_MS;
    return;
  }

  JsonArrayConst commands = doc["commands"];
  if (commands.size() > 0) {
    runCommands(commands);
    nextSyncAt = millis();  // report results and new state right away
  } else {
    nextSyncAt = millis() + POLL_INTERVAL_MS;
  }
}

}  // namespace

void begin() {
  enabled = strlen(SUPABASE_URL) > 0 && strlen(SUPABASE_KEY) > 0 && strlen(DEVICE_TOKEN) > 0;
  if (!enabled) {
    Serial.println("Supabase sync off (add SUPABASE_URL, SUPABASE_KEY, DEVICE_TOKEN to secrets.h to enable)");
    return;
  }
  tls.setInsecure();
  http.setReuse(true);
  http.setTimeout(8000);
  pendingResults.to<JsonArray>();
  Serial.printf("Supabase sync on: %s\n", SUPABASE_URL);
}

void loop() {
  if (!enabled || WiFi.status() != WL_CONNECTED) return;
  if ((int32_t)(millis() - nextSyncAt) < 0) return;
  sync();
}

}  // namespace cloud_sync
