/**
 * iaes-opta-runtime — Main Runtime Orchestrator
 *
 * The top-level class that ties everything together:
 *   read_modbus() → detect_changes() → publish_iaes()
 *
 * Usage:
 *   #include <IaesRuntime.h>
 *
 *   IaesRuntime runtime;
 *   runtime.loadSiteConfig(config);
 *   runtime.begin();
 *
 *   void loop() {
 *     runtime.poll();  // Handles everything (non-blocking)
 *   }
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IAES_RUNTIME_H
#define IAES_RUNTIME_H

#include "IaesConfig.h"
#include "IaesModbus.h"
#include "IaesDetector.h"
#include "IaesEvent.h"
#include "IaesMqtt.h"
#include <Ethernet.h>

// ─── Runtime Statistics ──────────────────────────────────────
struct RuntimeStats {
    uint32_t polls_total;
    uint32_t events_emitted;
    uint32_t events_suppressed;
    uint32_t modbus_errors;
    uint32_t mqtt_errors;
    unsigned long uptime_ms;
};

// ─── Callback Types ──────────────────────────────────────────
typedef void (*IaesEventCallback)(const JsonDocument& event, const char* event_type);
typedef void (*IaesErrorCallback)(const char* device_name, const char* error);

// ─── Pending Event (compact) ─────────────────────────────────
struct PendingEvent {
    uint8_t      device_index;
    uint8_t      register_index;
    DetectOutput detection;
    bool         pending;
};

// Max pending events — sized for typical use, not worst case
#define IAES_MAX_PENDING  64

// ─── Runtime ─────────────────────────────────────────────────
class IaesRuntime {
public:
    IaesRuntime();

    // ── Configuration ────────────────────────────────────────

    void loadSiteConfig(const SiteConfig& config);
    bool addDevice(const DeviceProfile& device);
    void setSource(const char* source);
    void setMqtt(const char* broker, uint16_t port = 1883,
                 const char* user = "", const char* password = "");
    void setTopicPrefix(const char* prefix);
    void setTiming(const TimingConfig& timing);
    void setEpoch(uint32_t epoch_seconds);

    // ── Lifecycle ────────────────────────────────────────────

    bool begin(byte* mac = nullptr);

    /**
     * Main polling loop. NON-BLOCKING — call in loop().
     * Handles read → detect → publish cycle with millis() timing.
     */
    void poll();

    /**
     * Force an immediate read + publish cycle.
     * WARNING: Blocking — waits for all register reads to complete.
     */
    void forcePublish();

    // ── Manual Control ───────────────────────────────────────

    uint16_t detectChanges();
    uint16_t publishIaes();

    // ── Statistics & Access ──────────────────────────────────

    const RuntimeStats& stats() const { return _stats; }
    DeviceProfile* getDevice(uint8_t index);
    uint8_t deviceCount() const { return _config.device_count; }
    void setDebug(bool enabled) { _debug = enabled; }

    // ── Callbacks ────────────────────────────────────────────

    void onEvent(IaesEventCallback callback) { _event_cb = callback; }
    void onError(IaesErrorCallback callback) { _error_cb = callback; }

private:
    SiteConfig     _config;
    IaesModbus     _modbus;
    IaesDetector   _detector;
    IaesMqtt       _mqtt;
    EthernetClient _eth_client;
    RuntimeStats   _stats;

    bool _initialized = false;
    bool _debug = false;

    // Latest readings buffer
    float _readings[IAES_MAX_DEVICES][IAES_MAX_REGISTERS];

    // Pending events (sized for typical use: 64, not worst-case 256)
    PendingEvent _pending[IAES_MAX_PENDING];
    uint16_t     _pending_count = 0;

    // Callbacks
    IaesEventCallback _event_cb = nullptr;
    IaesErrorCallback _error_cb = nullptr;

    // Timing (single source of truth — no redundant _last_poll/_last_publish)
    unsigned long _last_poll = 0;
    unsigned long _last_publish = 0;

    void debugPrint(const char* msg);
    void debugPrintf(const char* fmt, ...);
};

#endif // IAES_RUNTIME_H
