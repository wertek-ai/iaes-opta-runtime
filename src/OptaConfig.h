/**
 * iaes-opta-runtime — Configuration Structures
 *
 * Defines the data structures for device profiles, register mappings,
 * and runtime configuration. JSON-based on the Arduino side;
 * YAML profiles are converted via tools/yaml2json.py.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IAES_CONFIG_H
#define IAES_CONFIG_H

#include <Arduino.h>
#include "iaes/IaesVocabulary.h"

// ─── Limits ───────────────────────────────────────────────────
#define IAES_MAX_DEVICES       8    // Max devices per Opta
#define IAES_MAX_REGISTERS    32    // Max registers per device
#define IAES_MAX_UNIT_LEN     12
#define IAES_MAX_SOURCE_LEN   48
#define IAES_MAX_TOPIC_LEN    64

// Severity and asset identity are the standard's, not this runtime's.
// They live in iaes/IaesVocabulary.h.

// ─── Modbus Data Types ───────────────────────────────────────
enum class ModbusDataType : uint8_t {
    UINT16 = 0,     // 1 register, unsigned
    INT16,          // 1 register, signed
    UINT32,         // 2 registers, unsigned 32-bit
    INT32,          // 2 registers, signed 32-bit (two's complement)
    INT32_SIGNMAG,  // 2 registers, sign-magnitude (Ducati "Bit-Signed Long": MSB=sign)
    FLOAT32,        // 2 registers, IEEE 754 float
    FLOAT64,        // 4 registers, IEEE 754 double
    BITFIELD,       // 1 register, individual bits
    BCD,            // 1 register, Binary-Coded Decimal
    INT32_M10K,     // 2 registers, Schneider Modulus-10000: hi×10000 + lo (signed)
    UINT32_M10K,    // 2 registers, Schneider Modulus-10000: hi×10000 + lo (unsigned)
};

// ─── Modbus Byte/Word Order ──────────────────────────────────
enum class ModbusByteOrder : uint8_t {
    BIG_ENDIAN_BE = 0,   // AB CD (most common: ABB, Danfoss, Siemens)
    LITTLE_ENDIAN_LE,    // CD AB
    WORD_SWAPPED,        // CD AB (same as LE for 32-bit — Schneider, Eastron)
    BYTE_SWAPPED,        // BA DC (rare)
};

// ─── Modbus Protocol ─────────────────────────────────────────
enum class ModbusProtocol : uint8_t {
    RTU = 0,
    TCP,
};

// ─── Register Function Code ──────────────────────────────────
enum class ModbusFunction : uint8_t {
    HOLDING_REGISTERS = 3,   // FC03 — most common
    INPUT_REGISTERS   = 4,   // FC04 — read-only registers
};

// ─── Threshold Action ────────────────────────────────────────
enum class ThresholdAction : uint8_t {
    NONE = 0,
    HEALTH_EVENT,       // Emit asset.health event with severity
    MEASUREMENT_EVENT,  // Emit asset.measurement (force, ignore deadband)
};

// ─── Register Mapping ────────────────────────────────────────
struct RegisterMapping {
    uint16_t        reg;                                 // Starting register address
    ModbusDataType  data_type       = ModbusDataType::UINT16;
    ModbusFunction  function        = ModbusFunction::HOLDING_REGISTERS;
    char            measurement_type[IAES_MAX_NAME_LEN]; // IAES measurement_type
    char            unit[IAES_MAX_UNIT_LEN];             // IAES unit
    float           scale           = 1.0f;              // raw * scale = engineering value
    float           offset          = 0.0f;              // (raw * scale) + offset
    float           deadband        = 0.0f;              // Absolute change to trigger event
    float           threshold_high  = NAN;               // Above → health event
    float           threshold_low   = NAN;               // Below → health event
    ThresholdAction threshold_action = ThresholdAction::HEALTH_EVENT;
    IaesSeverity    severity        = IaesSeverity::HIGH; // Severity for threshold events

    // Runtime state (not from config)
    float           last_value      = NAN;               // Last published value
    unsigned long   last_event_ms   = 0;                 // Last event timestamp
};

// ─── Device Profile ──────────────────────────────────────────
struct DeviceProfile {
    char            name[IAES_MAX_NAME_LEN]       = {};  // e.g. "ABB ACS580 VFD"
    ModbusProtocol  protocol        = ModbusProtocol::RTU;
    uint8_t         address         = 1;                 // Modbus slave address (RTU)
    IPAddress       ip;                                  // Modbus TCP IP (if TCP)
    uint16_t        tcp_port        = 502;               // Modbus TCP port
    ModbusByteOrder byte_order      = ModbusByteOrder::BIG_ENDIAN_BE;
    uint32_t        baud_rate       = 9600;              // RTU baud rate
    uint16_t        serial_config   = SERIAL_8N1;        // RTU serial config
    ModbusFunction  default_function = ModbusFunction::HOLDING_REGISTERS; // Default FC for all registers

    // Asset identity, in the standard's own type. Everything above this
    // line is how to talk to the device; this is what the event is about.
    IaesAsset       asset;

    // Register mappings
    RegisterMapping registers[IAES_MAX_REGISTERS];
    uint8_t         register_count  = 0;

    // Runtime state
    bool            enabled         = true;
    bool            connected       = false;
    uint32_t        read_errors     = 0;
    uint32_t        read_success    = 0;

    // ── Convenience methods ─────────────────────────────────

    /**
     * Add a register mapping. Returns false if full.
     */
    bool addRegister(uint16_t reg_addr, ModbusDataType type,
                     const char* mtype, const char* unit_str,
                     float scale_val = 1.0f, float deadband_val = 0.0f,
                     float threshold_hi = NAN, IaesSeverity sev = IaesSeverity::HIGH) {
        if (register_count >= IAES_MAX_REGISTERS) return false;
        RegisterMapping& r = registers[register_count];
        r.reg = reg_addr;
        r.data_type = type;
        r.function = default_function;
        strncpy(r.measurement_type, mtype, IAES_MAX_NAME_LEN - 1);
        r.measurement_type[IAES_MAX_NAME_LEN - 1] = '\0';
        strncpy(r.unit, unit_str, IAES_MAX_UNIT_LEN - 1);
        r.unit[IAES_MAX_UNIT_LEN - 1] = '\0';
        r.scale = scale_val;
        r.deadband = deadband_val;
        r.threshold_high = threshold_hi;
        r.severity = sev;
        register_count++;
        return true;
    }

    /**
     * Set asset identity fields in one call.
     */
    void setIdentity(const char* id, const char* aname = "",
                     const char* p = "", const char* a = "") {
        strncpy(asset.asset_id, id, IAES_MAX_ASSET_LEN - 1);
        asset.asset_id[IAES_MAX_ASSET_LEN - 1] = '\0';
        strncpy(asset.asset_name, aname, IAES_MAX_NAME_LEN - 1);
        asset.asset_name[IAES_MAX_NAME_LEN - 1] = '\0';
        strncpy(asset.plant, p, IAES_MAX_PLANT_LEN - 1);
        asset.plant[IAES_MAX_PLANT_LEN - 1] = '\0';
        strncpy(asset.area, a, IAES_MAX_AREA_LEN - 1);
        asset.area[IAES_MAX_AREA_LEN - 1] = '\0';
    }
};

// ─── Timing Configuration ────────────────────────────────────
struct TimingConfig {
    uint16_t register_gap_ms    = 50;    // Delay between individual register reads
    uint16_t device_gap_ms      = 100;   // Delay between switching Modbus devices
    uint32_t poll_interval_ms   = 1000;  // Full polling cycle interval
    uint32_t publish_interval_ms = 5000; // MQTT/HTTPS publish interval
    uint16_t modbus_timeout_ms  = 1000;  // Modbus response timeout
    uint8_t  modbus_retries     = 2;     // Retries per failed read
    uint16_t rs485_pre_delay_us = 1000;  // RS485 pre-transmission delay (microseconds)
    uint16_t rs485_post_delay_us = 1000; // RS485 post-transmission delay (microseconds)
};

// ─── Transport Configuration ─────────────────────────────────
enum class TransportType : uint8_t {
    MQTT = 0,
    HTTPS,
};

struct MqttConfig {
    char     broker[64]         = "";
    uint16_t port               = 1883;
    char     user[32]           = "";
    char     password[65]       = "";
    char     topic_prefix[IAES_MAX_TOPIC_LEN] = "iaes";
    bool     use_tls            = false;
};

struct HttpsConfig {
    char     endpoint[128]      = "";
    char     api_key[65]        = "";
};

// ─── Site Configuration ──────────────────────────────────────
struct SiteConfig {
    char            source[IAES_MAX_SOURCE_LEN]   = {};  // e.g. "opta.plant1.mcc3"
    TransportType   transport       = TransportType::MQTT;
    MqttConfig      mqtt;
    HttpsConfig     https;
    TimingConfig    timing;
    DeviceProfile   devices[IAES_MAX_DEVICES];
    uint8_t         device_count    = 0;
};

#endif // IAES_CONFIG_H
