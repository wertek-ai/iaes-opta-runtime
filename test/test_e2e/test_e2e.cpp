/**
 * End to end, on a host: a Modbus slave, this runtime, and a broker.
 *
 * This is the test that authorizes publishing. Everything else checks a piece;
 * this one checks the claim -- that somebody who has never seen an Opta can
 * clone the repository and get a valid IAES 1.4 event out of it.
 *
 * The runtime is not modified or stubbed. It polls a real Modbus TCP slave
 * over a real socket and publishes real MQTT frames. What comes out the other
 * end is validated against the specification by tools/e2e.py, which is what
 * runs this.
 *
 * SPDX-License-Identifier: MIT
 */

#include <unity.h>

#include "OptaRuntime.h"

#include <stdlib.h>
#include <time.h>

// The Arduino globals the runtime expects. Defined here so that the host
// headers stay header-only and a second host program can reuse them.
HostSerial            Serial;
HostEthernet          Ethernet;
HostRS485             RS485;
HostModbusRTUClient   ModbusRTUClient;

static uint16_t envPort(const char* name, uint16_t fallback) {
    const char* v = getenv(name);
    return v ? (uint16_t)atoi(v) : fallback;
}

static uint16_t published_count = 0;

static void countEvent(const JsonDocument&, const char*) { published_count++; }

void test_a_reading_becomes_a_published_iaes_event(void) {
    const uint16_t modbus_port = envPort("IAES_E2E_MODBUS_PORT", 15020);
    const uint16_t mqtt_port   = envPort("IAES_E2E_MQTT_PORT", 15083);

    OptaRuntime runtime;

    DeviceProfile vfd = {};
    strncpy(vfd.name, "Simulated VFD", sizeof(vfd.name) - 1);
    vfd.protocol = ModbusProtocol::TCP;
    vfd.ip = IPAddress(127, 0, 0, 1);
    vfd.tcp_port = modbus_port;
    vfd.address = 1;
    vfd.byte_order = ModbusByteOrder::BIG_ENDIAN_BE;
    vfd.setIdentity("VFD-SIM-001", "Simulated pump drive", "Planta Norte", "MCC-3");

    // Register 1 holds 125; scaled by 0.1 that is 12.5 A. The slave in
    // tools/e2e.py serves exactly that, and the harness checks the number that
    // comes out the far end -- so a byte-order or scaling defect fails here.
    vfd.addRegister(1, ModbusDataType::UINT16, "motor_current", "A", 0.1f, 0.0f);

    TimingConfig fast;
    fast.poll_interval_ms = 50;
    fast.publish_interval_ms = 100;
    fast.register_gap_ms = 0;
    fast.device_gap_ms = 0;

    runtime.setSource("opta.e2e.test");
    runtime.setTiming(fast);
    runtime.setMqtt("127.0.0.1", mqtt_port);
    runtime.addDevice(vfd);
    runtime.onEvent(countEvent);

    // IAES needs an absolute clock and this runtime ships none. On a host the
    // system clock is the obvious provider; on a board it is NTP or an RTC.
    runtime.setEpoch((uint32_t)time(nullptr));

    byte mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    TEST_ASSERT_TRUE_MESSAGE(runtime.begin(mac), "runtime.begin() failed");

    const unsigned long deadline = millis() + 8000;
    while (millis() < deadline && runtime.stats().events_emitted == 0) {
        runtime.poll();
        delay(10);
    }

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, published_count,
        "the runtime never built an event");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, runtime.stats().events_emitted,
        "the runtime built events but published none");
}

void test_without_a_clock_it_refuses_to_start(void) {
    // The other half of the same rule: no absolute time, no IAES. This is what
    // used to produce "T+123.456" and send it anyway.
    IaesEvent::setEpoch(0);
    TEST_ASSERT_FALSE(IaesEvent::clockIsSet());

    OptaRuntime runtime;
    runtime.setSource("opta.e2e.test");
    byte mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02};
    TEST_ASSERT_FALSE_MESSAGE(runtime.begin(mac),
        "begin() started without a clock");
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_without_a_clock_it_refuses_to_start);
    RUN_TEST(test_a_reading_becomes_a_published_iaes_event);
    return UNITY_END();
}
