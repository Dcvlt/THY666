/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef THY_DIAGNOSTIC_BOARD_H
#define THY_DIAGNOSTIC_BOARD_H

#include <stddef.h>
#include <stdint.h>

#define DIAG_MAILBOX_MAGIC UINT32_C(0x54485944)
/* Mailbox layout version; the additive text command protocol is THYDIAG/1. */
#define DIAG_PROTOCOL_VERSION 3u
#define DIAG_EXTERNAL_FLASH_BYTES UINT32_C(0x01000000)
#define DIAG_INTERNAL_FLASH_BASE UINT32_C(0x08000000)
#define DIAG_INTERNAL_FLASH_BYTES UINT32_C(0x00100000)
#define DIAG_READ_MAX 256u
#define DIAG_ENTRY_MAGIC UINT32_C(0x5448454e)

/* Written by Reset_Handler outside .bss after inherited DMA is reset. The
 * first nine fields after version are sampled into CPU registers before
 * selecting our stack, masks, vectors or MPU policy. Remaining MMIO fields
 * are sampled after DMA quiescence, before clock/peripheral normalization.
 * They are observations; watchdog registers do not prove it is stopped. */
typedef struct {
    uint32_t magic, version;
    uint32_t control, ipsr, primask, basepri, faultmask, msp, psp, vtor, mpu_ctrl;
    uint32_t cpacr, fpccr, aircr, ccr, icsr, shcsr, cfsr, hfsr;
    uint32_t rcc_cr, rcc_cfgr, rcc_csr, rcc_apb1enr;
    uint32_t iwdg_sr, iwdg_pr, iwdg_rlr, wwdg_cr, wwdg_cfr;
} diag_entry_t;

enum diag_clock_source {
    DIAG_CLOCK_HSI_16 = 0,
    DIAG_CLOCK_HSE_PLL_168 = 1
};

enum diag_error {
    DIAG_ERROR_HSE = 1u << 0,
    DIAG_ERROR_PLL = 1u << 1,
    DIAG_ERROR_SPI = 1u << 2,
    DIAG_ERROR_NOR_ID = 1u << 3,
    DIAG_ERROR_I2C = 1u << 4,
    DIAG_ERROR_ADC = 1u << 5,
    DIAG_ERROR_CLOCK_SWITCH = 1u << 6,
    DIAG_ERROR_ENTRY_STATE = 1u << 7,
    DIAG_ERROR_USB_INIT = 1u << 8
};

enum diag_result {
    DIAG_SUCCESS = 0,
    DIAG_INVALID = -1,
    DIAG_TIMEOUT = -2,
    DIAG_UNAVAILABLE = -3,
    DIAG_BUSY = -4
};

enum diag_i2c_phase {
    DIAG_I2C_NONE = 0,
    DIAG_I2C_BUS_FREE,
    DIAG_I2C_START_WRITE,
    DIAG_I2C_ADDRESS_WRITE,
    DIAG_I2C_POINTER_READY,
    DIAG_I2C_POINTER_SENT,
    DIAG_I2C_START_READ,
    DIAG_I2C_ADDRESS_READ,
    DIAG_I2C_RECEIVE
};

typedef struct {
    uint32_t attempts;
    uint32_t successes;
    int32_t result; /* Latest attempt; consult touch_attempted_mask first. */
    uint32_t last_failure_phase;
    int32_t last_failure_result;
    /* SR1 is the failing wait's observation (or sampled after BUSY timeout);
     * SR2 is sampled on the failure path, before STOP/SWRST. Not atomic. */
    uint32_t last_failure_sr1;
    uint32_t last_failure_sr2;
    uint32_t last_failure_ms;
} diag_i2c_bank_t;

enum diag_sensor_state {
    DIAG_SENSOR_SETTLE = 0,
    DIAG_SENSOR_FAMILY,
    DIAG_SENSOR_IDENTITY,
    DIAG_SENSOR_STATUS,
    DIAG_SENSOR_READY,
    DIAG_SENSOR_FAILED,
    DIAG_SENSOR_UNSUPPORTED
};

typedef struct {
    uint32_t state;
    uint32_t identity_valid; /* Family 0x9a observed; exact part is not assumed. */
    uint32_t family_id, device_id, device_rev;
    uint32_t first_ready_ms; /* First successful BUTTON_STAT, retained on re-probe. */
    uint32_t wake_nacks; /* Clean address AF recovered with STOP, never SWRST. */
    uint32_t retries; /* Delayed retry transactions scheduled after wake NACK. */
    uint32_t init_failures; /* Initial/re-probe readiness episodes which failed. */
    uint32_t last_failed_register;
    uint32_t peripheral_resets;
    uint32_t last_status_ms, status_reads; /* Successful BUTTON_STAT only. */
    int32_t readiness_result; /* BUSY while probing; reason if readiness failed. */
} diag_sensor_bank_t;

/* Stable fixed-width telemetry. Counts are observations, not pad calibration.
 * USB-ready means the recovered HSE->PLLQ configuration locked; it is not a
 * measurement of crystal accuracy, wiring, or successful enumeration. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t boot_stage;
    uint32_t heartbeat;
    uint32_t uptime_ms;
    uint32_t errors;
    uint32_t hclk_hz;
    uint32_t pclk1_hz;
    uint32_t pclk2_hz;
    uint32_t clock_source;
    uint32_t usb_clock_ready;
    uint32_t device_id;
    uint32_t flash_kib;
    uint32_t uid[3];
    uint32_t reset_flags;
    uint32_t nor_jedec;
    uint32_t nor_status;
    uint32_t nor_ready;
    uint32_t touch_masks[4];
    uint32_t touch_ok_mask;
    uint32_t touch_errors[4];
    uint32_t raw_adc[6];
    uint32_t adc_ok_mask;
    uint32_t oled_frames;
    uint32_t usb_state;
    uint32_t core_selftest;
    uint32_t fault_exception;
    uint32_t fault_cfsr;
    uint32_t fault_hfsr;
    uint32_t fault_mmfar;
    uint32_t fault_bfar;
    uint32_t fault_sp;
    uint32_t fault_pc;
    uint32_t fault_lr;
    uint32_t fault_exc_return;
    /* Version 2 extension: original version 1 field offsets are retained. */
    uint32_t touch_attempted_mask;
    uint32_t touch_history_mask;
    diag_i2c_bank_t i2c[4];
    /* Version 3 extension: all version 1 and 2 offsets are retained. */
    diag_sensor_bank_t sensor[4];
    uint32_t touch_initialized_mask; /* Current READY with valid BUTTON_STAT. */
    uint32_t touch_failed_mask; /* FAILED or UNSUPPORTED; other banks continue. */
} diag_report_t;

extern volatile diag_report_t g_diag_mailbox;
extern volatile diag_entry_t g_diag_entry;
extern const char thy_diag_build_id[];

/* No flash program/erase/unlock, option-byte, Capsense configuration write,
 * mass-storage or MIDI transmission. Explicit volatile LED/pad tests use
 * the board peripheral contracts. */
void board_init(void);
void board_poll(void);
/* Publish a boot checkpoint immediately, including before USB initialization. */
void board_boot_stage(uint32_t stage);
uint32_t board_millis(void);
const volatile diag_report_t *board_report(void);
int board_nor_read(uint32_t address, uint8_t *output, size_t count);
int board_internal_read(uint32_t address, uint8_t *output, size_t count);
void board_fault_capture(uint32_t exception, uint32_t *stack, uint32_t exc_return);
void board_unexpected_irq(uint32_t exception);

#endif
