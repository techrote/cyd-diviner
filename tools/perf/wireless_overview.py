#!/usr/bin/env python3
"""Select the latest finite wireless cohort for each workload without pooling boots."""
import csv
import json
import pathlib
import sys


def generate(evidence):
    cohorts = ("campaign-link-wifi-02", "campaign-link-wifi-retry", "campaign-link-fresh-ap")
    selected = {}
    for cohort in cohorts:
        source = json.loads((evidence / cohort / "link-summary.json").read_text())
        grouped = {}
        for group in source["groups"]:
            grouped.setdefault(tuple(group["context"][k] for k in ("topology", "local_workload", "mode")), []).append(group)
        for key, groups in grouped.items():
            attempts = sum(g["attempted_runs"] for g in groups)
            good = [g for g in groups if g["measured_runs"]]
            if len(good) > 1:
                raise ValueError("Several distinct valid contexts; do not merge: " + str(key))
            g = good[0] if good else groups[0]
            c, r, b = g["context"], g.get("rtt") or {}, g["received_payload_bytes_per_second"]
            selected[key] = {"cohort": cohort, "group_id": g["group_id"], **dict(zip(("topology", "local_workload", "mode"), key)),
                "measured_runs": sum(v["measured_runs"] for v in groups), "attempted_runs": attempts,
                "payload_bytes": c["payload_bytes"], "requested_pps": c["packets_per_second"],
                "goodput_median_Bps": b["median"], "goodput_min_Bps": b["min"], "goodput_max_Bps": b["max"],
                "rtt_n": r.get("n"), "rtt_mean_us": r.get("mean_us"), "rtt_min_us": r.get("min_us"),
                "rtt_p95_upper_us": r.get("p95_us_upper"), "rtt_p99_upper_us": r.get("p99_us_upper"), "rtt_max_us": r.get("max_us"),
                "fps_median_successful_link_cases": g["fps_successful_cases"]["median"]}
    rows = list(selected.values())
    with (evidence / "WIRELESS-OVERVIEW.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    (evidence / "WIRELESS-OVERVIEW.json").write_text(json.dumps({"schema": "cyd-perf-wireless-selection/1",
        "selection": "Latest attempted cohort per topology/local/mode; no cross-cohort pooling. All earlier failures remain in source reports. Separate successful context retained when failed setup contexts have no measurements.",
        "cohort_order": list(cohorts), "rows": rows}, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return len(rows)


if __name__ == "__main__":
    print(generate(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "evidence")))
