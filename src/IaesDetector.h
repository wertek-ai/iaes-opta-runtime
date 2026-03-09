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

#include "IaesConfig.h"

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
};

// ─── Change Detector ─────────────────────────────────────────
class IaesDetector {
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

    void reset(RegisterMapping& reg);
    void resetAll(DeviceProfile& device);
};

#endif // IAES_DETECTOR_H
