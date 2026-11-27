#!/usr/bin/env python3
"""Compile the actual snapshot body against an explicit MMIO/RTOS test shim.

Requires Linux gcc (Windows can invoke with WSL). This does not model RF or
sample-clock performance. It checks completion/fault/ownership paths under
ASAN+UBSAN without editing the firmware source or accessing a serial port.
"""
from pathlib import Path
import argparse
import json
import re
import subprocess

PRELUDE = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/mman.h>
#include "snapshot.h"
#define CONFIG_IDF_TARGET_ESP32S3 1
#define IRAM_ATTR
#define SOC_RESERVE_MEMORY_REGION(a,b,c)
typedef int _lock_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned critical_depth, lock_depth, checks, fake_mode;
static uint32_t fake_owner, fake_ctrl, fake_lanes;
static bool triggered;
static int64_t fake_time;
static uint32_t fake_cycles;
static _lock_t phy_get_lock(void) {return 1;}
static void _lock_acquire(_lock_t *p) {(void)p;++lock_depth;}
static void _lock_release(_lock_t *p) {(void)p;assert(lock_depth);--lock_depth;}
#define portENTER_CRITICAL(p) ((void)(p),++critical_depth)
#define portEXIT_CRITICAL(p) ((void)(p),--critical_depth)
static int64_t esp_timer_get_time(void) {fake_time+=10;return fake_time;}
static uint32_t esp_cpu_get_cycle_count(void) {fake_cycles+=240;return fake_cycles;}
static void esp_rom_delay_us(uint32_t us) {(void)us;}
typedef struct {unsigned mhz;int offset_khz;bool alternate;} rx_lo_plan_t;
static rx_lo_plan_t rx_lo_plan(unsigned mhz) {return (rx_lo_plan_t){mhz,0,false};}
static bool rx_lo_select(bool alternate) {(void)alternate;return false;}
void set_chanfreq(unsigned a,unsigned b) {(void)a;(void)b;}
void set_rf_freq_offset(unsigned a,unsigned b,int c) {(void)a;(void)b;(void)c;}
void stop_tx_tone(unsigned a) {(void)a;}
void rom_pbus_workmode(void) {}
void rom_pbus_xpd_tx_off(void) {}
void rom_pbus_xpd_rx_on(unsigned a) {(void)a;}
void rom_set_rxclk_en(unsigned a) {(void)a;}
void rx_recalibrate(unsigned a) {(void)a;}
bool rx_recalibration_stale(void) {return false;}
bool rx_gain_verified_apply(bool a,unsigned b) {(void)a;(void)b;return true;}
bool rx_gain_verified_check(unsigned a) {(void)a;return true;}
void metal_gain_diagnostic(unsigned a,metal_gain_diagnostic_t *out) {(void)a;*out=(metal_gain_diagnostic_t){0};}
static uint32_t read_reg(uint32_t address) {
    if(address==0x600c101c)return fake_owner;
    if(address==0x60033d90)return fake_lanes;
    if(address==0x6001c02c)return 82u<<8;
    assert(address==0x60033d5c);
    if(triggered&&fake_mode!=1) {
        volatile uint32_t *p=(void*)0x3fcd0000;
        unsigned n=fake_ctrl&0x7fff;
        for(unsigned j=0;j<n;++j)if(fake_mode!=3||j)p[j]=j^0x000abcde;
        if(fake_mode==2)p[16380]=0;
        fake_ctrl|=1u<<18;triggered=false;
    }
    return fake_ctrl;
}
static void write_reg(uint32_t address,uint32_t value) {
    if(address==0x600c101c) {if(fake_mode!=4||(value&15)==4)fake_owner=value;return;}
    if(address==0x60033d90) {fake_lanes=value;return;}
    assert(address==0x60033d5c);fake_ctrl=value;
    if(value&(1u<<19))triggered=true;
    if(!value)triggered=false;
}
#define REG_READ(a) read_reg(a)
#define REG_WRITE(a,b) write_reg(a,b)
'''
TESTS = r'''
static void check(bool value) {++checks;assert(value);}
static void reset(unsigned mode) {
    fake_mode=mode;fake_owner=0x1234567f;fake_lanes=0x76543210;fake_ctrl=0;
    triggered=false;fake_time=0;fake_cycles=0;critical_depth=lock_depth=0;
}
int main(void) {
    void *p=mmap((void*)0x3fcd0000,65536,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(p==(void*)0x3fcd0000);
    metal_snapshot_t r;
    for(unsigned rate_index=0;rate_index<3;++rate_index)for(unsigned n=1;n<=16380;n=n==1?2:n==2?16379:n+1) {
        unsigned rates[]={16000000,40000000,80000000};unsigned rate=rates[rate_index];reset(0);
        check(metal_snapshot(n,rate,20000,&r));
        check(r.hardware_done&&r.registers_restored&&r.tail_canary_ok&&!r.unchanged_sentinel_words);
        check(r.owner_before==r.owner_after&&r.owner_before==0x1234567f);
        check(r.lanes_before==r.lanes_after&&r.lanes_before==0x76543210);
        check(r.requested_samples==n&&r.nominal_rate_hz==rate);
        check((r.control&((1u<<15)|(1u<<16)))==(rate==16000000?1u<<16:rate==40000000?1u<<15:0));
        check(!critical_depth&&!lock_depth);
    }
    reset(1);check(!metal_snapshot(16380,80000000,50,&r));
    check(!r.hardware_done&&r.registers_restored&&r.tail_canary_ok);
    check(!strcmp(r.reason,"hardware_done_timeout"));check(!fake_ctrl&&fake_owner==0x1234567f);
    reset(2);check(!metal_snapshot(16380,80000000,20000,&r));
    check(r.hardware_done&&r.registers_restored&&!r.tail_canary_ok);check(!strcmp(r.reason,"tail_canary_changed"));
    reset(3);check(!metal_snapshot(100,80000000,20000,&r));
    check(r.unchanged_sentinel_words==1);check(!strcmp(r.reason,"unchanged_sentinel_words"));
    reset(4);check(!metal_snapshot(100,80000000,20000,&r));
    check(!r.registers_restored);check(!strcmp(r.reason,"register_restore_mismatch"));
    reset(0);fake_ctrl=0x80000007;((uint32_t*)p)[0]=0x1234;
    check(!metal_snapshot(100,80000000,20000,&r));
    check(!strcmp(r.reason,"dump_already_active"));check(((uint32_t*)p)[0]==0x1234);
    check(fake_ctrl==0x80000007&&fake_owner==0x1234567f&&fake_lanes==0x76543210);
    check(!critical_depth&&!lock_depth);
    reset(0);check(!metal_snapshot(0,80000000,20000,&r));
    check(!metal_snapshot(16381,80000000,20000,&r));check(!metal_snapshot(1,80000001,20000,&r));
    check(!metal_snapshot(1,80000000,0,&r));check(!metal_snapshot(1,80000000,20000,NULL));
    check(!critical_depth&&!lock_depth);check(fake_ctrl==0&&fake_owner==0x1234567f);
    check(munmap(p,65536)==0);
    printf("{\"schema\":\"sdr-metal-snapshot-host-test/1\",\"evidence_class\":\"HOST_MMIO_SHIM\",\"checks\":%u,\"valid\":true,\"RF_measurement\":false}\n",checks);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    args.output.mkdir(parents=True, exist_ok=True)
    source = (root / "main/snapshot.c").read_text(encoding="utf-8")
    source = re.sub(r"^#include[^\n]*\n", "", source, flags=re.M)
    # Host assembler has no Xtensa memw. Replace only that instruction with
    # a compiler barrier; all register/control flow remains the actual source.
    source = source.replace('__asm__ __volatile__("memw" ::: "memory");',
                            '__asm__ __volatile__("" ::: "memory");')
    generated = args.output / "snapshot-test.c"
    generated.write_text(PRELUDE + source + TESTS, encoding="utf-8")
    binary = args.output / "snapshot-test"
    subprocess.run(["gcc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(root / "main"),
                    str(generated), "-o", str(binary)], check=True)
    completed = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
    result = json.loads(completed.stdout)
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(completed.stdout, end="")


if __name__ == "__main__":
    main()
