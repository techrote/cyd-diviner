# Owned-board wireless capture

`link_capture.py` orchestrates the CYD on COM21 and owner ESP32-S3 on COM22.
The PC carries serial control/telemetry only; payload traffic runs between the
two UART-identified, explicitly allowlisted boards. It never flashes, discovers
other peers, changes saved credentials or probes an arbitrary host.

Build/backup/flash authorization and receipts remain separate. The intended
profiles are `cyd-profile-link` and `kernel-s3-n4r2-peer`; use the actual approved
profile names and image hashes. Close other serial tools first. New images and
board profiles require the owner's authorization before physical use.

Keep SSID/password values in a private JSON file **outside every Git repository**:

```json
{
  "schema": "cyd-perf-link-private/1",
  "direct_softap": {
    "ssid": "replace privately with the controlled board AP name",
    "password": "replace privately with its generated password"
  },
  "through_ap": {
    "ssid": "replace privately with the owned hotspot name",
    "password": "replace privately with its password"
  }
}
```

Those are placeholders, not the owner's supplied values. The tool rejects a
private file beneath any `.git` ancestor. It sends credentials as bounded hex
fields through a private serial transaction: TX contents and transaction RX are
omitted from logs. Known-peer session tokens use the same private path and are
never exported. Public RX additionally redacts registered literal/hex/JSON-escaped
secrets across arbitrary read boundaries, preserving byte lengths and offsets.
Firmware suppresses SDK association logs and never echoes provisioning arguments.
No private-file path or credential-file hash is exported.

Prepare a public identity manifest from exact build and board/flash receipts:

```json
{
  "schema": "cyd-perf-link-identities/1",
  "cyd": {
    "board": "owner-confirmed CYD on COM21",
    "port": "COM21",
    "baud": 533333,
    "profile": "cyd-profile-link",
    "source_sha": "replace with exact 40-digit source commit",
    "source_digest": "replace with exact 64-digit source tree digest",
    "firmware_file": "C:/path/to/approved/cyd/firmware.bin",
    "firmware_sha256": "replace with exact approved binary SHA256"
  },
  "peer": {
    "board": "owner-confirmed S3 N4R2 on COM22",
    "port": "COM22",
    "baud": 115200,
    "profile": "kernel-s3-n4r2-peer",
    "source_sha": "replace with exact 40-digit source commit",
    "source_digest": "replace with exact 64-digit source tree digest",
    "firmware_file": "C:/path/to/approved/s3/firmware.bin",
    "firmware_sha256": "replace with exact approved binary SHA256"
  }
}
```

Manifest placeholders intentionally fail validation. Both image digests are
checked locally before opening ports, then each target's source/profile/digest
must match. The target does not independently attest its flashed binary; preserve
the owner's exact flash receipts. Local MAC/IP addresses come from those two
serial endpoints, not a discovery scan or user-entered arbitrary destination.

`plan-link.json` contains 66 windows: three repetitions for menu and fullscreen
Gate+Trails with the drone callback, across direct ESP-NOW, controlled S3 SoftAP
and the owned phone AP. It measures associated/controller idle, RTT, paced
one-way packets and Wi-Fi TCP goodput. Defaults are two seconds warmup and ten
seconds steady measurement; packet payloads/rates remain explicit in JSON.
`plan-link-ble.json` contains 18 directed GATT windows, including connected idle,
RTT and paced one-way traffic. It targets only the known peer BT MAC, waits for
both connection readiness and adequate negotiated MTU, and performs no BLE
device-discovery scan.

For independent capability checks, reboot both approved images before the BLE
plan; reboot both again before the ESP-NOW/Wi-Fi plan. This avoids treating a
post-Wi-Fi memory history as fresh BLE capability. Reset/flash remains an explicit
owner/root action; the harness never resets or flashes automatically.

```powershell
python tools/perf/link_capture.py capture --identities C:/private/link-identities.json `
  --provision-private C:/private/link-provision.json --plan tools/perf/plan-link-ble.json `
  --output evidence/physical/link-ble-01
python tools/perf/link_capture.py capture --identities C:/private/link-identities.json `
  --provision-private C:/private/link-provision.json --plan tools/perf/plan-link.json `
  --output evidence/physical/link-wifi-01
```

Every run uses a fresh private token, a named responder session, acknowledged
scene settings, bounded payload/rate and the CYD profiler's exact target window.
The worker sends warmup packets marked as such; responder counters exclude them.
After the fixed end, replies drain for at least 500 ms before both links stop and
export. Connection readiness is checked in a temporary menu before applying the
requested scene. The host sends `perf link idle` after stopping the CYD service
and before arming: this clears the approved image's retained completion flag so
TCP connection polling can proceed without starting application traffic.
After profile export and both link stops, the host returns the CYD to menu before
allocating link export/info formatters. Prewarm setup and post-window cleanup
receipts remain separate from the requested measured-scene `setup_target`.
Post-cleanup info describes stopped services with DSP released; in-window profile
samples describe workload heap/CPU. Link heap minima can extend through the reply
tail and profile export until stop. RTT uses one initiator clock. Receiver goodput divides unique marked
payload bytes by the initiator's fixed window, including the defined receive
tail, and excludes the 40-byte application header/PHY overhead. It is not derived
from unsynchronized board clocks or UART arrival timing.

RTT histograms are 32 ceil-log2 count bins. Derived percentile values are upper
bounds capped at the exact observed maximum; p99 is omitted below 100 samples.
“Jitter” here is the absolute difference between adjacent received RTT values,
not one-way delay variation. API acceptance, ESP-NOW MAC callback-time status,
receiver-unique packets, duplicates/reordering, CRC failures and callback queue
drops remain separate evidence. A zero-traffic/zero-RTT case fails instead of
being reported as zero packet loss. Peer CPU utilization remains null when the
firmware does not provide a sampler; the CYD retains CPU/audio/I2S/heap samples.

Outputs include versioned `results.json`, link metrics and derived CSVs, CYD
profile metric/sample CSVs, per-endpoint redacted raw RX/event/target logs and an
artifact-hash manifest. Setup/connection/allocation failures are retained per
case; independent cases continue. A failed case keeps the overall status FAILED
(exit code 2) even when execution reaches the end. A valid profiler window can
remain MEASURED separately when end-to-end traffic fails. Global UART corruption,
disconnects or interruption abort safely and retain partial evidence. All owned
board services are stopped and requested off during cleanup; no hotspot settings
or unrelated devices are changed.

Analyze the resulting public/redacted records without opening either port:

```powershell
python tools/perf/link_analyze.py evidence/physical/link-ble-01/results.json
python tools/perf/link_analyze.py evidence/physical/link-wifi-01/results.json
```

This writes `LINK-REPORT.md`, `link-summary.json`, per-run/group CSVs and an
input/output/tool hash manifest. It preserves FAILED/BLOCKED case status and
separately valid CYD profiler windows. Only matching successful contexts enter
successful-link aggregates. Idle RTT/goodput remain unavailable. RTT distributions
are conditional on valid replies, with sender/peer/round-trip response counts
alongside the tails. The not-observed fraction is not isolated RF loss: the
64-sequence horizon, callback queues and bounded receive tail limit attribution.
TCP frames created in the marked steady window may finish API acceptance during
the defined tail. Goodput and its limits retain those exact packet-window scopes.
