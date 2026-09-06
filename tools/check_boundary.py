#!/usr/bin/env python3
"""Every directory says which side of the boundary it is on, and the IAES
implementation layer may not reach across.

Two questions, both of which this repository got wrong and neither of which a
reviewer would reliably catch:

  1. Does this directory implement IAES, belong to the product, or neither?
     An unclassified
     directory fails, so the question gets answered when the code is written --
     which is when the answer is cheap and obvious. Asked later, it is neither.

  2. Does anything in the implementation layer depend on something outside it?
     Until
     2026-09-06 `IaesEvent.h` included `IaesDetector.h`, so building an IAES
     event required running this product's judgment first. The include looked
     ordinary. Nothing but a check like this notices.

Stdlib only, so it runs before any toolchain does.
"""

# SPDX-License-Identifier: MIT

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp", ".ino", ".py", ".json"}
SKIP_DIRS = {".git", ".pio", "build", "__pycache__", ".github"}


def load():
    path = ROOT / "BOUNDARY.json"
    if not path.exists():
        print("error: BOUNDARY.json is missing. Every IAES repository declares "
              "which of its directories implement it.", file=sys.stderr)
        raise SystemExit(1)
    return json.loads(path.read_text(encoding="utf-8"))


def source_dirs():
    """Directories that actually hold source, relative to the repository."""
    found = set()
    for path in ROOT.rglob("*"):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        if any(p in SKIP_DIRS for p in path.parts):
            continue
        rel = path.relative_to(ROOT)
        if rel.parent == Path("."):
            continue  # a file at the root belongs to no directory
        found.add(rel.parent.as_posix())
    return found


def owning_entry(rel_dir: str, declared: dict) -> str | None:
    """The most specific declared directory that contains this one."""
    best = None
    for name in declared:
        if rel_dir == name or rel_dir.startswith(name + "/"):
            if best is None or len(name) > len(best):
                best = name
    return best


def includes(path: Path):
    for n, line in enumerate(path.read_text(encoding="utf-8", errors="replace").split("\n"), 1):
        s = line.strip()
        if s.startswith("#include") and '"' in s:
            yield n, s.split('"')[1]


def main() -> None:
    spec = load()
    declared = spec["directories"]
    errors = []

    # 1. Nothing unclassified.
    for d in sorted(source_dirs()):
        if owning_entry(d, declared) is None:
            errors.append(
                f"{d}/ holds source and is not classified in BOUNDARY.json. "
                f"Say whether it is standard_implementation, product or neutral.")

    # 2. The standard side reaches nothing outside itself.
    for name, entry in declared.items():
        if entry.get("class") != "standard_implementation":
            continue
        allowed = entry.get("may_include", [name])
        base = ROOT / name
        if not base.exists():
            errors.append(f"BOUNDARY.json declares {name}/, which does not exist.")
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            for n, target in includes(path):
                resolved = (path.parent / target).resolve()
                if not resolved.exists():
                    continue  # a library header, not a file of ours
                rel = resolved.relative_to(ROOT).parent.as_posix()
                if not any(rel == a or rel.startswith(a + "/") for a in allowed):
                    errors.append(
                        f"{path.relative_to(ROOT).as_posix()}:{n} includes "
                        f"\"{target}\", which is outside the implementation layer. "
                        f"What implements the standard must not depend on the product.")

    if errors:
        for e in errors:
            print("error: " + e, file=sys.stderr)
        print(f"\n{len(errors)} boundary problem(s)", file=sys.stderr)
        raise SystemExit(1)

    counts = {}
    for entry in declared.values():
        counts[entry["class"]] = counts.get(entry["class"], 0) + 1
    summary = ", ".join(f"{v} {k}" for k, v in sorted(counts.items()))
    print(f"every source directory is classified ({summary}), and the IAES "
          f"implementation layer includes nothing outside itself")


if __name__ == "__main__":
    main()
