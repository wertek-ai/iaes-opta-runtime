/**
 * iaes — Event builder
 *
 * Builds IAES v1.4 events from values the caller already has. It takes no
 * detector, no thresholds and no register map: the standard gives the field,
 * and the caller decides what goes in it.
 *
 * That separation is not stylistic. Until 2026-09-06 buildMeasurement took a
 * DetectOutput, so producing a standard event required running this product's
 * judgment first -- and buildHealth invented what it could not be given,
 * reporting a health index of 0.8 on every event it ever emitted.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IAES_EVENT_H
#define IAES_EVENT_H

#include "IaesVocabulary.h"
#include <ArduinoJson.h>

#define IAES_SPEC_VERSION "1.4"

class IaesEvent {
public:
    /**
     * asset.measurement -- one reading, in engineering units.
     */
    static bool buildMeasurement(JsonDocument& doc,
                                 const IaesAsset& asset,
                                 const char* measurement_type,
                                 double value,
                                 const char* unit,
                                 const char* source);

    /**
     * asset.health -- a judgment about condition.
     *
     * Every field is the caller's. Passing nullptr for failure_mode or
     * recommended_action omits it, which is what the schema expects when there
     * is nothing to say; inventing a value would be worse than saying nothing.
     */
    static bool buildHealth(JsonDocument& doc,
                            const IaesAsset& asset,
                            double health_index,
                            IaesSeverity severity,
                            const char* failure_mode,
                            const char* recommended_action,
                            const char* source);

    /** Pseudo-UUID v4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx. Needs 37 bytes. */
    static void generateUUID(char* buffer, size_t len);

    /** ISO 8601 if setEpoch() was called; otherwise a millis-relative stamp. */
    static void getTimestamp(char* buffer, size_t len);
    static void setEpoch(uint32_t epoch_seconds);

    /**
     * The specification's canonical form of a data payload: compact JSON,
     * keys sorted, whole numbers written without a decimal point. This is what
     * gets hashed, and it is what makes the digest agree with the Python and
     * TypeScript SDKs.
     *
     * Returns the length written, or 0 if the buffer was too small -- never a
     * truncated result, because a truncated canonical form hashes two
     * different events to the same value.
     */
    static size_t canonicalize(JsonObjectConst data, char* out, size_t len);

    /** content_hash of a data payload. Needs a 17-byte buffer. */
    static bool computeContentHash(JsonObjectConst data, char* out, size_t len);

    /** Seeds the UUID generator from whatever entropy the board has. */
    static void seedRandom();

private:
    static void buildEnvelope(JsonDocument& doc,
                              const char* event_type,
                              const IaesAsset& asset,
                              const char* source);

    static uint32_t      _epoch_base;
    static unsigned long _epoch_millis;
    static bool          _seeded;
};

#endif // IAES_EVENT_H
