#!/usr/bin/env python3
"""Exercise actual deferred USB formatter and command gates with SDK fakes.

No serial, PHY, firmware execution or host delivery attestation. Windows uses
WSL gcc; ASAN/UBSAN guard the extracted production body and fixed buffers.
"""
import json
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "main/tx_metal_s3.c"
PRELUDE = r'''
#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define pdMS_TO_TICKS(n) (n)
static int64_t now;
static unsigned driver_calls,zero_budget,read_calls;
static int write_mode,flush_status;
static size_t wire_length;
static char wire[32768];
static int64_t esp_timer_get_time(void) { now+=1000;return now; }
static void vTaskDelay(unsigned ticks) { now+=1000*ticks; }
static int usb_serial_jtag_write_bytes(const void *p,size_t n,unsigned ticks) {
    assert(ticks==100);++driver_calls;
    if(write_mode==1&&n>7)n=7;
    if(write_mode==2&&zero_budget) { --zero_budget;return 0; }
    if(write_mode==3&&driver_calls==2)return -1;
    if(write_mode==4)return 0;
    if(write_mode==5)return (int)n+1;
    if(write_mode==6)return -1;
    assert(wire_length+n<=sizeof(wire));memcpy(wire+wire_length,p,n);wire_length+=n;return (int)n;
}
static int usb_serial_jtag_wait_tx_done(unsigned ticks) { assert(ticks==5000);return flush_status; }
'''
COMMAND_FAKES = r'''
enum { CpuHz=240000000,DurationLimitUs=2000000,MaskLimitUs=20000 };
typedef enum { Api,Ram,Gate,Ask,Fast } Backend;
static uint32_t capacity=64,loaded_bits=64,loaded_crc32,permit,trace_stride,trace_capacity=512;
static bool minimal_phy;
static uint8_t storage[64],*packed=storage;
static unsigned transmissions,prepare_calls;
static bool prepare_allowed;
static bool prepare_fast(uint32_t baud) { ++prepare_calls;return baud!=0&&prepare_allowed; }
static void info(void) {}
static void temperature(void) {}
static void error(const char *s) { (void)s; }
static bool reserve(uint32_t n,uint32_t t) { (void)n;(void)t;return true; }
static uint32_t esp_rom_crc32_le(uint32_t seed,const uint8_t *p,uint32_t n) { (void)seed;(void)p;(void)n;return 0; }
static int usb_serial_jtag_read_bytes(void *p,uint32_t n,unsigned ticks) { (void)p;(void)n;(void)ticks;++read_calls;return 0; }
static void transmit(Backend b,uint32_t a,uint32_t c,uint32_t d,uint32_t e,uint32_t f) { (void)b;(void)a;(void)c;(void)d;(void)e;(void)f;++transmissions; }
'''
TEST = r'''
static void reset(void) {
    memset(&output_stats,0,sizeof(output_stats));output_resync_pending=false;
    now=wire_length=driver_calls=zero_budget=read_calls=transmissions=0;
    write_mode=flush_status=0;permit=0x1234;loaded_bits=64;trace_stride=0;prepare_calls=0;prepare_allowed=true;
}
int main(void) {
    char maximum[4096],oversize[4097],longline[1501];
    memset(maximum,'x',sizeof(maximum));maximum[4094]='\n';maximum[4095]=0;
    memset(oversize,'y',sizeof(oversize));oversize[4096]=0;
    memset(longline,'z',1500);longline[1500]=0;
    reset();assert(output_puts("hello")==6);assert(wire_length==6&&!memcmp(wire,"hello\n",6));
    assert(output_stats.lines_attempted==1&&output_stats.lines_accepted==1&&output_stats.bytes_accepted==6);
    reset();assert(output_printf("%s",maximum)==4095);assert(wire_length==4095&&output_stats.write_calls==4);
    reset();assert(output_printf("%s",oversize)<0);assert(wire_length==0&&output_stats.format_failures==1&&output_stats.lines_failed==1&&driver_calls==0);
    reset();write_mode=1;assert(output_printf("%s",longline)==1500);assert(wire_length==1500&&!memcmp(wire,longline,1500));
    assert(output_stats.short_writes>0&&output_stats.bytes_requested==1500&&output_stats.bytes_accepted==1500);
    reset();write_mode=2;zero_budget=3;assert(output_puts("later")==6);assert(output_stats.zero_writes==3&&output_stats.short_writes==3&&wire_length==6);
    reset();write_mode=3;assert(output_printf("%s",longline)<0);assert(wire_length==1024&&output_resync_pending&&output_stats.write_errors==1);
    write_mode=0;assert(output_puts("next")==5);assert(wire_length==1030&&!memcmp(wire+1024,"\nnext\n",6));
    assert(output_stats.lines_failed==1&&output_stats.lines_accepted==1&&!output_resync_pending);
    reset();write_mode=4;assert(output_puts("timeout")<0);assert(now>=OUTPUT_TIMEOUT_US&&now<=OUTPUT_TIMEOUT_US+5000);
    assert(output_stats.zero_writes>0&&output_stats.zero_writes==output_stats.short_writes&&output_resync_pending&&wire_length==0);
    reset();write_mode=5;assert(output_puts("bad count")<0);assert(output_stats.write_errors==1&&wire_length==0&&output_stats.bytes_accepted==0);
    reset();assert(output_flush());flush_status=-1;assert(!output_flush());assert(output_stats.flush_failures==1&&output_stats.lines_failed==0);
    reset();output_stats.lines_attempted=UINT32_MAX;output_stats.lines_accepted=UINT32_MAX;
    assert(output_puts("saturated")>0);assert(output_stats.lines_attempted==UINT32_MAX&&output_stats.lines_accepted==UINT32_MAX);
    reset();assert(output_printf("%u\n",(unsigned)output_stats.lines_attempted)==2);assert(!memcmp(wire,"0\n",2));
    reset();flush_status=-1;char rejected_flush[]="tx gate 1000000 96 112 1000 0 1234";command(rejected_flush);
    assert(transmissions==0&&permit==0&&output_stats.flush_failures==1);
    reset();write_mode=6;char rejected_write[]="tx gate 1000000 96 112 1000 0 1234";command(rejected_write);
    assert(transmissions==0&&permit==0&&output_stats.lines_failed==1);
    reset();char accepted[]="tx gate 1000000 96 112 1000 0 1234";command(accepted);assert(transmissions==1&&permit==0);
    reset();write_mode=6;char failed_upload[]="load 8 0";command(failed_upload);assert(loaded_bits==0&&read_calls==0);
    reset();char accepted_fast[]="tx fast 2208000 96 112 1000 0 1234";command(accepted_fast);
    assert(transmissions==1&&prepare_calls==1&&permit==0);
    reset();char zero_fast[]="tx fast 0 96 112 1000 0 1234";command(zero_fast);assert(transmissions==0&&prepare_calls==0);
    reset();char masked_fast[]="tx fast 2208000 96 112 1000 1 1234";command(masked_fast);assert(transmissions==0&&prepare_calls==0);
    reset();trace_stride=1;char traced_fast[]="tx fast 2208000 96 112 1000 0 1234";command(traced_fast);assert(transmissions==0&&prepare_calls==0);
    reset();prepare_allowed=false;char unavailable_fast[]="tx fast 2208000 96 112 1000 0 1234";command(unavailable_fast);
    assert(transmissions==0&&prepare_calls==1&&permit==0&&output_stats.lines_attempted==0);
    puts("PASS 20 actual-body formatter/write-all/framing/counter/start-gate/fast-admission scenarios");return 0;
}
'''


def main():
    source = SOURCE.read_text(encoding="utf-8")
    helper = source[source.index("#define OUTPUT_CAPACITY"):source.index("/* Temperature is an explicit")]
    command = source[source.index("static void command(char *line)"):source.index("void app_main(void)")]
    runner = ["wsl"] if sys.platform == "win32" else []
    output = "/tmp/cyd-metal-tx-output-" + uuid.uuid4().hex
    built = subprocess.run(runner + ["gcc", "-x", "c", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                                    "-fsanitize=address,undefined", "-o", output, "-"],
                           input=PRELUDE + helper + COMMAND_FAKES + command + TEST, text=True, capture_output=True)
    if built.returncode:
        raise SystemExit(built.stderr)
    ran = subprocess.run(runner + [output], text=True, capture_output=True)
    if ran.returncode:
        raise SystemExit(ran.stdout + ran.stderr)
    print(json.dumps({"schema": "sdr-metal-tx-output-host/1", "evidence_class": "HOST_FAKE_SDK",
                      "physical_execution": False, "scenarios": 20, "status": "PASS", "stdout": ran.stdout.strip()}))


if __name__ == "__main__":
    main()
