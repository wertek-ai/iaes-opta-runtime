/**
 * iaes-opta-runtime — Change Detector Implementation
 * SPDX-License-Identifier: MIT
 */

#include "OptaDetector.h"
#include <math.h>
#include <string.h>

DetectOutput OptaDetector::detect(float value, RegisterMapping& reg) {
    DetectOutput out;
    out.value = value;
    // Copy strings to avoid dangling pointers if profile is moved
    strncpy(out.measurement_type, reg.measurement_type, IAES_MAX_NAME_LEN - 1);
    out.measurement_type[IAES_MAX_NAME_LEN - 1] = '\0';
    strncpy(out.unit, reg.unit, IAES_MAX_UNIT_LEN - 1);
    out.unit[IAES_MAX_UNIT_LEN - 1] = '\0';
    out.threshold_high = false;
    out.severity = reg.severity;
    out.delta = 0.0f;
    out.health_index = 1.0f;

    // NAN value = read failure — no event
    if (isnan(value)) {
        out.result = DetectResult::NO_CHANGE;
        return out;
    }

    // 1. Check thresholds first (always override deadband)
    if (!isnan(reg.threshold_high) && value >= reg.threshold_high) {
        out.result = DetectResult::HEALTH_EVENT;
        out.threshold_high = true;
        out.health_index = healthIndex(value, reg, true);
        out.delta = isnan(reg.last_value) ? 0.0f : (value - reg.last_value);
        reg.last_value = value;
        reg.last_event_ms = millis();
        return out;
    }

    if (!isnan(reg.threshold_low) && value <= reg.threshold_low) {
        out.result = DetectResult::HEALTH_EVENT;
        out.threshold_high = false;
        out.health_index = healthIndex(value, reg, false);
        out.delta = isnan(reg.last_value) ? 0.0f : (value - reg.last_value);
        reg.last_value = value;
        reg.last_event_ms = millis();
        return out;
    }

    // 2. First reading — always emit
    if (isnan(reg.last_value)) {
        out.result = DetectResult::MEASUREMENT_EVENT;
        out.delta = 0.0f;
        reg.last_value = value;
        reg.last_event_ms = millis();
        return out;
    }

    // 3. Deadband check
    out.delta = value - reg.last_value;
    float abs_delta = fabsf(out.delta);

    if (reg.deadband > 0.0f && abs_delta < reg.deadband) {
        out.result = DetectResult::NO_CHANGE;
        return out;
    }

    // 4. Value changed beyond deadband
    out.result = DetectResult::MEASUREMENT_EVENT;
    reg.last_value = value;
    reg.last_event_ms = millis();
    return out;
}

void OptaDetector::reset(RegisterMapping& reg) {
    reg.last_value = NAN;
    reg.last_event_ms = 0;
}

void OptaDetector::resetAll(DeviceProfile& device) {
    for (uint8_t i = 0; i < device.register_count; i++) {
        reset(device.registers[i]);
    }
}

// ─── Health index ────────────────────────────────────────────

float OptaDetector::healthIndex(float value, const RegisterMapping& reg, bool high) {
    const float threshold = high ? reg.threshold_high : reg.threshold_low;
    if (isnan(threshold)) return 0.5f;

    // Distance past the threshold, as a fraction of the threshold itself. The
    // span is what makes the number mean anything: at the threshold the reading
    // is borderline (0.5), and half again beyond it this runtime has nothing
    // left to say (0.0).
    const float span = fabsf(threshold) * 0.5f;
    if (span <= 0.0f) return 0.5f;

    const float past = high ? (value - threshold) : (threshold - value);
    float h = 0.5f - 0.5f * (past / span);
    if (h < 0.0f) h = 0.0f;
    if (h > 0.5f) h = 0.5f;
    return h;
}
