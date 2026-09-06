#!/usr/bin/env python3
"""
yaml2json.py — Convert YAML device profiles to JSON for Arduino.

IAES device profiles are authored in YAML (human-friendly) and
converted to JSON (Arduino-friendly) for use with iaes-opta-runtime.

Usage:
    python yaml2json.py profile.yaml              # → profile.json
    python yaml2json.py profiles/ --output build/  # Convert directory
    python yaml2json.py profile.yaml --validate    # Validate only

Requires: pip install pyyaml
"""

# SPDX-License-Identifier: MIT

import argparse
import json
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("Error: PyYAML required. Install with: pip install pyyaml")
    sys.exit(1)


VALID_DATA_TYPES = {
    "uint16", "int16", "uint32", "int32",
    "float32", "float64", "bitfield", "bcd",
}

VALID_BYTE_ORDERS = {
    "big_endian", "little_endian", "word_swapped", "byte_swapped",
}

VALID_PROTOCOLS = {"modbus_rtu", "modbus_tcp"}

VALID_SEVERITIES = {"info", "low", "medium", "high", "critical"}


def validate_profile(data: dict, filename: str) -> list[str]:
    """Validate a device profile. Returns list of errors (empty = valid)."""
    errors = []

    # Required top-level fields
    for field in ["device", "protocol", "registers"]:
        if field not in data:
            errors.append(f"Missing required field: {field}")

    if "protocol" in data and data["protocol"] not in VALID_PROTOCOLS:
        errors.append(f"Invalid protocol: {data['protocol']} (valid: {VALID_PROTOCOLS})")

    if "byte_order" in data and data["byte_order"] not in VALID_BYTE_ORDERS:
        errors.append(f"Invalid byte_order: {data['byte_order']} (valid: {VALID_BYTE_ORDERS})")

    # Validate registers
    regs = data.get("registers", [])
    if not regs:
        errors.append("No registers defined")

    seen_regs = set()
    for i, reg in enumerate(regs):
        prefix = f"registers[{i}]"

        for field in ["reg", "measurement_type", "unit"]:
            if field not in reg:
                errors.append(f"{prefix}: missing required field '{field}'")

        if "data_type" in reg and reg["data_type"] not in VALID_DATA_TYPES:
            errors.append(f"{prefix}: invalid data_type '{reg['data_type']}'")

        if "severity" in reg and reg["severity"] not in VALID_SEVERITIES:
            errors.append(f"{prefix}: invalid severity '{reg['severity']}'")

        reg_addr = reg.get("reg")
        if reg_addr is not None:
            if reg_addr in seen_regs:
                errors.append(f"{prefix}: duplicate register address {reg_addr}")
            seen_regs.add(reg_addr)

        # Validate scale/offset are numbers
        for num_field in ["scale", "offset", "deadband", "threshold_high", "threshold_low"]:
            if num_field in reg and not isinstance(reg[num_field], (int, float)):
                errors.append(f"{prefix}: {num_field} must be a number")

    return errors


def convert_file(yaml_path: Path, output_dir: Path | None = None) -> Path:
    """Convert a single YAML profile to JSON."""
    with open(yaml_path) as f:
        data = yaml.safe_load(f)

    # Validate
    errors = validate_profile(data, str(yaml_path))
    if errors:
        print(f"Validation errors in {yaml_path}:")
        for err in errors:
            print(f"  - {err}")
        raise ValueError(f"{len(errors)} validation errors")

    # Set defaults
    for reg in data.get("registers", []):
        reg.setdefault("data_type", "uint16")
        reg.setdefault("scale", 1.0)
        reg.setdefault("offset", 0.0)
        reg.setdefault("deadband", 0.0)
        reg.setdefault("function", "holding_registers")

    # Output path
    if output_dir:
        output_dir.mkdir(parents=True, exist_ok=True)
        json_path = output_dir / yaml_path.with_suffix(".json").name
    else:
        json_path = yaml_path.with_suffix(".json")

    with open(json_path, "w") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)

    return json_path


def main():
    parser = argparse.ArgumentParser(
        description="Convert YAML device profiles to JSON for iaes-opta-runtime"
    )
    parser.add_argument("input", help="YAML file or directory")
    parser.add_argument("--output", "-o", help="Output directory (default: same as input)")
    parser.add_argument("--validate", "-v", action="store_true", help="Validate only, don't convert")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_dir = Path(args.output) if args.output else None

    if input_path.is_file():
        files = [input_path]
    elif input_path.is_dir():
        files = sorted(input_path.rglob("*.yaml")) + sorted(input_path.rglob("*.yml"))
    else:
        print(f"Error: {input_path} not found")
        sys.exit(1)

    if not files:
        print(f"No YAML files found in {input_path}")
        sys.exit(1)

    errors_total = 0
    converted = 0

    for yaml_file in files:
        if args.validate:
            with open(yaml_file) as f:
                data = yaml.safe_load(f)
            errors = validate_profile(data, str(yaml_file))
            if errors:
                print(f"FAIL {yaml_file}:")
                for err in errors:
                    print(f"  - {err}")
                errors_total += len(errors)
            else:
                print(f"OK   {yaml_file}")
        else:
            try:
                json_path = convert_file(yaml_file, output_dir)
                print(f"OK   {yaml_file} -> {json_path}")
                converted += 1
            except ValueError:
                errors_total += 1

    if args.validate:
        print(f"\n{len(files)} files checked, {errors_total} errors")
    else:
        print(f"\n{converted}/{len(files)} files converted")

    sys.exit(1 if errors_total > 0 else 0)


if __name__ == "__main__":
    main()
