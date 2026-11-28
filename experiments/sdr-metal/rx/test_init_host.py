#!/usr/bin/env python3
"""Native lifecycle checks of the actual RX initialization body; no RF model."""
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
#include "snapshot.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_WIFI_NOT_STARTED 0x3002
#define PHY_MODEM_WIFI 1
#define WIFI_STORAGE_RAM 1
#define WIFI_MODE_NULL 0
#define WIFI_PS_NONE 0
#define WIFI_SECOND_CHAN_NONE 0
typedef struct {int nvs_enable;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){1})
static bool initialized,last_available,direct_phy_enabled,direct_power_domain_owned,direct_rx_enabled,initialization_attempted;
static bool wifi_driver_initialized,default_event_loop_created;
static int initialization_error,wifi_stop_status,wifi_deinit_status,event_loop_delete_status;
static uint32_t last_crc;
metal_rx_config_t metal_rx_config;
static unsigned power_refs,phy_refs,rx_refs,checks;
static uint32_t direct_rx_enable_invocations,direct_rx_disable_invocations;
static char calls[256],fail;
static void check(bool value) {++checks;assert(value);}
static int call(char value) {size_t n=strlen(calls);assert(n+1<sizeof(calls));calls[n]=value;calls[n+1]=0;return fail==value?-9:0;}
static void esp_wifi_bt_power_domain_on(void) {check(!phy_refs);++power_refs;call('P');}
static void esp_wifi_bt_power_domain_off(void) {check(power_refs==1&&!phy_refs&&!rx_refs);--power_refs;call('Q');}
static void esp_phy_enable(int mode) {check(mode==1&&power_refs==1&&!phy_refs);++phy_refs;call('E');}
static void esp_phy_disable(int mode) {check(mode==1&&power_refs==1&&phy_refs==1&&!rx_refs);--phy_refs;call('D');}
static void phy_wifi_enable_set(uint8_t enabled) {
    check(power_refs==1&&phy_refs==1);
    if(enabled) {check(!rx_refs);rx_refs=1;call('V');}
    else {check(rx_refs==1);rx_refs=0;call('v');}
}
static int esp_event_loop_create_default(void) {return call('C');}
static int esp_event_loop_delete_default(void) {return call('Y');}
static int esp_wifi_init(const wifi_init_config_t *cfg) {check(!cfg->nvs_enable);return call('I');}
static int esp_wifi_set_storage(int mode) {check(mode==WIFI_STORAGE_RAM);return call('S');}
static int esp_wifi_set_mode(int mode) {check(mode==WIFI_MODE_NULL);return call('M');}
static int esp_wifi_start(void) {return call('A');}
static int esp_wifi_set_ps(int mode) {check(mode==WIFI_PS_NONE);return call('N');}
static int esp_wifi_set_promiscuous(bool enable) {check(enable);return call('R');}
static int esp_wifi_set_channel(int channel,int second) {check(channel==1&&second==0);return call('H');}
static int esp_wifi_stop(void) {return call('T');}
static int esp_wifi_deinit(void) {return call('X');}
'''
TESTS = r'''
static void reset(void) {
    initialized=last_available=direct_phy_enabled=direct_power_domain_owned=direct_rx_enabled=initialization_attempted=false;
    wifi_driver_initialized=default_event_loop_created=false;
    metal_rx_config.ready=false;power_refs=phy_refs=rx_refs=0;direct_rx_enable_invocations=direct_rx_disable_invocations=0;calls[0]=fail=0;
}
static void clear(void) {calls[0]=0;fail=0;}
int main(void) {
    reset();check(!initialized&&!initialization_attempted&&!direct_power_domain_owned&&!calls[0]);
    check(!strcmp(init_mode(),"none"));
    check(initialize(false));check(!strcmp(calls,"PEV"));
    check(initialized&&initialization_attempted&&direct_power_domain_owned&&direct_phy_enabled);
    check(!wifi_driver_initialized&&!default_event_loop_created);
    check(direct_rx_enabled&&rx_refs==1&&direct_rx_enable_invocations==1&&!direct_rx_disable_invocations);
    clear();last_available=true;metal_rx_config.ready=true;
    check(initialize(false));check(!calls[0]&&power_refs==1&&phy_refs==1);
    check(last_available&&metal_rx_config.ready);
    check(direct_rx_enable_invocations==1&&!direct_rx_disable_invocations); /* same path is a no-op */
    check(initialize(true));check(!strcmp(calls,"vDQCISMANRH"));
    check(initialized&&wifi_driver_initialized&&default_event_loop_created);
    check(!last_available&&!metal_rx_config.ready&&!direct_power_domain_owned&&!power_refs&&!phy_refs);
    clear();check(initialize(false));check(!strcmp(calls,"TXYPEV"));
    check(initialized&&direct_power_domain_owned&&!default_event_loop_created&&!wifi_driver_initialized);

    /* Fail before a Wi-Fi driver exists: only the owned event loop remains,
     * and retrying direct cleans it before acquiring any PHY domain. */
    clear();fail='I';check(!initialize(true));check(!strcmp(calls,"vDQCI"));
    check(!initialized&&!wifi_driver_initialized&&default_event_loop_created&&!power_refs&&!phy_refs);
    clear();check(initialize(false));check(!strcmp(calls,"YPEV"));
    /* Partial driver setup is represented; recovery cannot double-power. */
    clear();fail='A';check(!initialize(true));check(!strcmp(calls,"vDQCISMA"));
    check(!strcmp(init_mode(),"WiFi_driver_partial"));
    clear();check(initialize(false));check(!strcmp(calls,"TXYPEV"));

    /* Each teardown error prevents enabling the newly requested mode. */
    clear();check(initialize(true));clear();fail='T';check(!initialize(false));
    check(!strcmp(calls,"T")&&!direct_power_domain_owned&&!power_refs&&!phy_refs);
    check(wifi_driver_initialized&&default_event_loop_created&&!initialized);
    clear();check(initialize(false));check(!strcmp(calls,"TXYPEV"));
    clear();check(initialize(true));clear();fail='X';check(!initialize(false));
    check(!strcmp(calls,"TX")&&!power_refs&&!phy_refs&&wifi_driver_initialized);
    clear();check(initialize(false));check(!strcmp(calls,"TXYPEV"));
    clear();check(initialize(true));clear();fail='Y';check(!initialize(false));
    check(!strcmp(calls,"TXY")&&!wifi_driver_initialized&&default_event_loop_created&&!power_refs);
    clear();check(initialize(false));check(!strcmp(calls,"YPEV"));
    check(direct_rx_enabled&&rx_refs==1&&direct_rx_enable_invocations==direct_rx_disable_invocations+1);
    printf("{\"schema\":\"sdr-metal-init-host-test/1\",\"evidence_class\":\"HOST_LIFECYCLE_SHIM\",\"checks\":%u,\"valid\":true,\"RF_measurement\":false}\n",checks);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    source = (root / "main/main.c").read_text(encoding="utf-8")
    boot = source[source.index("void app_main(void)"):]
    assert "initialize(" not in boot and "esp_phy_enable(" not in boot and "esp_wifi_init(" not in boot
    begin = source.index("static const char *init_mode(void)")
    end = source.index("/* Explicit outside-window query", begin)
    body = source[begin:end]
    args.output.mkdir(parents=True, exist_ok=True)
    generated, binary = args.output / "init-test.c", args.output / "init-test"
    generated.write_text(PRELUDE + body + TESTS, encoding="utf-8")
    subprocess.run(["gcc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-I", str(root / "main"), str(generated), "-o", str(binary)], check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
    result = json.loads(run.stdout)
    result["source_function_sha256"] = hashlib.sha256(body.encode()).hexdigest()
    result["idle_boot_has_no_RF_initialization"] = True
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
