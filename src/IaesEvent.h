/**
 * iaes-opta-runtime — IAES Event Builder
 *
 * Builds IAES v1.2 compliant JSON events from Modbus readings.
 * Produces asset.measurement and asset.health event types.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IAES_EVENT_H
#define IAES_EVENT_H

#include "IaesConfig.h"
#include "IaesDetector.h"
#include <ArduinoJson.h>

#define IAES_SPEC_VERSION "1.2"

class IaesEvent {
public:
    static bool buildMeasurement(JsonDocument& doc,
                                  const DeviceProfile& device,
                                  const DetectOutput& detection,
                                  const char* source);

    static bool buildHealth(JsonDocument& doc,
                             const DeviceProfile& device,
                             const DetectOutput& detection,
                             const char* source);

    /**
     * Generate a pseudo-UUID v4.
     * Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx (36 chars + null)
     */
    static void generateUUID(char* buffer, size_t len);

    /**
     * Get ISO 8601 timestamp. Uses NTP if setEpoch() was called,
     * otherwise returns millis-based relative timestamp.
     */
    static void getTimestamp(char* buffer, size_t len);

    /**
     * Set the NTP epoch and seed the PRNG. Call once in setup().
     */
    static void setEpoch(uint32_t epoch_seconds);

    /**
     * Seed PRNG for UUID generation. Called automatically by setEpoch(),
     * but can also be called manually if NTP is not used.
     */
    static void seedRandom();

    /**
     * Compute content_hash (16-char FNV-1a hash of data payload).
     */
    static void computeContentHash(const JsonObject& data,
                                    char* hash_buffer, size_t len);

private:
    static uint32_t _epoch_base;
    static unsigned long _epoch_millis;
    static bool _seeded;

    static void buildEnvelope(JsonDocument& doc,
                               const char* event_type,
                               const DeviceProfile& device,
                               const char* source);
};

#endif // IAES_EVENT_H
