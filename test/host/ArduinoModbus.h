/**
 * Modbus TCP on a PC, speaking the real protocol to a real software slave.
 *
 * Not a mock: it builds MBAP frames and parses the responses, so the end-to-end
 * test exercises the runtime's byte-order handling, function codes and register
 * counts against pymodbus rather than against an agreeable stub.
 *
 * ModbusRTUClient exists and always fails. There is no RS485 on a laptop, and a
 * silent success would be the same defect this library already had once: a
 * device configured for one wire being read over another.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HOST_ARDUINO_MODBUS_H
#define HOST_ARDUINO_MODBUS_H

#include "Arduino.h"
#include "Ethernet.h"

class ModbusClient {
public:
    virtual ~ModbusClient() {}
    virtual int requestFrom(int id, int fc, int address, int nb) = 0;
    virtual long read() = 0;
    virtual const char* lastError() { return _err; }
    virtual void setTimeout(unsigned long ms) { _timeout_ms = ms; }
protected:
    const char*   _err = nullptr;
    unsigned long _timeout_ms = 1000;
};

class ModbusTCPClient : public ModbusClient {
public:
    explicit ModbusTCPClient(EthernetClient& c) : _c(c) {}

    int begin(IPAddress ip, uint16_t port) {
        _c.setTimeoutMs((uint32_t)_timeout_ms);
        if (!_c.connect(ip, port)) { _err = "connect failed"; return 0; }
        _err = nullptr;
        return 1;
    }
    void stop() { _c.stop(); }
    uint8_t connected() { return _c.connected(); }

    int requestFrom(int id, int fc, int address, int nb) override {
        _avail = 0; _next = 0;
        if (nb < 1 || nb > 125) { _err = "bad register count"; return 0; }

        const uint16_t txn = ++_txn;
        uint8_t req[12] = {
            (uint8_t)(txn >> 8), (uint8_t)txn,     // transaction
            0, 0,                                   // protocol: Modbus
            0, 6,                                   // length of what follows
            (uint8_t)id, (uint8_t)fc,
            (uint8_t)(address >> 8), (uint8_t)address,
            (uint8_t)(nb >> 8), (uint8_t)nb,
        };
        if (!_c.raw().writeAll(req, sizeof(req))) { _err = "write failed"; return 0; }

        uint8_t head[8];
        if (!_c.raw().readExactly(head, sizeof(head))) { _err = "no response"; return 0; }
        if (head[0] != req[0] || head[1] != req[1]) { _err = "transaction mismatch"; return 0; }
        if (head[7] & 0x80) {  // exception response: one more byte, the code
            uint8_t code = 0;
            _c.raw().readExactly(&code, 1);
            _err = "modbus exception";
            return 0;
        }
        // head[6] is the unit id, head[7] the function; the byte count follows.
        uint8_t bytecount = 0;
        if (!_c.raw().readExactly(&bytecount, 1)) { _err = "truncated response"; return 0; }
        if (bytecount != nb * 2 || bytecount > sizeof(_buf)) { _err = "unexpected length"; return 0; }
        if (!_c.raw().readExactly(_buf, bytecount)) { _err = "truncated payload"; return 0; }

        _avail = nb;
        _err = nullptr;
        return nb;
    }

    long read() override {
        if (_next >= _avail) return -1;
        const uint8_t* p = _buf + _next * 2;
        _next++;
        return ((long)p[0] << 8) | p[1];
    }

private:
    EthernetClient& _c;
    uint16_t _txn = 0;
    uint8_t  _buf[250] = {0};
    int      _avail = 0;
    int      _next = 0;
};

class HostModbusRTUClient : public ModbusClient {
public:
    int begin(unsigned long, uint16_t) { _err = "no RS485 on a host build"; return 0; }
    int requestFrom(int, int, int, int) override { _err = "no RS485 on a host build"; return 0; }
    long read() override { return -1; }
};
extern HostModbusRTUClient ModbusRTUClient;

#endif  // HOST_ARDUINO_MODBUS_H
