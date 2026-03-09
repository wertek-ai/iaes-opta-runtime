/**
 * iaes-opta-runtime — IAES Event Builder Implementation
 * SPDX-License-Identifier: MIT
 */

#include "IaesEvent.h"

uint32_t IaesEvent::_epoch_base = 0;
unsigned long IaesEvent::_epoch_millis = 0;
bool IaesEvent::_seeded = false;

// ─── Envelope Builder ────────────────────────────────────────

void IaesEvent::buildEnvelope(JsonDocument& doc,
                               const char* event_type,
                               const DeviceProfile& device,
                               const char* source) {
    doc["spec_version"] = IAES_SPEC_VERSION;
    doc["event_type"] = event_type;

    char uuid[37];
    generateUUID(uuid, sizeof(uuid));
    doc["event_id"] = uuid;

    char ts[30];
    getTimestamp(ts, sizeof(ts));
    doc["timestamp"] = ts;

    doc["source"] = source;

    JsonObject asset = doc["asset"].to<JsonObject>();
    asset["asset_id"] = device.asset_id;
    if (strlen(device.asset_name) > 0) asset["asset_name"] = device.asset_name;
    if (strlen(device.plant) > 0)      asset["plant"] = device.plant;
    if (strlen(device.area) > 0)       asset["area"] = device.area;
}

// ─── Measurement Event ───────────────────────────────────────

bool IaesEvent::buildMeasurement(JsonDocument& doc,
                                  const DeviceProfile& device,
                                  const DetectOutput& detection,
                                  const char* source) {
    buildEnvelope(doc, "asset.measurement", device, source);

    JsonObject data = doc["data"].to<JsonObject>();
    data["measurement_type"] = detection.measurement_type;

    // Use snprintf instead of String class (avoids heap fragmentation)
    char val_buf[16];
    snprintf(val_buf, sizeof(val_buf), "%.2f", detection.value);
    data["value"] = serialized(val_buf);
    data["unit"] = detection.unit;

    char hash[17];
    computeContentHash(data, hash, sizeof(hash));
    doc["content_hash"] = hash;

    return true;
}

// ─── Health Event ────────────────────────────────────────────

bool IaesEvent::buildHealth(JsonDocument& doc,
                             const DeviceProfile& device,
                             const DetectOutput& detection,
                             const char* source) {
    buildEnvelope(doc, "asset.health", device, source);

    JsonObject data = doc["data"].to<JsonObject>();
    data["health_index"] = 0.8;
    data["severity"] = severityToString(detection.severity);

    char failure_mode[64];
    snprintf(failure_mode, sizeof(failure_mode), "threshold_%s", detection.measurement_type);
    data["failure_mode"] = failure_mode;

    char action[128];
    if (detection.threshold_high) {
        snprintf(action, sizeof(action), "%s %.1f %s exceeds high threshold",
                 detection.measurement_type, detection.value, detection.unit);
    } else {
        snprintf(action, sizeof(action), "%s %.1f %s below low threshold",
                 detection.measurement_type, detection.value, detection.unit);
    }
    data["recommended_action"] = action;

    char hash[17];
    computeContentHash(data, hash, sizeof(hash));
    doc["content_hash"] = hash;

    return true;
}

// ─── UUID Generation (fixed: correct 32 hex digits) ─────────

void IaesEvent::seedRandom() {
    if (!_seeded) {
        randomSeed(analogRead(A0) ^ micros());
        _seeded = true;
    }
}

void IaesEvent::generateUUID(char* buffer, size_t len) {
    if (len < 37) return;
    seedRandom();

    const char hex[] = "0123456789abcdef";

    // Generate 16 random bytes worth of hex
    // UUID v4 format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    // Positions (of hex chars): 0-7, 9-12, 14-17, 19-22, 24-35
    // Dashes at positions: 8, 13, 18, 23
    // '4' at position 14, variant bits at position 19

    int pos = 0;
    for (int hex_idx = 0; hex_idx < 32; hex_idx++) {
        // Insert dashes at the right positions
        if (hex_idx == 8 || hex_idx == 12 || hex_idx == 16 || hex_idx == 20) {
            buffer[pos++] = '-';
        }

        if (hex_idx == 12) {
            // Version 4
            buffer[pos++] = '4';
        } else if (hex_idx == 16) {
            // Variant 1: 10xx
            buffer[pos++] = hex[(random(0, 16) & 0x3) | 0x8];
        } else {
            buffer[pos++] = hex[random(0, 16)];
        }
    }
    buffer[pos] = '\0';
}

// ─── Timestamp ───────────────────────────────────────────────

void IaesEvent::setEpoch(uint32_t epoch_seconds) {
    _epoch_base = epoch_seconds;
    _epoch_millis = millis();
    seedRandom();
}

void IaesEvent::getTimestamp(char* buffer, size_t len) {
    if (len < 25) return;

    if (_epoch_base > 0) {
        // Real UTC time from NTP (millis() rollover safe: unsigned subtraction wraps correctly)
        uint32_t elapsed_s = (millis() - _epoch_millis) / 1000;
        uint32_t now = _epoch_base + elapsed_s;

        uint32_t remaining = now % 86400;
        uint8_t hours = remaining / 3600;
        uint8_t minutes = (remaining % 3600) / 60;
        uint8_t seconds = remaining % 60;

        // Epoch days to Y-M-D (start from 2000 to reduce iterations)
        uint32_t days = now / 86400;
        // Days from 1970-01-01 to 2000-01-01 = 10957
        uint32_t y;
        if (days >= 10957) {
            y = 2000;
            days -= 10957;
        } else {
            y = 1970;
        }

        while (true) {
            bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
            uint16_t days_in_year = leap ? 366 : 365;
            if (days < days_in_year) break;
            days -= days_in_year;
            y++;
        }

        static const uint8_t days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        uint8_t m = 0;
        while (m < 12) {
            uint8_t dim = days_in_month[m];
            if (m == 1 && leap) dim = 29;
            if (days < dim) break;
            days -= dim;
            m++;
        }

        snprintf(buffer, len, "%04lu-%02u-%02uT%02u:%02u:%02u+00:00",
                 (unsigned long)y, m + 1, (uint8_t)(days + 1),
                 hours, minutes, seconds);
    } else {
        // No NTP — use millis-based relative timestamp
        unsigned long ms = millis();
        snprintf(buffer, len, "T+%lu.%03lu", ms / 1000, ms % 1000);
    }
}

// ─── Content Hash ────────────────────────────────────────────

void IaesEvent::computeContentHash(const JsonObject& data,
                                    char* hash_buffer, size_t len) {
    if (len < 17) return;

    // FNV-1a 64-bit hash — fast, no heap allocation
    char json_buf[384];
    size_t json_len = serializeJson(data, json_buf, sizeof(json_buf));

    // If serialization was truncated, hash what we have (still unique enough)
    if (json_len > sizeof(json_buf)) json_len = sizeof(json_buf);

    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < json_len; i++) {
        hash ^= (uint8_t)json_buf[i];
        hash *= 1099511628211ULL;
    }

    const char hex[] = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        hash_buffer[i] = hex[hash & 0xF];
        hash >>= 4;
    }
    hash_buffer[16] = '\0';
}
