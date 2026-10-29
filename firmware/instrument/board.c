/* SPDX-License-Identifier: GPL-3.0-or-later
 * Reuse the device-tested board bring-up. The instrument schedules its own
 * display chunks; the frozen diagnostic profile remains unchanged. */
#include <stdint.h>
static void instrument_oled_ready(void);
static void instrument_boot_stage(uint32_t stage);
#define board_poll thy_bringup_poll_unused
#define THY_INSTRUMENT_TOUCH 1
#define THY_BOARD_DEFER_OLED_ON 1
#define THY_BOARD_OLED_READY() instrument_oled_ready()
#define THY_BOARD_BOOT_STAGE(stage) instrument_boot_stage(stage)
#include "../diagnostic/board.c"
#undef board_poll
#undef THY_BOARD_BOOT_STAGE
#undef THY_BOARD_OLED_READY
#undef THY_BOARD_DEFER_OLED_ON
#include "app.h"
#include "display.h"
#include "led.h"
#include "../diagnostic/center.h"

static thy_resources resources;
static thy_resource_clock resource_clock;
extern uint32_t __ram_end__, __stack_limit__, __stack_top__;
extern uint32_t __ccm_noinit_end__, __flash_used_end__;
const thy_resources *board_resources(void) { return &resources; }
void board_resources_task(uint32_t now_ms)
{
    static uintptr_t cursor;
    if(!resources.valid){
        resources.flash_used=(uint32_t)(uintptr_t)&__flash_used_end__-0x08040000u;
        resources.flash_total=512u*1024u;
        resources.ram_used=(uint32_t)(uintptr_t)&__ram_end__-0x20000000u;
        resources.ram_total=128u*1024u;
        resources.ccm_used=(uint32_t)(uintptr_t)&__ccm_noinit_end__-0x10000000u;
        resources.ccm_total=64u*1024u;
        resources.stack_total=(uint32_t)((uintptr_t)&__stack_top__-(uintptr_t)&__stack_limit__);
        cursor=(uintptr_t)&__stack_limit__;resources.valid=1u;
    }
    thy_resources_tick(&resources,&resource_clock,MMIO32(0xe0001004u),now_ms,g_diag_mailbox.hclk_hz);
    /* At most 16 word reads per turn. Never writes live stack or disables IRQs.
     * Sweep continues after the first changed word to catch a deeper watermark.
     * A larger used value is retained for the whole boot. */
    for(unsigned n=0u;n<16u;++n){
        if(*(volatile uint32_t *)cursor!=0xa55aa55au){
            uint32_t used=(uint32_t)((uintptr_t)&__stack_top__-cursor);
            if(used>resources.stack_used)resources.stack_used=used;
        }
        cursor+=4u;
        if(cursor>=(uintptr_t)&__stack_top__){cursor=(uintptr_t)&__stack_limit__;resources.stack_valid=1u;break;}
    }
}

static thy_display display;

/* DWT was enabled by the tested board bring-up. These elapsed CPU timings
 * include interruptions; they do not measure panel scan or optical latency. */
static uint32_t display_elapsed_us(uint32_t started)
{
    uint32_t per_us = g_diag_mailbox.hclk_hz / UINT32_C(1000000);
    return per_us ? (MMIO32(UINT32_C(0xe0001004)) - started) / per_us : 0u;
}

static void display_render_finished(uint32_t started)
{
    display.report.last_render_us = display_elapsed_us(started);
    if (display.report.last_render_us > display.report.max_render_us)
        display.report.max_render_us = display.report.last_render_us;
}

void thy_instrument_memory_init(void)
{
    /* The updater may leave this clock gated. Enable it before the first
     * high_level write; preserving the bootloader's enable bits is insufficient. */
    MMIO32(RCC + 0x30u) |= UINT32_C(1) << 20; /* AHB1ENR.CCMDATARAMEN */
    (void)MMIO32(RCC + 0x30u);
    __asm volatile("dsb" ::: "memory");
}

const thy_display_report *board_display_report(void) { return &display.report; }

#include "thy_studio_ui.h"
static thy_studio_ui ui;
static uint8_t ui_initialized, panel_on;
static uint8_t scene_key_seen;
static uint32_t requested_scene_key;

int board_companion_visual(void *context,uint8_t oled[1024],uint8_t rgb[288],
    uint8_t center[19],uint32_t *frame_ms,uint8_t *panel_visible)
{
    (void)context;
    const thy_instrument *instrument=thy_instrument_app_state();
    if(!instrument||!ui_initialized||!oled||!rgb||!center||!frame_ms||!panel_visible)return 0;
    *panel_visible=(uint8_t)thy_display_panel_visible(&display);
    if(*panel_visible && display.report.valid_bytes!=1024u)return 0;
    for(unsigned n=0;n<1024u;++n)oled[n]=*panel_visible?display.shadow[n]:0u;
    *frame_ms=display.report.last_completed_ms;
    if(!thy_instrument_lights_copy_rgb(instrument,rgb))return 0;
    for(unsigned n=0;n<19u;++n){
        int channel=diag_center_logical_channel(n<5u?0u:1u,n<5u?n:n-5u);
        center[n]=channel<0?0u:(uint8_t)((uint32_t)board_center_published_level((unsigned)channel)*255u/4095u);
    }
    return 1;
}

static void instrument_oled_ready(void)
{
    thy_display_init(&display);
    /* Bring-up already left the panel off. The portable presenter owns all
     * subsequent visibility fences and reveals only confirmed scene bytes. */
    thy_display_enable_scene_control(&display, 1);
    thy_studio_ui_init(&ui);
    ui.resources=board_resources();
    thy_studio_ui_set_navigation_animations(&ui, 0);
    scene_key_seen = 0u;
    thy_studio_ui_set_build_identity(&ui, THY_DIAG_BUILD_ID);
    thy_studio_ui_boot_begin(&ui, board_millis());
    thy_studio_ui_boot_progress(&ui, THY_STUDIO_BOOT_DISPLAY);
    ui_initialized = 1u;
    /* The panel stays off until this first complete branded frame arrives.
     * Its 16-byte transfers use the normal foreground scheduler below. */
    uint32_t started = MMIO32(UINT32_C(0xe0001004));
    thy_studio_ui_render(&ui, NULL, NULL, 0, 0, board_millis(), framebuffer);
    (void)thy_display_submit(&display, framebuffer, board_millis());
    display_render_finished(started);
}

void board_display_boot_progress(uint8_t stage)
{
    if (ui_initialized) thy_studio_ui_boot_progress(&ui, stage);
}
void board_display_boot_ready(void)
{
    if (ui_initialized) thy_studio_ui_boot_ready(&ui);
}
void board_display_boot_skip(void)
{
    if (ui_initialized) thy_studio_ui_boot_skip(&ui);
}

static void instrument_boot_stage(uint32_t stage)
{
    /* Ordinary stage checkpoints remain available in CDC/mailbox telemetry;
     * only a fatal entry-state stop uses the diagnostic dashboard. */
    if (stage != UINT32_C(0xbad) && stage != UINT32_C(0xdead)) return;
    board_display_boot_skip();
    oled_status();
    if (!panel_on) {
        const uint8_t on = UINT8_C(0xaf);
        if (oled_bytes(0, &on, 1u) == DIAG_SUCCESS) panel_on = 1u;
    }
}

void board_display_task(void)
{
    if (!ui_initialized || display.report.faulted) return;
    uint32_t now = board_millis();
    const thy_state *state = thy_instrument_app_menu_state();
    uint32_t scene_key = thy_studio_ui_scene_key(state);
    int scene_changed = state != NULL && scene_key_seen &&
        scene_key != requested_scene_key;
    if (state != NULL && !scene_key_seen) {
        requested_scene_key = scene_key;
        scene_key_seen = 1u;
    }
    /* New navigation may replace an off-screen scene, while ordinary animated
     * Play renders wait for a pinned scene to finish. This prevents continuous
     * visualizers from keeping the panel dark indefinitely. */
    if (scene_changed || thy_display_due(&display, now)) {
        uint32_t started = MMIO32(UINT32_C(0xe0001004));
        const thy_instrument_transport *output = thy_instrument_app_transport();
        thy_studio_ui_render(&ui, state, thy_instrument_app_scale_name(),
            thy_instrument_app_ready(),
            output != NULL && thy_instrument_transport_usb_ready(output),
            now, framebuffer);
        if (scene_changed) {
            (void)thy_display_submit_scene(&display, framebuffer, now);
            requested_scene_key = scene_key;
        } else (void)thy_display_submit(&display, framebuffer, now);
        display_render_finished(started);
    }
    /* New renders replace only the latest target. Each submitted bus slice
     * owns its payload until commit; successful bytes alone update the panel
     * shadow. Explicit page/column addressing permits sparse, fair updates.
     * At most 19 SPI bytes per foreground turn (0.464 ms wire minimum), with
     * acquisition and MIDI service between slices. Preserve the proven SPI
     * divider and the acquisition/LED exclusion windows. */
    if (!display.report.active) return;
    if (board_pads_busy() || board_led_report()->state == DIAG_LED_SENDING) {
        thy_display_defer(&display);
        return;
    }
    const thy_display_transfer *slice = thy_display_prepare(&display, now);
    if (slice == NULL) return;
    uint32_t started = MMIO32(UINT32_C(0xe0001004));
    int sent = oled_bytes(0, slice->command, slice->command_count) == DIAG_SUCCESS &&
        (!slice->count || oled_bytes(1, slice->data, slice->count) == DIAG_SUCCESS);
    display.report.last_chunk_us = display_elapsed_us(started);
    if (display.report.last_chunk_us > display.report.max_chunk_us)
        display.report.max_chunk_us = display.report.last_chunk_us;
    if (!sent) {
        thy_display_commit(&display, 0, board_millis());
        thy_display_fault(&display);
        g_diag_mailbox.errors |= DIAG_ERROR_SPI;
        return;
    }
    uint32_t completed = display.report.frames;
    thy_display_commit(&display, 1, board_millis());
    panel_on = (uint8_t)thy_display_panel_visible(&display);
    if (display.report.frames != completed) ++g_diag_mailbox.oled_frames;
}

void board_poll(void)
{
    uint32_t now = board_millis();
    g_diag_mailbox.uptime_ms = now;
    board_led_task(now);
    board_center_task(now);
    board_pads_task(now);
    if (!board_pads_busy() && !sensor_service(now)) board_touch_config_task(now);
    if ((int32_t)(now - next_poll) >= 0) {
        next_poll = now + 20u;
        if (!board_pads_active()) adc_snapshot();
        ++g_diag_mailbox.heartbeat;
    }
}
