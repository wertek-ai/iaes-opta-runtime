/**
 * iaes-opta-runtime — Main Runtime Implementation
 *
 * NON-BLOCKING: No delay() calls anywhere.
 * Uses millis()-based timing and Modbus state machine.
 *
 * SPDX-License-Identifier: MIT
 */

#include "OptaRuntime.h"
#include <stdarg.h>

OptaRuntime::OptaRuntime() {
    memset(&_stats, 0, sizeof(_stats));
    memset(_readings, 0, sizeof(_readings));
    memset(_pending, 0, sizeof(_pending));
}

// ─── Configuration ───────────────────────────────────────────

void OptaRuntime::loadSiteConfig(const SiteConfig& config) {
    _config = config;
}

bool OptaRuntime::addDevice(const DeviceProfile& device) {
    if (_config.device_count >= IAES_MAX_DEVICES) return false;
    _config.devices[_config.device_count++] = device;
    return true;
}

void OptaRuntime::setSource(const char* source) {
    strncpy(_config.source, source, IAES_MAX_SOURCE_LEN - 1);
    _config.source[IAES_MAX_SOURCE_LEN - 1] = '\0';
}

void OptaRuntime::setMqtt(const char* broker, uint16_t port,
                           const char* user, const char* password) {
    strncpy(_config.mqtt.broker, broker, sizeof(_config.mqtt.broker) - 1);
    _config.mqtt.broker[sizeof(_config.mqtt.broker) - 1] = '\0';
    _config.mqtt.port = port;
    strncpy(_config.mqtt.user, user, sizeof(_config.mqtt.user) - 1);
    _config.mqtt.user[sizeof(_config.mqtt.user) - 1] = '\0';
    strncpy(_config.mqtt.password, password, sizeof(_config.mqtt.password) - 1);
    _config.mqtt.password[sizeof(_config.mqtt.password) - 1] = '\0';
    _config.transport = TransportType::MQTT;
}

void OptaRuntime::setTopicPrefix(const char* prefix) {
    strncpy(_config.mqtt.topic_prefix, prefix, IAES_MAX_TOPIC_LEN - 1);
    _config.mqtt.topic_prefix[IAES_MAX_TOPIC_LEN - 1] = '\0';
}

void OptaRuntime::setTiming(const TimingConfig& timing) {
    _config.timing = timing;
}

void OptaRuntime::setEpoch(uint32_t epoch_seconds) {
    IaesEvent::setEpoch(epoch_seconds);
}

DeviceProfile* OptaRuntime::getDevice(uint8_t index) {
    if (index >= _config.device_count) return nullptr;
    return &_config.devices[index];
}

// ─── Lifecycle ───────────────────────────────────────────────

bool OptaRuntime::begin(byte* mac) {
    debugPrint("iaes-opta-runtime v0.1.0 starting...");

    // Seed PRNG for UUID generation
    IaesEvent::seedRandom();

    // Ethernet
    if (mac) {
        debugPrint("Ethernet DHCP...");
        if (Ethernet.begin(mac) == 0) {
            debugPrint("DHCP failed!");
            if (_error_cb) _error_cb("ethernet", "DHCP failed");
            return false;
        }
    }
    debugPrintf("IP: %d.%d.%d.%d",
                Ethernet.localIP()[0], Ethernet.localIP()[1],
                Ethernet.localIP()[2], Ethernet.localIP()[3]);

    // Modbus RTU — check if any device uses RTU
    bool has_rtu = false;
    for (uint8_t i = 0; i < _config.device_count; i++) {
        if (_config.devices[i].protocol == ModbusProtocol::RTU) {
            has_rtu = true;
            break;
        }
    }

    if (has_rtu) {
        uint32_t baud = _config.devices[0].baud_rate;
        uint16_t serial_cfg = _config.devices[0].serial_config;

        debugPrintf("Modbus RTU at %lu baud...", (unsigned long)baud);
        if (!_modbus.beginRTU(baud, serial_cfg,
                              _config.timing.rs485_pre_delay_us,
                              _config.timing.rs485_post_delay_us,
                              _config.timing.modbus_timeout_ms)) {
            debugPrint("Modbus RTU init failed!");
            if (_error_cb) _error_cb("modbus", "RTU init failed");
            return false;
        }
    }

    // MQTT
    if (_config.transport == TransportType::MQTT && strlen(_config.mqtt.broker) > 0) {
        debugPrintf("MQTT %s:%d...", _config.mqtt.broker, _config.mqtt.port);
        if (!_mqtt.begin(_eth_client, _config.mqtt)) {
            debugPrint("MQTT connect failed (will retry)");
        }
    }

    // Reset detector states
    for (uint8_t i = 0; i < _config.device_count; i++) {
        _detector.resetAll(_config.devices[i]);
    }

    _initialized = true;
    debugPrintf("Ready. %d devices, poll=%lums, publish=%lums",
                _config.device_count,
                (unsigned long)_config.timing.poll_interval_ms,
                (unsigned long)_config.timing.publish_interval_ms);
    return true;
}

// ─── Main Poll Loop (NON-BLOCKING) ──────────────────────────

void OptaRuntime::poll() {
    if (!_initialized) return;

    unsigned long now = millis();
    _stats.uptime_ms = now;

    // Maintain Ethernet
    Ethernet.maintain();

    // Maintain MQTT (non-blocking)
    if (!_mqtt.loop()) {
        _mqtt.reconnect();
    }

    // If Modbus cycle is in progress, advance it
    if (_modbus.busy()) {
        ModbusState ms = _modbus.step();

        if (ms == ModbusState::CYCLE_COMPLETE) {
            detectChanges();
            _stats.polls_total++;
        }
        return;
    }

    // Start a new read cycle when poll interval elapses
    if (now - _last_poll >= _config.timing.poll_interval_ms) {
        _modbus.startCycle(_config.devices, _config.device_count,
                           _readings,
                           _config.timing.register_gap_ms,
                           _config.timing.device_gap_ms);
        _last_poll = now;
    }

    // Publish cycle (independent of read cycle)
    if (now - _last_publish >= _config.timing.publish_interval_ms) {
        publishIaes();
        _last_publish = now;
    }
}

void OptaRuntime::forcePublish() {
    _modbus.startCycle(_config.devices, _config.device_count,
                       _readings,
                       _config.timing.register_gap_ms,
                       _config.timing.device_gap_ms);
    while (_modbus.busy()) {
        _modbus.step();
        yield(); // Prevent watchdog reset during blocking wait
    }
    detectChanges();
    publishIaes();
}

// ─── Detect Changes ──────────────────────────────────────────

uint16_t OptaRuntime::detectChanges() {
    _pending_count = 0;
    uint16_t events = 0;

    for (uint8_t d = 0; d < _config.device_count; d++) {
        DeviceProfile& device = _config.devices[d];
        if (!device.enabled) continue;

        for (uint8_t r = 0; r < device.register_count; r++) {
            DetectOutput out = _detector.detect(_readings[d][r], device.registers[r]);

            if (out.result == DetectResult::NO_CHANGE) {
                _stats.events_suppressed++;
                continue;
            }

            if (_pending_count < IAES_MAX_PENDING) {
                _pending[_pending_count].device_index = d;
                _pending[_pending_count].register_index = r;
                _pending[_pending_count].detection = out;
                _pending[_pending_count].pending = true;
                _pending_count++;
                events++;
            }
        }
    }

    return events;
}

// ─── Publish Events ──────────────────────────────────────────

uint16_t OptaRuntime::publishIaes() {
    uint16_t published = 0;

    // Reuse one JsonDocument for all events (avoids repeated heap alloc/free)
    JsonDocument doc;

    for (uint16_t i = 0; i < _pending_count; i++) {
        PendingEvent& pe = _pending[i];
        if (!pe.pending) continue;

        const DeviceProfile& device = _config.devices[pe.device_index];
        doc.clear();

        bool built = false;
        if (pe.detection.result == DetectResult::HEALTH_EVENT) {
            // What this runtime knows is that a configured threshold was
            // crossed. That is not a fault classification, so failure_mode
            // stays empty rather than carrying a synthesized one; the
            // description names the threshold so an operator can check it.
            const RegisterMapping& reg = device.registers[pe.register_index];
            const float threshold = pe.detection.threshold_high
                                      ? reg.threshold_high : reg.threshold_low;
            char action[160];
            snprintf(action, sizeof(action),
                     "%s read %.2f %s, %s the configured threshold of %.2f %s",
                     pe.detection.measurement_type, pe.detection.value,
                     pe.detection.unit,
                     pe.detection.threshold_high ? "above" : "below",
                     threshold, pe.detection.unit);

            built = IaesEvent::buildHealth(doc, device.asset,
                                           pe.detection.health_index,
                                           pe.detection.severity,
                                           nullptr, action, _config.source);
        } else {
            built = IaesEvent::buildMeasurement(doc, device.asset,
                                                pe.detection.measurement_type,
                                                pe.detection.value,
                                                pe.detection.unit,
                                                _config.source);
        }

        if (!built) continue;

        if (_event_cb) {
            const char* event_type = doc["event_type"] | "unknown";
            _event_cb(doc, event_type);
        }

        if (_mqtt.publish(doc, _config.source)) {
            published++;
            _stats.events_emitted++;
            pe.pending = false;

            if (_debug) {
                const char* et = doc["event_type"] | "?";
                debugPrintf("  >> %s [%s] %.2f %s",
                            et, pe.detection.measurement_type,
                            pe.detection.value, pe.detection.unit);
            }
        } else {
            _stats.mqtt_errors++;
        }
    }

    return published;
}

// ─── Debug Helpers ───────────────────────────────────────────

void OptaRuntime::debugPrint(const char* msg) {
    if (_debug) {
        Serial.print("[IAES] ");
        Serial.println(msg);
    }
}

void OptaRuntime::debugPrintf(const char* fmt, ...) {
    if (!_debug) return;

    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.print("[IAES] ");
    Serial.println(buf);
}
