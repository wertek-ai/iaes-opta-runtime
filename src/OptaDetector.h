/**
 * iaes-opta-runtime — Change Detector
 *
 * Deadband and threshold logic to avoid event spam.
 * Only emits events when values change meaningfully.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IAES_DETECTOR_H
#define IAES_DETECTOR_H

#include "OptaConfig.h"

// ─── Detection Result ────────────────────────────────────────
enum class DetectResult : uint8_t {
    NO_CHANGE = 0,        // Value within deadband — no event
    MEASUREMENT_EVENT,    // Value changed beyond deadband
    HEALTH_EVENT,         // Threshold crossed — emit health event
};

struct DetectOutput {
    DetectResult    result;
    float           value;              // Current engineering value
    float           delta;              // Absolute change from last
    // Copied values (not pointers) to avoid dangling references
    char            measurement_type[IAES_MAX_NAME_LEN];
    char            unit[IAES_MAX_UNIT_LEN];
    IaesSeverity    severity;
    bool            threshold_high;     // Which threshold was crossed
    float           health_index;       // See healthIndex() -- this runtime's
                                        // own crude reading, not a health model
};

// ─── Change Detector ─────────────────────────────────────────
class OptaDetector {
public:
    /**
     * Evaluate a new reading against the register's deadband and thresholds.
     *
     * Logic:
     * 1. If threshold_high is set and value >= threshold_high → HEALTH_EVENT
     * 2. If threshold_low is set and value <= threshold_low → HEALTH_EVENT
     * 3. If first reading (last_value == NAN) → MEASUREMENT_EVENT (always emit first)
     * 4. If |value - last_value| >= deadband → MEASUREMENT_EVENT
     * 5. Otherwise → NO_CHANGE
     *
     * Updates reg.last_value when an event is detected.
     */
    DetectOutput detect(float value, RegisterMapping& reg);

    /**
     * What this runtime can honestly say about condition.
     *
     * It has no health model: all it knows is that a reading passed a
     * threshold somebody configured. So health_index is 0.5 at the threshold
     * and falls linearly to 0.0 at half again beyond it. A real assessment
     * belongs to whoever has one, and the field exists so they can put it
     * there -- IAES gives the field and never the value.
     *
     * Until 2026-09-06 the event builder wrote 0.8 here, on every health event
     * it ever emitted, regardless of the reading.
     */
    static float healthIndex(float value, const RegisterMapping& reg, bool high);

    void reset(RegisterMapping& reg);
    void resetAll(DeviceProfile& device);
};

#endif // IAES_DETECTOR_H
