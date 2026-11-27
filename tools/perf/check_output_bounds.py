"""Conservative static telemetry budgets; these are not USB/hardware tests."""
import json
import pathlib
import re


SPECIFIER = re.compile(r'%[-+0-9.#]*(?P<length>ll|l|z)?(?P<type>[sudgfcx])')


def formats(source, writer):
    return [json.loads('"' + value + '"') for value in
            re.findall(r'\b' + writer + r'\.printf\("((?:\\.|[^"\\])*)"', source)]


def link_record_bound(record, string_bytes=64, histogram_entries=0):
    """Bound ASCII strings and actual integer types, retaining static JSON bytes."""
    def replace(match):
        kind = match["type"]
        if kind == "s":
            return "S" * string_bytes
        if kind not in "udx":
            raise ValueError("Unexpected link conversion: " + match[0])
        if kind == "x":
            if match[0] != "%02x":
                raise ValueError("Only audited uint8 MAC hex fields have a two-byte bound")
            return "f" * 2
        return "9" * (20 if match["length"] == "ll" else 11 if kind == "d" else 10)
    remainder = SPECIFIER.sub("", record)
    if "%" in remainder:
        raise ValueError("Unsupported printf conversion in telemetry format")
    rendered = SPECIFIER.sub(replace, record)
    # Each uint32 histogram count has <=10 digits plus its separator. Using
    # eleven for every element overbounds the first (no preceding comma).
    return len(rendered.encode("ascii")) + histogram_entries * 11 + (3 if histogram_entries else 0) + 1


def check():
    root = pathlib.Path(__file__).resolve().parents[2]
    source = (root / "src/perf_kernels.cpp").read_text(encoding="utf-8")
    record = next(value for value in formats(source, "outputLine") if '"type":"kernel"' in value)
    # Every actual descriptor/capability/name is <=160 ASCII bytes. A rendered
    # numeric field is <=32 bytes even at uint64/double extremes used here.
    worst = re.sub(r'%[-+0-9.#]*(?:ll|l|z)?([sudgfc])',
                   lambda match: "S" * 160 if match.group(1) == "s" else "9" * 32, record)
    maximum = len(worst.encode()) + 256 * 11 + 4  # uint32 values/comma + ]}\n/NUL
    capacity = 6144
    if maximum >= capacity:
        raise ValueError(f"Kernel record bound {maximum} exceeds capacity {capacity}")
    link = (root / "src/perf_link.cpp").read_text(encoding="utf-8")
    ble = (root / "src/perf_ble.cpp").read_text(encoding="utf-8")
    # Every live link %s is an enum/boolean, bounded IPv4/RSSI, runId[32],
    # operation or BLE stage. The latter use audited static ASCII literals.
    # Reject an expanded literal pool instead of silently retaining an obsolete
    # 64-byte assumption. printf/scanf strings themselves are not dynamic %s.
    plain = [json.loads('"' + value + '"') for text in (link, ble)
             for value in re.findall(r'"((?:\\.|[^"\\])*)"', text)
             if "%" not in value and "\\n" not in value]
    literal_max = max(len(value.encode("ascii")) for value in plain)
    if literal_max > 64 or not re.search(r'\brunId\[32\]', link):
        raise ValueError("Re-audit link dynamic-string bounds")
    link_formats = formats(link, "line")
    profile_guard = re.search(r'#ifdef GRIM_PERF_COLOR\s+(.*?)#else\s+(.*?)#endif', link, re.DOTALL)
    if not profile_guard:
        raise ValueError("Color formatter capacities must retain their opt-in guard")
    expected_capacities = {"LinkInfoLineCapacity": 4096, "LinkSummaryLineCapacity": 3072}
    for name, color_capacity in expected_capacities.items():
        if not re.search(r'constexpr size_t ' + name + r'\s*=\s*' + str(color_capacity) + r'\s*;', profile_guard[1]):
            raise ValueError("Wrong color formatter capacity: " + name)
        if not re.search(r'constexpr size_t ' + name + r'\s*=\s*6144\s*;', profile_guard[2]):
            raise ValueError("Existing profile formatter capacity changed: " + name)
        if not re.search(r'perf_output::Line line\(' + name + r'\)', link):
            raise ValueError("Declared capacity is not used by formatter: " + name)
    bounds = {}
    for kind, proposed_capacity, entries in (("link_info", 4096, 0), ("link_summary", 3072, 32), ("link_ack", 384, 0)):
        value = next(value for value in link_formats if '"type":"' + kind + '"' in value)
        bound = link_record_bound(value, histogram_entries=entries)
        if bound >= proposed_capacity:
            raise ValueError(f"{kind} bound {bound} exceeds proposed color-profile capacity {proposed_capacity}")
        bounds[kind] = {"upper_bytes_including_nul": bound, "color_profile_capacity_bytes": proposed_capacity}
    color = (root / "src/perf_color.cpp").read_text(encoding="utf-8")
    field_format = re.search(r'snprintf\(fields,\s*sizeof\(fields\),\s*((?:"(?:\\.|[^"\\])*"\s*)+),', color, re.DOTALL)
    if not field_format or not re.search(r'char fields\[1800\]', color):
        raise ValueError("Re-audit packed color receipt formatter")
    color_record = "".join(json.loads(literal) for literal in re.findall(r'"(?:\\.|[^"\\])*"', field_format[1]))
    color_bound = link_record_bound(color_record)
    if color_bound >= 1800:
        raise ValueError(f"Color receipt bound {color_bound} exceeds stack formatter capacity 1800")
    result = {"schema": 1, "kind": "static_telemetry_bound", "kernel_record_upper_bytes": maximum,
              "record_capacity_bytes": capacity, "native_usb_tx_ring_requested_bytes": 8192,
              "link_records": bounds, "link_string_upper_bytes": 64,
              "audited_link_ble_literal_max_bytes": literal_max,
              "color_receipt_fields_upper_bytes_including_nul": color_bound,
              "color_receipt_fields_capacity_bytes": 1800,
              "profile_capacity_guards_verified": True,
              "note": "Conservative format bound, not a measured USB reliability claim"}
    print(json.dumps(result))
    return result


if __name__ == "__main__":
    check()
