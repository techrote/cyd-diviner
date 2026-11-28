#!/usr/bin/env python3
"""Exercise actual read-only gain getter and tune decision bodies, ASAN/UBSAN."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "snapshot.h"
#define WIFI_GAINS 83u
#define GAIN_REG 0x6001c02cu
typedef int _lock_t;
static unsigned checks,reads,depth;
static uint32_t agc,rf,bb;
static bool apply_ok=true;
static void *gain_write;
static uint16_t codes[WIFI_GAINS];
static bool valid[WIFI_GAINS];
static void check(bool v) {++checks;assert(v);}
static _lock_t phy_get_lock(void) {return 1;}
static void _lock_acquire(_lock_t *p) {(void)p;check(!depth);++depth;}
static void _lock_release(_lock_t *p) {(void)p;check(depth==1);--depth;}
static uint32_t read_reg(uint32_t a) {
    ++reads;
    switch(a) {
      case GAIN_REG:return agc;
      case 0x60006118u:check(depth==1);return rf;
      case 0x6000611cu:check(depth==1);return bb;
      case 0x6002600cu:return 0x12345678u;
      case 0x60026014u:return 0xabcdef01u;
      case 0x60026018u:return 0x87654321u;
      default:assert(false);return 0;
    }
}
#define REG_READ(a) read_reg(a)
typedef struct {unsigned mhz;int offset_khz;bool alternate;} rx_lo_plan_t;
static rx_lo_plan_t rx_lo_plan(unsigned mhz) {return (rx_lo_plan_t){mhz,0,false};}
static void rx_lo_select(bool a) {(void)a;}
static void rx_recalibrate(unsigned a) {(void)a;}
static void set_chanfreq(unsigned a,unsigned b) {(void)a;(void)b;}
static void set_rf_freq_offset(unsigned a,unsigned b,int c) {(void)a;(void)b;(void)c;}
static void stop_tx_tone(unsigned a) {(void)a;}
static void rom_pbus_workmode(void) {}
static void rom_pbus_xpd_tx_off(void) {}
static void rom_pbus_xpd_rx_on(unsigned a) {check(a==1);}
static void rom_set_rxclk_en(unsigned a) {check(a==1);}
static void esp_rom_delay_us(uint32_t a) {check(a==3000);}
static bool rx_gain_verified_apply(bool manual,unsigned index) {check(manual&&index<83);return apply_ok;}
metal_rx_config_t metal_rx_config;
'''
TESTS = r'''
static unsigned reference_bb(unsigned code) {
    unsigned out=0;
    /* Independent per-bit mapping: fine[2:0] -> [8:6], reversed LNA[7:3]
     * -> [5:1], low RF bit8 -> bit0. */
    for(unsigned bit=0;bit<3;++bit) if(code&(1u<<bit))out|=1u<<(bit+6);
    for(unsigned bit=3;bit<8;++bit) if(code&(1u<<bit))out|=1u<<(8-bit);
    if(code&(1u<<8))out|=1;
    return out;
}
int main(void) {
    metal_rx_diagnostic_t d;metal_gain_diagnostic_t g;
    metal_rx_diagnostic(false,&d);check(!d.registers_valid&&!reads);
    check(!strcmp(d.stage,"phy_not_enabled")&&!d.agc_raw&&!d.gain.hook_ready);
    gain_write=(void*)1;valid[40]=true;
    for(unsigned c=0;c<=65535;++c) {
        codes[40]=(uint16_t)c;unsigned er=0x180u|(c>>8),eb=reference_bb(c);
        rf=(er<<9)|0x1ffu;bb=(eb<<18)|0x3ffffu;
        metal_gain_diagnostic(40,&g);
        check(g.hook_ready&&g.code_valid&&g.code==c&&g.expected_rf==er&&g.expected_bb==eb&&g.analog_matches);
        rf^=1u<<9;metal_gain_diagnostic(40,&g);check(!g.analog_matches);
    }
    metal_gain_diagnostic(83,&g);check(g.hook_ready&&!g.code_valid&&!g.analog_matches);
    agc=127u<<8;check(!metal_rx_tune(2433,-80,40));metal_rx_diagnostic(true,&d);
    check(!strcmp(d.stage,"maximum_gain_out_of_range")&&d.maximum_gain==127);
    agc=16u<<8;check(!metal_rx_tune(2433,-80,40));metal_rx_diagnostic(true,&d);
    check(!strcmp(d.stage,"requested_gain_above_maximum")&&d.requested_gain==40);
    agc=82u<<8;apply_ok=false;gain_write=0;
    check(!metal_rx_tune(2433,-80,40));metal_rx_diagnostic(true,&d);
    check(!strcmp(d.stage,"gain_table_code_unavailable"));
    gain_write=(void*)1;valid[40]=false;
    check(!metal_rx_tune(2433,-80,40));metal_rx_diagnostic(true,&d);
    check(!strcmp(d.stage,"gain_table_code_unavailable"));
    valid[40]=true;check(!metal_rx_tune(2433,-80,40));metal_rx_diagnostic(true,&d);
    check(!strcmp(d.stage,"analog_gain_not_latched"));
    apply_ok=true;check(metal_rx_tune(2433,-80,40));metal_rx_diagnostic(true,&d);
    check(!strcmp(d.stage,"ready")&&metal_rx_config.ready&&metal_rx_config.gain==40);
    check(d.syscon_bb_cfg==0x12345678u&&d.syscon_wifi_clock==0xabcdef01u&&d.syscon_wifi_reset==0x87654321u);
    unsigned old_reads=reads;metal_rx_diagnostic(false,&d);check(reads==old_reads&&!d.registers_valid);
    check(!metal_rx_tune(0,0,40));metal_rx_diagnostic(true,&d);check(!strcmp(d.stage,"invalid_arguments"));
    check(!metal_rx_tune(65536,0,40));check(!metal_rx_tune(2433,0,83));check(!depth);
    printf("{\"schema\":\"sdr-metal-gain-host-test/1\",\"evidence_class\":\"HOST_MMIO_SHIM\",\"checks\":%u,\"gain_words_exhaustive\":65536,\"valid\":true,\"RF_measurement\":false}\n",checks);
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    args=ap.parse_args();root=Path(__file__).resolve().parent
    source=(root/'main/snapshot.c').read_text(encoding='utf-8')
    start=source.index('static const char *tune_stage');end=source.index('bool metal_rx_prepare',start)
    body=source[start:end]
    vendor=(root/'vendor/esp-sdr/rx_gain_verify.c').read_text(encoding='utf-8')
    getter=vendor[vendor.index('void metal_gain_diagnostic('):]
    args.output.mkdir(parents=True,exist_ok=True)
    generated=args.output/'gain-test.c';binary=args.output/'gain-test'
    generated.write_text(PRELUDE+getter+body+TESTS,encoding='utf-8')
    subprocess.run(['gcc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer','-I',str(root/'main'),str(generated),'-o',str(binary)],check=True)
    run=subprocess.run([str(binary)],capture_output=True,text=True,check=True)
    result=json.loads(run.stdout);result['tune_body_sha256']=hashlib.sha256(body.encode()).hexdigest()
    result['getter_body_sha256']=hashlib.sha256(getter.encode()).hexdigest()
    (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))

if __name__=='__main__':main()
