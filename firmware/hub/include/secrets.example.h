// Copy this file to secrets.h (git-ignored) and fill in the values.
#pragma once

// WiFi (2.4 GHz only)
#define WIFI_SSID        "your-wifi-name"
#define WIFI_PASSWORD    "your-wifi-password"

// Firebase project (Project settings → General)
#define FIREBASE_API_KEY      "your-web-api-key"
#define FIREBASE_DATABASE_URL "https://your-project-default-rtdb.region.firebasedatabase.app"

// Hub account (Firebase Auth user with roles/<uid> = "hub")
#define HUB_EMAIL        "hub@tile-machine.local"
#define HUB_PASSWORD     "hub-account-password"

// Setup hotspot "TileHub-XXXX" (WPA2 password, 8-63 characters). Anyone with it can change the
// hub's WiFi, so pick your own.
#define PORTAL_PASSWORD  "choose-a-password"
