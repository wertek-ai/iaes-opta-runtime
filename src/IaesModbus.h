/**
 * iaes-opta-runtime — Modbus Reader
 *
 * Non-blocking Modbus reader using a state machine.
 * Reads one register per call to step(), respecting timing gaps
 * between reads without using delay().
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef IAES_MODBUS_H
#define IAES_MODBUS_H

#include "IaesConfig.h"
#include <ArduinoRS485.h>
#include <ArduinoModbus.h>

// ─── Read State Machine ─────────────────────────────────────
enum class ModbusState : uint8_t {
    IDLE = 0,           // Waiting for next poll cycle
    READ_REGISTER,      // Ready to read the current register
    WAIT_GAP,           // Waiting register_gap_ms between reads
    WAIT_DEVICE_GAP,    // Waiting device_gap_ms between devices
    CYCLE_COMPLETE,     // All devices/registers read
};

class IaesModbus {
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
    int lastError() const { return _last_error; }

    /**
     * Read a single register (blocking — used internally and for manual reads).
     */
    float readRegister(const DeviceProfile& device, const RegisterMapping& reg);

private:
    int _last_error = 0;
    bool _rtu_initialized = false;

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

#endif // IAES_MODBUS_H
