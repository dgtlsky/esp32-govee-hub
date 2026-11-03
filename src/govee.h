#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>
#include <vector>

struct Light {
  String id;
  String name;
  String sku;
  IPAddress ip;
  bool lan = false;
  bool inCloud = false;  // listed in the Govee account, so the cloud API can control it

  bool online = true;  // false once a LAN light stops answering, or Govee's cloud reports it offline
  uint8_t missedStatusPolls = 0;
  uint32_t lastSeenMs = 0;  // last LAN reply of any kind from this light

  bool supportsColorTemp = true;
  bool lanColorTemp = true;  // false for models that ignore Kelvin over LAN; those use the cloud for it

  bool stateKnown = false;
  bool on = false;
  uint8_t brightness = 100;
  uint8_t r = 255;
  uint8_t g = 255;
  uint8_t b = 255;
  uint16_t colorTempK = 0;  // 0 while the light is in RGB color mode
  uint16_t emulatedColorTempK = 0;  // set while showing a Kelvin value as an RGB color

  uint16_t minColorTempK = 2000;
  uint16_t maxColorTempK = 9000;
};

namespace govee {

void begin();

// (Re)opens the LAN reply listener on UDP port 4002. Call after WiFi reconnects.
void restartLan();

// Fetches the account's device list from the cloud and finds which lights answer on the LAN.
void discover();

// Re-runs only the LAN scan, picking up lights whose IP address changed.
void rescanLan();

// Asks every LAN light for its current state. Lights that stop answering are marked offline.
void refreshLanState();

// Fetches state (including online/offline) for cloud-only lights from Govee's cloud.
void refreshCloudState();

std::vector<Light> &lights();
Light *find(const String &id);

bool setPower(Light &light, bool on);
bool setBrightness(Light &light, uint8_t percent);
bool setColor(Light &light, uint8_t r, uint8_t g, uint8_t b);
bool setColorTemp(Light &light, uint16_t kelvin);  // clamped to the light's supported range

// Applies optional "power" (bool), "brightness" (1-100), "color" ({r,g,b}) and
// "color_temp_k" / "colorTempK" (Kelvin) fields from `command`
// to the lights listed in `ids`, or to every light if `ids` is null. IDs that fail go into `failed`, and
// `error` gets a readable reason per failed light ("TV light: light offline"). Offline lights fail when
// named in `ids` but are skipped when the command is for all lights. Returns false if no light matched.
bool applyCommand(JsonVariantConst ids, JsonObjectConst command, JsonArray failed, String &error);

void toJson(const Light &light, JsonObject out);
void allToJson(JsonArray out);

}  // namespace govee
