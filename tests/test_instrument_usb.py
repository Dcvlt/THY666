"""Compile the complete instrument USB source with mock endpoint declarations."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]

STM32 = r'''
#ifndef INSTRUMENT_USB_FAKE_STM32_H
#define INSTRUMENT_USB_FAKE_STM32_H
#include <stdint.h>
typedef struct {uint32_t AHB1ENR, AHB2ENR, AHB2RSTR;} RCC_TypeDef;
typedef struct {uint32_t MODER, OTYPER, OSPEEDR, PUPDR, AFR[2];} GPIO_TypeDef;
typedef struct {uint32_t GCCFG;} USB_OTG_GlobalTypeDef;
typedef struct {uint32_t DCTL;} USB_OTG_DeviceTypeDef;
extern RCC_TypeDef fake_rcc;
extern GPIO_TypeDef fake_gpioa;
extern USB_OTG_GlobalTypeDef fake_usb;
extern const uint32_t fake_uid[3];
#define RCC (&fake_rcc)
#define GPIOA (&fake_gpioa)
#define USB_OTG_FS (&fake_usb)
#define USB_OTG_DEVICE_BASE 0u
#define UID_BASE ((uintptr_t)fake_uid)
#define RCC_AHB1ENR_GPIOAEN 1u
#define RCC_AHB2ENR_OTGFSEN 2u
#define RCC_AHB2RSTR_OTGFSRST 2u
#define USB_OTG_DCTL_SDIS 2u
#define USB_OTG_GCCFG_PWRDWN 1u
#define USB_OTG_GCCFG_NOVBUSSENS 2u
#define OTG_FS_IRQn 67u
static inline void NVIC_SetPriority(unsigned irq, unsigned priority) {(void)irq;(void)priority;}
static inline void NVIC_ClearPendingIRQ(unsigned irq) {(void)irq;}
static inline void __NOP(void) {}
#endif
'''

TINYUSB = r'''
#ifndef INSTRUMENT_USB_FAKE_TUSB_H
#define INSTRUMENT_USB_FAKE_TUSB_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {unsigned role, speed;} tusb_rhport_init_t;
#define TUSB_ROLE_DEVICE 1u
#define TUSB_SPEED_FULL 1u
bool tusb_init(uint8_t rhport, const tusb_rhport_init_t *init);
void tusb_int_handler(uint8_t rhport, bool in_isr);
void tud_task(void);
bool tud_midi_packet_read(uint8_t packet[4]);
bool tud_cdc_connected(void);
uint32_t tud_cdc_available(void);
int tud_cdc_read_char(void);
void tud_cdc_read_flush(void);
bool tud_cdc_write_clear(void);
uint32_t tud_cdc_write(const void *bytes, uint32_t count);
void tud_cdc_write_flush(void);
#endif
'''


class InstrumentUsbTests(unittest.TestCase):
    def test_actual_usb_epoch_console_and_bounded_midi_rx(self):
        record_path = ROOT / ".tools/config/core-toolchain.json"
        if not record_path.exists():
            self.skipTest("Portable compiler is not prepared")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        if not compiler.exists():
            self.skipTest("Portable compiler executable is absent")
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(),
                         record["executable_sha256"])
        output = ROOT / ".tools/tmp" / ("instrument-usb-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        (output / "stm32f4xx.h").write_text(STM32, encoding="utf-8")
        (output / "tusb.h").write_text(TINYUSB, encoding="utf-8")
        binary = output / "test_instrument_usb.exe"
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        compiled = subprocess.run([
            str(compiler), "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(output), "-I", str(ROOT / "core"),
            "-I", str(ROOT / "firmware/diagnostic"), str(ROOT / "core/thy_midi.c"),
            str(ROOT / "tests/core/test_instrument_usb.c"), "-o", str(binary),
        ], cwd=ROOT, env=env, check=False, capture_output=True, text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, check=False,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("instrument USB lifecycle tests passed: 8 scenarios", result.stdout)


if __name__ == "__main__":
    unittest.main()
