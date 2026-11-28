#!/usr/bin/env python3
"""Conservative bounds for every complete METAL JSON printf format.

All dynamic strings are bounded IDs/static labels or the pinned source/SDK
identity, at most 64 bytes. Full-width integer and conservative finite-double
spellings are used. Target compilation also checks variadic argument types
through the line() printf-format attribute.
"""
import ast
import json
from pathlib import Path
import re

source = (Path(__file__).resolve().parent / "main/main.c").read_text(encoding="utf-8")
assert '__attribute__((format(printf, 1, 2)))' in source
assert 'strlen(id) > 31' in source
assert 'char text[2048]' in source
records = []
for match in re.finditer(r'line\(\s*((?:"(?:\\.|[^"\\])*"\s*|PRI[a-zA-Z0-9]+\s*)+)', source):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|PRI[a-zA-Z0-9]+', match.group(1))
    fmt = "".join(ast.literal_eval(t) if t.startswith('"') else {
        "PRIu32": "u", "PRIu64": "llu", "PRId64": "lld", "PRIx32": "x"}[t] for t in tokens)
    size, end = 0, 0
    for item in re.finditer(r'%(?:0?\d+)?(?:\.\d+)?(?:ll|l|z)?[sduixXg%]', fmt):
        size += len(fmt[end:item.start()])
        spelling, kind = item.group(), item.group()[-1]
        size += 64 if kind == "s" else 24 if kind == "g" else 20 if "ll" in spelling else (
            11 if kind in "di" else 8 if kind in "xX" else 10 if kind == "u" else 1)
        end = item.end()
    size += len(fmt[end:]) + 1
    record_type = re.search(r'"type":"([^"%]+)"', fmt)
    assert record_type and size <= 2048, (fmt, size)
    records.append({"type": record_type[1], "upper_bound_including_NUL": size, "capacity": 2048})
assert {r["type"] for r in records} == {"info", "temperature", "error", "init", "tune", "rx_diagnostics", "armed", "snapshot", "run_end"}
print(json.dumps({"schema": "sdr-metal-output-bounds/1", "evidence_class": "STATIC_BOUND",
                  "max_dynamic_string_bytes_assumed": 64, "records": records, "valid": True}))
