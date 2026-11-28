# TX source-buffer checkpoints

New S3 TX images can advertise `source_integrity_supported: true`. This is a
separate capability from the write-all serializer and rational fast clock.
Older images without it retain their original scope; their source tables are
not retrospectively declared CRC-verified. These checks operate outside the
timed loop and add real preparation/post-loop/teardown time.

For an advertised image, both capture harnesses require one accepted
`tx_source_integrity` record after `tx_result`, before a fast-clock/trace/segment
record and before `tx_end`. Terminal flags `source_integrity_required`,
`source_integrity_driver_accepted` and strict `source_integrity_ok` must agree.
Existing complete output/write/flush/cleanup checks remain required before
ACQUIRED. Missing, duplicated or inconsistent source receipts fail the case.

The host uses the exact uploaded bytes, not only firmware-provided CRC values:

- Packed expected CRC covers `ceil(bit_count/8)` bytes in MSB-first symbol
  order, including unused padding bits in the final byte. It must match the
  target's expected, before-radio, after-radio, after-tone, before-loop,
  after-loop and post-teardown checkpoints. Fresh post-terminal info must
  report the same loaded bit count/CRC; pre-run info may still describe the
  preceding upload and is not mistaken for the current one.
- Intended word CRC covers exactly `bit_count` native little-endian uint32
  values. Gate/API/RAM/fast source tables use the initialized word with enable
  bit18 cleared/set by each uploaded bit. ASK tables preserve the initialized
  word except amplitude bits10–17 copied from the observed backoff1 word for
  high symbols. The target's expected and pre/post/retained word CRCs must
  match this independently computed mapping. API/RAM loops can also consume
  packed levels; CRC agreement does not claim that every mode reads the table.
- Fast delta CRC covers the active phase-zero LE32 schedule, independently
  calculated as `floor((i+1)*240MHz/baud)-floor(i*240MHz/baud)`. Per-ring
  fractional carry is checked by the separate fast-clock receipt, not inserted
  into this precomputed array. Build/pre-loop/post-loop/post-teardown CRCs must
  match. Non-fast delta fields are explicit unused placeholders, not measured
  zero-error schedules.

Checked-stage/loop-start flags, population after SDK startup and tone setup,
byte/bit order, active lengths, nonzero aligned addresses and nonoverlapping
buffer ranges are validated. Addresses are retained diagnostics; they do not
independently attest physical memory ownership. The source's allocation policy
and exact image/helper identity remain part of the experiment context.

Stage interpretation is deliberately separate:

1. A pre-loop refusal can occur after SDK calibration or setup tone activation;
   it does not establish that no RF was emitted. It prevents the timed source
   test from being treated as a successful measurement.
2. Pre/post-loop checks describe buffer values at those checkpoints. CRCs do
   not exclude a transient modification/restoration between checks, collisions
   or a different physical RF cause. After-loop checks precede SDK/tone stop,
   so their observer cost can extend setup/post-loop RF activity outside the
   requested timed payload duration.
3. A teardown-only mismatch fails strict acquisition, but preserves a structured
   `TEARDOWN_ONLY_CHANGE` validation with matching timed checkpoints. The host
   explicitly makes **no RF BER attribution** from that later mutation.
4. Self-consistent target CRCs for the wrong intended word/schedule mapping also
   fail against the host oracle, even when target self-check flags are true.

Failure details and raw records remain in the captured run. Single and dual
paths retain the structured source validation when a strict source error is
raised; dual receiver bodies still keep their independent integrity/status
evidence. No bytes or missing records are reconstructed, and a failed case
does not silently become ACQUIRED. Buffer CRC, driver acceptance, SDK requests
and register readback are distinct from physical RF payload/edge/cessation
measurements.

Hardware-free fixtures include explicit gate/ASK LE32 vectors, partial-byte
padding, the literal rational delta ring, all receipt/flag/order requirements,
independently wrong mappings, in-loop changes and teardown-only changes:

```powershell
python -m unittest discover -s tools/perf -p test_sdr_metal_source_integrity.py
```
