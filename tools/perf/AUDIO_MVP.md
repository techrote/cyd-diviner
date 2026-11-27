# Tiny packetized PCM MVP and optional packetized PDM comparison

For dual-board capture, validation and offline analysis commands, see the
[audio evidence tools](AUDIO-MVP.md). This document describes the firmware
method and its measurement limits.

The default physical MVP is **synthetic multichannel PCM16 over ESP-NOW
broadcast**, with bounded local buffering, deadline drops, no application
retransmission and digital reconstruction/quality measurement. No microphone,
DAC/I2S sink or physical analog latency measurement is added. The existing CYD
audio/display functionality remains separate.

The optional PDM codec comparison still travels in ESP-NOW packets. It is
**not the requested proprietary continuous SDR/one-bit RF transport**, and no
result from it fulfills or characterizes that separate arm.

`GRIM_PERF_AUDIO_MVP` explicitly opts in. `begin()` allocates no radio ownership;
resources are created lazily on the first audio command, with the worker asleep
on task notifications. The ordinary firmware profiles exclude these definitions.
Only separately identified, approved images may be flashed for physical use.

## Serial control

Use the new `audio_mvp_capture.py` harness, which checks both source/image
identities, starts the receiver first, keeps control UART quiet during streaming
and exports each finite run. The normal direction is S3 transmitter to CYD
receiver; the same protocol supports reversal.

```text
perf audio config pcm16 CHANNELS RATE FRAMES 0 PHY_MBPS SEED
perf audio config pdm CHANNELS RATE FRAMES OSR PHY_MBPS SEED
perf audio allow PEER_MAC12 SESSION8HEX
perf audio serve RUN_ID DURATION_MS
perf audio run RUN_ID DURATION_MS
perf audio info
perf audio export
perf audio stop
```

Configuration accepts 1/2/4 channels, 24,000/48,000 samples/s and 8/16/32 audio
frames per block. PCM has OSR zero; optional PDM uses 16 or 32. PHY requests are
1 or 6 Mbps, with checked SDK configuration status; this is not an independent
RF-rate measurement. The fixed channel is 6 and the queried Wi-Fi power-save
mode is recorded. Duration is positive and at most 60 seconds, with at least
one whole planned source block. Seed 23063 is the reference default.

The `allow` transaction is **private**: neither its arguments nor transaction
RX are logged, and the firmware never echoes them. Its random 32-bit session
discriminator and explicit owned-peer MAC reject stale/unrelated packets;
public receipts expose only authorization state and run IDs. The session value
is not exported. CRC16 is integrity checking, not authentication/encryption.

Acknowledgments use `type: audio_ack`, fixed `action`, boolean `ok`, numeric
`code` and static `operation`. Successful serve/run also emits `audio_run` with
run ID, role, planned source-block count and duration. Transmitter start is
scheduled 200 ms ahead of its command handling, with exact local start/end
timestamps. Receiver timestamps are not synchronized to that clock.

`serve` prepares the receiver before the sender starts. Natural sender
completion includes at most 200 ms of completion-callback drain. Receiver
playout completes when it reaches the last planned source block; an unanchored
or stalled run has a finite duration-plus-five-second timeout and remains
incomplete. Export requires an inactive run. An early stop retains
`complete: false`; it must not be relabeled as a completed measurement.

Before starting, the module explicitly stops the existing `perf link` service
and requests that radio stack off. While this MVP owns the radio, attempts to
start/reconfigure `perf link` or `perf radio` are rejected. Stop the MVP before
changing configuration or session. Stop/export do not write Wi-Fi credentials;
the module requests RAM Wi-Fi storage and never associates or scans for peers.

## Packet, queue and age semantics

The destination is the broadcast MAC. The application does not request a
unicast MAC acknowledgment and never resends an attempted fragment. Broadcast
completion callbacks are driver completion observations, **not receiver
delivery acknowledgments**. Delivery evidence comes from the owned receiver.

The portable core uses a 20-byte versioned header, CRC16-CCITT-FALSE,
configuration/session/sequence checks and packets at most 250 bytes. Payloads
larger than one packet are fragmented on complete interleaved audio-frame
boundaries. Default physical PCM cases fit one packet; supported larger cases
must report their real fragment count and whole-block concealment semantics.

One timer-driven priority-3 task on core 1 handles each endpoint. Its one-shot
notifications follow rational source deadlines; no one-millisecond polling
loop is used for this transport. Callback work is limited to bounded packet
copies/counters and a task notification. There is one packet in flight and at
most one prepared source block. Obsolete source blocks and prepared blocks are
dropped instead of accumulating a transmit backlog. A fragment whose admission
deadline has passed is discarded. An already admitted driver packet cannot be
cancelled by this application; its actual over-air age is not bounded by that
software check or independently measured.

The receive callback queue has eight entries. Worker-admission delay above two
nominal block periods discards the callback packet. The portable receiver has
eight assembly slots and a fixed two-block playout delay. It uses **actual
worker-admission time** for its local deadline checks; callback timestamps are
retained separately for queue-age observations. Late or stale fragments are
discarded, and a long scheduler stall skips obsolete playout blocks
arithmetically instead of replaying an unbounded backlog.

The playout timeline anchors to the first valid worker-admitted source
sequence. Initial source blocks before that sequence are outside its PLC
domain. Reported first/last rendered sequences and planned source-block count
must accompany quality/PLC totals; concealed blocks alone do not count all RF
loss. Local queue age, slot dwell and playout lateness are measured in the
receiver clock. **Absolute one-way source-to-receiver age and analog latency
remain null** without a shared-clock or physical timing fixture.

## Digital reconstruction and quality

Each source channel is a deterministic integer triangle with a distinct
frequency (220 + 37 × channel Hz), shared seed and sample-index reference.
PCM16 is interleaved little-endian signed PCM. A missing/incomplete block holds
the prior emitted sample initially, then slews toward zero by at most 256 units
per audio sample. Valid audio returning after concealment is limited to a
2,048-unit/sample recovery step.

The optional PDM comparison maintains an independent first-order modulation
state per channel across normally encoded blocks. Subchannel bits are
interleaved by audio frame, oversampling tick and channel. Reconstruction uses
a causal OSR-sample boxcar. A transmitter source-deadline skip explicitly
resets the encoder and increments `encoder_reset_discontinuities`; it is not
hidden as uninterrupted modulation. Receiver packet loss does not reset the
transmitter encoder. This remains a packetized comparison.

`audio_summary` exports per-channel sample counts, reference energy, squared
error and maximum absolute error over **rendered** samples, including PLC and
recovery samples. The reference uses the same seed and source sequence with no
delay alignment that could conceal reconstruction error. Host analysis derives
RMSE/SNR from those exact sums and preserves zero-energy/error cases honestly.
Those statistics do not quantify an analog reconstruction filter or audible
output quality.

Encode, core receive/render, send API and quality-observer work have separate
bounded call/sum/maximum elapsed-time observations. They include RTOS
preemption and are not cycle-exact CPU runtime. Packetization/CRC, callback,
queue and timer-task overhead are outside those individual core-job timers;
their sum is not total task CPU utilization. Heap and stack receipts describe
the instrumented image. Info uses a 2,048-byte formatter; summary uses 4,096,
with conservative audited bounds of 1,555 and 3,924 bytes respectively. Metadata
formatting happens outside the streaming window.

Portable tests exercise configuration/packing, every fragment, CRC corruption,
truncation, session/config rejection, duplicate/stale/future packets, sequence
wrap, bounded stall skipping, PLC/recovery, source seeking and optional PDM
continuity against an independent bit-level oracle. These tests and offline
impairment simulations are explicitly separate from physical RF evidence.
