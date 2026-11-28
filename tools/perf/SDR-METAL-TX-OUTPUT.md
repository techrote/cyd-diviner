# Deferred S3 TX output validation

The new S3 TX image advertises
`output_transport: explicit_USB_Serial_JTAG_write_all` in runtime `info`, with
a 4,096-byte formatter and 5,000,000 µs between-call deadline. The SDK write
call can finish up to its 100 ms wait later; a separate flush has a 5 s wait.
Host validation identifies this capability explicitly. Legacy images without
these receipts retain their earlier validation scope; their closed captures
are not rewritten or retroactively credited with the new output proof.

`validate_tx_transport(records, before_info, after_info)` in
[sdr_metal_capture.py](sdr_metal_capture.py) checks the following before a new
transport run is acquired:

- Exactly one parsed result and terminal record, with all expected traces and
  segments between them. Parsed counts match result, terminal and driver
  acceptance counts; result and all prior output acceptance are true.
- Prior output drained successfully. Terminal restoration agrees with the
  result, stop requests are present, the enable bit is cleared in the reported
  register, and driver, event-loop, direct-power-domain and radio-ready flags
  show completed teardown.
- A post-terminal `info` has the same runtime identity and capability. This
  query occurs after cleanup, outside the RF window: `tx_end` snapshots its
  counters before formatting itself and therefore cannot report a failure of
  its own final flush.
- Cumulative failed-line, format-failure, write-error and flush-failure counts
  are zero at the baseline, terminal and post-terminal snapshots. Counts have
  valid unsigned widths and do not regress. Attempted lines equal accepted
  lines, requested bytes equal accepted bytes, and resynchronization is not
  pending. A nonzero cumulative failure requires recovery/reboot and a fresh
  clean experiment; it is not silently subtracted away.

Short or zero writes are retry observations. They are permitted when the
bounded write-all operation completes, accepted byte counts balance, and the
failure/drain checks pass. Short writes alone do not mean lost output.
`output_zero_writes <= output_short_writes <= output_write_calls` must hold.
The post-terminal counters must include progress beyond the terminal's own
pre-format snapshot. Counter saturation/reset cannot be used to manufacture
that proof.

This establishes the host-parsed batch and the SDK acceptance/drain receipts.
It does not establish independent physical RF cessation, RF bit integrity,
USB delivery timing, continuous reception or one-way audio latency. Raw serial
bytes and failed records remain evidence when validation fails.

Focused fixtures cover missing fields, false receipts, malformed counts,
unparsed/missing records, bad ordering, final-flush failure, reset/regression,
cleanup and stop contradictions, legacy compatibility and successful retries:

```powershell
C:/cyd-diviner/.venv/Scripts/python.exe -m unittest discover -s tools/perf -p test_sdr_metal_transport.py -v
```
