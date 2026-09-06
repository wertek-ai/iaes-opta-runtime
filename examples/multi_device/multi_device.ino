/**
 * iaes-opta-runtime — Multi-Device Example
 *
 * Reads 3 devices on the same RS-485 bus:
 *   - VFD (address 1)
 *   - Energy meter (address 2)
 *   - Compressor controller (address 3)
 *
 * Hardware: Arduino Opta (Ethernet + RS485)
 */

#include <OptaRuntime.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x02 };
OptaRuntime runtime;

void setup() {
    Serial.begin(115200);
    delay(2000);

    // ══════════════════════════════════════════════════════════
    // Device 1: ABB ACS580 VFD (address 1)
    // ══════════════════════════════════════════════════════════
    DeviceProfile vfd = {};
    strncpy(vfd.name, "ABB ACS580", sizeof(vfd.name) - 1);
    vfd.address = 1;
    vfd.setIdentity("VFD-001", "VFD Bomba P-101", "Planta Norte", "MCC-3");

    vfd.addRegister(1,  ModbusDataType::FLOAT32, "motor_current",   "A",   1.0f, 0.5f);
    vfd.addRegister(5,  ModbusDataType::UINT16,  "motor_speed",     "rpm", 1.0f, 10.0f);
    vfd.addRegister(3,  ModbusDataType::UINT16,  "frequency",       "Hz",  0.1f, 0.5f);
    vfd.addRegister(10, ModbusDataType::INT16,   "vfd_temperature", "C",   0.1f, 1.0f, 85.0f);

    // ══════════════════════════════════════════════════════════
    // Device 2: Eastron SDM630 Energy Meter (address 2, FC04)
    // ══════════════════════════════════════════════════════════
    DeviceProfile meter = {};
    strncpy(meter.name, "Eastron SDM630", sizeof(meter.name) - 1);
    meter.address = 2;
    meter.default_function = ModbusFunction::INPUT_REGISTERS; // FC04 for all registers
    meter.setIdentity("METER-001", "Medidor MCC-3", "Planta Norte", "MCC-3");

    meter.addRegister(0,   ModbusDataType::FLOAT32, "voltage",      "V",     1.0f, 2.0f);
    meter.addRegister(6,   ModbusDataType::FLOAT32, "current",      "A",     1.0f, 0.2f);
    meter.addRegister(12,  ModbusDataType::FLOAT32, "power",        "W",     1.0f, 50.0f);
    meter.addRegister(30,  ModbusDataType::FLOAT32, "power_factor", "ratio", 1.0f, 0.02f);
    meter.addRegister(70,  ModbusDataType::FLOAT32, "frequency",    "Hz",    1.0f, 0.1f);

    // ══════════════════════════════════════════════════════════
    // Device 3: Atlas Copco GA55 Compressor (address 3)
    // ══════════════════════════════════════════════════════════
    DeviceProfile compressor = {};
    strncpy(compressor.name, "Atlas Copco GA55", sizeof(compressor.name) - 1);
    compressor.address = 3;
    compressor.setIdentity("COMP-001", "Compresor GA55", "Planta Norte", "Compresores");

    compressor.addRegister(100, ModbusDataType::INT16,  "pressure",      "bar", 0.1f, 0.2f);
    compressor.addRegister(102, ModbusDataType::INT16,  "temperature",   "C",   0.1f, 2.0f,
                           105.0f, IaesSeverity::CRITICAL);
    compressor.addRegister(104, ModbusDataType::UINT16, "motor_current", "A",   0.1f, 1.0f);
    compressor.addRegister(106, ModbusDataType::UINT32, "run_hours",     "h",   1.0f, 0.0f);

    // ══════════════════════════════════════════════════════════
    // Configure runtime
    // ══════════════════════════════════════════════════════════
    runtime.setSource("opta.planta_norte.mcc3");
    runtime.setMqtt("broker.example.com", 1883);
    runtime.setDebug(true);

    TimingConfig timing;
    timing.register_gap_ms = 50;
    timing.device_gap_ms = 100;
    timing.poll_interval_ms = 2000;
    timing.publish_interval_ms = 5000;
    runtime.setTiming(timing);

    runtime.addDevice(vfd);
    runtime.addDevice(meter);
    runtime.addDevice(compressor);

    if (!runtime.begin(mac)) {
        Serial.println("Runtime init failed!");
        while (1) { yield(); }
    }

    Serial.println("Multi-device IAES injector running.");
}

void loop() {
    runtime.poll();
}
