/**
 * iaes-opta-runtime — Energy Meter Example
 *
 * Reads an Eastron SDM630 energy meter and publishes power quality
 * events with Codigo de Red threshold monitoring.
 *
 * Thresholds:
 *   - Power Factor < 0.95 → health event (CdR minimum)
 *   - THD Voltage > 5%    → health event (CdR maximum)
 *
 * Hardware: Arduino Opta (Ethernet + RS485)
 * SPDX-License-Identifier: MIT
 */

#include <OptaRuntime.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x03 };
OptaRuntime runtime;

void onIaesEvent(const JsonDocument& event, const char* event_type) {
    Serial.print(">> IAES ");
    Serial.print(event_type);
    Serial.print(": ");
    serializeJson(event["data"], Serial);
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    DeviceProfile meter = {};
    strncpy(meter.name, "Eastron SDM630", sizeof(meter.name) - 1);
    meter.address = 1;
    meter.default_function = ModbusFunction::INPUT_REGISTERS;
    meter.setIdentity("METER-SDM630-001", "Medidor MCC-3", "Planta Norte", "Subestacion-A");

    // Voltages
    meter.addRegister(0, ModbusDataType::FLOAT32, "voltage_l1", "V", 1.0f, 2.0f);
    meter.addRegister(2, ModbusDataType::FLOAT32, "voltage_l2", "V", 1.0f, 2.0f);
    meter.addRegister(4, ModbusDataType::FLOAT32, "voltage_l3", "V", 1.0f, 2.0f);

    // Currents
    meter.addRegister(6,  ModbusDataType::FLOAT32, "current_l1", "A", 1.0f, 0.2f);
    meter.addRegister(8,  ModbusDataType::FLOAT32, "current_l2", "A", 1.0f, 0.2f);
    meter.addRegister(10, ModbusDataType::FLOAT32, "current_l3", "A", 1.0f, 0.2f);

    // Power
    meter.addRegister(52, ModbusDataType::FLOAT32, "power", "W", 1.0f, 50.0f);

    // Power Factor (CdR threshold: low < 0.95)
    // Note: threshold_low needs to be set separately since addRegister only takes threshold_high
    meter.addRegister(62, ModbusDataType::FLOAT32, "power_factor", "ratio", 1.0f, 0.01f);
    meter.registers[meter.register_count - 1].threshold_low = 0.95f;

    // Frequency
    meter.addRegister(70, ModbusDataType::FLOAT32, "frequency", "Hz", 1.0f, 0.05f);

    // THD Voltage (CdR threshold: high > 5%)
    meter.addRegister(234, ModbusDataType::FLOAT32, "thd_voltage", "%", 1.0f, 0.2f, 5.0f);

    // ── Configure runtime ────────────────────────────────────
    runtime.setSource("opta.planta_norte.sub_a");
    runtime.setMqtt("broker.example.com", 1883);
    runtime.setDebug(true);
    runtime.onEvent(onIaesEvent);

    TimingConfig timing;
    timing.register_gap_ms = 50;
    timing.poll_interval_ms = 1000;
    timing.publish_interval_ms = 5000;
    runtime.setTiming(timing);

    runtime.addDevice(meter);

    // IAES requires a real timestamp, and this library ships no clock. Supply
    // one -- NTP, an RTC, your gateway -- before begin(). The constant below
    // only lets the example run: events built from it carry the wrong time.
    // Without it begin() refuses, which is the point.
    runtime.setEpoch(1788652800UL);  // 2026-09-06T00:00:00Z -- replace this

    if (!runtime.begin(mac)) {
        Serial.println("Runtime init failed!");
        while (1) { yield(); }
    }

    Serial.println("Energy meter IAES injector running.");
}

void loop() {
    runtime.poll();
}
