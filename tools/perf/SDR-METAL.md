# Deferred native snapshot and headless TX tools

These tools operate the separately approved headless SDR-metal images. They do
not reset, flash, erase, discover peers or authorize RF. No port is opened by plan
generation or offline analysis. Root/owner identifies and approves exact images,
MACs, source digests and the physical experiment first; capture requires an
explicit operator `--rf-authorized` flag.

```powershell
python tools/perf/sdr_metal_capture.py plan --output evidence/plan-metal-rx.json
python tools/perf/sdr_metal_capture.py capture --rf-authorized `
  --identities C:/private/metal-identities.json --plan evidence/plan-metal-rx.json `
  --output evidence/physical/metal-rx-01
python tools/perf/sdr_metal_analyze.py evidence/physical/metal-rx-01/results.json `
  --output evidence/metal-rx-analysis
```

The default plan contains receiver-only bench and exported-capture cases at
nominal16/40/80 MS/s,16,380 words per burst. Bench repeats20 times without
per-capture CRC/format/USB, then the host requests the last retained snapshot.
Exported capture acquires one burst and transfers it afterward. Receiver
initialization (`phy` or `wifi`), LO/offset and manual-gain index are explicit.
Their successful API/control receipts are recorded; they do not calibrate RF
clock, sensitivity or gain in dB. Failed default initialization can be followed
by the plan's explicit checked initialization mode before tuning.

`plan --with-tx` additionally prepares reviewed finite TX comparisons. The
generated defaults use API/RAM/gate paths, paced and unpaced cases, and one
locally masked gate case. They are proposed settings, not hardware results.
TX pattern/trace capacity, trace stride, PHY/driver initialization, mode, baud,
two backoffs, duration and mask segment are configurable and validated against
the current command contract. Masked segments require sufficient explicitly
preallocated segment records. API/RAM calls cannot run inside a requested masked
segment. A fresh one-use private permit is required for every TX command; its
arguments are omitted/redacted from logs. Packed waveforms upload once with
MSB-first bits and seed-zero CRC32, using LF-only control lines. No per-symbol
host timing or automatic retransmission occurs.

RX emits a flushed armed receipt before the host starts TX. Independent SDK
startup delays and board clocks do not ensure overlap of a short TX burst and
short RX aperture. Select duration/arm delay from an independently reviewed
cohort and inspect native IQ; do not infer RF overlap or end-to-end latency from
command ordering. The default coupled examples deliberately use longer finite
bursts for initial observation. Narrow-burst/maximum-throughput cases need their
own explicit timing plan and independent RF evidence.

The identities schema is `cyd-sdr-metal-identities/1`, with `tx` and `rx` entries:
confirmed Windows port/baud, firmware path/SHA256, and `expected_info`. TX profile
is `classic-sdr-metal-tx` or `s3-sdr-metal-tx`, with exact `source_sha`,
`tx_source_digest` and MAC. RX profile is `s3-metal-snapshot`, with exact
`source_sha`, `source_digest` and MAC. Additional pinned SDK/helper fields may be
included in `expected_info`; every supplied field is matched. Local image hashes
are checked before ports open. Runtime source identity is not binary attestation;
retain separate verified owner flash receipts.

The parser reads a complete `METAL` JSON header, exactly `sample_count*4` raw
bytes, and one LF. Binary bytes can contain arbitrary text/newlines; they are
never searched for delimiters. CRC32 covers all unmodified LE32 native words.
Each word contains signed I10 in bits0–9, native−Q10 in bits10–19 and twelve
upper metadata bits. Raw words and upper metadata remain intact. Every snapshot
starts its own sample index0. Later bench `read` exports preserve the original
source run/repetition and heap scope; the host checks these independently of the
new read request ID.

Acquisition validates finite terminal boundaries, snapshot counts, native format,
CRC/following LF, hardware completion, tail canaries, unchanged-sentinel count,
owner/lane restoration and transfer-failure counters. TX checks `tx_result`,
exact trace/segment totals and the final `tx_end`, initialization/mode/observer
settings, restored word and applicable Wi-Fi teardown codes. Every TX
teardown requires aggregate teardown_status0 and false residual
owned Wi-Fi driver/default-event-loop flags; missing fields or cleanup failures
cannot become `ACQUIRED`. An already-stopped Wi-Fi status0x3002 is allowed only
with successful deinit and aggregate cleanup. A direct PHY
disable is a void API and has no return-status proof. Incomplete/malformed
transfers fail with raw/partial evidence retained; no control query is injected
into an uncertain payload boundary. `ACQUIRED` does not mean physical RF edges,
modulation rate or cessation have been independently verified.

Temperature control occurs only outside timed loops. Classic TX retains raw RTC
indications; an S3 TX uses its explicit install/read/disable/uninstall sensor
command, retaining all returned status fields and unavailable values. RX uses
`metal temperature` before armed and after the completed run/read boundary.
Every invoked sensor disable/uninstall must succeed; a failed read can remain
`NOT MEASURED` after proven cleanup, while missing lifecycle proof or cleanup
failure aborts further work for owner recovery. No persistent sensor task or
handle is added to the timed path. Plans can explicitly disable these queries,
which leaves their temperature scope `NOT MEASURED`.

Raw serial/event logs, snapshot files, source/image provenance, JSON and an
artifact-hash manifest are retained. Offline analysis requires a closed physical
capture and verifies original hashes before producing JSON, rate CSV, native-IQ
CSV and Markdown. Optional NumPy comes from `analysis-requirements.txt`.
Nominal arithmetic sample horizon, CPU-observed trigger/completion time,
no-export loop occupancy, and exported-loop USB/check/setup deadtime remain
separate figures. Neither nominal rate nor loop occupancy proves continuous
sampling. Spectral/phase estimates use the configured clock and receiver codes;
they are not calibrated RF frequency/power. Field-limit observations are not
independent analog-clipping detection.

TX achieved writes/s and cycle counts include the IRAM loop, selected observers,
and permitted interrupt activity. They are not isolated instruction latency or
the RF hardware's ultimate modulation bandwidth. Raw late_cycles/maximum refer
to the pending deadline before obsolete paced slots are skipped; they are
backlog/missed-deadline evidence, not final output-symbol jitter. Source symbol
index and completion-cycle traces retain the information for a separate
post-skip timing analysis. Local RTOS-level3 masking does
not exclude higher-priority interrupts; actual mask/gap brackets retain their
documented measurement scope. Keep classic/S3, PHY/driver init, rate, mode,
observer stride, mask, pattern and no-export/export contexts separate.
