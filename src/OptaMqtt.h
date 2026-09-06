/**
 * iaes-opta-runtime — MQTT Publisher
 *
 * Publishes IAES JSON events to an MQTT broker.
 * Topic format: {prefix}/{source}/{event_type}
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IAES_MQTT_H
#define IAES_MQTT_H

#include "OptaConfig.h"
#include <ArduinoMqttClient.h>
#include <Ethernet.h>
#include <ArduinoJson.h>

class OptaMqtt {
public:
    ~OptaMqtt();

    bool begin(EthernetClient& eth_client, const MqttConfig& config);
    bool loop();
    bool publish(const JsonDocument& doc, const char* source);
    bool publishRaw(const char* topic, const char* json);
    bool connected();
    bool reconnect();

    uint32_t publishCount() const { return _publish_count; }
    uint32_t errorCount() const { return _error_count; }

private:
    MqttClient*  _mqtt = nullptr;
    MqttConfig   _config;
    uint32_t     _publish_count = 0;
    uint32_t     _error_count = 0;
    unsigned long _last_reconnect_ms = 0;
    uint32_t     _reconnect_interval_ms = 5000; // uint32_t to avoid overflow on doubling
};

#endif // IAES_MQTT_H
