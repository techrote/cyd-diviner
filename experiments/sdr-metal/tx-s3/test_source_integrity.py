#!/usr/bin/env python3
"""Actual source-integrity policy helpers under marked-stage corruption fakes."""
import json
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parent
PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
'''
TEST = r'''
static unsigned checks;
static void verify(const SourceIntegrity *s,bool timed,bool retained) {
    assert(timed_source_ok(s)==timed);assert(retained_source_ok(s)==retained);checks+=2;
}
static SourceIntegrity valid(void) {
    SourceIntegrity s={0};
    s.packed_expected=s.packed_before_radio=s.packed_after_radio=s.packed_after_tone=
        s.packed_before_loop=s.packed_after_loop=s.packed_after_teardown=UINT32_C(0x12345678);
    s.words_expected=s.words_before_loop=s.words_after_loop=s.words_after_teardown=UINT32_C(0xabcdef01);
    s.deltas_after_build=s.deltas_before_loop=s.deltas_after_loop=s.deltas_after_teardown=UINT32_C(0x76543210);
    s.after_radio_checked=s.after_tone_checked=s.preloop_checked=s.loop_started=s.teardown_checked=s.delta_required=s.delta_built=true;
    return s;
}
int main(void) {
    SourceIntegrity good=valid();verify(&good,true,true);
    const size_t timed_offsets[]={offsetof(SourceIntegrity,packed_expected),offsetof(SourceIntegrity,packed_before_radio),
        offsetof(SourceIntegrity,packed_after_radio),offsetof(SourceIntegrity,packed_after_tone),offsetof(SourceIntegrity,packed_before_loop),
        offsetof(SourceIntegrity,packed_after_loop),offsetof(SourceIntegrity,words_expected),offsetof(SourceIntegrity,words_before_loop),
        offsetof(SourceIntegrity,words_after_loop),offsetof(SourceIntegrity,deltas_after_build),offsetof(SourceIntegrity,deltas_before_loop),
        offsetof(SourceIntegrity,deltas_after_loop)};
    const size_t teardown_offsets[]={offsetof(SourceIntegrity,packed_after_teardown),offsetof(SourceIntegrity,words_after_teardown),
        offsetof(SourceIntegrity,deltas_after_teardown)};
    for(size_t i=0;i<sizeof(timed_offsets)/sizeof(*timed_offsets);++i)for(unsigned bit=0;bit<32;++bit) {
        SourceIntegrity s=good;*(uint32_t *)((uint8_t *)&s+timed_offsets[i])^=UINT32_C(1)<<bit;verify(&s,false,false);
    }
    for(size_t i=0;i<sizeof(teardown_offsets)/sizeof(*teardown_offsets);++i)for(unsigned bit=0;bit<32;++bit) {
        SourceIntegrity s=good;*(uint32_t *)((uint8_t *)&s+teardown_offsets[i])^=UINT32_C(1)<<bit;verify(&s,true,false);
    }
    const size_t flags[]={offsetof(SourceIntegrity,after_radio_checked),offsetof(SourceIntegrity,after_tone_checked),
        offsetof(SourceIntegrity,preloop_checked),offsetof(SourceIntegrity,loop_started),offsetof(SourceIntegrity,delta_built)};
    for(size_t i=0;i<sizeof(flags)/sizeof(*flags);++i) {
        SourceIntegrity s=good;*(bool *)((uint8_t *)&s+flags[i])=false;verify(&s,false,false);
    }
    SourceIntegrity s=good;s.teardown_checked=false;verify(&s,true,false);
    s=good;s.delta_required=false;s.delta_built=false;s.deltas_after_build=1;s.deltas_before_loop=2;s.deltas_after_loop=3;s.deltas_after_teardown=4;
    verify(&s,true,true); /* No delta table is used by other backends. */
    memset(&s,0,sizeof(s));verify(&s,false,false); /* Unchecked zero fields are not observations. */
    s=good;s.packed_expected=s.packed_before_radio=s.packed_after_radio=s.packed_after_tone=
        s.packed_before_loop=s.packed_after_loop=s.packed_after_teardown=0;
    verify(&s,true,true); /* A legitimate CRC value of zero is not a missing-value sentinel. */
    s=good;s.after_radio_checked=false;s.after_tone_checked=false;s.preloop_checked=false;s.loop_started=false;s.teardown_checked=false;
    s.packed_before_radio^=1;verify(&s,false,false);
    printf("{\"schema\":\"sdr-metal-source-integrity-policy-host/1\",\"status\":\"PASS\",\"checks\":%u,"
           "\"evidence_class\":\"ACTUAL_POLICY_HELPERS_MARKED_STAGE_CORRUPTION_FAKES\",\"physical_execution\":false}\n",checks);
}
'''


def main():
    source = (ROOT / "main/tx_metal_s3.c").read_text(encoding="utf-8")
    start = source.index("typedef struct {\n    uint32_t packed_expected")
    end = source.index("static bool emit_source_integrity", start)
    output = "/tmp/cyd-metal-source-integrity-" + uuid.uuid4().hex
    runner = ["wsl"] if sys.platform == "win32" else []
    built = subprocess.run(runner + ["gcc", "-x", "c", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                                    "-fsanitize=address,undefined", "-o", output, "-"],
                           input=PRELUDE + source[start:end] + TEST, text=True, capture_output=True)
    if built.returncode:
        raise SystemExit(built.stderr)
    ran = subprocess.run(runner + [output], text=True, capture_output=True)
    if ran.returncode:
        raise SystemExit(ran.stdout + ran.stderr)
    print(json.dumps(json.loads(ran.stdout)))


if __name__ == "__main__":
    main()
