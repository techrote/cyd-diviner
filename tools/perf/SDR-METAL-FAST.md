# Fast rational-clock TX receipt validation

The experimental **S3-only** `fast` backend is an additional source mode, not
the earlier generic paced loop or unpaced store loop. It accepts positive baud,
IRQ enabled (`mask_segment_us: 0`) and no trace observer (`trace_stride: 0`).
Existing SDK/private MMIO/RF limits and exact-image owner authorization still
apply. No default plan automatically selects the new backend.

Both single- and dual-receiver harnesses reject a fast case before arming or
issuing a TX command unless runtime info explicitly declares `s3-sdr-metal-tx`,
240MHz CPU, `fast_paced_supported: true`, a SHA256 schedule-helper identity and
the reviewed4096-byte/5s write-all transport. Classic/older S3 images are not
silently treated as supporting fast mode. Ordinary older captures retain their
original validation scope.

The firmware precomputes internal-DRAM rational deadline deltas before RF.
Every source word remains in order; lateness catches up rather than skipping
individual source bits. Overload can slow the actual stream or leave an unsent
tail at the finite end. An acquired measurement does not assert that requested
baud was sustained, nor that every requested word reached RF. Software write
rate and received IQ remain separate observations.

The output sequence is `tx_result`, `tx_source_integrity`, exactly one
`tx_fast_clock`, then `tx_end` for a fast case. The source record is independently
checked as described in [the source-integrity contract](SDR-METAL-SOURCE-INTEGRITY.md).
No trace or masked-segment records are allowed in this initial
mode. The terminal record must set `fast_clock_required` and
`fast_clock_driver_accepted` true; missing, duplicated, out-of-order or
unaccepted clock receipts fail validation. The shared serializer validator
still requires complete output acceptance, cleanup and a fresh post-terminal
info snapshot to observe final flush errors. Non-fast batches reject unexpected
fast receipts/flags, while old batches without new fields remain compatible.

The host validates these integer relationships using the reported nominal
240MHz CPU cycle clock:

- Source-word count, baud, finite duration, ring length and elapsed cycles agree
  with `tx_result`; skipped source slots, trace and requested masking are zero.
- Ideal word count is `ceil(duration_us * baud / 1e6)`. Missing target-tail and
  ahead-of-target counts match the observed completed source prefix.
- For CPU clock C, baud B, ring length L and completed words W, the ring floor
  is `floor(L*C/B)`, remainder is `(L*C) mod B` and phase after completed rings
  is `(floor(W/L) * remainder) mod B`.
- Next scheduled start is
  `floor(floor(W/L)*L*C/B) + floor((W mod L)*C/B)`. It must equal the receipt,
  and lie at most one cycle before `floor(W*C/B)`. The previous scheduled word
  must be before the finite target boundary.
- Precomputed minimum/maximum delta, one-cycle boundary carry, schedule memory
  identity and reserved byte capacity agree. The post-terminal allocation must
  match the receipt; retained capacity may exceed the current ring length.
- Signed endpoint clock error is elapsed minus the ideal next start. Positive
  endpoint backlog, measured write-rate arithmetic and cycle/write arithmetic
  must agree. Published floating rates allow only their0.001 display rounding.

The endpoint clock error is **not maximum transient lateness**. Per-word
lateness fields remain null, `per_word_lateness_observed` false and maximum
transient backlog unknown. A negative endpoint clock error can occur during
the normal final wait. Schedule rounding bounds describe the intended CPU
deadline sequence, not independently measured radio edges. Source counters,
SDK requests, driver acceptance and register readback do not prove RF
cessation, payload BER, calibrated sample/bit clocks, analog audio or one-way
latency. Use the retained IQ and separate demodulator for signal/payload claims.

Hardware-free fixtures include a literal fractional-carry example, valid unsent
tail, unsupported runtime capability, invalid fast plans, missing/duplicate
clock receipts, terminal rejection and corrupted count/phase/rate/allocation
fields:

```powershell
python -m unittest discover -s tools/perf -p test_sdr_metal_fast.py
```
