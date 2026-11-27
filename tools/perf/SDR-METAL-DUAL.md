# Two receivers observing one finite TX exposure

`sdr_metal_dual_capture.py` coordinates one metal TX and two independently timed
S3 snapshot receivers. It uses the existing frozen firmware commands, binary
decoder, source/image checks and cleanup validators. It neither flashes nor
resets boards. The operator must have owner approval for the exact images and
finite radio experiment before supplying `--rf-authorized`.

The identities file uses schema `cyd-sdr-metal-dual-identities/1` and entries
`tx`, `rx`, `rx2`. Each entry has the same `port`, `baud`, `firmware_file`,
`firmware_sha256`, `expected_info` fields as the single-receiver identity file.
TX requires the classic or S3 metal profile, source SHA, `tx_source_digest` and
MAC. Both RX entries require profile `s3-metal-snapshot`, source SHA,
`source_digest` and MAC. Image files are checked before any serial port opens;
runtime info must match every supplied expected field. Three distinct actual
ports and MACs are required. A board owner's label can differ from its current
Windows port; use its confirmed runtime port in `port`.

```powershell
python tools/perf/sdr_metal_dual_capture.py plan --output dual-plan.json
python tools/perf/sdr_metal_dual_capture.py capture `
  --identities C:/private/metal-dual-identities.json --plan dual-plan.json `
  --output evidence/physical/metal-dual-01 --rf-authorized
```

The plan retains schema `cyd-sdr-metal-plan/1` and existing finite bounds. Every
case must explicitly request `arm_delay_us: 500000`. Generated TX cases use
one second of TX, allowing the requested RX arm delay to expire while TX may
be active; PHY setup time still has no host-enforced RF start bound. Select the
workloads actually authorized for the next capture, including their TX mode,
backoff, rate, IRQ mask, observer and allocation settings. Shorter custom TX
durations can end before RX capture, so overlap must be established from IQ.

All setup, upload, private one-use permit and optional temperature queries
finish before arming. The harness sends both arm commands before waiting on
either receiver. Both matching armed ACKs must arrive within a 450ms host
dispatch bound before exactly one finite TX command is sent. Failure to meet
that barrier suppresses TX. It then polls the three ports without blocking on
an empty endpoint. No temperature, info or recovery query is sent during the
finite RF/snapshot loops. The one-use permit is redacted and omitted from TX
logs, as in the single-receiver harness.

Each receiver retains its own source ID/repetition, raw bytes, CRC32, canary,
owner/lane restoration, transfer statuses, native clock, target timestamps,
init/tune and before/after sensor records. Sensor queries happen only before
arming or after that endpoint's complete terminal/read boundary; failed sensor
cleanup aborts the campaign. A receiver framing failure switches only that
endpoint to raw retention, while the other endpoint and TX continue draining
to their finite terminal boundaries. Any failed case stops later cases without
automatic retry or retransmission. Native data already collected from the
other receiver remain available under their own acquisition status.

The output layout is:

- `results.json`: dual campaign, shared TX exposure and per-receiver statuses.
- `tx/`: one shared set of TX raw serial/event logs.
- `rx/results.json`, `rx2/results.json`: existing single-receiver capture schema,
  each with its corresponding RX identity, raw logs, native files, temperatures
  and shared TX result. Each child binds hashes of shared TX artifacts and names
  the parent manifest. A valid child can remain complete if the other receiver
  fails; the dual campaign still records the failure.
- Each root has `artifact-hashes.json`; child native paths retain the layout
  expected by `sdr_metal_analyze.py` and `sdr_metal_envelope.py`.

Run those offline analyzers separately on each child's `results.json`. These
are two observations of **one shared exposure**, not two independent RF trials.
Host arm/send times and independent target clocks do not synchronize samples,
prove simultaneous RF overlap, measure one-way latency or establish RF loss.
USB CRC/canary/restoration prove acquisition integrity; inspect native IQ for
signal presence and modulation. No-export bench timings stay separate from
later USB readout and are not continuous streaming measurements.

The hardware-free tests exercise the arm barrier, one-TX rule, independent split
binary streams, retained good-receiver data after another receiver fails,
identity collision rejection and reusable child artifact provenance:

```powershell
python -m unittest discover -s tools/perf -p test_sdr_metal_dual_capture.py
```
