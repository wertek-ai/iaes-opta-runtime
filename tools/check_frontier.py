#!/usr/bin/env python3
"""The standard must not depend on the steward.

A reader has to be able to implement IAES without asking us anything and
without pointing at anything of ours. That is the first of the four boundary
questions, and it is the one that fails silently, because a violation does not
look like the word "Wertek" in the text -- it looks like a default value, an
example payload, or an error message that happens to name our namespace.

So this checks for **normative dependency**, not for the word. "IAES is
maintained by Wertek AI" is attribution and is fine. Shipping
`https://api.wertek.ai/iaes/ingest` as a default is not: a reader who installs
the node and clicks through is pointed at our production server without being
told.

Three rules, each armed against a crossing that was really in this repository:

  host       our infrastructure as a value, a default or a placeholder
  namespace  our namespace taught as what a producer identity looks like --
             including in the normative schema, which is what somebody
             implementing from the artifact alone reads
  model      our operating model as structure: multi-tenancy in a topic layout

A path like `/iaes/ingest` is deliberately NOT a violation. Anyone can mount
that route on their own server; it names no infrastructure of ours. The host in
front of it is the dependency, and the host rule catches it.

Where we may be named: prose. Markdown prose is where the standard talks about
things, us included -- attribution, stewardship, and the changelog's record of
exactly which of our defaults were removed. Code fences and source files are
where a reader copies and a machine executes, and those are scanned in full.

Two gaps, named rather than papered over:

  - Prose that tells a reader to point at our host in words rather than in a
    value would pass. In practice the configuration it describes lives in a
    source file, which is scanned.
  - Customer site names are not checked. Recognising them needs the list of our
    customers, which does not belong in a public repository; that check runs
    before publishing, from the private side.
"""

# SPDX-License-Identifier: MIT

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

RULES = {
    "host": [r"\b(?:mqtt|api|app|admin|iaes)\.wertek\.ai\b"],
    "namespace": [r"\bwertek\.(?:ai|edge|energy)\.[a-z_]+"],
    "model": [r"\{org_id\}", r"\borganization_id\b"],
}

EXPLAIN = {
    "host": "our infrastructure as a value: a reader who copies this is pointed at our server",
    "namespace": "our namespace taught as a producer identity -- use acme.* or vendor.*",
    "model": "our operating model as structure -- IAES carries no tenancy",
}

# Where naming us is legitimate, and this file, which must contain the patterns.
ALLOWED_FILES = {"LICENSE", "tools/check_frontier.py",
                 # Authorship, not dependency.
                 "library.json", "library.properties"}

SKIP_DIRS = {".git", ".pio", "build", "__pycache__", ".pytest_cache"}
SKIP_SUFFIXES = {".png", ".jpg", ".svg", ".ico", ".zip", ".pdf", ".lock"}

FENCE = re.compile(r"^\s*(```|~~~)")


def scannable_lines(path: Path, text: str):
    """Every line of a source file; only fenced code in Markdown."""
    lines = text.split("\n")
    if path.suffix.lower() not in {".md", ".markdown"}:
        yield from enumerate(lines, 1)
        return
    inside = False
    for n, line in enumerate(lines, 1):
        if FENCE.match(line):
            inside = not inside
            continue
        if inside:
            yield n, line


def scan(root: Path = ROOT):
    findings = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or any(p in SKIP_DIRS for p in path.parts):
            continue
        rel = path.relative_to(root).as_posix()
        if path.suffix in SKIP_SUFFIXES or rel in ALLOWED_FILES:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for n, line in scannable_lines(path, text):
            for kind, patterns in RULES.items():
                if any(re.search(p, line) for p in patterns):
                    findings.append((rel, n, kind, line.strip()[:100]))
    return findings


def main() -> None:
    ap = argparse.ArgumentParser(description="Check that IAES depends on nothing of ours.")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    findings = scan()

    if args.json:
        print(json.dumps([{"file": f, "line": n, "kind": k, "text": t}
                          for f, n, k, t in findings], indent=2))
        raise SystemExit(1 if findings else 0)

    if not findings:
        print("the standard depends on no infrastructure, namespace or model of ours")
        print("not checked here: customer site names -- that runs before publishing, "
              "from the private side, where the customer list lives")
        return

    for rel, n, kind, text in findings:
        print(f"error: {rel}:{n} -- {EXPLAIN[kind]}", file=sys.stderr)
        print(f"       {text}", file=sys.stderr)
    print(f"\n{len(findings)} boundary violation(s)", file=sys.stderr)
    raise SystemExit(1)


if __name__ == "__main__":
    main()
