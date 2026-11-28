# ESP32-S3 atomic compatibility and bounded audio publication

## Observed failure and exact target evidence

The Cardputer job for initial integrated source
`8d74b4b68883c1691ad0e8d75d923385695af78e` failed the always-lock-free
`std::atomic<uint32_t>` assertion. The same source passed the three CYD
builds. Deleting the assertion would have concealed a real storage-dependent
blocking path.

The isolated [target probe run 46113050508](https://github.com/techrote/cyd-diviner/actions/runs/46113050508)
used source `9f047008e81cbd6a51c9071a31f208f97aac091d`, the actual CANON
compilation database entry, and the installed runtime archive. It compiled
inspection objects, collected compiler macros and disassembled code; it did
not execute target instructions or qualify the application build.

| Observed item | Value |
| --- | --- |
| Compiler | `xtensa-esp-elf-g++`, esp-14.2.0_20260121, GCC 14.2.0 |
| Generic int / long atomic trait | `__GCC_ATOMIC_*_LOCK_FREE == 1` (sometimes) |
| Hardware 32-bit CAS | `XCHAL_HAVE_S32C1I == 1` |
| IDF workaround | `CONFIG_STDATOMIC_S32C1I_SPIRAM_WORKAROUND == 1` |
| External RAM configuration | `CONFIG_SPIRAM == 1`, `CONFIG_SPIRAM_USE_MALLOC == 1` |
| General malloc internal-size preference | 4,096 bytes; not an internal-storage guarantee for these 12–21 KB States |
| Runtime archive | installed ESP32-S3 `libnewlib.a` |
| Runtime archive SHA-256 | `13d50fa4ad247eb3f0cc284940b7da87ca56a93cf26f24bac7f4239e2187c4b2` |

The observed acquire load and release store compile to aligned word accesses
with `memw` barriers. The standard strong 32-bit CAS calls
`__atomic_compare_exchange_4`. That runtime dispatch checks the address:
internal memory takes `__atomic_s32c1i_compare_exchange_4`; external memory
takes `xPortEnterCriticalTimeout(..., -1)` and exits the critical section after
the operation. The internal compare-and-swap body contains one `s32c1i`,
without a retry loop or a further call:

```text
__atomic_s32c1i_compare_exchange_4:
  l32i.n a8,a3,0
  wsr.scompare1 a8
  s32c1i a4,a2,0
  sub a8,a4,a8
  nsau a2,a8
  srli a2,a2,5
  beqz.n a8,...
  s32i.n a4,a3,0
  retw.n
```

In contrast, `__atomic_s32c1i_exchange_4` repeats `s32c1i` until it wins. An
atomic exchange can therefore retry inside the toolchain even when the C++
source contains no loop. The revised snapshot helper uses strong CAS with
an explicit finite bound on both producer and consumer endpoints.

Primary implementation references:

- [ESP-IDF v5.5 stdatomic.c](https://github.com/espressif/esp-idf/blob/v5.5/components/newlib/src/stdatomic.c)
- [ESP-IDF v5.5 esp_stdatomic.h](https://github.com/espressif/esp-idf/blob/v5.5/components/newlib/priv_include/esp_stdatomic.h)
- [Espressif GCC esp-14_2_0 Xtensa options](https://github.com/espressif/gcc/blob/esp-14_2_0/gcc/config/xtensa/xtensa.opt), including `-mdisable-hardware-atomics`

## Storage and ownership contract

All five new instrument State types inherit the empty `InternalState` base.
Device `new (std::nothrow) State()` requests
`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`; matching delete calls
`heap_caps_free`. Native builds retain global allocation and the existing
fault-injection seam. Insufficient internal heap returns null and follows
each instrument's existing allocation-failure path. This deliberately avoids
a silent move of audio control words into external RAM.

The exception to the generic always-lock-free assertion is restricted to
ESP32-S3, Xtensa S32C1I, IDF 5.5 and the enabled PSRAM workaround. Every
`Latest` and `SpscQueue` constructor checks its aligned control words with
`esp_ptr_internal`. The check remains active when assertions are disabled.
Other targets still need the always-lock-free 32-bit trait. A future IDF
minor version needs renewed target inspection before extending this gate.

The Cardputer/native callback lifetime gate uses separate aligned 32-bit words
for admission and writer-pending state. Its constructor enforces the same storage
contract. Audio admission attempts one strong CAS; only the control thread may
retry while waiting for in-flight audio. This also avoids an opaque subword
atomic-flag RMW implementation and interference between neighboring byte flags.
The UI overlay queue now reuses the same aligned 32-bit `SpscQueue` mechanism,
and HAL volume values read by audio use aligned 32-bit atomic words rather than
byte atomics. The existing CYD audio mutex remains intact.

`Latest<T>` requires one non-reentrant producer and one non-reentrant
consumer. Its three slots are privately owned by producer, consumer and the
atomic middle token. The producer copies into its own slot before release
publication. The consumer takes a slot with acquire ordering before reading
the payload. Superseded publications may be coalesced; ordered events belong
in `SpscQueue` and its overflow result must be handled.

Publication first tries one strong CAS. Only the consumer can cause that
attempt to fail, and a successful consumer operation clears the dirty bit.
Without another publication, the consumer cannot change the clean middle
token again. The producer's second strong CAS therefore succeeds under the
SPSC contract. A second failure aborts as a contract violation instead of
continuing with corrupted slot ownership. Consumption attempts one strong
CAS and returns false if a newer publication wins first. Its prior view and
caller output remain intact; it can adopt an update on the next callback.
This also bounds telemetry publication when the audio thread is producer.

Reappearance of the same token after multiple publications does not expose
a stale payload: a successful CAS transfers the slot currently represented
by that token. The producer cannot access that consumer-owned slot until the
consumer releases it in a later successful transfer.

## Reproduction and limits

```sh
pio run -e cardputer -t compiledb
python tools/probe_atomics.py --environment cardputer --output delivery/atomic-probe-cardputer
pio run -e cardputer
python tools/package_firmware.py cardputer
python tools/qualify.py --sanitizer none --output delivery/atomic-portable
python tools/qualify.py --sanitizer address --output delivery/atomic-asan
python tools/qualify.py --sanitizer thread --output delivery/atomic-tsan
```

The extended probe includes the actual snapshot publish/consume wrappers, the
bounded SPSC queue and word-sized volume load/store path, internal allocation/free
calls, IDF version macros and subword atomic runtime functions. Its receipt records source SHA, tree, clean/dirty snapshot,
compiler, commands, header hashes and runtime archive hashes. Refer to the
final integrated CI artifacts for the candidate's pass/fail classifications;
the initial probe above is diagnostic evidence from an earlier source.

Portable regression covers coherent equal-length text, repeated token reuse
through coalesced publications, 250,000 concurrent snapshots and ordered
events, actual instrument control/render concurrency, unchanged native State
sizes and explicit allocation failures. Sanitizer success is scoped to the
executed portable harnesses. Disassembly establishes the selected code path
and finite CAS count, not worst-case MCU cycle timing. Physical callback
timing, cache/bus effects and internal-heap recovery remain owner evaluation
requirements; no physical hardware execution is claimed here.
