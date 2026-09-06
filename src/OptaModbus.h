/**
 * iaes-opta-runtime — Modbus Reader
 *
 * Non-blocking Modbus reader using a state machine.
 * Reads one register per call to step(), respecting timing gaps
 * between reads without using delay().
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef OPTA_MODBUS_H
#define OPTA_MODBUS_H

#include "OptaConfig.h"
#include <ArduinoRS485.h>
#include <ArduinoModbus.h>
#include <Ethernet.h>

// ArduinoModbus defines COILS, DISCRETE_INPUTS, HOLDING_REGISTERS and
// INPUT_REGISTERS as plain macros, which the preprocessor then substitutes
// into ModbusFunction::HOLDING_REGISTERS -- so any file naming a member of
// that enum failed to compile. This library uses the enum, never the macros,
// so they go. Undone here rather than by renaming the enum, which is the
// public surface examples and profiles are written against.
#undef COILS
#undef DISCRETE_INPUTS
#undef HOLDING_REGISTERS
#undef INPUT_REGISTERS

// ─── Read State Machine ─────────────────────────────────────
enum class ModbusState : uint8_t {
    IDLE = 0,           // Waiting for next poll cycle
    READ_REGISTER,      // Ready to read the current register
    WAIT_GAP,           // Waiting register_gap_ms between reads
    WAIT_DEVICE_GAP,    // Waiting device_gap_ms between devices
    CYCLE_COMPLETE,     // All devices/registers read
};

class OptaModbus {
public:
    /**
     * Initialize Modbus RTU on the Opta's built-in RS485 port.
     */
    bool beginRTU(uint32_t baud_rate = 9600,
                  uint16_t serial_config = SERIAL_8N1,
                  uint16_t pre_delay_us = 1000,
                  uint16_t post_delay_us = 1000,
                  uint16_t timeout_ms = 1000);

    /**
     * Modbus TCP. Nothing to initialize on the wire -- each device is dialled
     * when it is read -- but this must be called for TCP devices to be
     * attempted at all, so that a repository configured for TCP fails loudly
     * instead of being read over RTU.
     *
     * Until 2026-09-06 DeviceProfile carried `protocol`, `ip` and `tcp_port`
     * and nothing read any of them: every device went out over RS485.
     */
    bool beginTCP(uint16_t timeout_ms = 1000);

    /**
     * Start a new read cycle across all devices.
     * Call once, then call step() repeatedly until complete.
     */
    void startCycle(DeviceProfile* devices, uint8_t device_count,
                    float readings[][IAES_MAX_REGISTERS],
                    uint16_t register_gap_ms, uint16_t device_gap_ms);

    /**
     * Advance the state machine by one step. NON-BLOCKING.
     * Returns the current state. Keep calling until CYCLE_COMPLETE or IDLE.
     */
    ModbusState step();

    /**
     * Get current state.
     */
    ModbusState state() const { return _state; }

    /**
     * Check if a cycle is in progress.
     */
    bool busy() const { return _state != ModbusState::IDLE && _state != ModbusState::CYCLE_COMPLETE; }

    /**
     * Get last error code.
     */
    /** The last failure, or nullptr. Owned by the caller of the read. */
    const char* lastError() const { return _last_error; }

    /**
     * Read a single register (blocking — used internally and for manual reads).
     */
    float readRegister(const DeviceProfile& device, const RegisterMapping& reg);

private:
    const char* _last_error = nullptr;
    bool _rtu_initialized = false;
    bool _tcp_initialized = false;
    uint16_t _tcp_timeout_ms = 1000;

    // One connection, reused: an Opta has more devices than it has sockets.
    EthernetClient  _tcp_socket;
    ModbusTCPClient _tcp{_tcp_socket};
    IPAddress       _tcp_connected_to;
    uint16_t        _tcp_connected_port = 0;

    /** Connects to this device if not already connected to it. */
    bool tcpConnect(const DeviceProfile& device);

    // State machine
    ModbusState     _state = ModbusState::IDLE;
    DeviceProfile*  _devices = nullptr;
    float         (*_readings)[IAES_MAX_REGISTERS] = nullptr;
    uint8_t         _device_count = 0;
    uint8_t         _cur_device = 0;
    uint8_t         _cur_register = 0;
    uint8_t         _cycle_success = 0;  // Per-cycle success count for current device
    uint16_t        _register_gap_ms = 50;
    uint16_t        _device_gap_ms = 100;
    unsigned long   _wait_start = 0;

    // Data type conversion
    float convertValue(const RegisterMapping& mapping,
                       const DeviceProfile& device,
                       uint16_t* raw, uint8_t count);
    uint32_t applyByteOrder32(uint16_t high, uint16_t low, ModbusByteOrder order);
    uint64_t applyByteOrder64(uint16_t* regs, ModbusByteOrder order);
};

#endif // OPTA_MODBUS_H
