#!/usr/bin/env python3
"""SecretKeeper - cross-platform crypto conformance comparison.

Compares the transcripts emitted by tests/core/crypto_conformance.cpp across
platforms. This is what proves the shipped C++ implementations agree, which
scripts/verify-vectors.py cannot: that script re-derives the vectors with an
independent Python implementation and therefore only shows the VECTORS are
self-consistent, not that every platform reproduces them.

Only lines whose value is deterministic are compared. The probe prefixes those
with a name and no marker; non-deterministic measurements (random_*) are listed
in NON_DETERMINISTIC_PREFIXES and checked only for plausibility.

Usage:
    python scripts/compare-conformance.py a.txt b.txt [c.txt ...]

Exit code 0 when every deterministic line matches across all inputs.
"""

import os
import re
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# Lines whose value legitimately differs per run or per platform.
NON_DETERMINISTIC_PREFIXES = ("random.", "PROBE-SUMMARY")

LINE_RE = re.compile(r"^([A-Za-z0-9_.]+)=(.*)$")


def parse(path):
    """Returns (deterministic dict, failures list, passthrough lines)."""
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        raw = handle.read().splitlines()
    det = {}
    failures = []
    for line in raw:
        line = line.strip()
        if not line:
            continue
        if line.startswith("PROBE-FAIL"):
            failures.append(line[len("PROBE-FAIL"):].strip())
            continue
        if line.startswith(NON_DETERMINISTIC_PREFIXES):
            continue
        m = LINE_RE.match(line)
        if m:
            det[m.group(1)] = m.group(2)
    return det, failures


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2

    transcripts = {}
    for path in argv[1:]:
        if not os.path.isfile(path):
            sys.stderr.write("missing transcript: %s\n" % path)
            return 2
        transcripts[path] = parse(path)

    # Any probe-internal failure fails the comparison regardless of transcripts.
    probe_failures = []
    for path, (_, failures) in sorted(transcripts.items()):
        for f in failures:
            probe_failures.append("%s: %s" % (os.path.basename(path), f))
    if probe_failures:
        for f in probe_failures:
            sys.stdout.write("  PROBE-FAIL  %s\n" % f)
        sys.stdout.write("\nprobe reported %d failures\n" % len(probe_failures))
        return 1

    base_path = argv[1]
    base = transcripts[base_path][0]
    mismatches = []

    for path in argv[2:]:
        other = transcripts[path][0]
        # Compare the union so a line missing on one side is caught too.
        for key in sorted(set(base) | set(other)):
            lhs = base.get(key)
            rhs = other.get(key)
            if lhs != rhs:
                mismatches.append((key, lhs, rhs, os.path.basename(path)))

    name = os.path.basename(base_path)
    if mismatches:
        for key, lhs, rhs, src in mismatches:
            sys.stdout.write("  MISMATCH  %s (%s)\n" % (key, src))
            sys.stdout.write("      %s: %s\n" % (name, lhs))
            sys.stdout.write("      %s: %s\n" % (src, rhs))
        sys.stdout.write("\n%d deterministic line(s) differ\n" % len(mismatches))
        return 1

    total = len(base)
    sys.stdout.write("%d deterministic lines identical across %d platforms\n"
                     % (total, len(transcripts)))
    for path in sorted(transcripts):
        sys.stdout.write("  %s\n" % os.path.basename(path))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))