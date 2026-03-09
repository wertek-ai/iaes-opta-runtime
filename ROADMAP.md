# iaes-opta-runtime -- Roadmap

## v0.1.0 -- Current (MVP)

- [x] Non-blocking Modbus RTU reader (state machine, no `delay()`)
- [x] Deadband + threshold change detection
- [x] IAES v1.2 event builder (JSON, FNV-1a content hash)
- [x] MQTT publisher with exponential backoff reconnection
- [x] 6 device profiles (ABB ACS580, Danfoss FC, Eastron X835, Schneider ION 8650, Ducati R5/R8/R14, Atlas Copco GA55)
- [x] 3 examples (single VFD, multi-device, energy meter)
- [x] PlatformIO + Arduino IDE support (`library.json` + `library.properties`)
- [x] `INT32_SIGNMAG` data type (Ducati sign-magnitude: MSB = sign bit)
- [x] `INT32_M10K` / `UINT32_M10K` data types (Schneider Modulus-10000: `hi * 10000 + lo`)
- [x] `IaesSeverity` enum (saves ~2.8 KB RAM vs `char` arrays)
- [x] `ModbusFunction` enum -- FC03 (holding registers) and FC04 (input registers)
- [x] `ModbusByteOrder` enum -- big-endian, little-endian, word-swapped, byte-swapped
- [x] Pseudo-UUID v4 generation for event IDs
- [x] YAML-to-JSON converter tool (`tools/yaml2json.py`)

## v0.2.0 -- Hardening

- [ ] **Capacitor bank example** (Ducati R5/R8/R14)
  - Demonstrate INT32_SIGNMAG reads, reactive power, cos-phi, step status
  - Include alarm thresholds for capacitor temperature and THD

- [ ] **Danfoss FC example** with conversion index notes
  - Document the Danfoss register-to-parameter mapping (conversion index)
  - Show how to read process feedback, motor status, and fault codes

- [ ] **Address offset field in DeviceProfile**
  - Currently: documented in profile JSON `address_note` but not applied automatically
  - Proposed: `int16_t address_offset = 0` in `DeviceProfile`, applied in `readRegister()`
  - Ducati: offset = -1 (documented address - 1 = wire address)
  - Danfoss: offset = -1 (4X register - 1 = wire address)
  - ABB / Eastron: offset = 0 (addresses already 0-based)
  - This removes the need for manual address adjustment in sketch code

- [ ] **Modbus TCP support** (`IaesModbusTCP` class using `EthernetClient`)
  - ION 8650 requires TCP; current runtime only supports RTU
  - Use ArduinoModbus TCP client (`ModbusTCPClient`)
  - New state machine variant or shared base with RTU reader
  - `DeviceProfile.protocol` field already exists (`ModbusProtocol::TCP`)

- [ ] **HTTPS transport** (`IaesHttps` class using `WiFiSSLClient`)
  - POST IAES events to an HTTPS endpoint as alternative to MQTT
  - `HttpsConfig` struct already defined in `IaesConfig.h`
  - TLS certificate pinning (optional, for production)

- [ ] **Retry logic for failed Modbus reads**
  - `TimingConfig.modbus_retries` field exists (default: 2) but is not wired into the state machine
  - On read failure: retry N times before marking register as failed
  - Track consecutive failures per register in `RegisterMapping`

- [ ] **Error event emission**
  - On persistent read failures (e.g. 5 consecutive), emit `asset.health` with `severity=critical`
  - Include device name, register address, error code in event data
  - Configurable failure threshold per device

- [ ] **NTP time sync**
  - Auto-fetch NTP on Ethernet init, call `IaesEvent::setEpoch()`
  - Periodic re-sync (e.g. every 6 hours)
  - Fallback to millis-based timestamps if NTP unavailable

## v0.3.0 -- Profile Loader

- [ ] **JSON profile loader from SD card or LittleFS**
  - Parse `DeviceProfile` JSON at runtime instead of hardcoding in `.ino`
  - Use ArduinoJson to deserialize profile files into `DeviceProfile` structs
  - Hot-reload profiles without reflashing firmware
  - Directory convention: `/profiles/{category}/{manufacturer}/{model}.json`

- [ ] **YAML-to-JSON converter improvements** (`tools/yaml2json.py`)
  - Batch conversion (entire directory)
  - Validate required fields during conversion
  - Generate C++ header from profile (optional, for compile-time embedding)

- [ ] **Profile validation tool**
  - Verify register addresses do not overlap
  - Check data types match register count (e.g. FLOAT32 needs 2 registers)
  - Validate scale/offset/deadband values are reasonable
  - Warn on missing thresholds for critical measurements

- [ ] **Web-based profile configurator** (optional)
  - Browser UI to build/edit device profiles visually
  - Export as JSON for SD card or as C++ code for embedding
  - Register address reference tables per manufacturer

## v1.0.0 -- Production Ready

- [ ] **OTA firmware updates via MQTT command**
  - Listen on a dedicated MQTT topic for update commands
  - Download firmware binary from HTTPS URL
  - Verify checksum before flashing
  - Automatic rollback on boot failure

- [ ] **Watchdog timer integration**
  - Hardware watchdog on Opta (STM32 IWDG)
  - Reset if `poll()` not called within timeout
  - Log watchdog resets to persistent storage

- [ ] **Diagnostic endpoint**
  - Register dump: current values, last read time, error count per register
  - Connection status: Modbus, MQTT, Ethernet
  - Memory usage: free heap, stack high-water mark
  - Expose via MQTT topic or serial command

- [ ] **Unit tests**
  - Desktop simulation using ArduinoFake or similar mock framework
  - Test change detection logic (deadband, thresholds, edge cases)
  - Test data type conversions (all 11 types, all byte orders)
  - Test event builder (JSON structure, content hash)
  - Test state machine transitions

- [ ] **CI/CD pipeline**
  - PlatformIO CI builds for Opta target (compile check, no hardware needed)
  - Run unit tests on push
  - Automated library.json/library.properties version bump
  - GitHub Actions workflow

- [ ] **Published to PlatformIO Registry**
  - `pio pkg publish` to PlatformIO Registry
  - Published to Arduino Library Manager
  - Semantic versioning with changelog

- [ ] **Multi-serial support**
  - Support multiple RS485 ports (Opta has 1 built-in, but shields can add more)
  - Independent state machines per serial port
  - Useful for isolating slow devices from fast polling loops

## Device Profile Backlog

### VFDs

- [ ] Siemens SINAMICS G120
- [ ] WEG CFW11
- [ ] Schneider ATV320
- [ ] Yaskawa GA500
- [ ] Mitsubishi FR-E800

### Energy Meters

- [ ] SEL-735 Power Quality Meter
- [ ] Elster A1800 Revenue Meter
- [ ] Schneider PM5xxx series (PM5100, PM5300, PM5500)
- [ ] Socomec DIRIS A40 / A60
- [ ] Carlo Gavazzi EM340

### Compressors

- [ ] Atlas Copco GA75 / GA90
- [ ] Kaeser BSD series
- [ ] Ingersoll Rand R-Series

### Other

- [ ] Generic Modbus RTU template (user fills in registers)
- [ ] Generic Modbus TCP template
