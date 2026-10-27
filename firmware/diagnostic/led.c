/* SPDX-License-Identifier: GPL-3.0-or-later
 * Opt-in stock-pad LED diagnostic, independently implemented from the
 * board contracts at 08092440 (encoder), 08094dd0 (SPI2/PC3), and
 * 08094d08 (DMA1 stream4/channel0). See docs/display.md.
 *
 * Each transfer is a
 * NORMAL-mode 2816-halfword frame for exactly 96 stock pad slots, bounded by
 * explicit low-reset intervals. The 48-LED
 * optional top-row modification is not addressed. RGB/GRB and physical pad
 * ordering remain observations to collect, not assumptions in this driver.
 * No GPIO, SPI or DMA register is touched until an explicit test request.
 */
#include "board.h"
#include "led.h"

#ifndef THY_LED_MMIO32
#define THY_LED_MMIO32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#endif
#ifndef THY_LED_BARRIER
#define THY_LED_BARRIER() __asm volatile("dsb" ::: "memory")
#endif
#ifndef THY_LED_WRITE32
#define THY_LED_WRITE32(address, value) (THY_LED_MMIO32(address) = (value))
#endif
#ifndef THY_LED_WRITE16
#define THY_LED_WRITE16(address, value) (*(volatile uint16_t *)(uintptr_t)(address) = (value))
#endif

#define LED_RCC UINT32_C(0x40023800)
#define LED_GPIOC UINT32_C(0x40020800)
#define LED_SPI2 UINT32_C(0x40003800)
#define LED_DMA1 UINT32_C(0x40026000)
#define LED_STREAM4 UINT32_C(0x40026070)
#define LED_DMA_FLAGS UINT32_C(0x3d)
#define LED_DMA_FAILURES UINT32_C(0x0d) /* FEIF4, DMEIF4, TEIF4. */
#define LED_DMA_COMPLETE UINT32_C(0x20)
#define LED_DISABLE_LIMIT UINT32_C(1000)
#define LED_PRIME_LIMIT UINT32_C(1000)

static uint16_t led_words[DIAG_LED_FRAME_WORDS] __attribute__((aligned(4)));
static diag_led_report_t led_report;
static uint32_t frame_started, next_walk;
static int pending_frame, dma_active, frame_off;
/* Desired selection is separate from the DMA-owned encoded words. */
static uint32_t selected_pixels[3];
static int bitmap_valid;
static uint8_t selected_colors[DIAG_LED_COUNT][3];
static int colors_valid;

static int valid_request(unsigned mode, unsigned pixel, unsigned component)
{
    if (mode == DIAG_LED_OFF) return 1;
    return mode <= DIAG_LED_WALK && component < 3u &&
        (mode == DIAG_LED_ALL || pixel < DIAG_LED_COUNT);
}

size_t diag_led_encode_bitmap(uint16_t *words, size_t capacity,
                              const uint32_t selected[3])
{
    if (words == NULL || selected == NULL || capacity < DIAG_LED_FRAME_WORDS)
        return 0u;
    size_t cursor = 0u;
    for (unsigned lead = 0u; lead < DIAG_LED_RESET_WORDS; ++lead) words[cursor++] = 0u;
    for (unsigned index = 0u; index < DIAG_LED_COUNT; ++index) {
        uint8_t value = (selected[index / 32u] & (UINT32_C(1) << (index & 31u))) ?
                        DIAG_LED_BRIGHTNESS : 0u;
        for (unsigned component = 0u; component < 3u; ++component)
            for (uint8_t bit = 0x80u; bit != 0u; bit >>= 1u)
                words[cursor++] = (value & bit) ? UINT16_C(0xffc0) : UINT16_C(0xf800);
    }
    for (unsigned tail = 0u; tail < DIAG_LED_RESET_WORDS; ++tail) words[cursor++] = 0u;
    return cursor;
}

size_t diag_led_encode_colors(uint16_t *words, size_t capacity,
                              const uint8_t components[DIAG_LED_COUNT][3])
{
    if (words == NULL || components == NULL || capacity < DIAG_LED_FRAME_WORDS)
        return 0u;
    size_t cursor = 0u;
    for (unsigned lead = 0u; lead < DIAG_LED_RESET_WORDS; ++lead) words[cursor++] = 0u;
    for (unsigned index = 0u; index < DIAG_LED_COUNT; ++index)
        for (unsigned component = 0u; component < 3u; ++component)
            for (uint8_t bit = 0x80u; bit != 0u; bit >>= 1u)
                words[cursor++] = (components[index][component] & bit) ?
                    UINT16_C(0xffc0) : UINT16_C(0xf800);
    for (unsigned tail = 0u; tail < DIAG_LED_RESET_WORDS; ++tail) words[cursor++] = 0u;
    return cursor;
}

size_t diag_led_encode(uint16_t *words, size_t capacity,
                       unsigned mode, unsigned pixel, unsigned component)
{
    if (words == NULL || capacity < DIAG_LED_FRAME_WORDS ||
        !valid_request(mode, pixel, component)) return 0u;
    size_t cursor = 0u;
    /* Suppress any startup/AF transition before the first LED's data. */
    for (unsigned lead = 0u; lead < DIAG_LED_RESET_WORDS; ++lead) words[cursor++] = 0u;
    for (unsigned index = 0u; index < DIAG_LED_COUNT; ++index) {
        for (unsigned channel = 0u; channel < 3u; ++channel) {
            uint8_t value = 0u;
            if (mode != DIAG_LED_OFF && channel == component &&
                (mode == DIAG_LED_ALL || index == pixel)) value = DIAG_LED_BRIGHTNESS;
            for (uint8_t bit = 0x80u; bit != 0u; bit >>= 1u)
                words[cursor++] = (value & bit) ? UINT16_C(0xffc0) : UINT16_C(0xf800);
        }
    }
    /* Same 390.1us actively driven low after the final LED's data. */
    for (unsigned tail = 0u; tail < DIAG_LED_RESET_WORDS; ++tail) words[cursor++] = 0u;
    return cursor;
}

void board_led_init(void)
{
    led_report = (diag_led_report_t){0};
    led_report.brightness = DIAG_LED_BRIGHTNESS;
    frame_started = next_walk = 0u;
    pending_frame = dma_active = frame_off = 0;
    selected_pixels[0] = selected_pixels[1] = selected_pixels[2] = 0u;
    bitmap_valid = 0;
    colors_valid = 0;
    for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
        for (unsigned c = 0u; c < 3u; ++c) selected_colors[i][c] = 0u;
}

const diag_led_report_t *board_led_report(void) { return &led_report; }

int board_led_request(unsigned mode, unsigned pixel, unsigned component)
{
    if (!valid_request(mode, pixel, component)) return DIAG_INVALID;
    if (mode != DIAG_LED_OFF && (dma_active || pending_frame)) return DIAG_BUSY;
    /* The recovered pulse lengths require this bus clock; do not generate
     * a different waveform on the HSI fallback or an unknown clock model. */
    if (g_diag_mailbox.pclk1_hz != UINT32_C(42000000)) return DIAG_UNAVAILABLE;
    bitmap_valid = 0;
    colors_valid = 0;
    uint32_t now = board_millis();
    led_report.mode = mode;
    led_report.pixel = mode == DIAG_LED_ALL || mode == DIAG_LED_OFF ? 0u : pixel;
    led_report.component = mode == DIAG_LED_OFF ? 0u : component;
    led_report.started_ms = now;
    led_report.expires_ms = mode == DIAG_LED_OFF ? 0u : now + DIAG_LED_TEST_MS;
    led_report.walk_steps = 0u;
    led_report.off_confirmed = 0u;
    led_report.last_result = DIAG_BUSY;
    pending_frame = 1;
    if (!dma_active) led_report.state = DIAG_LED_QUEUED;
    return DIAG_SUCCESS;
}

int board_led_request_bitmap(const uint32_t selected[3])
{
    if (selected == NULL) return DIAG_INVALID;
    if ((selected[0] | selected[1] | selected[2]) == 0u)
        return board_led_request(DIAG_LED_OFF, 0u, 0u);
    if (g_diag_mailbox.pclk1_hz != UINT32_C(42000000) ||
        led_report.state == DIAG_LED_FAULT) return DIAG_UNAVAILABLE;
    uint32_t now = board_millis();
    led_report.expires_ms = now + DIAG_LED_TEST_MS;
    if (bitmap_valid && led_report.mode == DIAG_LED_BITMAP &&
        selected_pixels[0] == selected[0] && selected_pixels[1] == selected[1] &&
        selected_pixels[2] == selected[2]) return DIAG_SUCCESS;
    for (unsigned i = 0u; i < 3u; ++i) selected_pixels[i] = selected[i];
    bitmap_valid = 1;
    colors_valid = 0;
    led_report.mode = DIAG_LED_BITMAP;
    led_report.pixel = led_report.component = 0u;
    led_report.started_ms = now;
    led_report.walk_steps = 0u;
    led_report.off_confirmed = 0u;
    led_report.last_result = DIAG_BUSY;
    pending_frame = 1;
    if (!dma_active) led_report.state = DIAG_LED_QUEUED;
    return DIAG_SUCCESS;
}

int board_led_request_colors(const uint8_t components[DIAG_LED_COUNT][3])
{
    int any = 0, changed = !colors_valid || led_report.mode != DIAG_LED_COLORS;
    if (components == NULL) return DIAG_INVALID;
    for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
        for (unsigned c = 0u; c < 3u; ++c) {
            if (components[i][c] > DIAG_LED_BRIGHTNESS) return DIAG_INVALID;
            any |= components[i][c] != 0u;
            changed |= components[i][c] != selected_colors[i][c];
        }
    if (!any) return board_led_request(DIAG_LED_OFF, 0u, 0u);
    if (g_diag_mailbox.pclk1_hz != UINT32_C(42000000) ||
        led_report.state == DIAG_LED_FAULT) return DIAG_UNAVAILABLE;
    uint32_t now = board_millis();
    led_report.expires_ms = now + DIAG_LED_TEST_MS;
    if (!changed) return DIAG_SUCCESS;
    for (unsigned i = 0u; i < DIAG_LED_COUNT; ++i)
        for (unsigned c = 0u; c < 3u; ++c) selected_colors[i][c] = components[i][c];
    colors_valid = 1;
    bitmap_valid = 0;
    led_report.mode = DIAG_LED_COLORS;
    led_report.pixel = led_report.component = 0u;
    led_report.started_ms = now;
    led_report.walk_steps = 0u;
    led_report.off_confirmed = 0u;
    led_report.last_result = DIAG_BUSY;
    pending_frame = 1;
    if (!dma_active) led_report.state = DIAG_LED_QUEUED;
    return DIAG_SUCCESS;
}

static int disable_stream(void)
{
    THY_LED_WRITE32(LED_STREAM4, THY_LED_MMIO32(LED_STREAM4) & ~UINT32_C(1));
    for (uint32_t budget = LED_DISABLE_LIMIT; budget != 0u; --budget)
        if ((THY_LED_MMIO32(LED_STREAM4) & 1u) == 0u) return DIAG_SUCCESS;
    return DIAG_TIMEOUT;
}

static void pin_low(void)
{
    /* Establish the ODR latch before selecting GPIO. At normal completion,
     * SPI's last shifted word is already zero, so both sides agree on low. */
    THY_LED_WRITE32(LED_GPIOC + 0x18u, UINT32_C(1) << 19);
    THY_LED_WRITE32(LED_GPIOC,
        (THY_LED_MMIO32(LED_GPIOC) & ~UINT32_C(0xc0)) | UINT32_C(0x40));
    (void)THY_LED_MMIO32(LED_GPIOC); /* Complete the GPIO write before SPI changes. */
    THY_LED_BARRIER();
}

static int spi_wait_idle(void)
{
    for (uint32_t budget = LED_PRIME_LIMIT; budget != 0u; --budget) {
        uint32_t status = THY_LED_MMIO32(LED_SPI2 + 0x08u);
        if ((status & UINT32_C(0x120)) != 0u) return DIAG_UNAVAILABLE; /* MODF/FRE. */
        if ((status & UINT32_C(0x82)) == 2u) return DIAG_SUCCESS;
    }
    return DIAG_TIMEOUT;
}

static int stop_hardware(void)
{
    THY_LED_WRITE32(LED_SPI2 + 0x04u, 0u);
    int result = disable_stream();
    /* Take ownership of the known-low line before disabling SPI. Disabling
     * SPE while AF is still exposed leaves its output transition undefined. */
    pin_low();
    THY_LED_WRITE32(LED_SPI2, THY_LED_MMIO32(LED_SPI2) & ~UINT32_C(0x40));
    /* Full-duplex RX is deliberately unused. DR then SR clears its OVR. */
    (void)THY_LED_MMIO32(LED_SPI2 + 0x0cu);
    (void)THY_LED_MMIO32(LED_SPI2 + 0x08u);
    THY_LED_WRITE32(LED_DMA1 + 0x0cu, LED_DMA_FLAGS);
    dma_active = 0;
    return result;
}

static void fail_frame(uint32_t now, int result)
{
    led_report.last_failure_dma_sr = THY_LED_MMIO32(LED_DMA1 + 0x04u);
    led_report.last_failure_spi_sr = THY_LED_MMIO32(LED_SPI2 + 0x08u);
    (void)stop_hardware();
    led_report.errors++;
    led_report.last_failure_ms = now;
    led_report.last_failure_result = led_report.last_result = result;
    led_report.off_confirmed = 0u;
    led_report.state = DIAG_LED_FAULT;
    /* Honor one explicit/expired OFF queued behind a failed lit frame.
     * A failed OFF is never retried indefinitely. */
    if (frame_off) pending_frame = 0;
}

static int start_frame(uint32_t now)
{
    THY_LED_WRITE32(LED_RCC + 0x30u,
        THY_LED_MMIO32(LED_RCC + 0x30u) | UINT32_C(0x00200004)); /* DMA1, GPIOC. */
    THY_LED_WRITE32(LED_RCC + 0x40u,
        THY_LED_MMIO32(LED_RCC + 0x40u) | UINT32_C(0x00004000)); /* SPI2. */
    (void)THY_LED_MMIO32(LED_RCC + 0x40u);
    pin_low();
    /* Do not rewrite the DMA buffer or address while an old stream is live. */
    THY_LED_WRITE32(LED_SPI2 + 0x04u, 0u);
    if (disable_stream() != DIAG_SUCCESS) return DIAG_TIMEOUT;
    frame_off = led_report.mode == DIAG_LED_OFF;
    size_t encoded = led_report.mode == DIAG_LED_COLORS ?
        diag_led_encode_colors(led_words, DIAG_LED_FRAME_WORDS, selected_colors) :
        led_report.mode == DIAG_LED_BITMAP ?
        diag_led_encode_bitmap(led_words, DIAG_LED_FRAME_WORDS, selected_pixels) :
        diag_led_encode(led_words, DIAG_LED_FRAME_WORDS, led_report.mode,
                        led_report.pixel, led_report.component);
    if (encoded != DIAG_LED_FRAME_WORDS)
        return DIAG_INVALID;
    pending_frame = 0;
    THY_LED_WRITE32(LED_DMA1 + 0x0cu, LED_DMA_FLAGS);
    THY_LED_WRITE32(LED_STREAM4, UINT32_C(0x2c40)); /* CH0, M2P, MINC, halfwords, NORMAL. */
    THY_LED_WRITE32(LED_STREAM4 + 0x04u, DIAG_LED_FRAME_WORDS);
    THY_LED_WRITE32(LED_STREAM4 + 0x08u, LED_SPI2 + 0x0cu);
    THY_LED_WRITE32(LED_STREAM4 + 0x0cu, (uint32_t)(uintptr_t)led_words);
    THY_LED_WRITE32(LED_STREAM4 + 0x10u, 0u);
    THY_LED_WRITE32(LED_STREAM4 + 0x14u, 0u); /* Direct mode; no DMA interrupts. */
    THY_LED_WRITE32(LED_SPI2, UINT32_C(0x0b0e)); /* Recovered CR1, SPE still clear. */
    (void)THY_LED_MMIO32(LED_SPI2 + 0x0cu);
    (void)THY_LED_MMIO32(LED_SPI2 + 0x08u);
    THY_LED_WRITE32(LED_GPIOC + 0x04u, THY_LED_MMIO32(LED_GPIOC + 0x04u) & ~UINT32_C(8));
    THY_LED_WRITE32(LED_GPIOC + 0x08u,
        (THY_LED_MMIO32(LED_GPIOC + 0x08u) & ~UINT32_C(0xc0)) | UINT32_C(0xc0));
    THY_LED_WRITE32(LED_GPIOC + 0x0cu, THY_LED_MMIO32(LED_GPIOC + 0x0cu) & ~UINT32_C(0xc0));
    THY_LED_WRITE32(LED_GPIOC + 0x20u,
        (THY_LED_MMIO32(LED_GPIOC + 0x20u) & ~UINT32_C(0xf000)) | UINT32_C(0x5000));
    /* Prime and clock a zero halfword while PC3 remains GPIO-low. This
     * replaces any inherited/stale transmit value without exposing the
     * SPE/first-load transition to the LED chain. No interrupts are masked. */
    THY_LED_WRITE16(LED_SPI2 + 0x0cu, 0u);
    THY_LED_WRITE32(LED_SPI2, UINT32_C(0x0b4e));
    int result = spi_wait_idle();
    if (result != DIAG_SUCCESS) return result;
    /* Also transmit zero with SPE already enabled. The known-low handoff
     * therefore does not depend solely on disabled-SPI preload semantics. */
    THY_LED_WRITE16(LED_SPI2 + 0x0cu, 0u);
    result = spi_wait_idle();
    if (result != DIAG_SUCCESS) return result;
    (void)THY_LED_MMIO32(LED_SPI2 + 0x0cu);
    (void)THY_LED_MMIO32(LED_SPI2 + 0x08u);
    THY_LED_BARRIER();
    THY_LED_WRITE32(LED_GPIOC,
        (THY_LED_MMIO32(LED_GPIOC) & ~UINT32_C(0xc0)) | UINT32_C(0x80));
    THY_LED_BARRIER();
    THY_LED_WRITE32(LED_STREAM4, THY_LED_MMIO32(LED_STREAM4) | 1u);
    THY_LED_WRITE32(LED_SPI2 + 0x04u, 2u);
    frame_started = now;
    dma_active = 1;
    led_report.state = DIAG_LED_SENDING;
    led_report.last_result = DIAG_BUSY;
    return DIAG_SUCCESS;
}

void board_led_task(uint32_t now)
{
    /* Signed deadline comparisons also work across the millisecond wrap. */
    if (led_report.mode != DIAG_LED_OFF &&
        (int32_t)(now - led_report.expires_ms) >= 0) {
        led_report.mode = DIAG_LED_OFF;
        led_report.expires_ms = 0u;
        pending_frame = 1;
    }
    if (dma_active) {
        uint32_t flags = THY_LED_MMIO32(LED_DMA1 + 0x04u);
        if ((flags & LED_DMA_FAILURES) != 0u) {
            fail_frame(now, DIAG_UNAVAILABLE);
            return;
        }
        int completed = (flags & LED_DMA_COMPLETE) != 0u &&
            (THY_LED_MMIO32(LED_SPI2 + 0x08u) & UINT32_C(0x82)) == 2u;
        if (!completed) {
            if ((uint32_t)(now - frame_started) >= DIAG_LED_FRAME_TIMEOUT_MS)
                fail_frame(now, DIAG_TIMEOUT);
            return;
        }
        /* TC means the final word reached SPI, not that its last bit left.
         * The TXE/!BSY check above precedes disabling SPI or changing PC3.
         * A completed frame may be observed after unrelated foreground work:
         * elapsed service time alone cannot establish a peripheral timeout. */
        if (stop_hardware() != DIAG_SUCCESS) {
            fail_frame(now, DIAG_TIMEOUT);
            return;
        }
        led_report.frames++;
        led_report.last_result = DIAG_SUCCESS;
        led_report.off_confirmed = frame_off ? 1u : 0u;
        led_report.state = frame_off ? DIAG_LED_IDLE : DIAG_LED_HOLDING;
        if (!frame_off && led_report.mode == DIAG_LED_WALK) {
            led_report.walk_steps++;
            next_walk = now + 100u;
        }
    }
    if (!pending_frame && led_report.mode == DIAG_LED_WALK &&
        led_report.state == DIAG_LED_HOLDING && led_report.pixel + 1u < DIAG_LED_COUNT &&
        (int32_t)(now - next_walk) >= 0) {
        led_report.pixel++;
        pending_frame = 1;
    }
    if (pending_frame) {
        int result = start_frame(now);
        if (result != DIAG_SUCCESS) {
            pending_frame = 0;
            fail_frame(now, result);
        }
    }
}
