#!/usr/bin/env python3
"""Exercise each actual TX ownership/cleanup body with deterministic SDK fakes.

This is a host lifecycle test, not PHY/RF validation. Windows uses WSL gcc.
"""
import json
import pathlib
import subprocess
import sys
import uuid

ROOT=pathlib.Path(__file__).resolve().parent
PRELUDE=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
typedef int esp_err_t;
typedef struct { int nvs_enable; } wifi_init_config_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_WIFI_NOT_STARTED 2
#define PHY_MODEM_WIFI 1
#define WIFI_STORAGE_RAM 1
#define WIFI_MODE_NULL 0
#define WIFI_PS_NONE 0
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){99})
enum { CpuHz=240000000 };
static bool minimal_phy,radio_ready,wifi_driver_initialized,event_loop_owned,direct_power_domain_owned;
static int wifi_stop_status,wifi_deinit_status,event_loop_create_status,event_loop_delete_status,teardown_status;
static int create_code,init_code,storage_code,stop_code,deinit_code,delete_code;
static unsigned created,deleted,phy_on,phy_off,power_on,power_off;
static int esp_clk_cpu_freq(void) { return CpuHz; }
static int xPortGetCoreID(void) { return 0; }
static void esp_wifi_power_domain_on(void) { assert(!direct_power_domain_owned);++power_on; }
static void esp_wifi_power_domain_off(void) { assert(direct_power_domain_owned&&phy_off==power_on);++power_off; }
static void esp_phy_enable(int m) { assert(m==1&&direct_power_domain_owned&&power_on>power_off);++phy_on; }
static void esp_phy_disable(int m) { assert(m==1&&direct_power_domain_owned&&phy_on>phy_off);++phy_off; }
static int esp_event_loop_create_default(void) { if(!create_code)++created;return create_code; }
static int esp_event_loop_delete_default(void) { ++deleted;return delete_code; }
static int esp_wifi_init(wifi_init_config_t *cfg) { assert(cfg->nvs_enable==0);return init_code; }
static int esp_wifi_set_storage(int m) { assert(m==1);return storage_code; }
static int esp_wifi_set_mode(int m) { assert(m==0);return 0; }
static int esp_wifi_start(void) { return 0; }
static int esp_wifi_set_ps(int m) { assert(m==0);return 0; }
static int esp_wifi_stop(void) { return stop_code; }
static int esp_wifi_deinit(void) { return deinit_code; }
'''
TEST=r'''
static void reset(void) {
    minimal_phy=radio_ready=wifi_driver_initialized=event_loop_owned=direct_power_domain_owned=false;
    create_code=init_code=storage_code=stop_code=deinit_code=delete_code=0;
    created=deleted=phy_on=phy_off=power_on=power_off=0;
}
int main(void) {
    reset();assert(start_radio()==0);assert(event_loop_owned&&wifi_driver_initialized);stop_radio();
    assert(deleted==1&&!event_loop_owned&&!wifi_driver_initialized&&!radio_ready&&teardown_status==0);
    minimal_phy=true;assert(start_radio()==0);stop_radio();assert(phy_on==1&&phy_off==1&&power_on==1&&power_off==1&&!direct_power_domain_owned&&deleted==1);
    assert(start_radio()==0);stop_radio();assert(power_on==2&&power_off==2&&phy_on==2&&phy_off==2);
    reset();init_code=-10;assert(start_radio()==-10);assert(deleted==1&&!event_loop_owned&&!wifi_driver_initialized);
    reset();storage_code=-11;assert(start_radio()==-11);assert(deleted==1&&!event_loop_owned&&!wifi_driver_initialized);
    reset();storage_code=-11;stop_code=-12;assert(start_radio()==-11);assert(teardown_status==-12&&wifi_driver_initialized&&event_loop_owned&&deleted==0);
    minimal_phy=true;assert(start_radio()==ESP_ERR_INVALID_STATE);assert(phy_on==0);
    reset();assert(start_radio()==0);stop_code=-13;stop_radio();assert(teardown_status==-13&&event_loop_owned&&wifi_driver_initialized&&deleted==0&&radio_ready);
    reset();assert(start_radio()==0);deinit_code=-14;stop_radio();assert(teardown_status==-14&&event_loop_owned&&wifi_driver_initialized&&deleted==0&&radio_ready);
    reset();assert(start_radio()==0);delete_code=-15;stop_radio();assert(teardown_status==-15&&event_loop_owned&&!wifi_driver_initialized&&radio_ready);
    reset();create_code=ESP_ERR_INVALID_STATE;assert(start_radio()==0);assert(!event_loop_owned);stop_radio();assert(deleted==0&&!radio_ready);
    reset();assert(start_radio()==0);stop_code=ESP_ERR_WIFI_NOT_STARTED;stop_radio();assert(teardown_status==0&&deleted==1&&!radio_ready);
    puts("PASS 10 actual-body ownership/cleanup scenarios");return 0;
}
'''

results=[]
runner=['wsl'] if sys.platform=='win32' else []
for target,name in [('classic','tx/main/tx_metal.c'),('s3','tx-s3/main/tx_metal_s3.c')]:
    text=(ROOT/name).read_text(encoding='utf-8')
    start=text.index('static esp_err_t cleanup_wifi(void)')
    end=text.index('/* Separate maximum-store-loop context.',start)
    body=text[start:end]
    output='/tmp/cyd-metal-lifecycle-'+uuid.uuid4().hex
    built=subprocess.run(runner+['gcc','-x','c','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-o',output,'-'],input=PRELUDE+body+TEST,text=True,capture_output=True)
    if built.returncode:raise SystemExit(built.stderr)
    ran=subprocess.run(runner+[output],text=True,capture_output=True)
    if ran.returncode:raise SystemExit(ran.stdout+ran.stderr)
    results.append({'target':target,'status':'PASS','scenarios':10,'stdout':ran.stdout.strip()})
print(json.dumps({'schema':'cyd-metal-tx-lifecycle-host/1','physical_execution':False,'results':results},indent=2))
