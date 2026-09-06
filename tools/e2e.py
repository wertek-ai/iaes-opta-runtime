#!/usr/bin/env python3
"""The gate: a stranger clones this, runs one command, and gets a valid IAES
1.4 event out of a runtime that never saw an Opta.

    pip install "iaes[validate]" platformio
    python tools/e2e.py

It starts a Modbus TCP slave and an MQTT sink -- both here, both stdlib, so
there is nothing to install and no broker to configure -- runs the real runtime
against them, and validates what arrives with the specification's own Python
validator.

Nothing about the runtime is stubbed. It opens real sockets, speaks real Modbus
TCP and real MQTT, and the number that comes out the far end is compared with
the number the slave served, so a byte-order or scaling defect fails here.
"""

from __future__ import annotations

import json
import os
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time

# Holding register 1 holds 125; the runtime scales it by 0.1.
REGISTERS = {1: 125}
EXPECTED_VALUE = 12.5
EXPECTED_UNIT = "A"


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class ModbusSlave(threading.Thread):
    """Enough Modbus TCP to answer a read of holding or input registers."""

    daemon = True

    def __init__(self, port: int):
        super().__init__()
        self.port = port
        self.requests = 0
        self._stop = threading.Event()
        self._srv = socket.socket()
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind(("127.0.0.1", port))
        self._srv.listen(1)
        self._srv.settimeout(0.3)

    def run(self) -> None:
        while not self._stop.is_set():
            try:
                conn, _ = self._srv.accept()
            except socket.timeout:
                continue
            except OSError:
                return  # stop() closed the listener
            with conn:
                conn.settimeout(0.5)
                while not self._stop.is_set():
                    head = self._recv_exactly(conn, 8)
                    if not head:
                        break
                    txn, _proto, _len, unit, fc = struct.unpack(">HHHBB", head)
                    body = self._recv_exactly(conn, 4)
                    if not body:
                        break
                    address, count = struct.unpack(">HH", body)
                    if fc not in (3, 4):
                        conn.sendall(struct.pack(">HHHBBB", txn, 0, 3, unit, fc | 0x80, 0x01))
                        continue
                    values = [REGISTERS.get(address + i, 0) for i in range(count)]
                    payload = b"".join(struct.pack(">H", v) for v in values)
                    conn.sendall(
                        struct.pack(">HHHBBB", txn, 0, 3 + len(payload), unit, fc, len(payload))
                        + payload
                    )
                    self.requests += 1

    @staticmethod
    def _recv_exactly(conn: socket.socket, n: int) -> bytes | None:
        buf = b""
        while len(buf) < n:
            try:
                chunk = conn.recv(n - len(buf))
            except socket.timeout:
                return None
            if not chunk:
                return None
            buf += chunk
        return buf

    def stop(self) -> None:
        self._stop.set()
        self._srv.close()


class MqttSink(threading.Thread):
    """Enough MQTT 3.1.1 to accept a connection and collect what is published.

    Terminating the protocol here rather than running a broker is deliberate:
    the gate should need nothing installed. Pointing the runtime at mosquitto
    instead works and exercises the same frames.
    """

    daemon = True

    def __init__(self, port: int):
        super().__init__()
        self.port = port
        self.messages: list[tuple[str, str]] = []
        self._stop = threading.Event()
        self._srv = socket.socket()
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind(("127.0.0.1", port))
        self._srv.listen(1)
        self._srv.settimeout(0.3)

    def run(self) -> None:
        while not self._stop.is_set():
            try:
                conn, _ = self._srv.accept()
            except socket.timeout:
                continue
            except OSError:
                return  # stop() closed the listener
            with conn:
                conn.settimeout(0.5)
                self._serve(conn)

    def _serve(self, conn: socket.socket) -> None:
        while not self._stop.is_set():
            frame = self._read_frame(conn)
            if frame is None:
                return
            kind, payload = frame
            if kind == 0x1:                       # CONNECT
                conn.sendall(bytes([0x20, 0x02, 0x00, 0x00]))
            elif kind == 0x3:                     # PUBLISH, QoS 0
                tlen = struct.unpack(">H", payload[:2])[0]
                topic = payload[2 : 2 + tlen].decode("utf-8", "replace")
                body = payload[2 + tlen :].decode("utf-8", "replace")
                self.messages.append((topic, body))
            elif kind == 0xC:                     # PINGREQ
                conn.sendall(bytes([0xD0, 0x00]))
            elif kind == 0xE:                     # DISCONNECT
                return

    def _read_frame(self, conn: socket.socket) -> tuple[int, bytes] | None:
        head = self._recv_exactly(conn, 1)
        if not head:
            return None
        kind = head[0] >> 4
        length, shift = 0, 0
        while True:
            b = self._recv_exactly(conn, 1)
            if not b:
                return None
            length |= (b[0] & 0x7F) << shift
            if not b[0] & 0x80:
                break
            shift += 7
        body = self._recv_exactly(conn, length) if length else b""
        if body is None:
            return None
        return kind, body

    @staticmethod
    def _recv_exactly(conn: socket.socket, n: int) -> bytes | None:
        buf = b""
        while len(buf) < n:
            try:
                chunk = conn.recv(n - len(buf))
            except socket.timeout:
                return None
            if not chunk:
                return None
            buf += chunk
        return buf

    def stop(self) -> None:
        self._stop.set()
        self._srv.close()


# Fields that differ every run. Everything else the README documents must match
# byte for byte, or the README is describing a program that no longer exists.
VOLATILE = {"event_id", "correlation_id", "timestamp", "content_hash"}


def compare_with_readme(event: dict) -> list[str]:
    """The README documents the bytes that come out, or CI says so."""
    readme = pathlib.Path(__file__).resolve().parent.parent / "README.md"
    text = readme.read_text(encoding="utf-8")
    marker = "<!-- e2e:example-event -->"
    if marker not in text:
        return [f"README.md has no {marker}: nothing pins the documented shape"]
    block = text.split(marker, 1)[1].split("```json", 1)[1].split("```", 1)[0]
    try:
        documented = json.loads(block)
    except json.JSONDecodeError as e:
        return [f"README.md example is not JSON: {e}"]

    def strip(d):
        return {k: (strip(v) if isinstance(v, dict) else v)
                for k, v in d.items() if k not in VOLATILE}

    a, b = strip(documented), strip(event)
    if a == b:
        return []
    out = ["README.md documents a different event than the runtime publishes:"]
    for k in sorted(set(a) | set(b)):
        if a.get(k) != b.get(k):
            out.append(f"    {k}: README {a.get(k)!r} vs wire {b.get(k)!r}")
    return out


def validator_that_can_say_no():
    """Returns a validator, having first proved it rejects something invalid.

    `pip install iaes` gives the package without jsonschema, and validation
    then raises rather than passing -- but a gate that trusts a validator it
    has never seen refuse anything is theatre. So it is shown an event with no
    correlation_id, which the envelope requires, and must reject it before it
    is believed when it approves.
    """
    try:
        from iaes.validation import validate
    except ImportError:
        print('error: the validator is missing. pip install "iaes[validate]"',
              file=sys.stderr)
        return None

    known_bad = {
        "spec_version": "1.4",
        "event_type": "asset.measurement",
        "event_id": "00000000-0000-4000-8000-000000000000",
        # correlation_id deliberately absent
        "timestamp": "2026-09-06T00:00:00+00:00",
        "source": "acme.control",
        "asset": {"asset_id": "CONTROL-1"},
        "data": {"measurement_type": "motor_current", "value": 1, "unit": "A"},
    }
    try:
        validate(known_bad)
    except Exception as e:
        if "correlation_id" in str(e):
            return validate
        print(f'error: the validator failed for the wrong reason: {e}', file=sys.stderr)
        print('       pip install "iaes[validate]"', file=sys.stderr)
        return None
    print("error: the validator accepted an event with no correlation_id. "
          "Whatever it is checking, it is not the envelope.", file=sys.stderr)
    return None


def main() -> int:
    validate = validator_that_can_say_no()
    if validate is None:
        return 2

    modbus = ModbusSlave(_free_port())
    mqtt = MqttSink(_free_port())
    modbus.start()
    mqtt.start()
    print(f"modbus slave on {modbus.port}, mqtt sink on {mqtt.port}")

    env = dict(os.environ,
               IAES_E2E_MODBUS_PORT=str(modbus.port),
               IAES_E2E_MQTT_PORT=str(mqtt.port))
    # PlatformIO usually lives in its own environment, not in the interpreter
    # running this script.
    pio = shutil.which("pio") or shutil.which("platformio")
    base = [pio] if pio else [sys.executable, "-m", "platformio"]
    cmd = base + ["test", "-e", "host", "-f", "test_e2e"]
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
    print(proc.stdout[-2500:])
    if proc.returncode != 0:
        print(proc.stderr[-2000:], file=sys.stderr)

    time.sleep(0.3)
    modbus.stop()
    mqtt.stop()

    print(f"\nmodbus reads served: {modbus.requests}")
    print(f"mqtt messages received: {len(mqtt.messages)}")

    if proc.returncode != 0:
        print("\nFAIL: the runtime's own tests did not pass", file=sys.stderr)
        return 1
    if not mqtt.messages:
        print("\nFAIL: the runtime published nothing", file=sys.stderr)
        return 1

    failures = []
    measurements = 0
    for topic, body in mqtt.messages:
        try:
            event = json.loads(body)
        except json.JSONDecodeError as e:
            failures.append(f"{topic}: not JSON ({e})")
            continue
        try:
            validate(event)
        except Exception as e:                     # the validator's own type
            failures.append(f"{topic}: {e}")
            continue
        if event.get("event_type") == "asset.measurement":
            measurements += 1
            data = event.get("data", {})
            if abs(float(data.get("value", 0)) - EXPECTED_VALUE) > 1e-9:
                failures.append(
                    f"{topic}: value {data.get('value')} != {EXPECTED_VALUE} "
                    f"(the slave served {REGISTERS[1]} and the scale is 0.1)")
            if data.get("unit") != EXPECTED_UNIT:
                failures.append(f"{topic}: unit {data.get('unit')!r} != {EXPECTED_UNIT!r}")

    first = json.loads(mqtt.messages[0][1])
    print("\nfirst event on the wire:")
    print(json.dumps(first, indent=2, ensure_ascii=False))

    failures += compare_with_readme(first)

    if failures:
        print("\nFAIL", file=sys.stderr)
        for f in failures:
            print("  " + f, file=sys.stderr)
        return 1
    if measurements == 0:
        print("\nFAIL: nothing published was an asset.measurement", file=sys.stderr)
        return 1

    print(f"\nOK: {len(mqtt.messages)} event(s) published, all valid against IAES 1.4, "
          f"{measurements} measurement(s) carrying the value the slave served")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
