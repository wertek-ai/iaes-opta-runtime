/**
 * iaes-opta-runtime — Non-blocking Modbus Reader Implementation
 *
 * Uses a state machine instead of delay() between reads.
 * Each call to step() either reads one register or waits for
 * the timing gap to elapse — never blocks.
 *
 * SPDX-License-Identifier: MIT
 */

#include "OptaModbus.h"

// ─── Initialization ──────────────────────────────────────────

bool OptaModbus::beginRTU(uint32_t baud_rate, uint16_t serial_config,
                          uint16_t pre_delay_us, uint16_t post_delay_us,
                          uint16_t timeout_ms) {
    RS485.setDelays(pre_delay_us, post_delay_us);

    if (!ModbusRTUClient.begin(baud_rate, serial_config)) {
        _last_error = "RS485 begin failed";
        return false;
    }

    ModbusRTUClient.setTimeout(timeout_ms);
    _rtu_initialized = true;
    _last_error = nullptr;
    return true;
}

// ─── Non-blocking Cycle ──────────────────────────────────────

void OptaModbus::startCycle(DeviceProfile* devices, uint8_t device_count,
                             float readings[][IAES_MAX_REGISTERS],
                             uint16_t register_gap_ms, uint16_t device_gap_ms) {
    _devices = devices;
    _device_count = device_count;
    _readings = readings;
    _register_gap_ms = register_gap_ms;
    _device_gap_ms = device_gap_ms;
    _cur_device = 0;
    _cur_register = 0;
    _cycle_success = 0;
    _state = ModbusState::READ_REGISTER;

    // Skip to first enabled device
    while (_cur_device < _device_count && !_devices[_cur_device].enabled) {
        _cur_device++;
    }
    if (_cur_device >= _device_count) {
        _state = ModbusState::CYCLE_COMPLETE;
    }
}

ModbusState OptaModbus::step() {
    switch (_state) {

        case ModbusState::READ_REGISTER: {
            // Read the current register from the current device
            DeviceProfile& dev = _devices[_cur_device];
            float val = readRegister(dev, dev.registers[_cur_register]);
            _readings[_cur_device][_cur_register] = val;

            if (!isnan(val)) {
                dev.read_success++;
                _cycle_success++;
            } else {
                dev.read_errors++;
            }

            // Advance to next register
            _cur_register++;

            if (_cur_register >= dev.register_count) {
                // Device complete — connected = had at least one success THIS cycle
                dev.connected = (_cycle_success > 0);
                _cycle_success = 0;
                _cur_register = 0;
                _cur_device++;

                // Skip disabled devices
                while (_cur_device < _device_count && !_devices[_cur_device].enabled) {
                    _cur_device++;
                }

                if (_cur_device >= _device_count) {
                    _state = ModbusState::CYCLE_COMPLETE;
                } else if (_device_gap_ms > 0) {
                    _state = ModbusState::WAIT_DEVICE_GAP;
                    _wait_start = millis();
                } else {
                    _state = ModbusState::READ_REGISTER;
                }
            } else if (_register_gap_ms > 0) {
                _state = ModbusState::WAIT_GAP;
                _wait_start = millis();
            }
            // else: stay in READ_REGISTER (no gap needed)
            break;
        }

        case ModbusState::WAIT_GAP:
            // Non-blocking wait between register reads
            if (millis() - _wait_start >= _register_gap_ms) {
                _state = ModbusState::READ_REGISTER;
            }
            break;

        case ModbusState::WAIT_DEVICE_GAP:
            // Non-blocking wait between devices
            if (millis() - _wait_start >= _device_gap_ms) {
                _state = ModbusState::READ_REGISTER;
            }
            break;

        case ModbusState::CYCLE_COMPLETE:
            _state = ModbusState::IDLE;
            break;

        case ModbusState::IDLE:
            break;
    }

    return _state;
}

// ─── Single Register Read (blocking, used by step) ───────────

float OptaModbus::readRegister(const DeviceProfile& device,
                                const RegisterMapping& reg) {
    uint8_t count = 1;
    switch (reg.data_type) {
        case ModbusDataType::UINT16:
        case ModbusDataType::INT16:
        case ModbusDataType::BITFIELD:
        case ModbusDataType::BCD:
            count = 1;
            break;
        case ModbusDataType::UINT32:
        case ModbusDataType::INT32:
        case ModbusDataType::INT32_SIGNMAG:
        case ModbusDataType::FLOAT32:
        case ModbusDataType::INT32_M10K:
        case ModbusDataType::UINT32_M10K:
            count = 2;
            break;
        case ModbusDataType::FLOAT64:
            count = 4;
            break;
    }

    // ModbusFunction's values are the function codes themselves.
    const int fc = static_cast<int>(reg.function);

    if (!ModbusRTUClient.requestFrom(device.address, fc, reg.reg, count)) {
        _last_error = ModbusRTUClient.lastError();
        return NAN;
    }

    uint16_t raw[4] = {0};
    for (uint8_t i = 0; i < count; i++) {
        int val = ModbusRTUClient.read();
        if (val < 0) {
            _last_error = "short read";
            return NAN;
        }
        raw[i] = (uint16_t)val;
    }

    _last_error = nullptr;
    return convertValue(reg, device, raw, count);
}

// ─── Data Type Conversion ────────────────────────────────────

float OptaModbus::convertValue(const RegisterMapping& mapping,
                                const DeviceProfile& device,
                                uint16_t* raw, uint8_t count) {
    float result = 0.0f;

    switch (mapping.data_type) {
        case ModbusDataType::UINT16:
            result = (float)raw[0];
            break;

        case ModbusDataType::INT16:
            result = (float)(int16_t)raw[0];
            break;

        case ModbusDataType::UINT32: {
            uint32_t val = applyByteOrder32(raw[0], raw[1], device.byte_order);
            result = (float)val;
            break;
        }

        case ModbusDataType::INT32: {
            uint32_t val = applyByteOrder32(raw[0], raw[1], device.byte_order);
            result = (float)(int32_t)val;
            break;
        }

        case ModbusDataType::INT32_SIGNMAG: {
            // Ducati "Bit-Signed Long": MSB=1 means negative, remaining bits = magnitude
            // Example: 0x80000007 = -7 (NOT two's complement)
            uint32_t val = applyByteOrder32(raw[0], raw[1], device.byte_order);
            if (val & 0x80000000) {
                result = -(float)(val & 0x7FFFFFFF);
            } else {
                result = (float)val;
            }
            break;
        }

        case ModbusDataType::FLOAT32: {
            uint32_t val = applyByteOrder32(raw[0], raw[1], device.byte_order);
            float f;
            memcpy(&f, &val, sizeof(float));
            result = f;
            break;
        }

        case ModbusDataType::FLOAT64: {
            uint64_t val = applyByteOrder64(raw, device.byte_order);
            double d;
            memcpy(&d, &val, sizeof(double));
            result = (float)d;
            break;
        }

        case ModbusDataType::BITFIELD:
            result = (float)raw[0];
            break;

        case ModbusDataType::BCD: {
            uint16_t bcd = raw[0];
            result = (float)(
                ((bcd >> 12) & 0x0F) * 1000 +
                ((bcd >> 8)  & 0x0F) * 100 +
                ((bcd >> 4)  & 0x0F) * 10 +
                (bcd & 0x0F)
            );
            break;
        }

        case ModbusDataType::INT32_M10K: {
            // Schneider Modulus-10000: value = hi × 10000 + lo (both signed)
            // Does NOT use applyByteOrder32 — M10K is a semantic encoding,
            // not a packed 32-bit value. raw[0]=high, raw[1]=low always.
            int16_t hi = (int16_t)raw[0];
            int16_t lo = (int16_t)raw[1];
            result = (float)((int32_t)hi * 10000 + (int32_t)lo);
            break;
        }

        case ModbusDataType::UINT32_M10K: {
            // Schneider Modulus-10000 unsigned: value = hi × 10000 + lo
            // Same as INT32_M10K but treats both registers as unsigned.
            uint16_t hi = raw[0];
            uint16_t lo = raw[1];
            result = (float)((uint32_t)hi * 10000 + (uint32_t)lo);
            break;
        }
    }

    return (result * mapping.scale) + mapping.offset;
}

// ─── Byte Order Helpers ──────────────────────────────────────

uint32_t OptaModbus::applyByteOrder32(uint16_t high, uint16_t low,
                                       ModbusByteOrder order) {
    switch (order) {
        case ModbusByteOrder::BIG_ENDIAN_BE:
            return ((uint32_t)high << 16) | low;
        case ModbusByteOrder::LITTLE_ENDIAN_LE:
        case ModbusByteOrder::WORD_SWAPPED:
            return ((uint32_t)low << 16) | high;
        case ModbusByteOrder::BYTE_SWAPPED:
            return ((uint32_t)((high >> 8) | (high << 8)) << 16) |
                   ((low >> 8) | (low << 8));
        default:
            return ((uint32_t)high << 16) | low;
    }
}

uint64_t OptaModbus::applyByteOrder64(uint16_t* regs, ModbusByteOrder order) {
    switch (order) {
        case ModbusByteOrder::BIG_ENDIAN_BE:
            return ((uint64_t)regs[0] << 48) | ((uint64_t)regs[1] << 32) |
                   ((uint64_t)regs[2] << 16) | regs[3];
        case ModbusByteOrder::LITTLE_ENDIAN_LE:
        case ModbusByteOrder::WORD_SWAPPED:
            return ((uint64_t)regs[3] << 48) | ((uint64_t)regs[2] << 32) |
                   ((uint64_t)regs[1] << 16) | regs[0];
        default:
            return ((uint64_t)regs[0] << 48) | ((uint64_t)regs[1] << 32) |
                   ((uint64_t)regs[2] << 16) | regs[3];
    }
}
