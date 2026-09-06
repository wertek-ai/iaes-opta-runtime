/**
 * iaes — Event builder implementation
 * SPDX-License-Identifier: MIT
 */

#include "IaesEvent.h"

#include "IaesCanonical.h"
#include "IaesHash.h"

#include <Arduino.h>
#include <string.h>

uint32_t IaesEvent::_epoch_base = 0;
unsigned long IaesEvent::_epoch_millis = 0;
bool IaesEvent::_seeded = false;

// ─── Envelope ────────────────────────────────────────────────

void IaesEvent::buildEnvelope(JsonDocument& doc,
                              const char* event_type,
                              const IaesAsset& asset_id,
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

    JsonObject a = doc["asset"].to<JsonObject>();
    a["asset_id"] = asset_id.asset_id;
    if (strlen(asset_id.asset_name) > 0) a["asset_name"] = asset_id.asset_name;
    if (strlen(asset_id.plant) > 0)      a["plant"] = asset_id.plant;
    if (strlen(asset_id.area) > 0)       a["area"] = asset_id.area;
}

// ─── asset.measurement ───────────────────────────────────────

bool IaesEvent::buildMeasurement(JsonDocument& doc,
                                 const IaesAsset& asset,
                                 const char* measurement_type,
                                 double value,
                                 const char* unit,
                                 const char* source) {
    buildEnvelope(doc, "asset.measurement", asset, source);

    JsonObject data = doc["data"].to<JsonObject>();
    data["measurement_type"] = measurement_type;
    data["value"] = value;
    data["unit"] = unit;

    char hash[17];
    if (!computeContentHash(data.operator JsonObjectConst(), hash, sizeof(hash))) return false;
    doc["content_hash"] = hash;
    return true;
}

// ─── asset.health ────────────────────────────────────────────

bool IaesEvent::buildHealth(JsonDocument& doc,
                            const IaesAsset& asset,
                            double health_index,
                            IaesSeverity severity,
                            const char* failure_mode,
                            const char* recommended_action,
                            const char* source) {
    buildEnvelope(doc, "asset.health", asset, source);

    JsonObject data = doc["data"].to<JsonObject>();
    data["health_index"] = health_index;
    data["severity"] = severityToString(severity);
    // Omitted rather than invented. The previous implementation synthesized a
    // failure mode from the measurement name and wrote its own prose.
    if (failure_mode && *failure_mode)             data["failure_mode"] = failure_mode;
    if (recommended_action && *recommended_action) data["recommended_action"] = recommended_action;

    char hash[17];
    if (!computeContentHash(data.operator JsonObjectConst(), hash, sizeof(hash))) return false;
    doc["content_hash"] = hash;
    return true;
}

// ─── Canonical form and content hash ─────────────────────────

size_t IaesEvent::canonicalize(JsonObjectConst data, char* out, size_t len) {
    return iaes::canonicalize(data, out, len);
}

bool IaesEvent::computeContentHash(JsonObjectConst data, char* out, size_t len) {
    if (len < 17) return false;
    char canonical[512];
    // Refuses rather than truncates: a truncated canonical form gives two
    // different payloads the same digest, which defeats the only thing the
    // hash is for.
    if (iaes::canonicalize(data, canonical, sizeof(canonical)) == 0) return false;
    IaesHash::contentHash(canonical, out, len);
    return true;
}

// ─── Identifiers and time ────────────────────────────────────

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
