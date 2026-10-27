"""Run the actual cooperative sensor readiness policy with a fake clock/transport.

This proves bounded scheduling and validity bookkeeping, not electrical sensor
behavior or a measured wake interval on the real board.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest
import uuid

from tests.test_diagnostic_i2c import function_source

ROOT = Path(__file__).resolve().parents[1]

HARNESS = r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "board.h"
volatile diag_report_t g_diag_mailbox;
static unsigned touch_index;
'''

TRANSPORT = r'''
static uint32_t now, calls, calls_by_bank[4], status_calls[4];
static unsigned behavior[4], failures_left[4], last_bank;
static uint8_t last_reg;
enum { HEALTHY, WAKE, ABSENT, HARD, BAD_FAMILY };
uint32_t board_millis(void) { return now; }
static int i2c_read2(unsigned index, uint8_t reg, uint16_t *result) {
    ++calls; ++calls_by_bank[index]; last_bank=index; last_reg=reg;
    volatile diag_i2c_bank_t *bank=&g_diag_mailbox.i2c[index];
    unsigned bit=1u<<index;
    ++bank->attempts; g_diag_mailbox.touch_attempted_mask |= bit;
    sensor_address_nack[index]=0;
    if (behavior[index]==ABSENT || behavior[index]==HARD ||
        (behavior[index]==WAKE && failures_left[index]-- != 0)) {
        int wake=behavior[index]!=HARD;
        sensor_address_nack[index]=(uint8_t)wake;
        bank->result=DIAG_UNAVAILABLE;
        bank->last_failure_phase=DIAG_I2C_ADDRESS_WRITE;
        bank->last_failure_result=DIAG_UNAVAILABLE;
        bank->last_failure_sr1=wake?0x400:0x100;
        bank->last_failure_ms=now;
        ++g_diag_mailbox.touch_errors[index];
        g_diag_mailbox.touch_history_mask|=bit;
        g_diag_mailbox.touch_ok_mask&=~bit;
        g_diag_mailbox.sensor[index].last_failed_register=reg;
        if (wake) ++g_diag_mailbox.sensor[index].wake_nacks;
        else {
            ++g_diag_mailbox.sensor[index].peripheral_resets;
            g_diag_mailbox.errors|=DIAG_ERROR_I2C;
        }
        return DIAG_UNAVAILABLE;
    }
    if (behavior[index]==WAKE) behavior[index]=HEALTHY;
    ++bank->successes; bank->result=DIAG_SUCCESS;
    if(reg==0x8f) *result=behavior[index]==BAD_FAMILY?0x0500:0x059a;
    else if(reg==0x91) *result=0x010a;
    else {
        assert(reg==0xaa);
        *result=(uint16_t)bit; ++status_calls[index];
        g_diag_mailbox.touch_masks[index]=*result;
        g_diag_mailbox.touch_ok_mask|=bit;
        ++g_diag_mailbox.sensor[index].status_reads;
        g_diag_mailbox.sensor[index].last_status_ms=now;
    }
    return DIAG_SUCCESS;
}
'''

CASES = r'''
static void fresh(uint32_t start) {
    memset((void*)&g_diag_mailbox,0,sizeof g_diag_mailbox);
    memset(behavior,0,sizeof behavior); memset(failures_left,0,sizeof failures_left);
    memset(calls_by_bank,0,sizeof calls_by_bank); memset(status_calls,0,sizeof status_calls);
    memset(sensor_ever_ready,0,sizeof sensor_ever_ready);
    memset(sensor_address_nack,0,sizeof sensor_address_nack);
    now=start; calls=0; sensor_init(now);
}
static void service(uint32_t at) {
    now=at;
    unsigned before=calls;
    sensor_service(now);
    assert(calls-before<=1); /* No retry loop can block USB behind five attempts. */
    for(unsigned i=0;i<4;i++)
        assert(g_diag_mailbox.i2c[i].attempts==g_diag_mailbox.i2c[i].successes+
               g_diag_mailbox.touch_errors[i]);
}
static void run(uint32_t start, unsigned ms) {
    for(unsigned delta=0;delta<=ms;delta++) {
        /* Repeated USB main-loop iterations during each same millisecond. */
        for(unsigned spin=0;spin<8;spin++)service(start+delta);
    }
}
static void basic_and_wake(void) {
    fresh(0); behavior[0]=WAKE; failures_left[0]=1;
    for(unsigned at=0;at<15;at++)service(at);
    assert(calls==0 && g_diag_mailbox.touch_attempted_mask==0);
    service(15); assert(calls==1 && last_bank==0 && last_reg==0x8f);
    assert(g_diag_mailbox.sensor[0].state==DIAG_SENSOR_FAMILY);
    assert(sensor_due[0]==17 && g_diag_mailbox.touch_ok_mask==0);
    run(15,10);
    assert(g_diag_mailbox.touch_initialized_mask==15 && g_diag_mailbox.touch_ok_mask==15);
    assert(g_diag_mailbox.touch_failed_mask==0 && g_diag_mailbox.errors==0);
    for(unsigned i=0;i<4;i++) {
        const volatile diag_sensor_bank_t *s=&g_diag_mailbox.sensor[i];
        assert(s->state==DIAG_SENSOR_READY && s->identity_valid==1);
        assert(s->family_id==0x9a && s->device_id==0x0a05 && s->device_rev==1);
        assert(s->first_ready_ms>=15 && s->first_ready_ms<=25);
        assert(s->status_reads==1 && s->last_status_ms==s->first_ready_ms);
        assert(s->peripheral_resets==0 && s->readiness_result==DIAG_SUCCESS);
    }
    assert(g_diag_mailbox.sensor[0].wake_nacks==1 && g_diag_mailbox.sensor[0].retries==1);
    assert(g_diag_mailbox.sensor[0].init_failures==0 && g_diag_mailbox.touch_history_mask==1);
    unsigned before=calls; run(26,50); assert(calls==before);
    run(77,30); assert(g_diag_mailbox.sensor[0].status_reads==2);
    assert(g_diag_mailbox.sensor[0].wake_nacks==1); /* History survives success. */
}
static void absent_is_bounded_and_fair(void) {
    fresh(0); behavior[0]=ABSENT;
    run(0,200);
    assert(calls_by_bank[0]==SENSOR_MAX_ATTEMPTS);
    assert(g_diag_mailbox.sensor[0].state==DIAG_SENSOR_FAILED);
    assert(g_diag_mailbox.sensor[0].wake_nacks==5 && g_diag_mailbox.sensor[0].retries==4);
    assert(g_diag_mailbox.sensor[0].init_failures==1 && g_diag_mailbox.touch_failed_mask==1);
    assert(g_diag_mailbox.touch_initialized_mask==14 && g_diag_mailbox.touch_ok_mask==14);
    assert(g_diag_mailbox.errors==DIAG_ERROR_I2C);
    for(unsigned i=1;i<4;i++)assert(status_calls[i]>=3);
    unsigned before=calls_by_bank[0]; run(201,700); assert(calls_by_bank[0]==before);
    behavior[0]=HEALTHY; run(902,150);
    assert(g_diag_mailbox.sensor[0].state==DIAG_SENSOR_READY);
    assert(g_diag_mailbox.touch_initialized_mask==15 && g_diag_mailbox.touch_failed_mask==0);
    assert(g_diag_mailbox.sensor[0].init_failures==1); /* Historical failure retained. */
}
static void hard_failure_and_identity_mismatch(void) {
    fresh(0); behavior[0]=HARD; behavior[1]=BAD_FAMILY;
    run(0,40);
    assert(calls_by_bank[0]==1 && calls_by_bank[1]==1);
    assert(g_diag_mailbox.sensor[0].state==DIAG_SENSOR_FAILED);
    assert(g_diag_mailbox.sensor[0].wake_nacks==0 && g_diag_mailbox.sensor[0].peripheral_resets==1);
    assert(g_diag_mailbox.sensor[1].state==DIAG_SENSOR_UNSUPPORTED);
    assert(g_diag_mailbox.sensor[1].identity_valid==0);
    assert(g_diag_mailbox.sensor[1].readiness_result==DIAG_INVALID);
    assert(g_diag_mailbox.touch_failed_mask==3 && g_diag_mailbox.touch_initialized_mask==12);
    assert(g_diag_mailbox.errors==DIAG_ERROR_I2C);
}
static void delayed_deadline_and_tick_wrap(void) {
    fresh(50); service(50+SENSOR_DEADLINE_MS);
    assert(calls==0); /* Deadline expired before any transaction: explicit timeout. */
    for(unsigned i=0;i<4;i++) {
        assert(g_diag_mailbox.sensor[i].state==DIAG_SENSOR_FAILED);
        assert(g_diag_mailbox.sensor[i].readiness_result==DIAG_TIMEOUT);
    }
    fresh(UINT32_MAX-10u); run(UINT32_MAX-10u,30);
    assert(g_diag_mailbox.touch_initialized_mask==15 && g_diag_mailbox.errors==0);
    for(unsigned i=0;i<4;i++)assert(g_diag_mailbox.sensor[i].first_ready_ms<20);
}
static void runtime_wake_and_stale_data(void) {
    fresh(0); run(0,30);
    uint32_t first=g_diag_mailbox.sensor[0].first_ready_ms;
    uint32_t status_time=g_diag_mailbox.sensor[0].last_status_ms;
    uint32_t reads=g_diag_mailbox.sensor[0].status_reads;
    behavior[0]=WAKE; failures_left[0]=1;
    uint32_t due=sensor_due[0]; touch_index=0; service(due);
    assert(last_bank==0 && last_reg==0xaa);
    assert(g_diag_mailbox.touch_masks[0]==1 && !(g_diag_mailbox.touch_ok_mask&1));
    assert(!(g_diag_mailbox.touch_initialized_mask&1));
    assert(g_diag_mailbox.sensor[0].state==DIAG_SENSOR_STATUS);
    assert(g_diag_mailbox.sensor[0].last_status_ms==status_time);
    assert(g_diag_mailbox.sensor[0].status_reads==reads);
    assert(g_diag_mailbox.sensor[0].readiness_result==DIAG_BUSY);
    run(due,5);
    assert(g_diag_mailbox.sensor[0].state==DIAG_SENSOR_READY);
    assert(g_diag_mailbox.sensor[0].first_ready_ms==first);
    assert(g_diag_mailbox.sensor[0].status_reads==reads+1);
    assert(g_diag_mailbox.sensor[0].last_status_ms>=due+2);
    assert(g_diag_mailbox.errors==0 && g_diag_mailbox.sensor[0].retries==1);
    behavior[0]=HARD; run(due+6,90);
    assert(g_diag_mailbox.sensor[0].state==DIAG_SENSOR_FAILED);
    assert(!(g_diag_mailbox.touch_ok_mask&1) && !(g_diag_mailbox.touch_initialized_mask&1));
    assert(g_diag_mailbox.sensor[0].identity_valid==1); /* Historical ID still identifiable. */
}
int main(void) {
    _Static_assert(offsetof(diag_report_t,touch_attempted_mask)==192,"v2 offset");
    _Static_assert(offsetof(diag_report_t,touch_history_mask)==196,"v2 offset");
    _Static_assert(offsetof(diag_report_t,i2c)==200,"v2 records");
    _Static_assert(offsetof(diag_report_t,sensor)==328,"v3 appended only");
    assert(DIAG_PROTOCOL_VERSION==3);
    basic_and_wake(); absent_is_bounded_and_fair();
    hard_failure_and_identity_mismatch(); delayed_deadline_and_tick_wrap();
    runtime_wake_and_stale_data();
    puts("PASS: actual sensor policy has bounded wake retries, fair bank scheduling, identities, deadlines, wrap-safe clocks, valid fresh status and retained failure history");
    return 0;
}
'''


class DiagnosticSensorReadinessTests(unittest.TestCase):
    def test_actual_readiness_policy_with_fake_clock_and_transport(self):
        record_path = ROOT / ".tools/config/core-toolchain.json"
        if not record_path.exists():
            self.skipTest("Portable compiler is not prepared")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(), record["executable_sha256"])
        source = (ROOT / "firmware/diagnostic/board.c").read_text(encoding="utf-8")
        globals_start = source.index("#define SENSOR_SETTLE_MS")
        globals_end = source.index("static int spi_ready;", globals_start)
        signatures = ("static int sensor_due_now(", "static void sensor_begin(",
                      "static void sensor_init(", "static void sensor_failed(",
                      "static void sensor_service(")
        output = ROOT / ".tools/tmp" / ("diagnostic-sensor-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        cfile, binary = output / "sensor-test.c", output / "sensor-test.exe"
        cfile.write_text(HARNESS + source[globals_start:globals_end] + TRANSPORT +
                         "\n".join(function_source(source, s) for s in signatures) + CASES,
                         encoding="utf-8")
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        compiled = subprocess.run([str(compiler), "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                   "-fno-sanitize=undefined", "-I", str(ROOT / "firmware/diagnostic"),
                                   str(cfile), "-o", str(binary)], cwd=ROOT, env=env,
                                  check=False, capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, check=False,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS: actual sensor policy", result.stdout)
        print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
