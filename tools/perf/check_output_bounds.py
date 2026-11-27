"""Conservative static budget for the bounded kernel JSON record (no board I/O)."""
import json
import pathlib
import re


def check():
    root = pathlib.Path(__file__).resolve().parents[2]
    source = (root / "src/perf_kernels.cpp").read_text(encoding="utf-8")
    literals = re.findall(r'outputLine\.printf\("((?:\\.|[^"\\])*)"', source)
    formats = [json.loads('"' + value + '"') for value in literals]
    record = next(value for value in formats if '"type":"kernel"' in value)
    # Every actual descriptor/capability/name is <=160 ASCII bytes. A rendered
    # numeric field is <=32 bytes even at uint64/double extremes used here.
    worst = re.sub(r'%[-+0-9.#]*(?:ll|l|z)?([sudgfc])',
                   lambda match: "S" * 160 if match.group(1) == "s" else "9" * 32, record)
    maximum = len(worst.encode()) + 256 * 11 + 4  # uint32 values/comma + ]}\n/NUL
    capacity = 6144
    if maximum >= capacity:
        raise ValueError(f"Kernel record bound {maximum} exceeds capacity {capacity}")
    result = {"schema": 1, "kind": "static_telemetry_bound", "kernel_record_upper_bytes": maximum,
              "record_capacity_bytes": capacity, "native_usb_tx_ring_requested_bytes": 8192,
              "note": "Conservative format bound, not a measured USB reliability claim"}
    print(json.dumps(result))
    return result


if __name__ == "__main__":
    check()
