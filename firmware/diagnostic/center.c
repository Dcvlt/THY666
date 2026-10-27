/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent center-indicator driver from the recovered electrical contract:
 * PE5 data, PE4 clock, PE3 latch, 24 channels of 12-bit MSB-first brightness.
 * See analysis/local/diagnostic/third-test-20261022-01/centre-led-investigation.json.
 * No recovered firmware waveform bytes or brightness lookup table are copied.
 * CPU bit-banging owns only PE3..5; no timer, DMA or ADC peripheral is used.
 */
#include "board.h"
#include "center.h"

#ifndef THY_CENTER_READ32
#define THY_CENTER_READ32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#endif
#ifndef THY_CENTER_WRITE32
#define THY_CENTER_WRITE32(address, value) \
    (*(volatile uint32_t *)(uintptr_t)(address) = (uint32_t)(value))
#endif
#ifndef THY_CENTER_NOP
#define THY_CENTER_NOP() __asm volatile("nop")
#endif
#ifndef THY_CENTER_BARRIER
#define THY_CENTER_BARRIER() __asm volatile("dmb" ::: "memory")
#endif

#define CENTER_RCC_AHB1ENR UINT32_C(0x40023830)
#define CENTER_GPIOE UINT32_C(0x40021000)
#define CENTER_PIN_MASK UINT32_C(0x38)
#define CENTER_MODE_MASK UINT32_C(0x00000fc0)
#define CENTER_OUTPUT_MODE UINT32_C(0x00000540)
#define CENTER_ALL_LOW UINT32_C(0x00380000)

static uint32_t frame[DIAG_CENTER_FRAME_WORDS];
static uint16_t levels[DIAG_CENTER_CHANNELS];
static diag_center_report_t center_report;
static int pending;

uint16_t board_center_published_level(unsigned channel)
{
    if(channel<5u||channel>=DIAG_CENTER_CHANNELS||!center_report.frames)return 0u;
    uint16_t value=0;
    for(unsigned bit=0;bit<12u;++bit)
        value=(uint16_t)((value<<1u)|((frame[channel*36u+bit*3u]&UINT32_C(0x20))!=0u));
    return value;
}

int diag_center_logical_channel(unsigned group, unsigned index)
{
    static const uint8_t navigation[] = {15u,5u,6u,7u,8u};
    /* Slot1/2/3 handlers use record brightness offsets1f/1c/19,
     * not ascending LED-record order; their channels are20/19/18. */
    static const uint8_t functions[] = {16u,22u,10u,11u,21u,23u,9u,
                                        20u,19u,18u,17u,14u,13u,12u};
    if (group == 0u && index < sizeof navigation) return navigation[index];
    if (group == 1u && index < sizeof functions) return functions[index];
    return -1;
}

size_t diag_center_encode(uint32_t *words, size_t capacity,
                          const uint16_t input[DIAG_CENTER_CHANNELS])
{
    if (words == NULL || input == NULL || capacity < DIAG_CENTER_FRAME_WORDS) return 0u;
    for (unsigned channel = 0u; channel < DIAG_CENTER_CHANNELS; ++channel)
        if (input[channel] > DIAG_CENTER_MAX_BRIGHTNESS) return 0u;
    size_t cursor = 0u;
    for (unsigned channel = 0u; channel < DIAG_CENTER_CHANNELS; ++channel) {
        uint16_t value = channel < 5u ? 0u : input[channel];
        for (uint16_t mask = UINT16_C(0x800); mask != 0u; mask >>= 1u) {
            int high = (value & mask) != 0u;
            /* Prepare with clock/latch low, then raise clock with stable
             * data, then lower clock. Only PE3..5 can be touched. */
            words[cursor++] = high ? UINT32_C(0x00180020) : CENTER_ALL_LOW;
            words[cursor++] = high ? UINT32_C(0x30) : UINT32_C(0x00200010);
            words[cursor++] = high ? UINT32_C(0x00100020) : UINT32_C(0x00300000);
        }
    }
    /* Recovered polarity: clock low, latch high, hold, latch/clock low.
     * STM32 BSRR set wins over reset for the same bit in the first word. */
    words[cursor++] = UINT32_C(0x00180008);
    for (unsigned hold = 0u; hold < 22u; ++hold) words[cursor++] = UINT32_C(8);
    words[cursor++] = UINT32_C(0x00180000);
    return cursor;
}

void board_center_init(void)
{
    center_report = (diag_center_report_t){0};
    center_report.brightness = DIAG_CENTER_DEFAULT_BRIGHTNESS;
    pending = 0;
    for (unsigned channel = 0u; channel < DIAG_CENTER_CHANNELS; ++channel) levels[channel] = 0u;
}

const diag_center_report_t *board_center_report(void) { return &center_report; }

static void record_failure(int result, uint32_t now)
{
    pending = 0;
    center_report.state = DIAG_CENTER_FAULT;
    center_report.last_result = result;
    center_report.updated_ms = now;
    ++center_report.errors;
}

int board_center_request(unsigned mode, unsigned channel, unsigned brightness)
{
    if (mode > DIAG_CENTER_CHANNEL || channel >= DIAG_CENTER_CHANNELS ||
        brightness > DIAG_CENTER_MAX_BRIGHTNESS) return DIAG_INVALID;
    if (pending && mode != DIAG_CENTER_OFF) return DIAG_BUSY;
    if (g_diag_mailbox.hclk_hz != UINT32_C(168000000)) {
        record_failure(DIAG_UNAVAILABLE, board_millis());
        return DIAG_UNAVAILABLE;
    }
    center_report.mode = mode;
    center_report.channel = mode == DIAG_CENTER_CHANNEL ? channel : 0u;
    center_report.brightness = mode == DIAG_CENTER_OFF ? 0u : brightness;
    center_report.state = DIAG_CENTER_QUEUED;
    center_report.last_result = DIAG_BUSY;
    pending = 1;
    return DIAG_SUCCESS;
}

int board_center_request_levels(const uint16_t input[DIAG_CENTER_CHANNELS])
{
    if (input == NULL) return DIAG_INVALID;
    unsigned peak = 0u;
    for (unsigned channel = 0u; channel < DIAG_CENTER_CHANNELS; ++channel) {
        if (input[channel] > DIAG_CENTER_MAX_BRIGHTNESS ||
            (channel < 5u && input[channel] != 0u)) return DIAG_INVALID;
        if (input[channel] > peak) peak = input[channel];
    }
    if (pending) return DIAG_BUSY;
    if (g_diag_mailbox.hclk_hz != UINT32_C(168000000)) {
        record_failure(DIAG_UNAVAILABLE, board_millis());
        return DIAG_UNAVAILABLE;
    }
    for (unsigned channel = 0u; channel < DIAG_CENTER_CHANNELS; ++channel)
        levels[channel] = input[channel];
    center_report.mode = DIAG_CENTER_LEVELS;
    center_report.channel = 0u;
    center_report.brightness = peak;
    center_report.state = DIAG_CENTER_QUEUED;
    center_report.last_result = DIAG_BUSY;
    pending = 1;
    return DIAG_SUCCESS;
}

static void phase_delay(void)
{
    /* Fixed iteration count; no missing hardware event can hang this loop.
     * At the guarded clock this supplies conservative data/clock setup time.
     * Interrupts may extend a phase; no IRQ mask is changed. */
    for (unsigned count = 0u; count < DIAG_CENTER_PHASE_NOPS; ++count) THY_CENTER_NOP();
}

static void prepare_outputs(void)
{
    THY_CENTER_WRITE32(CENTER_RCC_AHB1ENR,
                       THY_CENTER_READ32(CENTER_RCC_AHB1ENR) | UINT32_C(0x10));
    (void)THY_CENTER_READ32(CENTER_RCC_AHB1ENR);
    /* Preload all three low before changing their output modes. PE5 is
     * explicitly push-pull, never left floating while clocks are emitted. */
    THY_CENTER_WRITE32(CENTER_GPIOE + 0x18u, CENTER_ALL_LOW);
    THY_CENTER_WRITE32(CENTER_GPIOE + 0x04u,
                       THY_CENTER_READ32(CENTER_GPIOE + 0x04u) & ~CENTER_PIN_MASK);
    THY_CENTER_WRITE32(CENTER_GPIOE + 0x0cu,
                       THY_CENTER_READ32(CENTER_GPIOE + 0x0cu) & ~CENTER_MODE_MASK);
    THY_CENTER_WRITE32(CENTER_GPIOE + 0x08u,
                       (THY_CENTER_READ32(CENTER_GPIOE + 0x08u) & ~CENTER_MODE_MASK) | CENTER_MODE_MASK);
    THY_CENTER_WRITE32(CENTER_GPIOE,
                       (THY_CENTER_READ32(CENTER_GPIOE) & ~CENTER_MODE_MASK) | CENTER_OUTPUT_MODE);
    THY_CENTER_BARRIER();
    phase_delay();
}

void board_center_task(uint32_t now)
{
    if (!pending) return;
    if (g_diag_mailbox.hclk_hz != UINT32_C(168000000)) {
        record_failure(DIAG_UNAVAILABLE, now);
        return;
    }
    uint32_t enabled = 0u;
    for (unsigned channel = 0u; channel < DIAG_CENTER_CHANNELS; ++channel) {
        if (center_report.mode != DIAG_CENTER_LEVELS) {
            int illuminate = channel >= 5u && center_report.mode != DIAG_CENTER_OFF &&
                (center_report.mode == DIAG_CENTER_ALL || center_report.channel == channel);
            levels[channel] = illuminate ? (uint16_t)center_report.brightness : 0u;
        }
        if (levels[channel] != 0u) enabled |= UINT32_C(1) << channel;
    }
    if (diag_center_encode(frame, DIAG_CENTER_FRAME_WORDS, levels) != DIAG_CENTER_FRAME_WORDS) {
        record_failure(DIAG_INVALID, now);
        return;
    }
    prepare_outputs();
    for (size_t i = 0u; i < DIAG_CENTER_FRAME_WORDS; ++i) {
        THY_CENTER_WRITE32(CENTER_GPIOE + 0x18u, frame[i]);
        phase_delay();
    }
    THY_CENTER_WRITE32(CENTER_GPIOE + 0x18u, CENTER_ALL_LOW);
    THY_CENTER_BARRIER();
    phase_delay();
    pending = 0;
    center_report.enabled_mask = enabled;
    ++center_report.frames;
    center_report.state = DIAG_CENTER_READY;
    center_report.last_result = DIAG_SUCCESS;
    center_report.updated_ms = now;
}
