# Host sequence audit for approved source 174a796

This read-only source audit explains two failure modes seen while
`physical/link-wifi-02` was running. It proposes host ordering changes using the
already-flashed API, without changing the binary. Existing failed cases remain
failed evidence; a later rerun must have its own run IDs and receipts.

## TCP readiness before starting the fixed window

The current worker calls `serviceReceive()` (including nonblocking TCP connect
completion) only for a responder or when `complete == false`. A completed prior
initiator run leaves `complete == true`. `perf link stop` preserves that
completion state, and `perf link arm` does not clear it. Consequently an arm
whose `connect()` returns in-progress can remain unpolled while the host waits
for both `tcp_connected` fields **before** calling `perf run`.

This is a firmware/host state-sequence interaction, not evidence that TCP is
unsupported. The complete flag is cleared by `onRun`, which explains why
starting a run can unstick it. It is also cleared by the already-flashed
`perf link idle` command. After a stop, `started` is false and pending TCP
transmission bytes are zero, satisfying that command's guard.

The cleaner host-only readiness sequence is:

1. Stop both link services, preserving any previous exported results.
2. Put CYD in the menu while it still has enough heap for readiness telemetry.
3. Perform the private allowlist/session setup and start the peer responder
   with the actual run ID.
4. On CYD, send **`perf link idle` before `perf link arm tcp ...`**. Idle clears
   the stale completion flag; arm leaves it false.
5. Wait for both reported TCP connections with a bounded timeout. The worker
   can now advance connect/accept while `started == false`; no application
   benchmark packets are generated before the actual run.
6. Apply the requested CYD workload, retain its actual allocation/settings
   receipts, and start the canonical `perf run ID WARMUP DURATION`.
7. Retain the same fixed-window, reply-tail, peer receipt and integrity checks.

This sequence is established by source inspection; it is not a claimed
successful physical rerun. A timeout or telemetry/allocation failure still fails
the case. The current harness's `perf link idle` used *after* arm for connected
BLE idle has a different purpose: it disarms traffic. Do not insert that form
between TCP arm and its actual measured run, because it would disarm the sender.

A warmup-only `perf link run PRIME 60000 1000` can also clear completion, but is
unnecessary with idle-before-arm. A five-second primer combined with a
15-second readiness timeout is unsafe for measurement labeling: the primer
could enter its marked steady packet window and contaminate peer counters.
If a primer is used, the actual run must supersede it before its warmup expires,
and every failure path must stop both services. The idle-before-arm sequence
avoids this complication and extra priming traffic.

## Export formatting under combined Wi-Fi/DSP memory pressure

`link_info` and `link_summary` allocate a 6,144-byte internal formatting buffer.
Some combined fullscreen/drone cases preserve a valid canonical profiler
window but fail the later telemetry allocation. That failure means the link
summary is unavailable; it does not mean zero link traffic or prove an RF
failure. Conversely, zero validated traffic in an exported summary remains an
actual failed traffic condition and cannot be repaired by relabeling it.

After the canonical fixed window and its bounded reply tail have ended, use:

1. Export and validate the CYD profiler's completed `end` record and retained
   metrics/samples for the actual workload.
2. Stop both link services using their small acknowledgments. Natural client
   completion must already be true; do not turn a prematurely stopped run into
   a completed one. The responder stop marks its externally bounded session
   complete.
3. Send CYD **`perf scene menu`**, explicitly recorded as post-window cleanup.
   This waits for the active audio callback to finish, leaves the secondary
   drone and releases its delay, and releases Visions history/tunnel/fire
   storage. It does not reset or reconfigure `perf_link`.
4. Export both saved link summaries, followed by post-cleanup info snapshots.
   Match the original run IDs, roles, exact client duration and peer counters.

`stopService()` preserves counter storage, run ID, reported role and transport.
Stopping before changing the scene freezes responder reception and worker heap
minima. Changing scenes before the measured window ends would alter the tested
workload and is not this workaround. Changing scenes before stopping could also
alter post-window link observations. Export-time/post-cleanup free heap must not
be described as in-window DSP headroom; retained profiler samples provide that
evidence.

For TCP, readiness polling must also occur in the menu **before** allocating the
heavy scene; otherwise the 6,144-byte info buffer can fail before measurement.
Applying the heavy scene after successful TCP readiness leaves sockets alone.
This ordering does not guarantee sufficient workload or telemetry memory: actual
allocation receipts and end-to-end counters still decide whether the rerun
succeeds. No radio-off, reprovision, reset, re-arm or new token may occur between
the measured run and exporting its frozen summaries.

The audited source paths are `src/perf_link.cpp` (`worker`, `onRun`, `stopService`,
`command`), `src/main.cpp` (`close`, `grimPerfCommand`), `src/apps/drone.cpp`
(`leave`), `src/apps/visions.cpp` (`leave`) and `src/perf_output.h` (`Line`).
This document records an implementation limit and proposed reproducible host
ordering; it makes no optimization, source edit, board access or new-image claim.
