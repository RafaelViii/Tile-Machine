// Binary (ESP-NOW) <-> JSON (Firebase) conversion. The JSON shapes are defined in docs/DATA_MODEL.md.
#pragma once

#include <ArduinoJson.h>
#include <TileProtocol.h>

namespace codec {

/** STATUS payload -> modules/{id}/state JSON. False if the size doesn't match this protocol version. */
bool statusToJson(tile::ModuleId id, const uint8_t* data, size_t len, JsonObject out);

/**
 * modules/{id}/config JSON -> CONFIG payload (starts with configVersion). Missing fields take the
 * protocol defaults. Out-of-range values are passed through (never silently fixed), so the module
 * rejects them and the web shows "rejected".
 */
bool configFromJson(tile::ModuleId id, JsonObjectConst in, uint8_t* out, size_t& len);

}  // namespace codec
