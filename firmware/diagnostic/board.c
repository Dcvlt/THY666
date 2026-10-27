/* SPDX-License-Identifier: GPL-3.0-or-later
 * Candidate STM32F405VGT6 drivers. Register layout: ST RM0090/CMSIS F405.
 * Board pins: docs/hardware.md.
 * The first diagnostic ran on the owner's device; subsequent changes still
 * require their own device validation. */
#include "board.h"
#include "led.h"
#include "pads.h"
#include "center.h"
#if defined(THY_INSTRUMENT_TOUCH) && THY_INSTRUMENT_TOUCH
#include "../instrument/touch_config.h"
#endif

#define MMIO32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define MMIO8(address) (*(volatile uint8_t *)(uintptr_t)(address))
#define RCC UINT32_C(0x40023800)
#define GPIOA UINT32_C(0x40020000)
#define GPIOB UINT32_C(0x40020400)
#define GPIOC UINT32_C(0x40020800)
#define GPIOD UINT32_C(0x40020c00)
#define SPI3 UINT32_C(0x40003c00)
#define I2C1 UINT32_C(0x40005400)
#define ADC1 UINT32_C(0x40012000)
#define ADC_COMMON UINT32_C(0x40012300)
#define LOOP_LIMIT UINT32_C(200000)
#define CLOCK_LOOP_LIMIT UINT32_C(4000000)

#ifndef THY_DIAG_BUILD_ID
#define THY_DIAG_BUILD_ID "THY666 diagnostic candidate v1; hardware unvalidated"
#endif
const char thy_diag_build_id[] = THY_DIAG_BUILD_ID;
volatile diag_report_t g_diag_mailbox;
volatile diag_entry_t g_diag_entry __attribute__((section(".noinit.entry"), used));
_Static_assert(offsetof(diag_entry_t, control) == 8u, "Startup snapshot register block");
_Static_assert(offsetof(diag_entry_t, cpacr) == 44u, "Startup snapshot MMIO block");
_Static_assert(offsetof(diag_entry_t, wwdg_cfr) == 108u && sizeof(diag_entry_t) == 112u,
               "Startup snapshot field layout must match assembly");
uint32_t SystemCoreClock = UINT32_C(16000000);

static volatile uint32_t ticks;
static uint8_t framebuffer[1024];
static uint32_t next_poll;
static uint32_t next_oled;
static unsigned touch_index;
/* Scheduling intervals are conservative engineering policy, not a claimed
 * power-on timing measurement or a substitute for identifying the real IC. */
#define SENSOR_SETTLE_MS 15u
#define SENSOR_RETRY_MS 2u
#define SENSOR_POLL_MS 80u
#define SENSOR_DEADLINE_MS 1000u
#define SENSOR_MAX_ATTEMPTS 5u
static uint32_t sensor_due[4], sensor_deadline[4];
static uint8_t sensor_attempts[4], sensor_retry_pending[4];
static uint8_t sensor_address_nack[4], sensor_ever_ready[4];
static int spi_ready;
#if defined(THY_INSTRUMENT_TOUCH) && THY_INSTRUMENT_TOUCH
static uint32_t instrument_center_poll_ms = THY_TOUCH_DEFAULT_CENTER_POLL_MS;
static thy_touch_capture instrument_touch_capture;
static uint8_t instrument_touch_capture_initialized;
#endif

static int wait_mask(uint32_t address, uint32_t mask, uint32_t expected, uint32_t limit)
{
    while (limit-- != 0u) {
        if ((MMIO32(address) & mask) == expected) return DIAG_SUCCESS;
    }
    return DIAG_TIMEOUT;
}

static void pin_write(uint32_t port, unsigned pin, int high)
{
    MMIO32(port + 0x18u) = UINT32_C(1) << (pin + (high ? 0u : 16u));
}

static void pin_config(uint32_t port, unsigned pin, unsigned mode,
                       unsigned af, unsigned pull, int open_drain)
{
    uint32_t shift = pin * 2u;
    uint32_t mask = UINT32_C(3) << shift;
    MMIO32(port) = (MMIO32(port) & ~mask) | ((uint32_t)mode << shift);
    MMIO32(port + 0x0cu) = (MMIO32(port + 0x0cu) & ~mask) | ((uint32_t)pull << shift);
    MMIO32(port + 0x08u) = (MMIO32(port + 0x08u) & ~mask) | (UINT32_C(2) << shift);
    if (open_drain) MMIO32(port + 0x04u) |= UINT32_C(1) << pin;
    else MMIO32(port + 0x04u) &= ~(UINT32_C(1) << pin);
    shift = (pin & 7u) * 4u;
    uint32_t reg = port + 0x20u + (pin >= 8u ? 4u : 0u);
    MMIO32(reg) = (MMIO32(reg) & ~(UINT32_C(15) << shift)) | ((uint32_t)af << shift);
}

static void delay_us(uint32_t microseconds)
{
    uint32_t start = MMIO32(UINT32_C(0xe0001004));
    uint32_t duration = (g_diag_mailbox.hclk_hz / UINT32_C(1000000)) * microseconds;
    /* DWT is enabled explicitly; a separate finite guard prevents a hung
     * debug counter from creating an unbounded initialization wait. */
    uint32_t guard = duration + 1u;
    while ((uint32_t)(MMIO32(UINT32_C(0xe0001004)) - start) < duration && guard-- != 0u) {
        __asm volatile("nop");
    }
}

static int hsi_clock(void)
{
    MMIO32(RCC) &= ~(UINT32_C(1) << 19); /* Disable inherited clock security NMI. */
    MMIO32(RCC + 0x0cu) = UINT32_C(0x00bf0000); /* Disable RCC IRQs, clear ready/CSS flags. */
    MMIO32(RCC) |= 1u;
    if (wait_mask(RCC, 2u, 2u, CLOCK_LOOP_LIMIT) != DIAG_SUCCESS) return DIAG_TIMEOUT;
    MMIO32(RCC + 0x08u) &= ~3u;
    if (wait_mask(RCC + 0x08u, 12u, 0u, CLOCK_LOOP_LIMIT) != DIAG_SUCCESS) return DIAG_TIMEOUT;
    MMIO32(RCC) &= ~((UINT32_C(1) << 24) | (UINT32_C(1) << 19) | (UINT32_C(1) << 16));
    if (wait_mask(RCC, UINT32_C(1) << 25, 0u, CLOCK_LOOP_LIMIT) != DIAG_SUCCESS) return DIAG_TIMEOUT;
    MMIO32(RCC) &= ~(UINT32_C(1) << 26); /* PLLI2S is unused by this diagnostic. */
    if (wait_mask(RCC, UINT32_C(1) << 27, 0u, CLOCK_LOOP_LIMIT) != DIAG_SUCCESS) return DIAG_TIMEOUT;
    MMIO32(RCC + 0x08u) = 0u;
    /* Invalidate inherited flash caches while disabled, at the safe HSI rate.
     * This does not unlock/program/erase flash or touch option bytes. */
    MMIO32(UINT32_C(0x40023c00)) = 0u;
    MMIO32(UINT32_C(0x40023c00)) = UINT32_C(0x1800);
    MMIO32(UINT32_C(0x40023c00)) = 0u;
    MMIO32(UINT32_C(0x40023c00)) = UINT32_C(0x700);
    __asm volatile("dsb\n\tisb" ::: "memory");
    g_diag_mailbox.hclk_hz = UINT32_C(16000000);
    g_diag_mailbox.pclk1_hz = UINT32_C(16000000);
    g_diag_mailbox.pclk2_hz = UINT32_C(16000000);
    g_diag_mailbox.clock_source = DIAG_CLOCK_HSI_16;
    g_diag_mailbox.usb_clock_ready = 0u;
    SystemCoreClock = UINT32_C(16000000);
    return DIAG_SUCCESS;
}

static void clock_init(void)
{
    if (hsi_clock() != DIAG_SUCCESS) {
        g_diag_mailbox.errors |= DIAG_ERROR_CLOCK_SWITCH;
        board_unexpected_irq(0u);
    }
    /* Recovered 8 MHz crystal model; HSE bypass is deliberately disabled. */
    MMIO32(RCC) &= ~(UINT32_C(1) << 18);
    MMIO32(RCC) |= UINT32_C(1) << 16;
    if (wait_mask(RCC, UINT32_C(1) << 17, UINT32_C(1) << 17, CLOCK_LOOP_LIMIT) != DIAG_SUCCESS) {
        g_diag_mailbox.errors |= DIAG_ERROR_HSE;
        (void)hsi_clock();
        return;
    }
    MMIO32(RCC + 0x40u) |= UINT32_C(1) << 28;
    (void)MMIO32(RCC + 0x40u);
    MMIO32(UINT32_C(0x40007000)) |= UINT32_C(1) << 14; /* F405 voltage scale1. */
    MMIO32(UINT32_C(0x40023c00)) = UINT32_C(0x705); /* 5 WS, prefetch/I/D cache. */
    MMIO32(RCC + 0x04u) = 8u | (336u << 6) | (1u << 22) | (7u << 24);
    MMIO32(RCC) |= UINT32_C(1) << 24;
    if (wait_mask(RCC, UINT32_C(1) << 25, UINT32_C(1) << 25, CLOCK_LOOP_LIMIT) != DIAG_SUCCESS) {
        g_diag_mailbox.errors |= DIAG_ERROR_PLL;
        (void)hsi_clock();
        return;
    }
    MMIO32(RCC + 0x08u) = UINT32_C(0x9402); /* AHB1, APB1/4, APB2/2, PLL. */
    if (wait_mask(RCC + 0x08u, 12u, 8u, CLOCK_LOOP_LIMIT) != DIAG_SUCCESS) {
        g_diag_mailbox.errors |= DIAG_ERROR_CLOCK_SWITCH;
        if (hsi_clock() != DIAG_SUCCESS) board_unexpected_irq(0u);
        return;
    }
    g_diag_mailbox.hclk_hz = UINT32_C(168000000);
    g_diag_mailbox.pclk1_hz = UINT32_C(42000000);
    g_diag_mailbox.pclk2_hz = UINT32_C(84000000);
    g_diag_mailbox.clock_source = DIAG_CLOCK_HSE_PLL_168;
    g_diag_mailbox.usb_clock_ready = 1u;
    SystemCoreClock = UINT32_C(168000000);
}

static void quiesce_inherited_peripherals(void)
{
    /* Mask EXTI before changing modes; startup already masked NVIC/SysTick.
     * Reset DMA and peripheral state instead of assuming a power-on handoff.
     * GPIO reset restores SWD defaults; PA13/14 are never repurposed. */
    MMIO32(UINT32_C(0x40013c00)) = 0u;
    MMIO32(UINT32_C(0x40013c04)) = 0u;
    MMIO32(RCC + 0x10u) = UINT32_C(0x2060001f);
    MMIO32(RCC + 0x14u) = UINT32_C(0xc0);
    MMIO32(RCC + 0x20u) = UINT32_C(0x26fec9ff);
    MMIO32(RCC + 0x24u) = UINT32_C(0x00075933);
    __asm volatile("dsb" ::: "memory");
    MMIO32(RCC + 0x10u) = 0u;
    MMIO32(RCC + 0x14u) = 0u;
    MMIO32(RCC + 0x20u) = 0u;
    MMIO32(RCC + 0x24u) = 0u;
    MMIO32(RCC + 0x30u) = (MMIO32(RCC + 0x30u) & UINT32_C(0x00130000)) | UINT32_C(0x1f);
    MMIO32(RCC + 0x34u) = 0u;
    MMIO32(RCC + 0x40u) = UINT32_C(0x10208000); /* PWR, I2C1, SPI3. */
    MMIO32(RCC + 0x44u) = UINT32_C(0x100);      /* ADC1. */
    (void)MMIO32(RCC + 0x44u);
}

static int spi_byte(uint8_t output, uint8_t *input)
{
    if (wait_mask(SPI3 + 0x08u, 2u, 2u, LOOP_LIMIT) != DIAG_SUCCESS) goto timeout;
    MMIO8(SPI3 + 0x0cu) = output;
    if (wait_mask(SPI3 + 0x08u, 1u, 1u, LOOP_LIMIT) != DIAG_SUCCESS) goto timeout;
    uint8_t value = MMIO8(SPI3 + 0x0cu);
    if (input != NULL) *input = value;
    return DIAG_SUCCESS;
timeout:
    g_diag_mailbox.errors |= DIAG_ERROR_SPI;
    return DIAG_TIMEOUT;
}

static int spi_end(void)
{
    int result = wait_mask(SPI3 + 0x08u, 128u, 0u, LOOP_LIMIT);
    pin_write(GPIOA, 15u, 1);
    pin_write(GPIOD, 14u, 1);
    if (result != DIAG_SUCCESS) g_diag_mailbox.errors |= DIAG_ERROR_SPI;
    return result;
}

static void spi_init(void)
{
    pin_write(GPIOA, 15u, 1);
    pin_write(GPIOD, 14u, 1);
    pin_write(GPIOD, 11u, 0);
    pin_write(GPIOD, 12u, 1);
    pin_config(GPIOA, 15u, 1u, 0u, 0u, 0);
    pin_config(GPIOD, 14u, 1u, 0u, 0u, 0);
    pin_config(GPIOD, 11u, 1u, 0u, 0u, 0);
    pin_config(GPIOD, 12u, 1u, 0u, 0u, 0);
    pin_config(GPIOB, 3u, 2u, 6u, 0u, 0);
    pin_config(GPIOB, 4u, 2u, 6u, 0u, 0);
    pin_config(GPIOB, 5u, 2u, 6u, 0u, 0);
    MMIO32(SPI3 + 0x04u) = 0u;
    MMIO32(SPI3) = UINT32_C(0x0374); /* mode0,8-bit,software NSS,master,/128,SPE. */
    spi_ready = 1;
}

static int nor_command_read(uint8_t command, uint8_t *output, size_t count)
{
    if (!spi_ready) return DIAG_UNAVAILABLE;
    pin_write(GPIOD, 14u, 1);
    pin_write(GPIOA, 15u, 0);
    int result = spi_byte(command, NULL);
    for (size_t i = 0; result == DIAG_SUCCESS && i < count; ++i) result = spi_byte(0xffu, &output[i]);
    int end = spi_end();
    return result == DIAG_SUCCESS ? end : result;
}

static void nor_probe(void)
{
    uint8_t id[3] = {0u, 0u, 0u};
    uint8_t status = 0u;
    int result = nor_command_read(0x9fu, id, sizeof id);
    g_diag_mailbox.nor_jedec = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
    /* W25Q128JV RevJ IDs: IN/IQ/JQ EF4018, IM/JM EF7018. Both describe the
     * same16MiB array and standard03/05/9F commands. Other IDs stay blocked. */
    g_diag_mailbox.nor_ready = result == DIAG_SUCCESS &&
        (g_diag_mailbox.nor_jedec == UINT32_C(0xef4018) ||
         g_diag_mailbox.nor_jedec == UINT32_C(0xef7018));
    if (!g_diag_mailbox.nor_ready) g_diag_mailbox.errors |= DIAG_ERROR_NOR_ID;
    if (nor_command_read(0x05u, &status, 1u) == DIAG_SUCCESS) g_diag_mailbox.nor_status = status;
}

int board_nor_read(uint32_t address, uint8_t *output, size_t count)
{
    if (output == NULL || count == 0u || count > DIAG_READ_MAX ||
        address >= DIAG_EXTERNAL_FLASH_BYTES || count > DIAG_EXTERNAL_FLASH_BYTES - address) return DIAG_INVALID;
    if (!g_diag_mailbox.nor_ready) return DIAG_UNAVAILABLE;
    uint8_t status = 0u;
    int result = nor_command_read(0x05u, &status, 1u);
    if (result != DIAG_SUCCESS) return result;
    g_diag_mailbox.nor_status = status;
    if (status & 1u) return DIAG_BUSY;
    pin_write(GPIOD, 14u, 1);
    pin_write(GPIOA, 15u, 0);
    result = spi_byte(0x03u, NULL);
    for (int shift = 16; result == DIAG_SUCCESS && shift >= 0; shift -= 8) result = spi_byte((uint8_t)(address >> shift), NULL);
    for (size_t i = 0; result == DIAG_SUCCESS && i < count; ++i) result = spi_byte(0xffu, &output[i]);
    int end = spi_end();
    return result == DIAG_SUCCESS ? end : result;
}

int board_internal_read(uint32_t address, uint8_t *output, size_t count)
{
    if (output == NULL || count == 0u || count > DIAG_READ_MAX ||
        address < DIAG_INTERNAL_FLASH_BASE || address - DIAG_INTERNAL_FLASH_BASE >= DIAG_INTERNAL_FLASH_BYTES ||
        count > DIAG_INTERNAL_FLASH_BYTES - (address - DIAG_INTERNAL_FLASH_BASE)) return DIAG_INVALID;
    if (g_diag_mailbox.flash_kib != 1024u) return DIAG_UNAVAILABLE;
    const volatile uint8_t *source = (const volatile uint8_t *)(uintptr_t)address;
    for (size_t i = 0; i < count; ++i) output[i] = source[i];
    return DIAG_SUCCESS;
}

static int oled_bytes(int data, const uint8_t *bytes, size_t count)
{
    pin_write(GPIOA, 15u, 1);
    pin_write(GPIOD, 11u, data);
    pin_write(GPIOD, 14u, 0);
    int result = DIAG_SUCCESS;
    for (size_t i = 0; result == DIAG_SUCCESS && i < count; ++i) result = spi_byte(bytes[i], NULL);
    int end = spi_end();
    return result == DIAG_SUCCESS ? end : result;
}

/* Original independently drawn 4x5 diagnostic glyphs. Four columns, each
 * carrying five low bits. No firmware font or third-party font is embedded. */
static void glyph(char ch, uint8_t out[4])
{
    uint32_t bits;
    switch (ch) {
    case '0': bits=0x1f11111fu; break; case '1': bits=0x001f1200u; break;
    case '2': bits=0x1715151du; break; case '3': bits=0x1f151511u; break;
    case '4': bits=0x1f040407u; break; case '5': bits=0x1d151517u; break;
    case '6': bits=0x1d15151fu; break; case '7': bits=0x03051901u; break;
    case '8': bits=0x1f15151fu; break; case '9': bits=0x1f151517u; break;
    case 'A': bits=0x1e05051eu; break; case 'B': bits=0x0a15151fu; break;
    case 'C': bits=0x1111110eu; break; case 'D': bits=0x0e11111fu; break;
    case 'E': bits=0x1115151fu; break; case 'F': bits=0x0105051fu; break;
    case 'G': bits=0x1d15110eu; break; case 'H': bits=0x1f04041fu; break;
    case 'I': bits=0x001f0000u; break; case 'J': bits=0x0f101008u; break;
    case 'K': bits=0x110a041fu; break; case 'L': bits=0x1010101fu; break;
    case 'M': bits=0x1f02021fu; break; case 'N': bits=0x1f08041fu; break;
    case 'O': bits=0x0e11110eu; break; case 'P': bits=0x0205051fu; break;
    case 'Q': bits=0x1e19110eu; break; case 'R': bits=0x120d051fu; break;
    case 'S': bits=0x09151512u; break; case 'T': bits=0x01011f01u; break;
    case 'U': bits=0x0f10100fu; break; case 'V': bits=0x030c1807u; break;
    case 'W': bits=0x1f08081fu; break; case 'X': bits=0x110a041bu; break;
    case 'Y': bits=0x03041803u; break; case 'Z': bits=0x13151911u; break;
    case '-': bits=0x04040404u; break; case ':': bits=0x000a0000u; break;
    case '/': bits=0x01020418u; break; default: bits=0u; break;
    }
    for (unsigned i = 0; i < 4u; ++i) out[i] = (uint8_t)(bits >> (i * 8u));
}

static void oled_text(unsigned page, unsigned column, const char *text)
{
    while (*text != '\0' && page < 8u && column + 5u <= 128u) {
        uint8_t columns[4];
        glyph(*text++, columns);
        for (unsigned i = 0; i < 4u; ++i) framebuffer[page * 128u + column + i] = columns[i];
        column += 5u;
    }
}

static void oled_hex(unsigned page, unsigned column, uint32_t value, unsigned digits)
{
    char text[9];
    static const char hex[] = "0123456789ABCDEF";
    if (digits > 8u) digits = 8u;
    for (unsigned i = 0; i < digits; ++i) text[i] = hex[(value >> ((digits - i - 1u) * 4u)) & 15u];
    text[digits] = '\0';
    oled_text(page, column, text);
}

static void oled_status(void)
{
    for (size_t i = 0; i < sizeof framebuffer; ++i) framebuffer[i] = 0u;
    oled_text(0u, 0u, "THY666 STAGE "); oled_hex(0u, 65u, g_diag_mailbox.boot_stage, 4u);
    oled_text(1u, 0u, g_diag_mailbox.usb_clock_ready ? "CLOCK HSE PLL 168" : "CLOCK HSI16 USB OFF");
    oled_text(2u, 0u, "NOR "); oled_hex(2u, 25u, g_diag_mailbox.nor_jedec, 6u);
    oled_text(3u, 0u, "TOUCH ");
    oled_hex(3u, 30u, g_diag_mailbox.touch_masks[0], 4u); oled_hex(3u, 55u, g_diag_mailbox.touch_masks[1], 4u);
    oled_hex(4u, 30u, g_diag_mailbox.touch_masks[2], 4u); oled_hex(4u, 55u, g_diag_mailbox.touch_masks[3], 4u);
    oled_text(3u, 82u, "I2C "); oled_hex(3u, 102u, g_diag_mailbox.touch_ok_mask, 1u);
    oled_text(4u, 82u, "RDY "); oled_hex(4u, 102u, g_diag_mailbox.touch_initialized_mask, 1u);
    oled_text(5u, 0u, "BUILD ");
    char build[9];
    for (unsigned i = 0; i < 8u; ++i) {
        char c = thy_diag_build_id[i];
        if (!c) { build[i] = 0; break; }
        build[i] = c >= 'a' && c <= 'f' ? (char)(c - 'a' + 'A') : c;
    }
    build[8] = 0;
    oled_text(5u, 30u, build);
    oled_text(6u, 0u, "HIST "); oled_hex(6u, 25u, g_diag_mailbox.errors, 8u);
    oled_text(7u, 0u, "BEAT "); oled_hex(7u, 25u, g_diag_mailbox.heartbeat, 8u);
    oled_text(7u, 70u, "USB "); oled_hex(7u, 90u, g_diag_mailbox.usb_state, 1u);
    for (unsigned page = 0u; page < 8u; ++page) {
        uint8_t address[] = {(uint8_t)(0xb0u | page), 0u, 0x10u};
        if (oled_bytes(0, address, sizeof address) != DIAG_SUCCESS ||
            oled_bytes(1, &framebuffer[page * 128u], 128u) != DIAG_SUCCESS) return;
    }
    ++g_diag_mailbox.oled_frames;
}

static void oled_init(void)
{
    /* Recovered controller command contract, not optical panel identification. */
    static const uint8_t commands[] = {
        0xae,0x20,0x02,0xb0,0xc0,0x00,0x10,0x40,0x81,0xbb,0xa0,0xa6,
        0xa8,0x3f,0xa4,0xd3,0x00,0xd5,0xf0,0xd9,0x22,0xda,0x12,0xdb,0x20,0x8d,0x14
#if !defined(THY_BOARD_DEFER_OLED_ON)
        ,0xaf
#endif
    };
    pin_write(GPIOD, 12u, 0);
    delay_us(10000u);
    pin_write(GPIOD, 12u, 1);
    delay_us(20000u);
    if (oled_bytes(0, commands, sizeof commands) == DIAG_SUCCESS) {
#if defined(THY_BOARD_OLED_READY)
        THY_BOARD_OLED_READY();
#else
        oled_status();
#endif
    }
}

static void i2c_config(void)
{
    MMIO32(I2C1) = 0u;
    uint32_t mhz = g_diag_mailbox.pclk1_hz / UINT32_C(1000000);
    MMIO32(I2C1 + 0x04u) = mhz;
    MMIO32(I2C1 + 0x08u) = UINT32_C(0x4000); /* Required OAR1 reserved bit14. */
    MMIO32(I2C1 + 0x1cu) = g_diag_mailbox.pclk1_hz / UINT32_C(200000); /* 100 kHz standard. */
    MMIO32(I2C1 + 0x20u) = mhz + 1u;
    MMIO32(I2C1) = UINT32_C(0x401); /* ACK+PE. */
}

static int i2c_wait(uint32_t mask, uint32_t *last_sr1)
{
    uint32_t loops = LOOP_LIMIT;
    while (loops-- != 0u) {
        uint32_t status = MMIO32(I2C1 + 0x14u);
        *last_sr1 = status;
        if (status & UINT32_C(0xf00)) return DIAG_UNAVAILABLE; /* BERR/ARLO/AF/OVR. */
        if ((status & mask) == mask) return DIAG_SUCCESS;
    }
    return DIAG_TIMEOUT;
}

static void i2c_clear_address(void)
{
    (void)MMIO32(I2C1 + 0x14u);
    (void)MMIO32(I2C1 + 0x18u);
}

static int i2c_read2(unsigned index, uint8_t reg, uint16_t *result)
{
    static const uint8_t addresses[] = {0x36u,0x35u,0x39u,0x38u};
    volatile diag_i2c_bank_t *bank = &g_diag_mailbox.i2c[index];
    uint32_t bit = UINT32_C(1) << index;
    uint32_t phase = DIAG_I2C_BUS_FREE, sr1 = 0u;
    sensor_address_nack[index] = 0u;
    ++bank->attempts;
    g_diag_mailbox.touch_attempted_mask |= bit;
    int status = wait_mask(I2C1 + 0x18u, 2u, 0u, LOOP_LIMIT);
    if (status != DIAG_SUCCESS) goto failed;
    MMIO32(I2C1) = UINT32_C(0x501); /* ACK, START, PE. */
    phase = DIAG_I2C_START_WRITE;
    status = i2c_wait(1u, &sr1);
    if (status != DIAG_SUCCESS) goto failed;
    MMIO32(I2C1 + 0x10u) = (uint32_t)addresses[index] << 1;
    phase = DIAG_I2C_ADDRESS_WRITE;
    status = i2c_wait(2u, &sr1);
    if (status != DIAG_SUCCESS) goto failed;
    i2c_clear_address();
    phase = DIAG_I2C_POINTER_READY;
    status = i2c_wait(128u, &sr1);
    if (status != DIAG_SUCCESS) goto failed;
    /* Register pointer only: identity, BUTTON_STAT and optional instrument
     * configuration readback never send a configuration value or command. */
    MMIO32(I2C1 + 0x10u) = reg;
    phase = DIAG_I2C_POINTER_SENT;
    status = i2c_wait(4u, &sr1);
    if (status != DIAG_SUCCESS) goto failed;
    MMIO32(I2C1) |= UINT32_C(0x100);
    phase = DIAG_I2C_START_READ;
    status = i2c_wait(1u, &sr1);
    if (status != DIAG_SUCCESS) goto failed;
    /* Two-byte reception sequence from RM0090: POS=1/ACK=0 before ADDR clear,
     * wait BTF, then STOP and consume both bytes. */
    MMIO32(I2C1) = (MMIO32(I2C1) | UINT32_C(0x800)) & ~UINT32_C(0x400);
    MMIO32(I2C1 + 0x10u) = ((uint32_t)addresses[index] << 1) | 1u;
    phase = DIAG_I2C_ADDRESS_READ;
    status = i2c_wait(2u, &sr1);
    if (status != DIAG_SUCCESS) goto failed;
    i2c_clear_address();
    phase = DIAG_I2C_RECEIVE;
    status = i2c_wait(4u, &sr1);
    if (status != DIAG_SUCCESS) goto failed;
    MMIO32(I2C1) |= UINT32_C(0x200);
    uint32_t low = MMIO32(I2C1 + 0x10u) & 255u;
    uint32_t high = MMIO32(I2C1 + 0x10u) & 255u;
    *result = (uint16_t)(low | (high << 8));
    MMIO32(I2C1) = UINT32_C(0x401);
    bank->result = DIAG_SUCCESS;
    ++bank->successes;
    if (reg == UINT8_C(0xaa)) {
        g_diag_mailbox.touch_masks[index] = *result;
        g_diag_mailbox.touch_ok_mask |= bit;
        ++g_diag_mailbox.sensor[index].status_reads;
        g_diag_mailbox.sensor[index].last_status_ms = board_millis();
    }
    return DIAG_SUCCESS;
failed:
    /* No SR2 sampling is added to the successful wait path: SR1 followed by
     * SR2 clears ADDR. On failure this transaction is already abandoned.
     * Preserve the SR1 observation which actually ended the wait, rather
     * than rereading it after recovery has cleared the peripheral state. */
    if (phase == DIAG_I2C_BUS_FREE) sr1 = MMIO32(I2C1 + 0x14u);
    bank->last_failure_sr1 = sr1;
    bank->last_failure_sr2 = MMIO32(I2C1 + 0x18u);
    bank->last_failure_phase = phase;
    bank->last_failure_result = status;
    bank->last_failure_ms = board_millis();
    bank->result = status;
    ++g_diag_mailbox.touch_errors[index];
    g_diag_mailbox.touch_history_mask |= bit;
    g_diag_mailbox.touch_ok_mask &= ~bit;
    g_diag_mailbox.sensor[index].last_failed_register = reg;
    MMIO32(I2C1) |= UINT32_C(0x200);
    /* An address NACK may be the documented wake handshake. Preserve its
     * evidence, issue STOP and clear AF without resetting a healthy bus.
     * Never classify BERR/ARLO/OVR, data NACK, or failed cleanup as wake-up. */
    if (status == DIAG_UNAVAILABLE && (sr1 & UINT32_C(0xf00)) == UINT32_C(0x400) &&
        (phase == DIAG_I2C_ADDRESS_WRITE || phase == DIAG_I2C_ADDRESS_READ)) {
        MMIO32(I2C1 + 0x14u) = ~UINT32_C(0x400); /* RM0090: write zero clears AF. */
        if (wait_mask(I2C1, UINT32_C(0x200), 0u, LOOP_LIMIT) == DIAG_SUCCESS &&
            wait_mask(I2C1 + 0x18u, 2u, 0u, LOOP_LIMIT) == DIAG_SUCCESS) {
            MMIO32(I2C1) = UINT32_C(0x401); /* Restore ACK/POS only after STOP. */
            sensor_address_nack[index] = 1u;
            ++g_diag_mailbox.sensor[index].wake_nacks;
            return status;
        }
    }
    ++g_diag_mailbox.sensor[index].peripheral_resets;
    MMIO32(I2C1) = UINT32_C(0x8000); /* SWRST; bounded failure, no pin clock pulses. */
    MMIO32(I2C1) = 0u;
    i2c_config();
    g_diag_mailbox.errors |= DIAG_ERROR_I2C;
    return status;
}

/* A single bounded transaction per call keeps retries out of the USB task.
 * Due times and deadlines use signed modular differences (intervals < 2^31). */
static int sensor_due_now(uint32_t now, uint32_t due)
{
    return (int32_t)(now - due) >= 0;
}

static void sensor_begin(unsigned index, uint32_t now, int initial)
{
    volatile diag_sensor_bank_t *sensor = &g_diag_mailbox.sensor[index];
    uint32_t bit = UINT32_C(1) << index;
    sensor->state = initial ? DIAG_SENSOR_SETTLE : DIAG_SENSOR_FAMILY;
    sensor->identity_valid = 0u;
    sensor->readiness_result = DIAG_BUSY;
    sensor_attempts[index] = sensor_retry_pending[index] = 0u;
    sensor_due[index] = now + (initial ? SENSOR_SETTLE_MS : 0u);
    sensor_deadline[index] = now + SENSOR_DEADLINE_MS;
    g_diag_mailbox.touch_ok_mask &= ~bit;
    g_diag_mailbox.touch_initialized_mask &= ~bit;
    g_diag_mailbox.touch_failed_mask &= ~bit;
}

static void sensor_init(uint32_t now)
{
    touch_index = 0u;
    for (unsigned index = 0u; index < 4u; ++index) sensor_begin(index, now, 1);
}

static void sensor_failed(unsigned index, uint32_t now, uint32_t state, int result)
{
    volatile diag_sensor_bank_t *sensor = &g_diag_mailbox.sensor[index];
    uint32_t bit = UINT32_C(1) << index;
    sensor->state = state;
    sensor->readiness_result = result;
    ++sensor->init_failures;
    sensor_due[index] = now + SENSOR_DEADLINE_MS;
    sensor_attempts[index] = sensor_retry_pending[index] = 0u;
    g_diag_mailbox.touch_ok_mask &= ~bit;
    g_diag_mailbox.touch_initialized_mask &= ~bit;
    g_diag_mailbox.touch_failed_mask |= bit;
    g_diag_mailbox.errors |= DIAG_ERROR_I2C;
}

static int sensor_service(uint32_t now)
{
    for (unsigned scanned = 0u; scanned < 4u; ++scanned) {
        unsigned index = touch_index;
        touch_index = (touch_index + 1u) & 3u;
        volatile diag_sensor_bank_t *sensor = &g_diag_mailbox.sensor[index];
        if (!sensor_due_now(now, sensor_due[index])) continue;
        if (sensor->state == DIAG_SENSOR_FAILED || sensor->state == DIAG_SENSOR_UNSUPPORTED)
            sensor_begin(index, now, 0);
        if (sensor->state == DIAG_SENSOR_SETTLE) sensor->state = DIAG_SENSOR_FAMILY;
        if (sensor->state != DIAG_SENSOR_READY &&
            sensor_due_now(now, sensor_deadline[index])) {
            sensor_failed(index, now, DIAG_SENSOR_FAILED, DIAG_TIMEOUT);
            continue;
        }
        uint8_t reg = sensor->state == DIAG_SENSOR_FAMILY ? UINT8_C(0x8f) :
                      sensor->state == DIAG_SENSOR_IDENTITY ? UINT8_C(0x91) : UINT8_C(0xaa);
        if (sensor_retry_pending[index]) {
            ++sensor->retries;
            sensor_retry_pending[index] = 0u;
        }
        uint16_t value = 0u;
        int status = i2c_read2(index, reg, &value);
        if (status != DIAG_SUCCESS) {
            uint32_t bit = UINT32_C(1) << index;
            g_diag_mailbox.touch_initialized_mask &= ~bit;
            sensor->readiness_result = DIAG_BUSY;
            if (sensor->state == DIAG_SENSOR_READY) {
                sensor->state = DIAG_SENSOR_STATUS;
                sensor_deadline[index] = now + SENSOR_DEADLINE_MS;
            }
            if (sensor_address_nack[index] && ++sensor_attempts[index] < SENSOR_MAX_ATTEMPTS &&
                !sensor_due_now(now, sensor_deadline[index])) {
                sensor_retry_pending[index] = 1u;
                sensor_due[index] = now + SENSOR_RETRY_MS;
            } else sensor_failed(index, now, DIAG_SENSOR_FAILED, status);
            return 1;
        }
        sensor_attempts[index] = 0u;
        if (sensor->state == DIAG_SENSOR_FAMILY) {
            sensor->family_id = value & 255u;
            sensor->device_id = value >> 8;
            sensor->device_rev = 0u;
            if (sensor->family_id != UINT32_C(0x9a)) {
                sensor_failed(index, now, DIAG_SENSOR_UNSUPPORTED, DIAG_INVALID);
                return 1;
            }
            sensor->state = DIAG_SENSOR_IDENTITY;
            sensor_due[index] = now;
        } else if (sensor->state == DIAG_SENSOR_IDENTITY) {
            sensor->device_id = (sensor->device_id & 255u) | ((value & 255u) << 8);
            sensor->device_rev = value >> 8;
            sensor->identity_valid = 1u;
            sensor->state = DIAG_SENSOR_STATUS;
            sensor_due[index] = now;
        } else {
            sensor->state = DIAG_SENSOR_READY;
            sensor->readiness_result = DIAG_SUCCESS;
            if (!sensor_ever_ready[index]) {
                sensor->first_ready_ms = now;
                sensor_ever_ready[index] = 1u;
            }
            g_diag_mailbox.touch_initialized_mask |= UINT32_C(1) << index;
            g_diag_mailbox.touch_failed_mask &= ~(UINT32_C(1) << index);
#if defined(THY_INSTRUMENT_TOUCH) && THY_INSTRUMENT_TOUCH
            sensor_due[index] = now + thy_touch_poll_period(index, instrument_center_poll_ms);
#else
            sensor_due[index] = now + SENSOR_POLL_MS;
#endif
        }
        return 1;
    }
    return 0;
}

#if defined(THY_INSTRUMENT_TOUCH) && THY_INSTRUMENT_TOUCH
int board_touch_response_set(uint32_t milliseconds)
{
    if (thy_touch_poll_period(0u, milliseconds) == 0u) return DIAG_INVALID;
    if (instrument_center_poll_ms == milliseconds) return DIAG_SUCCESS;
    instrument_center_poll_ms = milliseconds;
    /* A speed change must not remain behind the previous 80 ms deadline. */
    uint32_t now = board_millis();
    for (unsigned i = 0; i < 2u; ++i)
        if (g_diag_mailbox.sensor[i].state == DIAG_SENSOR_READY) sensor_due[i] = now;
    return DIAG_SUCCESS;
}

uint32_t board_touch_response_ms(void)
{
    return instrument_center_poll_ms;
}

static void instrument_touch_capture_init(void)
{
    if (!instrument_touch_capture_initialized) {
        uint32_t uid[3];
        for (unsigned i = 0; i < 3u; ++i) uid[i] = g_diag_mailbox.uid[i];
        thy_touch_capture_init(&instrument_touch_capture, uid);
        instrument_touch_capture_initialized = 1u;
    }
}

int board_touch_config_begin(uint32_t mask)
{
    instrument_touch_capture_init();
    return thy_touch_capture_begin(&instrument_touch_capture, mask, board_millis());
}

const thy_touch_capture *board_touch_config_report(void)
{
    instrument_touch_capture_init();
    return &instrument_touch_capture;
}

static int instrument_touch_read2(void *context, unsigned index, uint8_t reg, uint16_t *word)
{
    (void)context;
    /* Strict allow-list: full 128-byte configuration and four identity bytes.
     * No arbitrary register command path is exposed through readback. */
    if (index >= THY_TOUCH_BANKS || word == NULL ||
        !((reg <= 0x7eu && (reg & 1u) == 0u) || reg == 0x8fu || reg == 0x91u))
        return DIAG_INVALID;
    return i2c_read2(index, reg, word);
}

void board_touch_config_task(uint32_t now)
{
    if (!instrument_touch_capture_initialized) return;
    uint32_t ready = 0u;
    for (unsigned i = 0; i < THY_TOUCH_BANKS; ++i)
        if (g_diag_mailbox.sensor[i].state == DIAG_SENSOR_READY &&
            g_diag_mailbox.sensor[i].identity_valid &&
            g_diag_mailbox.sensor[i].family_id == 0x9au &&
            g_diag_mailbox.sensor[i].device_id == 0x0a05u &&
            g_diag_mailbox.sensor[i].device_rev == 1u) ready |= UINT32_C(1) << i;
    (void)thy_touch_capture_step(&instrument_touch_capture, now, ready, instrument_touch_read2, NULL);
}
#endif

static void adc_init(void)
{
    pin_config(GPIOC, 0u, 3u, 0u, 0u, 0);
    for (unsigned pin = 3u; pin <= 6u; ++pin) pin_config(GPIOA, pin, 3u, 0u, 0u, 0);
    pin_config(GPIOB, 0u, 3u, 0u, 0u, 0);
    /* /4 =>21MHz at PCLK2=84MHz, within DS8626 limit at VDDA>=2.4V.
     * Still no claim of calibration or valid pad sensitivity. */
    MMIO32(ADC_COMMON + 0x04u) = UINT32_C(1) << 16;
    MMIO32(ADC1 + 0x04u) = 0u;
    MMIO32(ADC1 + 0x08u) = 1u;
    MMIO32(ADC1 + 0x0cu) = UINT32_C(7); /* Channel10 sample480 cycles. */
    MMIO32(ADC1 + 0x10u) = (7u << 9) | (7u << 12) | (7u << 15) | (7u << 18) | (7u << 24);
    MMIO32(ADC1 + 0x2cu) = 0u;
    delay_us(10u);
}

static void adc_snapshot(void)
{
    static const uint8_t channels[] = {10u,3u,4u,5u,6u,8u};
    uint32_t valid = 0u;
    for (unsigned i = 0u; i < 6u; ++i) {
        MMIO32(ADC1 + 0x34u) = channels[i];
        MMIO32(ADC1) = 0u;
        MMIO32(ADC1 + 0x08u) |= UINT32_C(1) << 30;
        if (wait_mask(ADC1, 2u, 2u, LOOP_LIMIT) != DIAG_SUCCESS) {
            g_diag_mailbox.errors |= DIAG_ERROR_ADC;
            continue;
        }
        g_diag_mailbox.raw_adc[i] = MMIO32(ADC1 + 0x4cu) & UINT32_C(0xfff);
        valid |= UINT32_C(1) << i;
    }
    g_diag_mailbox.adc_ok_mask = valid;
}

void SysTick_Handler(void)
{
    ++ticks;
}

uint32_t board_millis(void) { return ticks; }
const volatile diag_report_t *board_report(void) { return &g_diag_mailbox; }
void board_boot_stage(uint32_t stage)
{
    g_diag_mailbox.boot_stage = stage;
    if (spi_ready) {
#if defined(THY_BOARD_BOOT_STAGE)
        THY_BOARD_BOOT_STAGE(stage);
#else
        oled_status();
#endif
    }
}

void board_init(void)
{
    g_diag_mailbox.magic = DIAG_MAILBOX_MAGIC;
    g_diag_mailbox.version = DIAG_PROTOCOL_VERSION;
    g_diag_mailbox.boot_stage = 1u;
    if (g_diag_entry.magic != DIAG_ENTRY_MAGIC || g_diag_entry.version != 1u ||
        (g_diag_entry.control & 1u) || g_diag_entry.ipsr || (g_diag_entry.fpccr & 1u))
        g_diag_mailbox.errors |= DIAG_ERROR_ENTRY_STATE;
    g_diag_mailbox.reset_flags = MMIO32(RCC + 0x74u);
    g_diag_mailbox.device_id = MMIO32(UINT32_C(0xe0042000));
    g_diag_mailbox.flash_kib = *(const volatile uint16_t *)(uintptr_t)UINT32_C(0x1fff7a22);
    for (unsigned i = 0u; i < 3u; ++i) g_diag_mailbox.uid[i] = MMIO32(UINT32_C(0x1fff7a10) + 4u * i);
    quiesce_inherited_peripherals();
    clock_init();
    g_diag_mailbox.boot_stage = 2u;
    MMIO32(UINT32_C(0xe000ed88)) |= UINT32_C(0x00f00000);
    __asm volatile("dsb\n\tisb" ::: "memory");
    MMIO32(UINT32_C(0xe000edfc)) |= UINT32_C(1) << 24;
    MMIO32(UINT32_C(0xe0001004)) = 0u;
    MMIO32(UINT32_C(0xe0001000)) |= 1u;
    /* Faults report distinct state instead of escalating every event. */
    MMIO32(UINT32_C(0xe000ed24)) = UINT32_C(0x70000);
    MMIO32(UINT32_C(0xe000ed14)) = UINT32_C(0x210); /* STKALIGN and DIV_0_TRP. */
    spi_init();
    nor_probe();
    pin_config(GPIOB, 8u, 2u, 4u, 1u, 1);
    pin_config(GPIOB, 9u, 2u, 4u, 1u, 1);
    i2c_config();
    adc_init();
    g_diag_mailbox.boot_stage = 3u;
    oled_init();
    MMIO32(UINT32_C(0xe000e014)) = g_diag_mailbox.hclk_hz / 1000u - 1u;
    MMIO32(UINT32_C(0xe000e018)) = 0u;
    /* SysTick lowest priority; USB IRQ is configured by the transport. */
    MMIO8(UINT32_C(0xe000ed23)) = UINT8_C(0xf0);
    MMIO32(UINT32_C(0xe000e010)) = 7u;
    sensor_init(board_millis());
    next_poll = 20u;
    next_oled = 500u;
    g_diag_mailbox.boot_stage = 4u;
}

void board_poll(void)
{
    uint32_t now = board_millis();
    g_diag_mailbox.uptime_ms = now;
    board_led_task(now);
    board_center_task(now);
    board_pads_task(now);
    if (!board_pads_busy()) sensor_service(now);
    if ((int32_t)(now - next_poll) >= 0) {
        next_poll = now + 20u;
        if (!board_pads_active()) adc_snapshot();
        ++g_diag_mailbox.heartbeat;
    }
    if ((int32_t)(now - next_oled) >= 0 &&
        board_led_report()->state != DIAG_LED_SENDING && !board_pads_busy()) {
        next_oled = now + 500u;
        oled_status();
    }
}

void board_fault_capture(uint32_t exception, uint32_t *stack, uint32_t exc_return)
{
    __asm volatile("cpsid i" ::: "memory");
    g_diag_mailbox.magic = DIAG_MAILBOX_MAGIC;
    g_diag_mailbox.version = DIAG_PROTOCOL_VERSION;
    g_diag_mailbox.boot_stage = UINT32_C(0xdead);
    g_diag_mailbox.fault_exception = exception;
    g_diag_mailbox.fault_cfsr = MMIO32(UINT32_C(0xe000ed28));
    g_diag_mailbox.fault_hfsr = MMIO32(UINT32_C(0xe000ed2c));
    g_diag_mailbox.fault_mmfar = MMIO32(UINT32_C(0xe000ed34));
    g_diag_mailbox.fault_bfar = MMIO32(UINT32_C(0xe000ed38));
    g_diag_mailbox.fault_exc_return = exc_return;
    uintptr_t sp = (uintptr_t)stack;
    g_diag_mailbox.fault_sp = (uint32_t)sp;
    /* Faulted/unaligned stack must not create another fault during logging. */
    /* ST PM0214 figure 12 places R0..xPSR at the same lowest addresses in
     * basic and extended FP frames. Reject incomplete stacking faults. */
    if (exc_return != 0u &&
        (g_diag_mailbox.fault_cfsr & ((1u << 4) | (1u << 12))) == 0u &&
        (sp & 3u) == 0u && sp >= UINT32_C(0x20000000) && sp <= UINT32_C(0x2001ffe0)) {
        g_diag_mailbox.fault_pc = stack[6];
        g_diag_mailbox.fault_lr = stack[5];
    }
    for (;;) __asm volatile("nop");
}

void board_unexpected_irq(uint32_t exception)
{
    board_fault_capture(exception, NULL, 0u);
}
