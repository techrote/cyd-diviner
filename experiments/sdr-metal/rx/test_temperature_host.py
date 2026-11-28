#!/usr/bin/env python3
"""Run the actual temperature formatter/lifecycle against a native sensor shim.

This validates command behavior, not hardware temperature accuracy or RF.
Requires Linux gcc; Windows callers can invoke through WSL.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

PRELUDE = r'''
#include <assert.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
typedef void *temperature_sensor_handle_t;
typedef struct {int minimum,maximum;} temperature_sensor_config_t;
#define TEMPERATURE_SENSOR_CONFIG_DEFAULT(a,b) ((temperature_sensor_config_t){a,b})
static unsigned test_case;
static char calls[16];
static void record(char c) {size_t n=strlen(calls);assert(n+1<sizeof(calls));calls[n]=c;calls[n+1]=0;}
static int temperature_sensor_install(const temperature_sensor_config_t *c,temperature_sensor_handle_t *s) {
    record('I');assert(c->minimum==10&&c->maximum==80);
    if(test_case==1)return -1;
    *s=(void*)1;return 0;
}
static int temperature_sensor_enable(temperature_sensor_handle_t s) {assert(s==(void*)1);record('E');return test_case==2?-2:0;}
static int temperature_sensor_get_celsius(temperature_sensor_handle_t s,float *value) {
    assert(s==(void*)1);record('R');
    *value=test_case==6?NAN:test_case==7?FLT_MAX:test_case==8?INFINITY:37.25f;
    return test_case==3?-3:0;
}
static int temperature_sensor_disable(temperature_sensor_handle_t s) {assert(s==(void*)1);record('D');return test_case==4?-4:0;}
static int temperature_sensor_uninstall(temperature_sensor_handle_t s) {assert(s==(void*)1);record('U');return test_case==5?-5:0;}
static int64_t esp_timer_get_time(void) {return INT64_C(123456789012345);}
static bool line(const char *fmt,...) __attribute__((format(printf,1,2)));
static bool line(const char *fmt,...) {
    char text[2048];va_list args;va_start(args,fmt);int n=vsnprintf(text,sizeof(text),fmt,args);va_end(args);
    assert(n>0&&(size_t)n<sizeof(text));fputs(text,stdout);return true;
}
'''
MAIN = r'''
int main(void) {
    for(test_case=0;test_case<9;++test_case) {
        calls[0]=0;temperature();fprintf(stderr,"calls:%s\n",calls);
    }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    source = (root / "main/main.c").read_text(encoding="utf-8")
    begin = source.index("static void temperature(void) {")
    end = source.index("static void info(void) {", begin)
    body = source[begin:end]
    args.output.mkdir(parents=True, exist_ok=True)
    generated, binary = args.output / "temperature-test.c", args.output / "temperature-test"
    generated.write_text(PRELUDE + body + MAIN, encoding="utf-8")
    subprocess.run(["gcc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(generated), "-lm", "-o", str(binary)], check=True)
    run = subprocess.run([str(binary)], text=True, capture_output=True, check=True)
    records = [json.loads(row.removeprefix("METAL ")) for row in run.stdout.splitlines()]
    orders = run.stderr.splitlines()
    assert orders == ["calls:IERDU", "calls:I", "calls:IEU"] + ["calls:IERDU"] * 6
    assert len(records) == 9
    for index, receipt in enumerate(records):
        assert receipt["schema"] == 1 and receipt["type"] == "temperature"
        assert receipt["query_completed_us"] == 123456789012345
        assert receipt["status_not_called"] == -2147483648
        assert receipt["install_status"] == (-1 if index == 1 else 0)
        assert receipt["enable_status"] == (-2147483648 if index == 1 else -2 if index == 2 else 0)
        assert receipt["read_status"] == (-2147483648 if index in (1, 2) else -3 if index == 3 else 0)
        assert receipt["disable_status"] == (-2147483648 if index in (1, 2) else -4 if index == 4 else 0)
        assert receipt["uninstall_status"] == (-2147483648 if index == 1 else -5 if index == 5 else 0)
        assert receipt["temperature_value_valid"] == (index in (0, 4, 5))
        assert receipt["temperature_c_driver"] == (37.25 if index in (0, 4, 5) else None)
    result = {"schema": "sdr-metal-temperature-host-test/1", "evidence_class": "HOST_SENSOR_SHIM",
              "cases": 9, "source_function_sha256": hashlib.sha256(body.encode()).hexdigest(),
              "tested": ["success", "install_failure", "enable_failure", "read_failure", "disable_failure",
                         "uninstall_failure", "NaN", "finite_formatter_overflow", "infinity"],
              "valid": True, "temperature_accuracy_measured": False, "RF_measurement": False}
    (args.output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
