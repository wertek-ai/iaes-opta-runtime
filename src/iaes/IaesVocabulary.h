/**
 * iaes — Vocabulary and asset identity
 *
 * The part of this runtime that implements IAES rather than this product's
 * judgment: the severity levels IAES publishes, and the fields an event needs
 * to say which asset it is about.
 *
 * Nothing here knows what a Modbus register is, what a deadband is, or when a
 * value is bad. Those are the implementation's judgment, and they live one
 * directory up.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IAES_VOCABULARY_H
#define IAES_VOCABULARY_H

#include <stddef.h>
#include <stdint.h>

#define IAES_MAX_ASSET_LEN  32
#define IAES_MAX_NAME_LEN   32
#define IAES_MAX_PLANT_LEN  32
#define IAES_MAX_AREA_LEN   32

// ─── Severity ────────────────────────────────────────────────
// A published enumeration. Which reading deserves which level is not part of
// it: the standard gives the field, never the value.
enum class IaesSeverity : uint8_t {
    INFO = 0,
    LOW,
    MEDIUM,
    HIGH,
    CRITICAL,
};

inline const char* severityToString(IaesSeverity s) {
    switch (s) {
        case IaesSeverity::INFO:     return "info";
        case IaesSeverity::LOW:      return "low";
        case IaesSeverity::MEDIUM:   return "medium";
        case IaesSeverity::HIGH:     return "high";
        case IaesSeverity::CRITICAL: return "critical";
        default:                     return "high";
    }
}

// ─── Asset identity ──────────────────────────────────────────
// Enough context to say which asset an event is about, and no more. IAES
// defines no hierarchy: plant and area are labels the producer carries, not a
// tree the standard understands.
struct IaesAsset {
    char asset_id[IAES_MAX_ASSET_LEN]   = {};
    char asset_name[IAES_MAX_NAME_LEN]  = {};
    char plant[IAES_MAX_PLANT_LEN]      = {};
    char area[IAES_MAX_AREA_LEN]        = {};
};

#endif // IAES_VOCABULARY_H
