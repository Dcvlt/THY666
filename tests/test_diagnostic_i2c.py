"""Execute actual diagnostic I2C functions against scripted registers, offline.

The model checks the driver's register accesses and recovery bookkeeping. It
does not emulate an electrical bus, Cortex-M interrupts, or sensor timing.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


def function_source(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError("Unterminated diagnostic function")


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
#define I2C1 UINT32_C(0x40005400)
/* A shortened finite loop makes timeout paths quick; source logic is exact. */
#define LOOP_LIMIT 4u
volatile diag_report_t g_diag_mailbox;
static uint32_t registers[9], sr1_script[32], sr2_script[16];
static unsigned sr1_count, sr1_at, sr2_count, sr2_at;
static unsigned data_accesses, reset_count, bank_index;
static uint8_t sensor_address_nack[4];
static unsigned stop_stuck;
static uint32_t data_writes[3], now;
static int capture_data_write;
static unsigned pending_data_write;
uint32_t board_millis(void) { return now; }
static volatile uint32_t *fake_mmio(uint32_t address) {
    assert(address >= I2C1 && address <= I2C1 + 0x20 && !(address & 3));
    unsigned reg = (address-I2C1)/4;
    /* Observe writes at the next access, before supplying another read. */
    if (capture_data_write) {
        data_writes[pending_data_write] = registers[4];
        capture_data_write = 0;
    }
    if (registers[0] == 0x8000) {
        /* The snapshot must exist before the software reset clears flags. */
        const volatile diag_i2c_bank_t *bank = &g_diag_mailbox.i2c[bank_index];
        assert(bank->last_failure_phase != DIAG_I2C_NONE);
        assert(bank->last_failure_result != DIAG_SUCCESS);
        assert(bank->last_failure_ms == now);
        assert(g_diag_mailbox.touch_history_mask & (1u << bank_index));
        registers[5] = registers[6] = 0;
        reset_count++;
    }
    if (reg == 0 && !stop_stuck && (registers[0] & 0x200)) registers[0] &= ~0x200u;
    if (reg == 5 && sr1_at == sr1_count && (registers[0] & 0x200)) {
        /* AF clear is a write, not a newly sampled SR1 observation. */
    } else if (reg == 5) {
        assert(sr1_at < sr1_count);
        registers[5] = sr1_script[sr1_at++];
    } else if (reg == 6) {
        assert(sr2_at < sr2_count);
        registers[6] = sr2_script[sr2_at++];
    } else if (reg == 4) {
        assert(data_accesses < 5);
        if (data_accesses < 3) {
            pending_data_write = data_accesses;
            capture_data_write = 1;
        } else registers[4] = data_accesses == 3 ? 0x5a : 0xa5;
        data_accesses++;
    }
    return &registers[reg];
}
#define MMIO32(address) (*fake_mmio(address))
static void script(const uint32_t *sr1, unsigned n1,
                   const uint32_t *sr2, unsigned n2) {
    memset(registers, 0, sizeof registers);
    memcpy(sr1_script, sr1, n1*sizeof *sr1);
    memcpy(sr2_script, sr2, n2*sizeof *sr2);
    sr1_count=n1; sr2_count=n2; sr1_at=sr2_at=0;
    data_accesses=reset_count=0; capture_data_write=0; stop_stuck=0;
    memset(data_writes, 0, sizeof data_writes);
}
static void fresh(unsigned index) {
    memset((void *)&g_diag_mailbox, 0, sizeof g_diag_mailbox);
    g_diag_mailbox.pclk1_hz=42000000;
    bank_index=index; now=120;
}
'''

CASES = r'''
static const uint32_t healthy_sr1[] = {1,2,2,128,4,1,2,2,4};
static const uint32_t healthy_sr2[] = {0,3,3};
static void healthy(unsigned index) {
    uint16_t result=0;
    script(healthy_sr1,9,healthy_sr2,3);
    assert(i2c_read2(index,0xaa,&result)==DIAG_SUCCESS);
    assert(result==0xa55a && g_diag_mailbox.touch_masks[index]==0xa55a);
    assert(g_diag_mailbox.i2c[index].result==DIAG_SUCCESS);
    assert((g_diag_mailbox.touch_ok_mask & (1u<<index))!=0);
    assert(sr1_at==9 && sr2_at==3 && data_accesses==5 && reset_count==0);
    static const uint32_t addresses[]={0x36,0x35,0x39,0x38};
    assert(data_writes[0]==addresses[index]*2);
    assert(data_writes[1]==0xaa);
    assert(data_writes[2]==addresses[index]*2+1);
    assert(registers[0]==0x401);
    assert(g_diag_mailbox.sensor[index].last_status_ms == now);
    assert(g_diag_mailbox.sensor[index].status_reads != 0);
}
static void failure_case(unsigned phase, uint32_t flags, int expected_result) {
    uint32_t sr1[32], sr2[16];
    unsigned n1=0,n2=0;
    fresh(2);
    if (phase==DIAG_I2C_BUS_FREE) {
        for(unsigned n=0;n<LOOP_LIMIT;n++)sr2[n2++]=2;
        sr1[n1++]=flags;
        sr2[n2++]=3;
    } else {
        sr2[n2++]=0;
        unsigned sequence_phase[]={
            DIAG_I2C_START_WRITE,DIAG_I2C_ADDRESS_WRITE,
            DIAG_I2C_POINTER_READY,DIAG_I2C_POINTER_SENT,
            DIAG_I2C_START_READ,DIAG_I2C_ADDRESS_READ,DIAG_I2C_RECEIVE};
        uint32_t ready[]={1,2,128,4,1,2,4};
        for(unsigned n=0;n<7;n++) {
            if(sequence_phase[n]==phase) {
                if(expected_result==DIAG_TIMEOUT)
                    for(unsigned k=0;k<LOOP_LIMIT;k++)sr1[n1++]=0;
                else sr1[n1++]=flags|ready[n]; /* Errors take priority. */
                break;
            }
            sr1[n1++]=ready[n];
            if(sequence_phase[n]==DIAG_I2C_ADDRESS_WRITE ||
               sequence_phase[n]==DIAG_I2C_ADDRESS_READ) {
                sr1[n1++]=2; sr2[n2++]=3; /* Actual ADDR-clear sequence. */
            }
        }
        sr2[n2++]=3; /* Failure-only SR2 observation. */
    }
    int wake_nack = flags == 0x400 &&
        (phase == DIAG_I2C_ADDRESS_WRITE || phase == DIAG_I2C_ADDRESS_READ);
    if (wake_nack) sr2[n2++] = 0; /* Bus released after STOP. */
    g_diag_mailbox.touch_masks[2]=0x2468;
    g_diag_mailbox.touch_ok_mask=4;
    script(sr1,n1,sr2,n2);
    uint16_t result=0x1357;
    assert(i2c_read2(2,0xaa,&result)==expected_result);
    const volatile diag_i2c_bank_t *bank=&g_diag_mailbox.i2c[2];
    assert(result==0x1357 && g_diag_mailbox.touch_masks[2]==0x2468);
    assert(bank->attempts==1 && bank->successes==0 && bank->result==expected_result);
    assert(bank->last_failure_phase==phase && bank->last_failure_result==expected_result);
    assert(bank->last_failure_sr1==(phase==DIAG_I2C_BUS_FREE?flags:sr1[n1-1]));
    assert(bank->last_failure_sr2==3 && bank->last_failure_ms==120);
    assert(g_diag_mailbox.touch_errors[2]==1 && g_diag_mailbox.touch_history_mask==4);
    assert(g_diag_mailbox.touch_attempted_mask==4 && g_diag_mailbox.touch_ok_mask==0);
    assert(g_diag_mailbox.errors==(wake_nack ? 0u : DIAG_ERROR_I2C));
    assert(sensor_address_nack[2] == wake_nack);
    assert(g_diag_mailbox.sensor[2].wake_nacks == (unsigned)wake_nack);
    assert(g_diag_mailbox.sensor[2].peripheral_resets == (unsigned)!wake_nack);
    assert(g_diag_mailbox.sensor[2].last_failed_register == 0xaa);
    assert(sr1_at==n1 && sr2_at==n2 && reset_count==(unsigned)!wake_nack);
    assert(registers[0]==0x401);
    if (!wake_nack) {
        assert(registers[1]==42 && registers[2]==0x4000);
        assert(registers[7]==210 && registers[8]==43); /* Original 100kHz config. */
    }
    uint32_t saved_sr1=bank->last_failure_sr1;
    now=200;
    healthy(2);
    assert(bank->attempts==2 && bank->successes==1 && bank->result==DIAG_SUCCESS);
    assert(bank->last_failure_phase==phase && bank->last_failure_result==expected_result);
    assert(bank->last_failure_sr1==saved_sr1 && bank->last_failure_sr2==3);
    assert(bank->last_failure_ms==120 && g_diag_mailbox.touch_errors[2]==1);
    assert(g_diag_mailbox.touch_history_mask==4 && g_diag_mailbox.errors==(wake_nack ? 0u : DIAG_ERROR_I2C));
}
static void identity_read_does_not_publish_button_status(void) {
    fresh(0); script(healthy_sr1,9,healthy_sr2,3);
    uint16_t result=0;
    assert(i2c_read2(0,0x8f,&result)==DIAG_SUCCESS && result==0xa55a);
    assert(data_writes[1]==0x8f);
    assert(g_diag_mailbox.i2c[0].successes==1);
    assert(g_diag_mailbox.touch_ok_mask==0 && g_diag_mailbox.touch_masks[0]==0);
    assert(g_diag_mailbox.sensor[0].status_reads==0 && reset_count==0);
}
static void address_nack_cleanup_failure_is_hard_fault(int stuck_stop) {
    const uint32_t sr1[]={1,0x402};
    const uint32_t sr2[]={0,3,2,2,2,2};
    fresh(1); script(sr1,2,sr2,stuck_stop?2:6);
    stop_stuck=(unsigned)stuck_stop;
    uint16_t result=0xbeef;
    assert(i2c_read2(1,0x91,&result)==DIAG_UNAVAILABLE);
    assert(result==0xbeef && reset_count==1 && !sensor_address_nack[1]);
    assert(g_diag_mailbox.sensor[1].wake_nacks==0);
    assert(g_diag_mailbox.sensor[1].peripheral_resets==1);
    assert(g_diag_mailbox.sensor[1].last_failed_register==0x91);
    assert(g_diag_mailbox.i2c[1].last_failure_sr1==0x402);
    assert(g_diag_mailbox.i2c[1].last_failure_sr2==3);
    assert(g_diag_mailbox.errors==DIAG_ERROR_I2C);
    assert(sr1_at==2 && sr2_at==(unsigned)(stuck_stop?2:6));
}
int main(void) {
    for(unsigned i=0;i<4;i++) {
        fresh(i); healthy(i);
        assert(g_diag_mailbox.touch_attempted_mask==(1u<<i));
        assert(g_diag_mailbox.i2c[i].attempts==1 && g_diag_mailbox.i2c[i].successes==1);
        assert(g_diag_mailbox.touch_history_mask==0 && g_diag_mailbox.errors==0);
    }
    failure_case(DIAG_I2C_BUS_FREE,0x100,DIAG_TIMEOUT);
    failure_case(DIAG_I2C_START_WRITE,0x400,DIAG_UNAVAILABLE);
    failure_case(DIAG_I2C_ADDRESS_WRITE,0x100,DIAG_UNAVAILABLE);
    failure_case(DIAG_I2C_ADDRESS_WRITE,0x400,DIAG_UNAVAILABLE);
    failure_case(DIAG_I2C_POINTER_READY,0x200,DIAG_UNAVAILABLE);
    failure_case(DIAG_I2C_POINTER_SENT,0x800,DIAG_UNAVAILABLE);
    failure_case(DIAG_I2C_START_READ,0,DIAG_TIMEOUT);
    failure_case(DIAG_I2C_ADDRESS_READ,0x400,DIAG_UNAVAILABLE);
    failure_case(DIAG_I2C_RECEIVE,0xf00,DIAG_UNAVAILABLE);
    identity_read_does_not_publish_button_status();
    address_nack_cleanup_failure_is_hard_fault(1);
    address_nack_cleanup_failure_is_hard_fault(0);
    puts("PASS: actual I2C source covers four addresses, eight failed phases, raw flags before reset, address NACK STOP recovery, unchanged successful transactions and retained failure history after recovery");
    return 0;
}
'''


class DiagnosticI2cTests(unittest.TestCase):
    def test_failure_stages_and_history_against_scripted_registers(self):
        record_path = ROOT / ".tools/config/core-toolchain.json"
        if not record_path.exists():
            self.skipTest("Portable compiler is not prepared")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        if not compiler.exists():
            self.skipTest("Portable compiler executable is absent")
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(),
                         record["executable_sha256"])
        source = (ROOT / "firmware/diagnostic/board.c").read_text(encoding="utf-8")
        signatures = ("static int wait_mask(", "static void i2c_config(",
                      "static int i2c_wait(", "static void i2c_clear_address(",
                      "static int i2c_read2(")
        output = ROOT / ".tools/tmp" / ("diagnostic-i2c-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        cfile, binary = output / "i2c-test.c", output / "i2c-test.exe"
        cfile.write_text(HARNESS + "\n".join(function_source(source, s) for s in signatures)
                         + CASES, encoding="utf-8")
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        compiled = subprocess.run([str(compiler), "cc", "-std=c11", "-O2", "-Wall",
                                   "-Wextra", "-Werror", "-fno-sanitize=undefined",
                                   "-I", str(ROOT / "firmware/diagnostic"),
                                   str(cfile), "-o", str(binary)],
                                  cwd=ROOT, env=env, check=False, capture_output=True,
                                  text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, check=False,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS: actual I2C source", result.stdout)
        print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
