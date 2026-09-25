// WiFi manager: saved networks (NVS), background reconnect, and the setup hotspot (captive portal).
// Spec: docs/modules/hub.md "WiFi and setup hotspot".
#pragma once

#include <Arduino.h>

namespace net {

enum class PortalReason : uint8_t { None, Boot, Button, Offline };

/** Load saved networks (seeded from secrets.h the first time), start connecting, open the boot hotspot. */
void begin();

/** Call every loop(). Non-blocking: reconnects, runs the hotspot timers and answers web requests. */
void loop(bool cloudOnline);

bool connected();
bool portalOpen();
/** Open (or keep open) the setup hotspot. */
void openPortal(PortalReason why);
/** SSID of the network the hub is on right now ("" when not connected). */
String currentSsid();
/** Human-readable reason for a WiFi disconnect code. */
const char* reasonText(uint8_t reason);

}  // namespace net
