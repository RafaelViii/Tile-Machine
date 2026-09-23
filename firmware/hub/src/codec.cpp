#include "codec.h"

#include <TileConfig.h>
#include <math.h>

using namespace tile;

namespace codec {

namespace {

// Values that don't fit the wire type become the type's max, which is outside every allowed range,
// so the module rejects the config instead of receiving a wrapped-around number.
uint16_t u16(JsonVariantConst v, uint16_t def) {
  if (v.isNull()) return def;
  uint32_t x = v.as<uint32_t>();
  return x > 0xFFFF ? 0xFFFF : (uint16_t)x;
}
uint8_t u8(JsonVariantConst v, uint8_t def) {
  if (v.isNull()) return def;
  uint32_t x = v.as<uint32_t>();
  return x > 0xFF ? 0xFF : (uint8_t)x;
}
uint32_t u32(JsonVariantConst v, uint32_t def) { return v.isNull() ? def : v.as<uint32_t>(); }

// "LOADCELL"/"MANUAL" -> 0, "TIME" -> 1, anything else -> 0xFF (invalid, rejected by the module).
uint8_t modeFrom(JsonVariantConst v, const char* zeroName, uint8_t def) {
  if (v.isNull()) return def;
  const char* s = v.as<const char*>();
  if (!s) return 0xFF;
  if (strcmp(s, "TIME") == 0) return 1;
  if (strcmp(s, zeroName) == 0) return 0;
  return 0xFF;
}

void common(const StatusCommon& c, JsonObject o) {
  o["interlock"] = c.interlock != 0;
  o["faults"] = c.faults;
  o["uptimeS"] = c.uptimeS;
  o["configVersion"] = c.configVersion;
}

}  // namespace

bool statusToJson(ModuleId id, const uint8_t* data, size_t len, JsonObject o) {
  if (len != statusSizeFor(id)) return false;

  switch (id) {
    case ModuleId::SHREDDER: {
      StatusShredder s;
      memcpy(&s, data, sizeof(s));
      common(s.c, o);
      o["mode"] = shredderModeName(s.mode);
      o["state"] = shredderStateName(s.state);
      o["relayOn"] = s.relayOn != 0;
      o["irDetected"] = s.irDetected != 0;
      o["estopLatched"] = s.estopLatched != 0;
      o["countdownMs"] = s.countdownMs;
      return true;
    }

    case ModuleId::CONTAINING: {
      StatusContaining s;
      memcpy(&s, data, sizeof(s));
      common(s.c, o);
      o["selector"] = selectorName(s.selector);
      o["hxOkMask"] = s.hxOkMask;
      o["pcaOk"] = s.pcaOk != 0;
      JsonArray cs = o["containers"].to<JsonArray>();
      for (int i = 0; i < 4; i++) {
        const ContainerStatus& c = s.ct[i];
        JsonObject j = cs.add<JsonObject>();
        j["weightG"] = c.weightG;
        j["state"] = containerStateName(c.state);
        j["mode"] = i < 2 ? rawModeName(c.mode) : mixedModeName(c.mode);
        j["selectedKg"] = c.selectedKg;
        j["progressPct"] = c.progressPct;
        j["remainingMs"] = c.remainingMs;
        j["dispensedG"] = c.dispensedG;
      }
      JsonArray cal = o["calFactor"].to<JsonArray>();
      for (int i = 0; i < 4; i++) cal.add(isfinite(s.calFactor[i]) ? s.calFactor[i] : 0.0f);
      return true;
    }

    case ModuleId::HOTPRESS: {
      StatusHotpress s;
      memcpy(&s, data, sizeof(s));
      common(s.c, o);
      o["onButton"] = s.onButton != 0;
      o["selector"] = selectorName(s.selector);
      o["relayDesignCure"] = s.relayDesignCure != 0;
      o["relayHotpress"] = s.relayHotpress != 0;
      o["stopLatched"] = s.stopLatched != 0;
      return true;
    }

    default:
      return false;
  }
}

bool configFromJson(ModuleId id, JsonObjectConst in, uint8_t* out, size_t& len) {
  const uint32_t version = u32(in["version"], 0);

  switch (id) {
    case ModuleId::SHREDDER: {
      ConfigShredder c = defaultShredderConfig();
      c.configVersion = version;
      c.autoStartDelayMs = u16(in["autoStartDelayMs"], c.autoStartDelayMs);
      c.autoEmptyStopDelayMs = u16(in["autoEmptyStopDelayMs"], c.autoEmptyStopDelayMs);
      c.manualConfirmTimeoutMs = u16(in["manualConfirmTimeoutMs"], c.manualConfirmTimeoutMs);
      c.irDebounceMs = u16(in["irDebounceMs"], c.irDebounceMs);
      c.buzzerVolumePct = u8(in["buzzerVolumePct"], c.buzzerVolumePct);
      memcpy(out, &c, sizeof(c));
      len = sizeof(c);
      return true;
    }

    case ModuleId::CONTAINING: {
      ConfigContaining c = defaultContainingConfig();
      c.configVersion = version;
      for (int i = 0; i < 2; i++) {
        JsonObjectConst r = in["raw"][i];
        RawContainerCfg& rc = c.raw[i];
        rc.mode = modeFrom(r["mode"], "LOADCELL", rc.mode);
        for (int k = 0; k < 5; k++) rc.timeTableMs[k] = u32(r["timeTableMs"][k], rc.timeTableMs[k]);
        rc.maxDispenseMs = u32(r["maxDispenseMs"], rc.maxDispenseMs);
        rc.jamTimeoutMs = u32(r["jamTimeoutMs"], rc.jamTimeoutMs);
        rc.toleranceG = u16(r["toleranceG"], rc.toleranceG);

        JsonObjectConst m = in["mixed"][i];
        c.mixed[i].mode = modeFrom(m["mode"], "MANUAL", c.mixed[i].mode);
        c.mixed[i].runTimeMs = u32(m["runTimeMs"], c.mixed[i].runTimeMs);
      }
      for (int s = 0; s < 8; s++) {
        JsonObjectConst sv = in["servo"][s];
        c.servo[s].stopUs = u16(sv["stopUs"], c.servo[s].stopUs);
        c.servo[s].runUs = u16(sv["runUs"], c.servo[s].runUs);
      }
      memcpy(out, &c, sizeof(c));
      len = sizeof(c);
      return true;
    }

    case ModuleId::HOTPRESS: {
      ConfigHotpress c = defaultHotpressConfig();
      c.configVersion = version;
      c.autoModeBehaviour = u8(in["autoModeBehaviour"], c.autoModeBehaviour);
      memcpy(out, &c, sizeof(c));
      len = sizeof(c);
      return true;
    }

    default:
      return false;
  }
}

}  // namespace codec
