/**
 * iaes-opta-runtime — Single VFD Example
 *
 * Reads an ABB ACS580 VFD via Modbus RTU and publishes
 * IAES events to an MQTT broker.
 *
 * Hardware: Arduino Opta (Ethernet + RS485)
 * Connection: Opta RS485 A/B → VFD terminal 104(+)/105(-)
 */

#include <OptaRuntime.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x01 };
OptaRuntime runtime;

void setup() {
    Serial.begin(115200);
    delay(2000);

    // ── Configure the VFD ────────────────────────────────────
    DeviceProfile vfd = {};
    strncpy(vfd.name, "ABB ACS580", sizeof(vfd.name) - 1);
    vfd.address = 1;
    vfd.baud_rate = 9600;
    vfd.byte_order = ModbusByteOrder::BIG_ENDIAN_BE;
    vfd.setIdentity("VFD-ACS580-001", "VFD Bomba P-101", "Planta Norte", "MCC-3");

    // ── Register mappings ────────────────────────────────────
    vfd.addRegister(1,  ModbusDataType::FLOAT32, "motor_current",   "A",   1.0f, 0.5f);
    vfd.addRegister(5,  ModbusDataType::UINT16,  "motor_speed",     "rpm", 1.0f, 10.0f);
    vfd.addRegister(3,  ModbusDataType::UINT16,  "frequency",       "Hz",  0.1f, 0.5f);
    vfd.addRegister(10, ModbusDataType::INT16,   "vfd_temperature", "C",   0.1f, 1.0f, 85.0f);
    vfd.addRegister(7,  ModbusDataType::UINT16,  "voltage",         "V",   1.0f, 5.0f);

    // ── Configure runtime ────────────────────────────────────
    runtime.setSource("opta.planta_norte.mcc3");
    runtime.setMqtt("broker.example.com", 1883);
    runtime.setDebug(true);
    runtime.addDevice(vfd);

    if (!runtime.begin(mac)) {
        Serial.println("Runtime init failed!");
        while (1) { yield(); }
    }

    Serial.println("IAES Edge Injector running.");
}

void loop() {
    runtime.poll();
}
