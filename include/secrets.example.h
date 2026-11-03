#pragma once

// Copy this file to secrets.h and fill in your values. secrets.h is git-ignored.
// The ESP32 only supports 2.4 GHz WiFi networks.

#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"
#define GOVEE_API_KEY "your-govee-api-key"

// Optional: remote control through Supabase (see supabase/setup.sql). Leave empty to disable.
#define SUPABASE_URL ""  // e.g. "https://abcdefgh.supabase.co"
#define SUPABASE_KEY ""  // the project's anon / publishable key
#define DEVICE_TOKEN ""  // random secret; its SHA-256 hash goes in public.devices
