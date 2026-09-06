/**
 * Enough of the Ethernet library for the runtime to run on a PC.
 *
 * EthernetClient is a TCP socket; Ethernet.begin() succeeds because the host
 * already has a network. localIP() reports loopback, which is where the
 * simulator and the broker live during the end-to-end test.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HOST_ETHERNET_H
#define HOST_ETHERNET_H

#include "Arduino.h"
#include "HostSocket.h"

class EthernetClient {
public:
    int connect(IPAddress ip, uint16_t port) {
        return _s.connect(ip.asBigEndian(), port, _timeout_ms) ? 1 : 0;
    }
    uint8_t connected() { return _s.connected() ? 1 : 0; }
    void stop() { _s.close(); }
    void setTimeoutMs(uint32_t ms) { _timeout_ms = ms; _s.setTimeout(ms); }
    hostnet::Socket& raw() { return _s; }
private:
    hostnet::Socket _s;
    uint32_t _timeout_ms = 2000;
};

class HostEthernet {
public:
    int begin(byte*) { hostnet::startup(); return 1; }
    IPAddress localIP() { return IPAddress(127, 0, 0, 1); }
    int maintain() { return 0; }
};
extern HostEthernet Ethernet;

#endif  // HOST_ETHERNET_H
