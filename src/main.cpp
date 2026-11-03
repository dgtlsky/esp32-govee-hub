#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "cloud_sync.h"
#include "govee.h"
#include "secrets.h"

static const char *HOSTNAME = "govee";
static const uint8_t BOOT_BUTTON_PIN = 0;
static const uint32_t LAN_RESCAN_INTERVAL_MS = 5UL * 60UL * 1000UL;

static WebServer server(80);
static uint32_t lastLanRescan = 0;

static bool connectWifi() {
  Serial.printf("Connecting to WiFi \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed. Check secrets.h (and that the network is 2.4 GHz).");
    return false;
  }
  Serial.printf("Connected. IP: %s  Signal: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

static void sendLights() {
  JsonDocument doc;
  govee::allToJson(doc["lights"].to<JsonArray>());
  String body;
  serializeJson(doc, body);
  server.send(200, "application/json", body);
}

static void sendError(int status, const char *message) {
  JsonDocument doc;
  doc["error"] = message;
  String body;
  serializeJson(doc, body);
  server.send(status, "application/json", body);
}

static void handleGetLights() {
  govee::refreshLanState();
  sendLights();
}

static void handleControl() {
  JsonDocument req;
  if (deserializeJson(req, server.arg("plain"))) {
    sendError(400, "Body must be JSON");
    return;
  }

  JsonDocument res;
  JsonArray failed = res["failed"].to<JsonArray>();
  if (!govee::applyCommand(req["ids"], req.as<JsonObjectConst>(), failed)) {
    sendError(404, "No matching lights");
    return;
  }

  govee::allToJson(res["lights"].to<JsonArray>());
  String body;
  serializeJson(res, body);
  server.send(200, "application/json", body);
}

static void handleIndex() {
  File file = LittleFS.open("/index.html.gz", "r");
  if (!file) {
    server.send(200, "text/plain",
                "Govee controller is running, but the web panel isn't uploaded yet. "
                "Run: pio run -t uploadfs");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

static void startServer() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/lights", HTTP_GET, handleGetLights);
  server.on("/api/control", HTTP_POST, handleControl);
  server.serveStatic("/assets", LittleFS, "/assets", "max-age=31536000, immutable");
  server.serveStatic("/", LittleFS, "/", "max-age=86400");
  server.onNotFound(handleIndex);
  server.begin();

  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
  Serial.printf("Control panel: http://%s.local  or  http://%s\n", HOSTNAME, WiFi.localIP().toString().c_str());
}

static void toggleAllLights() {
  bool anyOn = false;
  for (const Light &light : govee::lights()) anyOn |= light.on;

  Serial.printf("BOOT button: turning all lights %s\n", anyOn ? "off" : "on");
  for (Light &light : govee::lights()) govee::setPower(light, !anyOn);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  Serial.println("\n\nESP32 Govee controller");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  while (!connectWifi()) delay(5000);

  govee::begin();
  govee::discover();
  lastLanRescan = millis();
  startServer();
  cloud_sync::begin();
}

void loop() {
  server.handleClient();
  cloud_sync::loop();

  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
      govee::refreshLanState();
      toggleAllLights();
      while (digitalRead(BOOT_BUTTON_PIN) == LOW) delay(10);
    }
  }

  if (millis() - lastLanRescan > LAN_RESCAN_INTERVAL_MS) {
    govee::rescanLan();
    lastLanRescan = millis();
  }

  delay(2);
}
