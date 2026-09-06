# iaes-opta-runtime

IAES Edge Injector for Arduino Opta -- translates raw Modbus registers into semantic IAES v1.4 events.

## What it does

- Reads Modbus RTU/TCP devices (VFDs, energy meters, compressors, capacitor banks)
- Detects meaningful changes using deadband and threshold logic
- Publishes IAES events via MQTT or HTTPS
- Fully non-blocking -- no `delay()` calls, uses `millis()`-based state machine

## Supported Hardware

- **Arduino Opta** (WiFi or Lite) -- built-in RS485 + Ethernet
- Any Arduino-compatible board with RS485/Ethernet shield (architectures: `mbed_opta`, `mbed_portenta`)

## Installation

### PlatformIO (recommended)

```ini
[env:opta]
platform = ststm32
board = opta
framework = arduino
lib_deps = iaes-opta-runtime
```

### Arduino IDE

Download the ZIP from [Releases](https://github.com/wertek-ai/iaes-opta-runtime/releases), then go to **Sketch > Include Library > Add .ZIP Library**.

### Dependencies

Installed automatically by PlatformIO. For Arduino IDE, install these via Library Manager:

- ArduinoModbus
- ArduinoRS485
- ArduinoJson
- ArduinoMqttClient
- Ethernet

## Quick Start

```cpp
#include <OptaRuntime.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x01 };
OptaRuntime runtime;

void setup() {
    Serial.begin(115200);

    DeviceProfile vfd = {};
    strncpy(vfd.name, "ABB ACS580", sizeof(vfd.name) - 1);
    vfd.address = 1;
    vfd.baud_rate = 9600;
    vfd.byte_order = ModbusByteOrder::BIG_ENDIAN_BE;
    vfd.setIdentity("VFD-ACS580-001", "VFD Bomba P-101", "Planta Norte", "MCC-3");

    vfd.addRegister(1,  ModbusDataType::FLOAT32, "motor_current",   "A",   1.0f, 0.5f);
    vfd.addRegister(5,  ModbusDataType::UINT16,  "motor_speed",     "rpm", 1.0f, 10.0f);
    vfd.addRegister(3,  ModbusDataType::UINT16,  "frequency",       "Hz",  0.1f, 0.5f);
    vfd.addRegister(10, ModbusDataType::INT16,   "vfd_temperature", "C",   0.1f, 1.0f, 85.0f);

    runtime.setSource("opta.plant1.mcc3");
    runtime.setMqtt("mqtt.example.com", 1883);
    runtime.addDevice(vfd);
    runtime.begin(mac);
}

void loop() {
    runtime.poll();  // Non-blocking: read -> detect -> publish
}
```

## Device Profiles

Pre-built YAML/JSON profiles for common industrial devices. Each profile defines register addresses, data types, scaling, deadbands, and thresholds.

| Manufacturer | Model | Category | Registers | File |
|---|---|---|---|---|
| ABB | ACS580 | VFD | 7 | `profiles/vfd/abb/acs580.json` |
| Danfoss | FC Series | VFD | 15 | `profiles/vfd/danfoss/fc-series.json` |
| Eastron | SMART X835 | Energy Meter | 28 | `profiles/energy/eastron/x835.json` |
| Schneider | ION 8650 | Energy Meter | 18 | `profiles/energy/schneider/ion8650.json` |
| Ducati | R5/R8/R14 | Capacitor Bank | 28 | `profiles/capacitor-bank/ducati/r5-r8-r14.json` |
| Atlas Copco | GA55 | Compressor | 6 | `profiles/compressor/atlas-copco/ga55.json` |

Convert YAML to JSON for use on the Opta:

```bash
python tools/yaml2json.py profiles/vfd/abb/acs580.yaml
```

## API Reference

### OptaRuntime

Main orchestrator. Ties together Modbus reading, change detection, and event publishing.

```cpp
OptaRuntime runtime;

runtime.loadSiteConfig(config);       // Load full site configuration
runtime.addDevice(device);            // Add a single device profile
runtime.setSource("opta.plant1.mcc3");// Set IAES source identifier
runtime.setMqtt("broker", 1883);      // Configure MQTT broker
runtime.setTiming(timing);            // Override default timing
runtime.setEpoch(epoch_seconds);      // Set NTP epoch for real timestamps
runtime.setDebug(true);               // Enable serial debug output

runtime.begin(mac);                   // Initialize Ethernet + Modbus + MQTT
runtime.poll();                       // Non-blocking main loop

runtime.onEvent(callback);            // Register event callback
runtime.onError(callback);            // Register error callback
runtime.stats();                      // Get RuntimeStats (polls, events, errors)
runtime.forcePublish();               // Force immediate read + publish (blocking)
```

### OptaModbus

Non-blocking Modbus reader using a state machine. Reads one register per call to `step()`.

```cpp
OptaModbus modbus;
modbus.beginRTU(9600, SERIAL_8N1, 1000, 1000, 1000);
modbus.startCycle(devices, count, readings, 50, 100);
while (modbus.busy()) { modbus.step(); }
```

States: `IDLE` -> `READ_REGISTER` -> `WAIT_GAP` -> `WAIT_DEVICE_GAP` -> `CYCLE_COMPLETE`

### OptaDetector

Deadband and threshold change detection. Prevents event spam by only emitting events when values change meaningfully.

```cpp
OptaDetector detector;
DetectOutput result = detector.detect(value, reg);
// result.result: NO_CHANGE, MEASUREMENT_EVENT, or HEALTH_EVENT
```

Detection logic:
1. If `threshold_high` is set and value >= threshold -> `HEALTH_EVENT`
2. If `threshold_low` is set and value <= threshold -> `HEALTH_EVENT`
3. If first reading (last_value == NAN) -> `MEASUREMENT_EVENT`
4. If |value - last_value| >= deadband -> `MEASUREMENT_EVENT`
5. Otherwise -> `NO_CHANGE`

### IaesEvent

Static class that builds IAES v1.4 compliant JSON events. Generates pseudo-UUID v4 identifiers and the specification's SHA-256 content hashes.

```cpp
JsonDocument doc;
IaesEvent::buildMeasurement(doc, device.asset,
                            "vibration_velocity", 1.23, "mm/s",
                            "opta.plant1.mcc3");
IaesEvent::buildHealth(doc, device.asset,
                       0.4, IaesSeverity::HIGH,
                       nullptr,                    // no fault classification
                       "vibration_velocity read 12.4 mm/s, above the "
                       "configured threshold of 10.0 mm/s",
                       "opta.plant1.mcc3");
IaesEvent::setEpoch(epoch_seconds);  // Call once for real timestamps
```

It takes values, not a `DetectOutput`. Building a standard event must not
require running this runtime's judgment first -- see **The boundary** below.

### OptaMqtt

MQTT publisher with exponential backoff reconnection.

```cpp
OptaMqtt mqtt;
mqtt.begin(eth_client, mqtt_config);
mqtt.publish(doc, "opta.plant1.mcc3");
mqtt.loop();  // Call regularly for keepalive
```

Topic format: `{prefix}/{source}/{event_type}` (e.g. `iaes/opta.plant1.mcc3/asset.measurement`)

## The boundary

This repository holds two different things, and [`BOUNDARY.json`](BOUNDARY.json)
says which is which. CI fails if a directory that holds source is not
classified, and fails if anything on the standard side includes a file from
outside it.

| | |
|---|---|
| `src/iaes/` | **the standard.** Expressing an IAES event: the published vocabulary, asset identity, the envelope, the canonical form, the content hash. Implementable without asking us anything. |
| `src/`, `profiles/`, `examples/` | **this product.** Deadbands, thresholds, when a reading deserves a severity, and how to talk to a device. `profiles/` is an equipment catalog, which IAES governance excludes by name. |
| `tools/`, `test/` | neither. |

Two things were wrong until 2026-09-06, and both looked ordinary:

- `IaesEvent.h` included `IaesDetector.h`, so producing a standard event
  required this product's judgment. Reversed: the builder now takes values.
- `buildHealth` reported a health index of **0.8 on every health event it ever
  emitted**, and synthesized a `failure_mode` from the measurement name. Both
  are the caller's now, and a threshold crossing no longer claims to be a fault
  classification.

The classes that are not the standard no longer carry its name: `IaesRuntime`,
`IaesDetector`, `IaesModbus`, `IaesMqtt` and `IaesConfig` became `Opta*`.

## Configuration

### DeviceProfile

```cpp
DeviceProfile device = {};
strncpy(device.name, "ABB ACS580", sizeof(device.name) - 1);
device.protocol     = ModbusProtocol::RTU;    // RTU or TCP
device.address      = 1;                      // Modbus slave address
device.baud_rate    = 9600;
device.byte_order   = ModbusByteOrder::BIG_ENDIAN_BE;
device.default_function = ModbusFunction::HOLDING_REGISTERS; // FC03

// For TCP devices (e.g. ION 8650):
device.protocol = ModbusProtocol::TCP;
device.ip = IPAddress(192, 168, 1, 100);
device.tcp_port = 502;
```

### RegisterMapping

```cpp
device.addRegister(
    1,                           // reg: Modbus register address
    ModbusDataType::FLOAT32,     // data_type: see table below
    "motor_current",             // measurement_type: IAES field name
    "A",                         // unit: engineering unit
    1.0f,                        // scale: raw * scale = value
    0.5f,                        // deadband: minimum change to trigger event
    85.0f,                       // threshold_high: above -> health event
    IaesSeverity::HIGH           // severity: for threshold events
);
```

Additional fields available on `RegisterMapping` directly:

| Field | Type | Default | Description |
|---|---|---|---|
| `offset` | `float` | 0.0 | Added after scaling: `(raw * scale) + offset` |
| `threshold_low` | `float` | NAN | Below this value -> health event |
| `function` | `ModbusFunction` | inherited | FC03 (holding) or FC04 (input) |
| `threshold_action` | `ThresholdAction` | `HEALTH_EVENT` | Action on threshold: `HEALTH_EVENT` or `MEASUREMENT_EVENT` |

### TimingConfig

```cpp
TimingConfig timing;
timing.register_gap_ms    = 50;    // Between individual register reads
timing.device_gap_ms      = 100;   // Between switching devices
timing.poll_interval_ms   = 1000;  // Full polling cycle interval
timing.publish_interval_ms = 5000; // MQTT publish interval
timing.modbus_timeout_ms  = 1000;  // Modbus response timeout
timing.modbus_retries     = 2;     // Retries per failed read
timing.rs485_pre_delay_us = 1000;  // RS485 pre-transmission delay (us)
timing.rs485_post_delay_us = 1000; // RS485 post-transmission delay (us)
```

### SiteConfig

Top-level configuration that holds all devices, transport, and timing:

```cpp
SiteConfig config;
strncpy(config.source, "opta.plant1.mcc3", sizeof(config.source) - 1);
config.transport = TransportType::MQTT;
strncpy(config.mqtt.broker, "mqtt.example.com", sizeof(config.mqtt.broker) - 1);
config.mqtt.port = 1883;
config.timing = timing;
config.devices[0] = vfd;
config.device_count = 1;
runtime.loadSiteConfig(config);
```

## Data Types Supported

| Enum Value | Registers | Description |
|---|---|---|
| `UINT16` | 1 | Unsigned 16-bit integer |
| `INT16` | 1 | Signed 16-bit integer |
| `UINT32` | 2 | Unsigned 32-bit integer |
| `INT32` | 2 | Signed 32-bit (two's complement) |
| `INT32_SIGNMAG` | 2 | Sign-magnitude (Ducati: MSB = sign bit) |
| `INT32_M10K` | 2 | Schneider Modulus-10000: `hi * 10000 + lo` (signed) |
| `UINT32_M10K` | 2 | Schneider Modulus-10000: `hi * 10000 + lo` (unsigned) |
| `FLOAT32` | 2 | IEEE 754 single-precision float |
| `FLOAT64` | 4 | IEEE 754 double-precision float |
| `BITFIELD` | 1 | Individual bits (status words) |
| `BCD` | 1 | Binary-Coded Decimal |

## Byte Order

| Enum Value | Byte Pattern | Devices |
|---|---|---|
| `BIG_ENDIAN_BE` | AB CD | ABB, Danfoss, Siemens (most common) |
| `LITTLE_ENDIAN_LE` | CD AB | Some legacy devices |
| `WORD_SWAPPED` | CD AB | Schneider, Eastron |
| `BYTE_SWAPPED` | BA DC | Rare |

## IAES Event Output

### asset.measurement

```json
{
  "spec_version": "1.4",
  "id": "a1b2c3d4-5678-4abc-9def-0123456789ab",
  "event_type": "asset.measurement",
  "timestamp": "2026-03-09T14:30:00Z",
  "source": "opta.plant1.mcc3",
  "data": {
    "asset_id": "VFD-ACS580-001",
    "asset_name": "VFD Bomba P-101",
    "plant": "Planta Norte",
    "area": "MCC-3",
    "measurement_type": "motor_current",
    "value": 12.5,
    "unit": "A"
  },
  "content_hash": "a3f2b8c1d4e5f678"
}
```

### asset.health

```json
{
  "spec_version": "1.4",
  "id": "b2c3d4e5-6789-4bcd-aef0-123456789abc",
  "event_type": "asset.health",
  "timestamp": "2026-03-09T14:30:05Z",
  "source": "opta.plant1.mcc3",
  "data": {
    "asset_id": "VFD-ACS580-001",
    "asset_name": "VFD Bomba P-101",
    "plant": "Planta Norte",
    "area": "MCC-3",
    "measurement_type": "vfd_temperature",
    "value": 87.3,
    "unit": "C",
    "severity": "high",
    "threshold_high": 85.0
  },
  "content_hash": "f6e5d4c3b2a19087"
}
```

## Limits

| Constant | Value | Description |
|---|---|---|
| `IAES_MAX_DEVICES` | 8 | Max devices per Opta |
| `IAES_MAX_REGISTERS` | 32 | Max registers per device |
| `IAES_MAX_PENDING` | 64 | Max pending events per publish cycle |

## License

MIT

## Links

- [IAES Standard](https://iaes.dev)
- [pip install iaes](https://pypi.org/project/iaes/) -- Python SDK
- [npm install @iaes/sdk](https://www.npmjs.com/package/@iaes/sdk) -- TypeScript SDK
- [GitHub](https://github.com/wertek-ai/iaes-opta-runtime)
