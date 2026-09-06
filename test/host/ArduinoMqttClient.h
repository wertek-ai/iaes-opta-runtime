/**
 * MQTT 3.1.1 CONNECT and PUBLISH, on a PC, against a real broker.
 *
 * Only what the runtime calls, and only QoS 0, which is what it publishes.
 * Real frames rather than a stub, so the end-to-end test proves that what a
 * subscriber receives is a valid IAES event -- which is the only claim worth
 * making before publishing this library.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HOST_ARDUINO_MQTT_CLIENT_H
#define HOST_ARDUINO_MQTT_CLIENT_H

#include "Arduino.h"
#include "Ethernet.h"

#include <string>
#include <vector>

class MqttClient {
public:
    explicit MqttClient(EthernetClient& c) : _c(c) {}

    void setUsernamePassword(const char* u, const char* p) { _user = u ? u : ""; _pass = p ? p : ""; }

    /** The broker is addressed by name on a board; here, by dotted quad. */
    int connect(const char* host, uint16_t port) {
        IPAddress ip;
        if (!parseDottedQuad(host, ip)) return 0;
        _c.setTimeoutMs(3000);
        if (!_c.connect(ip, port)) return 0;

        std::vector<uint8_t> p;
        putString(p, "MQTT");
        p.push_back(0x04);                                   // protocol level 3.1.1
        uint8_t flags = 0x02;                                // clean session
        if (!_user.empty()) flags |= 0x80;
        if (!_pass.empty()) flags |= 0x40;
        p.push_back(flags);
        p.push_back(0x00); p.push_back(0x3C);                // keepalive 60 s
        putString(p, _clientId);
        if (!_user.empty()) putString(p, _user);
        if (!_pass.empty()) putString(p, _pass);

        if (!sendPacket(0x10, p)) { _c.stop(); return 0; }

        uint8_t ack[4];
        if (!_c.raw().readExactly(ack, 4)) { _c.stop(); return 0; }
        // CONNACK, remaining length 2, session-present, return code 0.
        if (ack[0] != 0x20 || ack[1] != 0x02 || ack[3] != 0x00) { _c.stop(); return 0; }
        _connected = true;
        return 1;
    }

    void poll() {}
    uint8_t connected() { return (_connected && _c.connected()) ? 1 : 0; }
    void stop() { _c.stop(); _connected = false; }

    void beginMessage(const char* topic) { _topic = topic; _payload.clear(); }
    void print(const char* s) { _payload += s; }
    void print(const std::string& s) { _payload += s; }

    int endMessage() {
        if (!_connected) return 0;
        std::vector<uint8_t> p;
        putString(p, _topic);                                 // QoS 0: no packet id
        p.insert(p.end(), _payload.begin(), _payload.end());
        return sendPacket(0x30, p) ? 1 : 0;
    }

private:
    static bool parseDottedQuad(const char* s, IPAddress& out) {
        unsigned a, b, c, d;
        if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
        out = IPAddress((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d);
        return true;
    }
    static void putString(std::vector<uint8_t>& v, const std::string& s) {
        v.push_back((uint8_t)(s.size() >> 8));
        v.push_back((uint8_t)s.size());
        v.insert(v.end(), s.begin(), s.end());
    }
    bool sendPacket(uint8_t type, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> f;
        f.push_back(type);
        size_t len = payload.size();
        do {                                                  // variable-length encoding
            uint8_t byte_ = len % 128;
            len /= 128;
            if (len) byte_ |= 0x80;
            f.push_back(byte_);
        } while (len);
        f.insert(f.end(), payload.begin(), payload.end());
        return _c.raw().writeAll(f.data(), f.size());
    }

    EthernetClient& _c;
    std::string _user, _pass, _topic, _payload;
    std::string _clientId = "iaes-opta-host";
    bool _connected = false;
};

#endif  // HOST_ARDUINO_MQTT_CLIENT_H
