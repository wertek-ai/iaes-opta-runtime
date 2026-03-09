/**
 * iaes-opta-runtime — MQTT Publisher Implementation
 * SPDX-License-Identifier: MIT
 */

#include "IaesMqtt.h"

IaesMqtt::~IaesMqtt() {
    delete _mqtt;
    _mqtt = nullptr;
}

bool IaesMqtt::begin(EthernetClient& eth_client, const MqttConfig& config) {
    _config = config;

    // Clean up previous instance if begin() is called again
    delete _mqtt;
    _mqtt = new MqttClient(eth_client);

    if (strlen(_config.user) > 0) {
        _mqtt->setUsernamePassword(_config.user, _config.password);
    }

    if (!_mqtt->connect(_config.broker, _config.port)) {
        _error_count++;
        return false;
    }

    return true;
}

bool IaesMqtt::loop() {
    if (!_mqtt) return false;
    _mqtt->poll();
    return connected();
}

bool IaesMqtt::connected() {
    if (!_mqtt) return false;
    return _mqtt->connected();
}

bool IaesMqtt::reconnect() {
    if (!_mqtt) return false;

    unsigned long now = millis();
    if (now - _last_reconnect_ms < _reconnect_interval_ms) {
        return false;
    }
    _last_reconnect_ms = now;

    if (_mqtt->connect(_config.broker, _config.port)) {
        _reconnect_interval_ms = 5000;
        return true;
    }

    // Exponential backoff up to 60s (safe: uint32_t, no overflow)
    _reconnect_interval_ms = min(_reconnect_interval_ms * 2, (uint32_t)60000);
    _error_count++;
    return false;
}

bool IaesMqtt::publish(const JsonDocument& doc, const char* source) {
    if (!connected()) return false;

    char topic[128];
    const char* event_type = doc["event_type"] | "unknown";
    snprintf(topic, sizeof(topic), "%s/%s/%s",
             _config.topic_prefix, source, event_type);

    // Serialize — check for truncation
    char json[768];
    size_t len = serializeJson(doc, json, sizeof(json));
    if (len == 0 || len >= sizeof(json)) {
        _error_count++;
        return false;
    }

    return publishRaw(topic, json);
}

bool IaesMqtt::publishRaw(const char* topic, const char* json) {
    if (!connected()) return false;

    _mqtt->beginMessage(topic);
    _mqtt->print(json);
    if (_mqtt->endMessage()) {
        _publish_count++;
        return true;
    }

    _error_count++;
    return false;
}
