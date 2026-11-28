#!/usr/bin/env python3
"""Run the actual fast-loop body with deterministic CPU/MMIO fakes, not RF."""
import json
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parent
PRELUDE = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define IRAM_ATTR
typedef struct { const uint32_t *words,*deltas;uint32_t length,baud,ring_remainder,duration_cycles; } FastLoop;
typedef struct { uint32_t writes,elapsed_cycles,next_start_cycles,ring_phase; } FastResult;
static uint64_t fake_clock;
static uint32_t read_cost,store_cost,irq_period,irq_cost,word_count;
static uint32_t observed[100000];
static uint32_t cycles(void) { fake_clock+=read_cost;return (uint32_t)fake_clock; }
static void store_word(uint32_t value) {
    assert(word_count<100000);observed[word_count++]=value;fake_clock+=store_cost;
    if(irq_period&&word_count%irq_period==0)fake_clock+=irq_cost;
}
'''
TEST = r'''
static unsigned scenarios;
static void exercise(uint32_t baud,uint32_t length,uint32_t duration,uint32_t read_cycles,
                     uint32_t write_cycles,uint32_t irq_every,uint32_t irq_cycles,bool wrap) {
    uint32_t words[97],deltas[97];assert(length<=97);
    for(uint32_t i=0;i<length;++i)words[i]=UINT32_C(0xa5b60000)+i;
    metal_fast_schedule_t schedule;
    assert(metal_fast_schedule_build(&schedule,240000000,baud,length,deltas));
    FastLoop p={words,deltas,length,baud,schedule.ring_remainder,duration};
    fake_clock=wrap?UINT32_MAX-50:0;read_cost=read_cycles;store_cost=write_cycles;
    irq_period=irq_every;irq_cost=irq_cycles;word_count=0;
    FastResult result=fast_store_loop(&p);
    assert(result.writes==word_count&&result.writes>0);
    for(uint32_t i=0;i<word_count;++i)assert(observed[i]==words[i%length]);
    assert(result.elapsed_cycles>=duration);
    assert(result.elapsed_cycles<=duration+read_cycles+write_cycles+irq_cycles+read_cycles);
    uint64_t ideal_next=(uint64_t)result.writes*240000000/baud;
    assert(result.next_start_cycles<=ideal_next&&ideal_next-result.next_start_cycles<=1);
    uint64_t rings=result.writes/length;
    assert(result.ring_phase==rings*schedule.ring_remainder%baud);
    if(baud==240000000&&write_cycles>10) {
        assert(result.writes<duration/2); /* Overload slows an intact prefix. */
        assert(result.elapsed_cycles>ideal_next);
    }
    ++scenarios;
}
int main(void) {
    exercise(2208000,97,100000,1,2,0,0,false);
    exercise(2208000,97,100000,1,2,7,3000,false);
    exercise(2208000,97,100000,1,2,7,3000,true);
    exercise(240000000,97,100000,1,20,0,0,true);
    exercise(1,1,1000,1,2,0,0,true);
    for(uint32_t i=0;i<100;++i)
        exercise(i%2?2208000:44100,1+i%97,50000,1+i%3,1+i%80,3+i%11,i%4?0:4000,i%2);
    printf("{\"schema\":\"sdr-metal-fast-loop-host/1\",\"status\":\"PASS\",\"scenarios\":%u,"
           "\"evidence_class\":\"ACTUAL_LOOP_BODY_FAKE_CPU_MMIO_NOT_RF\",\"physical_execution\":false}\n",scenarios);
}
'''


def main():
    source = (ROOT / "main/tx_metal_s3.c").read_text(encoding="utf-8")
    # Skip the earlier prototype, then extract the unchanged body by its marker.
    start = source.index("static FastResult IRAM_ATTR __attribute__((noinline)) fast_store_loop", source.index("/* Minimal paced backend:"))
    end = source.index("\n}", start) + 2
    code = PRELUDE + (ROOT / "main/fast_schedule.h").read_text(encoding="utf-8") + source[start:end] + TEST
    output = "/tmp/cyd-metal-fast-loop-" + uuid.uuid4().hex
    runner = ["wsl"] if sys.platform == "win32" else []
    result = subprocess.run(runner + ["gcc", "-x", "c", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                                     "-fsanitize=address,undefined", "-o", output, "-"],
                            input=code, text=True, capture_output=True)
    if result.returncode:
        raise SystemExit(result.stderr)
    result = subprocess.run(runner + [output], text=True, capture_output=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    print(json.dumps(json.loads(result.stdout)))


if __name__ == "__main__":
    main()
