# Byte-capable heap evidence required for the separate PCM prototype

The approved PCM firmware is unchanged. The first closed
`evidence/physical/audio-bidirectional-01` attempt has setup/telemetry failures,
not a measured packet-loss or codec result. Its first CYD raw stream has two
consecutive bounded-telemetry errors after the internal link stop/mode ACKs.
Post-cleanup evidence retains the new run ID, RX role and zero driver error,
showing that radio initialization and RX arming occurred before the reporting
failure. Retained summaries have no packet attempts/acceptance or reconstructed
blocks. The finite window never received a validated measurement receipt.

The actual success ACK needs93bytes including NUL; the initial RX run receipt
needs221bytes. Their configured allocations are384/512bytes; even maximum-width
bounded run fields require305bytes. Ordinary bounded formatting therefore does
not explain the observed failure. `Line`'s common error cannot distinguish a
failed allocation from another formatter failure, so memory pressure remains a
strong inference rather than a captured allocator error code.

The pinned Arduino2.0.17 `Esp.cpp` queries only `MALLOC_CAP_INTERNAL` for
`ESP.getFreeHeap()` and `ESP.getMaxAllocHeap()`. `perf_output::Line` requests
`MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT`. The first failed run's approximately
14,048-byte all-internal minimum and13,300-byte largest block therefore do not
prove that either telemetry allocation was possible. A32bit-only internal
region can contribute to the older counters. Neither actual8bit-free space nor
8bit fragmentation was captured, so those explanations cannot be separated from
the old counters alone. A fresh boot tests dependence on prior colour/radio
history, not the absence of a static memory constraint.

A future separately reviewed PCM diagnostic should capture these exact
capability-matched values outside the streaming window and at critical setup
boundaries: before radio initialization, after Wi-Fi/ESP-NOW/peer setup, before
the ACK/run receipts, and after teardown:

```c
heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
```

Retain the older all-internal counters separately; do not subtract overlapping
capability totals as if they were disjoint memory pools. A fixed allocator
failure counter should identify which buffer/capacity failed without allocating
another buffer to report that failure. Any diagnostic changes observer cost and
needs its own image identity and physical authorization. The new headless SDR
probes include byte-capable heap snapshots for their own interpretation; those
snapshots do not retroactively measure or fix the approved PCM image.
