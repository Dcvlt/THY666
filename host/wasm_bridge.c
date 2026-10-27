/* Thin host ABI around the portable engine. No musical behavior lives here. */
#include "thy_core.h"
#include "thy_harmony.h"
#include "thy_midi.h"
#include "thy_instrument.h"
#include "thy_lighting.h"
#include "thy_ui.h"
#include "thy_engine.h"
#include "thy_studio_ui.h"
#include "thy_settings.h"
#include "thy_display.h"
#include "thy_sequencer.h"
#include "thy_sequencer_ui.h"
#include "thy_calibration.h"
#include "thy_companion.h"

void bridge_firmware_init(void);

static thy_core engine;
static thy_instrument firmware;
static thy_instrument_pad firmware_layout[THY_PAD_COUNT], staged_layout[THY_PAD_COUNT];
static thy_lighting firmware_lights;
static thy_ui firmware_ui;
static thy_engine shared_engine;
static thy_calibration calibration_workspace;
static thy_mpe expression_workspace;
static thy_channel_expression channel_expression_workspace;
static thy_studio_ui shared_ui;
static char state_output[256];
static uint32_t firmware_colors[THY_PAD_COUNT];
static uint8_t firmware_frame[1024];
/* Host-only companion staging. The MCU borrows its existing journal buffer. */
static uint8_t companion_frame[THY_COMPANION_SNAPSHOT_BYTES];
static uint32_t firmware_frame_ms;
static uint8_t firmware_frame_valid;
static thy_display preview_display;
static uint8_t display_input[THY_DISPLAY_FRAME_BYTES];
static const thy_display_transfer *display_transfer;
static int16_t note_input[THY_MAX_NOTES];
static int16_t note_preview[THY_MAX_NOTES];
static size_t preview_count;
static uint8_t byte_input[256];
static uint8_t library_file[THY_LIBRARY_FILE_BYTES];
static uint8_t settings_input[THY_SETTINGS_PAYLOAD_MAX];
static uint16_t center_levels[19];
static thy_midi_event output_event;
static uint8_t usb_bytes[4];
static uint8_t din_bytes[3];
static uint16_t voice_keys[THY_KEY_COUNT];
static thy_usb_parser input_parser;
static thy_midi_event received[256];
static uint16_t received_head;
static uint16_t received_count;
static uint32_t received_overflow;
static uint8_t sequencer_file[THY_SEQ_SONG_MAX];
static thy_seq_event sequencer_input[64];
static uint16_t sequencer_colors[96 * 4];

static int shared_control_filter(void *context, uint8_t bank, uint8_t index, int pressed) {
    return thy_engine_control(context, bank, index, pressed);
}

_Static_assert(sizeof(thy_midi_event) == 4, "Host ABI requires four event bytes");

void bridge_init(void) {
    thy_core_init(&engine);
    bridge_firmware_init();
    thy_usb_parser_init(&input_parser);
    received_head = received_count = 0;
    received_overflow = 0;
}
uintptr_t bridge_notes(void) { return (uintptr_t)note_input; }
uintptr_t bridge_preview_notes(void) { return (uintptr_t)note_preview; }
uintptr_t bridge_input(void) { return (uintptr_t)byte_input; }
uintptr_t bridge_event(void) { return (uintptr_t)&output_event; }
uintptr_t bridge_usb_bytes(void) { return (uintptr_t)usb_bytes; }
uintptr_t bridge_din_bytes(void) { return (uintptr_t)din_bytes; }
uintptr_t bridge_voices(void) { return (uintptr_t)voice_keys; }

/* The web instrument uses the target's dirty-region scheduler. Browser code
 * supplies only elapsed bus time and the visible panel pixels. */
void bridge_display_init(void) {
    thy_display_init(&preview_display);
    display_transfer = 0;
}
void bridge_display_enable_scene_control(int enabled) {
    thy_display_enable_scene_control(&preview_display, enabled);
}
uintptr_t bridge_display_input(void) { return (uintptr_t)display_input; }
int bridge_display_submit(uint32_t now) {
    return thy_display_submit(&preview_display, display_input, now);
}
int bridge_display_submit_scene(uint32_t now) {
    return thy_display_submit_scene(&preview_display, display_input, now);
}
int bridge_display_prepare(uint32_t now) {
    display_transfer = thy_display_prepare(&preview_display, now);
    return display_transfer != 0;
}
uint32_t bridge_display_transfer_value(int field) {
    if (display_transfer == 0) return 0;
    if (field == 0) return display_transfer->offset;
    if (field == 1) return display_transfer->count;
    if (field == 2) return display_transfer->generation;
    if (field == 3) return display_transfer->submitted_ms;
    if (field >= 4 && field < 7) return display_transfer->command[field - 4];
    if (field >= 7 && field < 23) return display_transfer->data[field - 7];
    if (field == 23) return display_transfer->kind;
    if (field == 24) return display_transfer->command_count;
    return 0;
}
void bridge_display_commit(int success, uint32_t now) {
    thy_display_commit(&preview_display, success, now);
    if (success) display_transfer = 0;
}
uint32_t bridge_display_report(int field) {
    const thy_display_report *report = &preview_display.report;
    switch (field) {
    case 0: return report->frames; case 1: return report->chunks;
    case 2: return report->command_bytes; case 3: return report->data_bytes;
    case 4: return report->submitted; case 5: return report->coalesced;
    case 6: return report->unchanged; case 7: return report->generation;
    case 8: return report->dirty_bytes; case 9: return report->valid_bytes;
    case 10: return report->initial_complete; case 11: return report->active;
    case 12: return report->last_generation_age_ms;
    case 13: return report->max_generation_age_ms;
    case 14: return report->failed_chunks;
    case 15: return report->scene_requests; case 16: return report->scene_coalesced;
    case 17: return report->scene_deferred; case 18: return report->scene_completed;
    case 19: return (uint32_t)thy_display_panel_visible(&preview_display);
    case 20: return (uint32_t)thy_display_scene_active(&preview_display);
    default: return 0;
    }
}

static int route_valid(int mask, int channel) {
    return mask >= 1 && mask <= 7 && channel >= 0 && channel <= 15;
}
int bridge_press(int pad, int count, int transpose, int channel, int mask, int velocity) {
    if (pad < 0 || pad >= (int)THY_PAD_COUNT || count < 0 || count > (int)THY_MAX_NOTES ||
        transpose < -32768 || transpose > 32767 || !route_valid(mask, channel) ||
        velocity < 1 || velocity > 127) return THY_INVALID_ARGUMENT;
    int already_held = thy_core_pad_held(&engine, (uint8_t)pad);
    int result = thy_engine_press_shaped(&shared_engine, (uint8_t)pad, note_input, (size_t)count,
                          (int16_t)transpose, (uint8_t)channel, (uint8_t)mask, (uint8_t)velocity, NULL);
    if (result == THY_OK && !already_held) thy_instrument_capture_pad_color(&firmware, (uint8_t)pad);
    return result;
}
static int shape_arguments(int spread, int inversion, int octave, int upper,
                           int order, int delay, int excluded, thy_shape *shape) {
    if (spread < 0 || spread > 3 || inversion < 0 || inversion > 64 ||
        octave < -10 || octave > 10 || upper < 0 || upper > 3 || order < 0 || order > 3 ||
        delay < 0 || delay > 1000 || excluded < 0 || excluded > 127) return 0;
    *shape = (thy_shape){.spread = (uint8_t)spread, .inversion = (uint8_t)inversion,
                         .octave_shift = (int8_t)octave, .upper_octaves = (uint8_t)upper,
                         .strum_order = (uint8_t)order, .strum_ms = (uint16_t)delay,
                         .excluded_slots = (uint64_t)excluded};
    return 1;
}
int bridge_press_shaped(int pad, int count, int transpose, int channel, int mask, int velocity,
                        int spread, int inversion, int octave, int upper, int order, int delay, int excluded) {
    thy_shape shape;
    if (pad < 0 || pad >= (int)THY_PAD_COUNT || count < 0 || count > (int)THY_MAX_NOTES ||
        transpose < -32768 || transpose > 32767 || !route_valid(mask, channel) ||
        velocity < 1 || velocity > 127 ||
        !shape_arguments(spread, inversion, octave, upper, order, delay, excluded, &shape)) return THY_INVALID_ARGUMENT;
    int already_held = thy_core_pad_held(&engine, (uint8_t)pad);
    int result = thy_engine_press_shaped(&shared_engine, (uint8_t)pad, note_input, (size_t)count,
                                 (int16_t)transpose, (uint8_t)channel, (uint8_t)mask, (uint8_t)velocity, &shape);
    if (result == THY_OK && !already_held) thy_instrument_capture_pad_color(&firmware, (uint8_t)pad);
    return result;
}
int bridge_preview(int count, int transpose, int spread, int inversion, int octave,
                   int upper, int order, int delay, int excluded) {
    thy_shape shape;
    if (count < 0 || count > (int)THY_MAX_NOTES || transpose < -32768 || transpose > 32767 ||
        !shape_arguments(spread, inversion, octave, upper, order, delay, excluded, &shape)) return THY_INVALID_ARGUMENT;
    return thy_core_preview(&engine, note_input, (size_t)count, (int16_t)transpose, &shape, note_preview, &preview_count);
}
int bridge_preview_count(void) { return (int)preview_count; }
int bridge_melody_pitch(int layout, int anchor, int count, int column, int row) {
    int16_t note;
    if (anchor < -32768 || anchor > 32767 || count < 0 || count > 12 ||
        column < 0 || column > 11 || row < 0 || row > 3 ||
        thy_core_melody_pitch(layout, (int16_t)anchor, note_input, (size_t)count, (uint8_t)column, (uint8_t)row, &note) != THY_OK) return -1;
    return note;
}
int bridge_advance(uint32_t now_ms) { return thy_engine_advance(&shared_engine, now_ms); }
int bridge_release(int pad) {
    if (pad < 0 || pad >= (int)THY_PAD_COUNT) return THY_INVALID_ARGUMENT;
    return thy_instrument_release_pad(&firmware, (uint8_t)pad);
}
int bridge_pressure(int pad, int value) {
    if (pad < 0 || pad >= (int)THY_PAD_COUNT || value < 0 || value > 127) return THY_INVALID_ARGUMENT;
    return shared_engine.expression_config.enabled ?
        thy_engine_expression_pressure(&shared_engine,(uint8_t)pad,(uint8_t)value) :
        thy_core_pressure(&engine, (uint8_t)pad, (uint8_t)value);
}
int bridge_sustain(int mask, int channel, int enabled) {
    if (!route_valid(mask, channel) || (enabled != 0 && enabled != 1)) return THY_INVALID_ARGUMENT;
    return thy_core_sustain(&engine, (uint8_t)mask, (uint8_t)channel, enabled);
}
int bridge_control(int mask, int channel, int cc, int value) {
    if (!route_valid(mask, channel) || cc < 0 || cc > 127 || value < 0 || value > 127) return THY_INVALID_ARGUMENT;
    return thy_core_cc(&engine, (uint8_t)mask, (uint8_t)channel, (uint8_t)cc, (uint8_t)value);
}
int bridge_bend(int mask, int channel, int value) {
    if (!route_valid(mask, channel)) return THY_INVALID_ARGUMENT;
    return thy_core_bend(&engine, (uint8_t)mask, (uint8_t)channel, value);
}
void bridge_panic(void) { thy_engine_panic(&shared_engine); }
int bridge_poll(void) { return thy_core_poll(&engine, &output_event); }
int bridge_usb_size(void) { return (int)thy_midi_encode_usb(&output_event, usb_bytes); }
int bridge_din_size(void) { return (int)thy_midi_encode_din(&output_event, din_bytes); }
int bridge_queued(void) { return thy_core_queued(&engine); }
int bridge_reserved(void) { return thy_core_reserved(&engine); }
int bridge_sounding(void) { return thy_core_sounding(&engine); }
int bridge_pending(void) { return thy_core_pending(&engine); }
int bridge_held(int pad) {
    return pad >= 0 && pad < (int)THY_PAD_COUNT && thy_core_pad_held(&engine, (uint8_t)pad);
}
int bridge_held_count(void) {
    unsigned count = 0;
    for (unsigned pad = 0; pad < THY_PAD_COUNT; ++pad) count += thy_core_pad_held(&engine, (uint8_t)pad) != 0;
    return (int)count;
}
int bridge_voice_snapshot(void) {
    unsigned count = 0;
    for (unsigned key = 0; key < THY_KEY_COUNT; ++key)
        if (engine.refs[key] || thy_core_deferred(&engine, (uint16_t)key)) voice_keys[count++] = (uint16_t)key;
    return (int)count;
}
int bridge_refs(int key) { return key >= 0 && key < (int)THY_KEY_COUNT ? engine.refs[key] : 0; }
int bridge_deferred(int key) { return key >= 0 && key < (int)THY_KEY_COUNT ? thy_core_deferred(&engine, (uint16_t)key) : 0; }
int bridge_owner_count(int pad) { return bridge_held(pad) ? engine.owners[pad].count : 0; }
int bridge_owner_key(int pad, int index) {
    if (pad < 0 || pad >= (int)THY_PAD_COUNT || index < 0 || index >= engine.owners[pad].count) return -1;
    return thy_core_owner_key(&engine, (uint8_t)pad, (uint8_t)index);
}
int bridge_state_bytes(void) { return (int)sizeof(engine); }

static uint32_t receive_now_ms;
static void receive(void *context, const thy_midi_event *event) {
    (void)context;
    /* Default clock input is USB cable 0. Observing packets must not depend on
     * space in the diagnostic receive queue, nor echo anything to outputs. */
    if (event->destination == THY_USB0) {
        const uint8_t message[3] = {event->status, event->data1, event->data2};
        const uint8_t kind = event->status & 0xf0u;
        const uint8_t length = event->status == 0xf2u ? 3u :
            event->status >= 0xf0u ? 1u : kind == 0xc0u || kind == 0xd0u ? 2u : 3u;
        (void)thy_engine_receive_midi(&shared_engine, message, length, receive_now_ms);
    }
    if (received_count == 256) { ++received_overflow; return; }
    received[(received_head + received_count) % 256] = *event;
    ++received_count;
}
int bridge_feed(int length) {
    if (length < 0 || length > 256) return THY_INVALID_ARGUMENT;
    receive_now_ms = shared_engine.musical_clock.now_ms;
    thy_usb_parser_feed(&input_parser, byte_input, (size_t)length, receive, NULL);
    return THY_OK;
}
int bridge_feed_at(int length, uint32_t now_ms) {
    if (length < 0 || length > 256) return THY_INVALID_ARGUMENT;
    int result = thy_engine_advance(&shared_engine, now_ms);
    if (result != THY_OK) return result;
    receive_now_ms = now_ms;
    thy_usb_parser_feed(&input_parser, byte_input, (size_t)length, receive, NULL);
    return THY_OK;
}
int bridge_receive_poll(void) {
    if (!received_count) return 0;
    output_event = received[received_head];
    received_head = (received_head + 1) % 256;
    --received_count;
    return 1;
}
int bridge_rx_fragment(void) { return input_parser.fragment_size; }
uint32_t bridge_rx_received(void) { return input_parser.received; }
uint32_t bridge_rx_invalid(void) { return input_parser.invalid; }
uint32_t bridge_rx_unsupported(void) { return input_parser.unsupported; }
uint32_t bridge_rx_overflow(void) { return received_overflow; }

/* The browser shares the installed instrument's controller, detector, note
 * illumination and OLED renderer. All calls are local WebAssembly only. */
void bridge_firmware_init(void) {
    firmware_frame_valid = 0u;
    thy_instrument_init(&firmware, &engine);
    (void)thy_instrument_set_detector_profile(&firmware, THY_DETECTOR_FACTORY_LINEAR);
    thy_lighting_init(&firmware_lights);
    thy_ui_init(&firmware_ui);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&shared_engine, &firmware, 21u, 70u, undo_workspace_1);
    thy_engine_bind_calibration(&shared_engine,&calibration_workspace);
    thy_engine_bind_expression(&shared_engine,&expression_workspace);
    thy_engine_bind_channel_expression(&shared_engine,&channel_expression_workspace);
    thy_engine_bind_controls(&shared_engine);
    /* The explicit legacy-navigation ABI below temporarily selects its
     * reference navigator. Physical control edges use the shared engine. */
    thy_instrument_set_control_filter(&firmware, shared_control_filter, &shared_engine);
    thy_studio_ui_init(&shared_ui);
    thy_studio_ui_set_build_identity(&shared_ui, "WEB PREVIEW");
    for (unsigned i = 0; i < THY_PAD_COUNT; ++i) {
        staged_layout[i] = firmware_layout[i] = (thy_instrument_pad){0};
        firmware_colors[i] = 0;
    }
}
int bridge_firmware_layout_pad(int pad, int valid_mask, int color) {
    if (pad < 0 || pad >= 96 || valid_mask < 0 || valid_mask > 127 || color < 0 || color > 255)
        return THY_INVALID_ARGUMENT;
    for (unsigned i = 0; i < THY_INSTRUMENT_SLOTS; ++i) {
        if ((valid_mask & (1 << i)) && (note_input[i] < 0 || note_input[i] > 127))
            return THY_INVALID_ARGUMENT;
    }
    for (unsigned i = 0; i < THY_INSTRUMENT_SLOTS; ++i) staged_layout[pad].notes[i] = note_input[i];
    staged_layout[pad].valid_mask = (uint8_t)valid_mask;
    staged_layout[pad].color = (uint8_t)color;
    return THY_OK;
}
int bridge_firmware_commit_layout(int anchor, int degree_count, int root) {
    if (anchor < 0 || anchor > 127 || degree_count < 1 || degree_count > 12 || root < 0 || root > 11)
        return THY_INVALID_ARGUMENT;
    int result = thy_instrument_set_layout(&firmware, staged_layout, (int16_t)anchor,
        note_input, (uint8_t)degree_count);
    if (result != THY_OK) return result;
    for (unsigned i = 0; i < THY_PAD_COUNT; ++i) firmware_layout[i] = staged_layout[i];
    firmware.layout = firmware_layout;
    if(firmware.performance.root!=(uint8_t)root)thy_instrument_key_changed(&firmware);
    firmware.performance.root = (uint8_t)root;
    return THY_OK;
}
int bridge_firmware_performance(int inversion, int spread, int strum, int delay, int order,
    int chord_octave, int melody_octave, int upper, int melody_layout, int selected) {
    if (inversion < 0 || inversion > 3 || spread < 0 || spread > 3 || strum < 0 || strum > 1 ||
        delay < 1 || delay > 1000 || order < 0 || order > 3 || chord_octave < -3 || chord_octave > 3 ||
        melody_octave < -3 || melody_octave > 3 || upper < 0 || upper > 3 || melody_layout < 0 ||
        melody_layout >= THY_LAYOUT_COUNT || selected < 0 || selected > 95) return THY_INVALID_ARGUMENT;
    firmware.performance.inversion = (uint8_t)inversion;
    firmware.performance.spread = (uint8_t)spread;
    firmware.performance.strum = (uint8_t)strum;
    firmware.performance.strum_ms = (uint16_t)delay;
    firmware.performance.strum_order = (uint8_t)order;
    firmware.performance.chord_octave = (int8_t)chord_octave;
    firmware.performance.melody_octave = (int8_t)melody_octave;
    firmware.performance.upper_octaves = (uint8_t)upper;
    firmware.performance.melody_layout = (uint8_t)melody_layout;
    firmware.engine_kind = THY_PLAY_ENGINE_PLAY;
    shared_engine.session.playing_mode = firmware.engine_kind;
    firmware.selected_pad = (uint8_t)selected;
    if (selected < 48) firmware.selected_chord = (uint8_t)selected;
    return THY_OK;
}
int bridge_firmware_route(int side, int channel, int mask, int transpose) {
    if (side < 0 || side > 1 || !route_valid(mask, channel) || transpose < -127 || transpose > 127)
        return THY_INVALID_ARGUMENT;
    return thy_instrument_set_route(&firmware, (uint8_t)side, (uint8_t)channel, (uint8_t)mask, (int16_t)transpose);
}
int bridge_firmware_route_value(int target, int field) {
    thy_instrument_route route;
    if (target < 0 || target >= (int)THY_INSTRUMENT_ROUTE_TARGETS ||
        thy_instrument_get_pad_route(&firmware, (uint8_t)target, &route) != THY_OK) return -1;
    switch (field) {
    case 0: return route.channel; case 1: return route.destinations;
    case 2: return route.velocity; case 3: return route.pressure_enabled;
    default: return -1;
    }
}
int bridge_firmware_edit_side_route(int side, int channel, int mask) {
    if (side < 0 || side > 1 || !route_valid(mask, channel)) return THY_INVALID_ARGUMENT;
    return thy_instrument_set_pad_route(&firmware, (uint8_t)(side + 1), (uint8_t)channel, (uint8_t)mask);
}
int bridge_firmware_transpose(int transpose) {
    if (transpose < -127 || transpose > 127) return THY_INVALID_ARGUMENT;
    firmware.transpose[0] = firmware.transpose[1] = (int16_t)transpose;
    ++firmware.revision;
    return THY_OK;
}
int bridge_firmware_route_target(int logical) {
    return logical >= 0 && logical < (int)THY_PAD_COUNT ?
        thy_instrument_route_target(&firmware, (uint8_t)logical) : -1;
}
int bridge_firmware_pressure_pad(int pad, int pressure) {
    if (pad < 0 || pad >= (int)THY_PAD_COUNT || pressure < 0 || pressure > 127)
        return THY_INVALID_ARGUMENT;
    return thy_engine_expression_pressure(&shared_engine,(uint8_t)pad,(uint8_t)pressure);
}

int bridge_expression_configure(int enabled,int upper,int members,int bend,
    int manager,int glide,int pads,int semitones)
{
    if(enabled<0 || enabled>1 || upper<0 || upper>1 || members<1 || members>15 ||
        bend<1 || bend>96 || manager<1 || manager>96 || glide<0 || glide>1 ||
        pads<1 || pads>11 || semitones<1 || semitones>24) return THY_INVALID_ARGUMENT;
    thy_mpe_config config={(uint8_t)enabled,(uint8_t)upper,(uint8_t)members,
        (uint8_t)bend,(uint8_t)manager,(uint8_t)glide,(uint8_t)pads,(uint8_t)semitones};
    return thy_engine_expression_configure(&shared_engine,&config);
}
int bridge_expression_value(int field)
{
    const thy_mpe_config *c=&shared_engine.expression_config;
    switch(field) {
    case 0:return c->enabled; case 1:return c->upper_zone; case 2:return c->members;
    case 3:return c->bend_range; case 4:return c->manager_bend_range;
    case 5:return c->glide; case 6:return c->pad_window; case 7:return c->semitone_window;
    case 8:return thy_mpe_active(shared_engine.expression);
    case 9: {int count=0;for(unsigned p=0;p<96;++p)count+=thy_mpe_consumed(shared_engine.expression,(uint8_t)p)!=0;return count;}
    default:return -1;
    }
}
int bridge_expression_setup(void)
{ return thy_engine_expression_send_setup(&shared_engine); }
int bridge_expression_pressure(int pad,int value)
{
    return pad>=0 && pad<96 && value>=0 && value<=127 ?
        thy_engine_expression_pressure(&shared_engine,(uint8_t)pad,(uint8_t)value) : THY_INVALID_ARGUMENT;
}
int bridge_expression_bend(int pad,int value)
{
    return pad>=0 && pad<96 && value>=0 && value<=16383 ?
        thy_engine_expression_bend(&shared_engine,(uint8_t)pad,(uint16_t)value) : THY_INVALID_ARGUMENT;
}
int bridge_expression_voice(int slot,int field)
{
    if(slot<0 || slot>=15)return -1;
    const thy_mpe_voice *v=thy_mpe_voice_at(shared_engine.expression,(uint8_t)slot);
    if(!v)return -1;
    switch(field) {
    case 0:return v->pad;case 1:return v->logical;case 2:return v->pitch;case 3:return v->channel;
    case 4:return v->destinations;case 5:return v->state;case 6:return v->pressure;
    case 7:return v->sent_pressure;case 8:return v->bend;case 9:return v->sent_bend;
    case 10:return v->modifier;case 11:return v->interval;case 12:return v->modifier_pressure;
    case 13:return v->timbre;case 14:return v->sent_timbre;default:return -1;
    }
}
int bridge_firmware_slot(int pad, int excluded) {
    if (pad < 0 || pad >= 48 || excluded < 0 || excluded > 127) return THY_INVALID_ARGUMENT;
    firmware.excluded_slots[pad] = (uint8_t)excluded;
    return THY_OK;
}
int bridge_firmware_slot_value(int pad) {
    return pad >= 0 && pad < 48 ? firmware.excluded_slots[pad] : -1;
}
int bridge_firmware_chord_cell(int pad,int field) {
    thy_chord_cell cell;
    if(pad<0||pad>=48||thy_instrument_chord_cell(&firmware,(uint8_t)pad,&cell)!=THY_OK)return -1;
    return field==0?cell.valid_mask:field==1?cell.root:field==2?cell.degree:
        field>=3&&field<10?cell.notes[field-3]:-1;
}
int bridge_firmware_control(int bank, int index, int pressed) {
    if (bank < 0 || bank >= 4 || index < 0 || index >= 32 || (pressed != 0 && pressed != 1)) return THY_INVALID_ARGUMENT;
    int result = thy_instrument_control(&firmware, (uint8_t)bank, (uint8_t)index, pressed);
    shared_engine.session.playing_mode = firmware.engine_kind;
    return result;
}
int bridge_firmware_scale_shortcut(int key, int assignment) {
    return key >= 0 && key < (int)THY_SCALE_SHORTCUT_COUNT && assignment >= 0 &&
        assignment <= (int)(THY_SCALE_SHORTCUT_LAYOUT + THY_LIBRARY_SCALES) ?
        thy_instrument_set_scale_shortcut(&firmware, (uint8_t)key,
            (uint8_t)assignment) : THY_INVALID_ARGUMENT;
}
int bridge_firmware_scale_shortcut_value(int key) {
    return key >= 0 && key < (int)THY_SCALE_SHORTCUT_COUNT ?
        thy_instrument_scale_shortcut(&firmware, (uint8_t)key) : -1;
}
int bridge_firmware_press_pad(int pad) {
    return pad >= 0 && pad < (int)THY_PAD_COUNT ?
        thy_instrument_press_pad(&firmware, (uint8_t)pad) : THY_INVALID_ARGUMENT;
}
int bridge_firmware_preview_pad(int pad) {
    preview_count = 0;
    return pad >= 0 && pad < (int)THY_PAD_COUNT ?
        thy_instrument_preview_pad(&firmware, (uint8_t)pad, note_preview, &preview_count) : THY_INVALID_ARGUMENT;
}
int bridge_firmware_context(int pad) {
    return pad >= 0 && pad < (int)THY_PAD_COUNT ?
        thy_instrument_context_pad(&firmware, (uint8_t)pad) : 0;
}
int bridge_firmware_context_held(int pad) {
    return pad >= 0 && pad < (int)THY_PAD_COUNT ? firmware.context_held[pad] : 0;
}
int bridge_firmware_sample(int pad, int raw, uint32_t now) {
    if (pad < 0 || pad >= 96 || raw < 0 || raw > 4095) return THY_INVALID_ARGUMENT;
    return thy_instrument_sample(&firmware, (uint8_t)pad, (uint16_t)raw, now);
}
int bridge_firmware_sensor(int pad, int field) {
    if (pad < 0 || pad >= 96) return -1;
    const thy_instrument_sensor *s = &firmware.sensors[pad];
    switch (field) {
    case 0: return s->raw; case 1: return s->calibration.baseline; case 2: return s->pressure;
    case 3: return s->held; case 4: return s->blocked; case 5: return s->warmup_count;
    case 6: return s->attack_raw; case 7: return s->release_raw; case 8: return s->detector_profile;
    case 9: return s->strike_velocity; case 10: return s->strike_peak_rate;
    case 11: return s->strike_ready;
    case 12: return firmware.sensitivity_guard[pad];
    default: return -1;
    }
}
int bridge_firmware_response(int pad, int field) {
    thy_instrument_response response; uint8_t overridden = 0;
    if (pad < -2 || pad >= 96) return -1;
    int result = pad < 0 ? thy_instrument_get_cluster_response(&firmware,(uint8_t)(pad== -2), &response) :
        thy_instrument_get_pad_response(&firmware, (uint8_t)pad, &response, &overridden);
    if (result != THY_OK) return -1;
    switch (field) {
    case 0: return response.attack_raw; case 1: return response.release_raw;
    case 2: return response.pressure_floor; case 3: return response.pressure_full_scale;
    case 4: return response.pressure_curve; case 5: return overridden; default: return -1;
    }
}
int bridge_firmware_set_response(int pad, int attack, int release, int floor, int ceiling, int curve) {
    if (pad < -2 || pad >= 96 || attack < 0 || attack > 4095 || release < 0 || release > 4095 ||
        floor < 0 || floor > 4095 || ceiling < 0 || ceiling > 4095 || curve < 0 || curve > 2)
        return THY_INVALID_ARGUMENT;
    thy_instrument_response r = {(uint16_t)attack, (uint16_t)release, (uint16_t)floor,
        (uint16_t)ceiling, (uint8_t)curve};
    return pad < 0 ? thy_instrument_set_cluster_response(&firmware,(uint8_t)(pad== -2), &r) :
        thy_instrument_set_pad_response(&firmware, (uint8_t)pad, &r);
}
int bridge_firmware_clear_response(int pad) {
    if (pad < 0 || pad >= 96) return THY_INVALID_ARGUMENT;
    return thy_instrument_set_pad_response(&firmware, (uint8_t)pad, NULL);
}
int bridge_firmware_release_policy(int pad,int field) {
    thy_instrument_release_policy policy;uint8_t overridden=0u;
    if(pad< -2 || pad>=96 || (pad<0?thy_instrument_get_cluster_release_policy(&firmware,(uint8_t)(pad== -2),&policy):
       thy_instrument_get_release_policy(&firmware,pad,&policy,&overridden))!=THY_OK)return -1;
    return field==0?policy.linked:field==1?policy.gap:field==2?overridden:-1;
}
int bridge_firmware_set_release_policy(int pad,int linked,int gap) {
    if(pad< -2 || pad>=96 || linked<0 || linked>1 || gap<1 || gap>4095)return THY_INVALID_ARGUMENT;
    const thy_instrument_release_policy policy={(uint16_t)gap,(uint8_t)linked};
    return pad<0?thy_instrument_set_cluster_release_policy(&firmware,(uint8_t)(pad== -2),&policy):
        thy_instrument_set_release_policy(&firmware,pad,&policy);
}
int bridge_firmware_set_trigger(int pad,int attack) {
    if(pad< -2 || pad>=96 || attack<1 || attack>4095)return THY_INVALID_ARGUMENT;
    if(pad>=0)return thy_instrument_set_trigger(&firmware,pad,(uint16_t)attack);
    unsigned side=pad== -2;
    thy_instrument_response r=firmware.cluster_response[side];
    thy_instrument_release_policy p=firmware.cluster_release_policy[side];
    r.attack_raw=(uint16_t)attack;if(p.linked)r.release_raw=attack>p.gap?(uint16_t)(attack-p.gap):0u;
    return thy_instrument_set_cluster_sensitivity_policy(&firmware,(uint8_t)side,&r,&firmware.cluster_velocity[side],&p);
}
static uint8_t sensitivity_instance(int pad) {return pad== -2?97u:pad== -1?96u:firmware.mapping[pad];}
int bridge_firmware_sensitivity_amount(int pad) {
    int32_t value;
    return pad< -2 || pad>=96 || thy_state_get_instance(&shared_engine.state,
        THY_ROW_SENSITIVITY_AMOUNT,sensitivity_instance(pad),&value)!=THY_STATE_OK?-1:value;
}
int bridge_firmware_set_sensitivity_amount(int pad,int value) {
    return pad< -2 || pad>=96?THY_STATE_INVALID:thy_state_set_instance(&shared_engine.state,
        THY_ROW_SENSITIVITY_AMOUNT,sensitivity_instance(pad),value);
}
int bridge_firmware_sensitivity_bounds(int pad,int field) {
    int32_t low,high;
    return pad< -2 || pad>=96 || thy_state_edit_bounds(&shared_engine.state,THY_ROW_SENSITIVITY_AMOUNT,
        sensitivity_instance(pad),&low,&high)!=THY_STATE_OK?-1:field==0?low:field==1?high:-1;
}
int bridge_calibration_info(int field) {
    const thy_calibration *w=shared_engine.calibration;
    if(!w)return -1;
    switch(field) {
    case 0:return shared_engine.calibration_open;case 1:return w->state;
    case 2:return w->target;case 3:return w->selected;case 4:return w->stage;
    case 5:return w->hits;case 6:return w->completed_count;case 7:return w->reason;
    case 8:return w->action_cursor;case 9:return w->contact_active;case 10:return (int)w->revision;
    case 11:return w->live_raw;case 12:return w->live_rate;case 13:return w->baseline;
    case 14:return w->quiet_span;case 15:return w->peak;case 16:return w->rise_rate;
    case 17:return w->samples;
    case 18:return w->hold_soft;case 19:return w->hold_firm;
    case 20:return (int)(w->hold_last_ms-w->hold_started_ms);case 21:return w->hold_soft_samples;
    case 22:return w->preview_percent;case 23:return w->review_outlier_count;
    case 24:return w->review_ready;case 25:return shared_engine.calibration_review_cursor;
    case 26:return shared_engine.calibration_review_editing;case 27:return shared_engine.calibration_review_draft;
    case 28:return shared_engine.calibration_pick;
    case 29:return w->scope_first;case 30:return w->scope_count;
    case 31:return w->review_leak_count;default:return -1;
    }
}
int bridge_calibration_open(void) {return thy_engine_calibration_open(&shared_engine);}
void bridge_calibration_close(void) {thy_engine_calibration_close(&shared_engine);}
int bridge_calibration_action(int action) {
    if(action<0 || action>=THY_CALIBRATION_ACTION_COUNT || !shared_engine.calibration_open)return THY_STATE_INVALID;
    shared_engine.calibration->action_cursor=(uint8_t)action;
    return thy_engine_calibration_navigate(&shared_engine,THY_STATE_RIGHT);
}
int bridge_calibration_select(int pad) {
    return pad<0 || pad>=96 || !shared_engine.calibration_open?THY_CALIBRATION_INVALID:
        thy_engine_calibration_select(&shared_engine,(uint8_t)pad);
}
int bridge_calibration_set_sensitivity(int percent) {return thy_engine_calibration_set_sensitivity(&shared_engine,percent);}
int bridge_calibration_strength(int pad) {return pad<0 || pad>=96?-1:(int)thy_calibration_strength_percent(shared_engine.calibration,(uint8_t)pad);}
int bridge_calibration_outlier(int pad) {return pad>=0 && pad<96 && thy_calibration_outlier(shared_engine.calibration,(uint8_t)pad);}
int bridge_calibration_completed(int pad) {
    return pad>=0 && pad<96 && thy_calibration_completed(shared_engine.calibration,(uint8_t)pad);
}
int bridge_calibration_candidate(int pad,int field) {
    thy_calibration_candidate c;
    if(pad<0 || pad>=96 || thy_calibration_candidate_get(shared_engine.calibration,(uint8_t)pad,&c)!=THY_CALIBRATION_OK)return -1;
    switch(field) {
    case 0:return c.trigger;case 1:return c.release;case 2:return c.pressure_floor;
    case 3:return c.pressure_full;case 4:return c.soft_rate;case 5:return c.hard_rate;
    case 6:case 7:case 8:return c.anchors[field-6];case 9:return c.pressure_curve;
    case 10:return c.velocity_curve;default:return -1;
    }
}
uintptr_t bridge_calibration_reason(void) {
    return (uintptr_t)thy_calibration_reason_text(shared_engine.calibration->reason);
}
int bridge_calibration_value(int pad,int raw,int rate,int candidate,int velocity) {
    return pad<0 || pad>=96 || raw<0 || raw>4095 || rate<0 || rate>4095 ||
        candidate<0 || candidate>1 || velocity<0 || velocity>1?-1:
        thy_engine_calibration_value(&shared_engine,(uint8_t)pad,(uint16_t)raw,(uint16_t)rate,candidate,velocity);
}
int bridge_firmware_velocity(int pad, int field) {
    thy_instrument_velocity config; uint8_t overridden = 0u;
    if (pad < -2 || pad >= 96 ||
        (pad<0?thy_instrument_get_cluster_velocity(&firmware,(uint8_t)(pad== -2),&config):
        thy_instrument_get_velocity(&firmware, pad, &config, &overridden)) != THY_OK) return -1;
    switch (field) {
    case 0: return config.mode; case 1: return config.curve;
    case 2: return config.soft_rate; case 3: return config.hard_rate;
    case 4: return config.minimum; case 5: return config.maximum;
    case 6: return overridden; default: return -1;
    }
}
static int velocity_input(int mode, int curve, int soft, int hard, int minimum,
    int maximum, thy_instrument_velocity *config) {
    if (mode < 0 || mode > 1 || curve < 0 || curve > 2 || soft < 0 || soft > 4094 ||
        hard < 1 || hard > 4095 || minimum < 1 || minimum > 127 || maximum < 1 || maximum > 127)
        return 0;
    *config = (thy_instrument_velocity){.soft_rate=(uint16_t)soft, .hard_rate=(uint16_t)hard,
        .mode=(uint8_t)mode, .curve=(uint8_t)curve, .minimum=(uint8_t)minimum, .maximum=(uint8_t)maximum};
    return thy_instrument_velocity_valid(config);
}
int bridge_firmware_set_velocity(int pad, int mode, int curve, int soft, int hard,
    int minimum, int maximum) {
    thy_instrument_velocity config;
    if (pad < -2 || pad >= 96 || !velocity_input(mode, curve, soft, hard, minimum, maximum, &config))
        return THY_INVALID_ARGUMENT;
    return pad < 0 ? thy_instrument_set_cluster_velocity(&firmware,(uint8_t)(pad== -2), &config) :
        thy_instrument_set_pad_velocity(&firmware, (uint8_t)pad, &config);
}
int bridge_firmware_set_sensitivity(int pad, int attack, int release, int floor, int ceiling,
    int pressure_curve, int mode, int velocity_curve, int soft, int hard, int minimum, int maximum) {
    thy_instrument_velocity velocity;
    if (pad < -2 || pad >= 96 || attack < 0 || attack > 4095 || release < 0 || release > 4095 ||
        floor < 0 || floor > 4095 || ceiling < 0 || ceiling > 4095 || pressure_curve < 0 || pressure_curve > 2 ||
        !velocity_input(mode, velocity_curve, soft, hard, minimum, maximum, &velocity)) return THY_INVALID_ARGUMENT;
    thy_instrument_response response = {(uint16_t)attack, (uint16_t)release, (uint16_t)floor,
        (uint16_t)ceiling, (uint8_t)pressure_curve};
    if(pad<0){
        thy_instrument_release_policy policy={(uint16_t)(attack>release?attack-release:1),0u};
        return thy_instrument_set_cluster_sensitivity_policy(&firmware,(uint8_t)(pad== -2),&response,&velocity,&policy);
    }
    return thy_instrument_set_sensitivity(&firmware, pad, &response, &velocity);
}
int bridge_firmware_set_sensitivity_policy(int pad,int attack,int release,int floor,int ceiling,
    int pressure_curve,int mode,int velocity_curve,int soft,int hard,int minimum,int maximum,int linked,int gap) {
    thy_instrument_velocity velocity;
    if(pad< -2 || pad>=96 || attack<0 || attack>4095 || release<0 || release>4095 ||
       floor<0 || floor>4095 || ceiling<0 || ceiling>4095 || pressure_curve<0 || pressure_curve>2 ||
       linked<0 || linked>1 || gap<1 || gap>4095 ||
       !velocity_input(mode,velocity_curve,soft,hard,minimum,maximum,&velocity))return THY_INVALID_ARGUMENT;
    const thy_instrument_response response={(uint16_t)attack,(uint16_t)release,(uint16_t)floor,
        (uint16_t)ceiling,(uint8_t)pressure_curve};
    const thy_instrument_release_policy policy={(uint16_t)gap,(uint8_t)linked};
    return pad<0?thy_instrument_set_cluster_sensitivity_policy(&firmware,(uint8_t)(pad== -2),&response,&velocity,&policy):
        thy_instrument_set_sensitivity_policy(&firmware,pad,&response,&velocity,&policy);
}
int bridge_firmware_clear_velocity(int pad) {
    if (pad < 0 || pad >= 96) return THY_INVALID_ARGUMENT;
    return thy_instrument_clear_pad_velocity(&firmware, (uint8_t)pad);
}
int bridge_firmware_reset_velocity(int pad) {
    thy_instrument_velocity config;
    thy_instrument_velocity_defaults(&config);
    return pad < -2 || pad >= 96 ? THY_INVALID_ARGUMENT : pad < 0 ?
        thy_instrument_set_cluster_velocity(&firmware,(uint8_t)(pad== -2), &config) :
        thy_instrument_set_pad_velocity(&firmware, (uint8_t)pad, &config);
}
int bridge_firmware_velocity_value(int mode, int curve, int soft, int hard,
    int minimum, int maximum, int rate) {
    thy_instrument_velocity config;
    return rate < 0 || rate > 4095 || !velocity_input(mode, curve, soft, hard, minimum, maximum, &config) ?
        -1 : thy_instrument_velocity_value(&config, (uint16_t)rate);
}
int bridge_firmware_factory(void) {
    return thy_instrument_set_detector_profile(&firmware, THY_DETECTOR_FACTORY_LINEAR);
}
int bridge_firmware_expression(int velocity, int pressure) {
    if (velocity < 1 || velocity > 127 || pressure < 0 || pressure > 1) return THY_INVALID_ARGUMENT;
    return thy_instrument_set_expression(&firmware, (uint8_t)velocity, (uint8_t)pressure);
}
int bridge_firmware_lighting(int enabled, int idle, int held, int fade) {
    if (enabled < 0 || enabled > 1 || idle < 0 || idle > 8 || held < 0 || held > 8 || fade < 0 || fade > 500)
        return THY_INVALID_ARGUMENT;
    return thy_instrument_set_lighting(&firmware, (uint8_t)enabled, (uint8_t)idle, (uint8_t)held, (uint16_t)fade);
}
int bridge_firmware_nav(int index) {
    if (index < 0 || index > 4) return THY_INVALID_ARGUMENT;
    thy_instrument_navigation_fn handler=firmware.navigation;void *context=firmware.navigation_context;
    thy_instrument_set_navigation_handler(&firmware,NULL,NULL);
    int result = thy_instrument_control(&firmware, 0, (uint8_t)index, 1);
    (void)thy_instrument_control(&firmware, 0, (uint8_t)index, 0);
    thy_instrument_set_navigation_handler(&firmware,handler,context);
    return result;
}
int bridge_firmware_focus(int row) {
    if (row < 0 || row >= (int)THY_INSTRUMENT_MENU_ROWS) return THY_INVALID_ARGUMENT;
    firmware.menu_open = 1; firmware.menu_editing = 0; firmware.menu_row = (uint8_t)row;
    return THY_OK;
}
int bridge_firmware_state(int field) {
    switch (field) {
    case 0: return firmware.menu_open; case 1: return firmware.menu_editing;
    case 2: return firmware.menu_row; case 3: return firmware.menu_notice;
    case 4: return firmware.performance.inversion; case 5: return firmware.performance.spread;
    case 6: return firmware.performance.strum; case 7: return firmware.performance.strum_ms;
    case 8: return firmware.performance.strum_order; case 9: return firmware.performance.melody_layout;
    case 10: return firmware.performance.chord_octave; case 11: return firmware.performance.melody_octave;
    case 12: return firmware.performance.upper_octaves; case 13: return firmware.performance.root;
    case 14: return firmware.performance.shortcut; case 15: return firmware.fixed_velocity;
    case 16: return firmware.pressure_enabled; case 17: return firmware.lights_enabled;
    case 18: return firmware.light_idle; case 19: return firmware.light_held;
    case 20: return firmware.light_fade_ms; case 21: return firmware.selected_pad;
    case 22: return firmware.performance.guitar_window; case 23: return firmware.context_controls;
    case 24: return firmware.center_poll_ms; case 25: return firmware.display_view;
    case 26: return firmware.light_chord_mode; case 27: return firmware.light_palette;
    case 28: return firmware.light_key_contrast; case 29: return firmware.light_held_style;
    case 30: return firmware.light_held_feedback; case 31: return firmware.animations_enabled;
    case 32: return firmware.routing_profile;
    case 33: return firmware.slot_scope;
    case 34: return firmware.global_excluded_slots;
    case 35: return firmware.selected_chord;
    case 36: return firmware.light_key_contrast_percent;
    case 37: return firmware.center_brightness;
    case 38: return firmware.context_location;
    case 39: return firmware.context_mask;
    case 40: return firmware.bass_octave;
    case 41: return firmware.lower_mode;
    case 42: return thy_instrument_lower_mask(&firmware);
    case 43: return thy_instrument_lower_available(&firmware);
    case 44:return firmware.voicing_mode;
    case 45:return firmware.drop_mask;
    case 46:return thy_instrument_drop_available(&firmware);
    case 47:return firmware.chord_layout;
    case 48:return firmware.live_update;
    case 49:return firmware.user_scale;
    default: return -1;
    }
}
uintptr_t bridge_firmware_frame(int length, int din, int usb, uint32_t now, int animations) {
    char name[32];
    if (length < 0 || length > 31 || din < 0 || din > 1 || usb < 0 || usb > 1) return 0;
    for (int i = 0; i < length; ++i) name[i] = byte_input[i] >= 32 && byte_input[i] <= 126 ? (char)byte_input[i] : '?';
    name[length] = 0;
    firmware_ui.animations_enabled = animations != 0;
    thy_ui_render(&firmware_ui, &firmware, name, din, usb, now, firmware_frame);
    firmware_frame_ms = now;
    firmware_frame_valid = 1u;
    return (uintptr_t)firmware_frame;
}
uintptr_t bridge_firmware_colors(uint32_t now) {
    if(thy_engine_calibration_lights(&shared_engine,now,firmware_colors))return (uintptr_t)firmware_colors;
    thy_lighting_config config;
    thy_lighting_defaults(&config);
    config.enabled = firmware.lights_enabled;
    config.idle_brightness = firmware.light_idle;
    config.held_brightness = firmware.light_held;
    config.fade_ms = firmware.light_fade_ms;
    config.chord_color_mode = firmware.light_chord_mode;
    config.palette = firmware.light_palette;
    config.key_contrast = firmware.light_key_contrast;
    config.key_dim_percent = firmware.light_key_contrast_percent;
    config.held_style = firmware.light_held_style;
    config.held_feedback = firmware.light_held_feedback;
    config.right_illumination = firmware.performance.melody_layout == THY_LAYOUT_IN_KEY_LIT ?
        THY_LIGHTING_RIGHT_IN_KEY : THY_LIGHTING_RIGHT_ALL;
    if (thy_lighting_render(&firmware_lights, &firmware, &config, now, firmware_colors) != THY_OK) return 0;
    return (uintptr_t)firmware_colors;
}

/* Serialized shared-state ABI. No menu or configuration rules live in JS. */
int bridge_state_design_mode(int enabled) {
    return enabled == 0 || enabled == 1 ?
        thy_state_set_design_mode(&shared_engine.state, (uint8_t)enabled) : THY_STATE_INVALID;
}
int bridge_state_rows(void) { return THY_MENU_ROW_COUNT; }
int bridge_state_groups(void) { return THY_MENU_GROUP_COUNT; }
int bridge_state_visible(int row) {
    return row >= 0 && row < (int)THY_MENU_ROW_COUNT && thy_state_row_visible(&shared_engine.state, (uint16_t)row);
}
uintptr_t bridge_state_center_levels(void) {
    thy_engine_center_levels(&shared_engine, center_levels);
    return (uintptr_t)center_levels;
}
uintptr_t bridge_state_schema(void) { return (uintptr_t)THY_MENU_SCHEMA_SHA256; }
int bridge_state_catalog(int group, int count) {
    if (group < 0 || group > 65535 || count < 1 || count > 65535) return THY_STATE_INVALID;
    return thy_engine_catalog(&shared_engine, (uint16_t)group, (uint16_t)count);
}
int bridge_state_get(int row) {
    int32_t value;
    return row >= 0 && row < (int)THY_MENU_ROW_COUNT &&
        thy_state_get(&shared_engine.state, (uint16_t)row, &value) == THY_STATE_OK ? value : INT32_MIN;
}
int bridge_state_bounds(int row,int instance,int field) {
    int32_t low,high;
    if(row<0 || row>=(int)THY_MENU_ROW_COUNT || instance< -1 ||
       instance>=thy_state_instance_count((uint16_t)row))return INT32_MIN;
    uint8_t target=instance>=0?(uint8_t)instance:shared_engine.state.editing && shared_engine.state.edit_row==row?
        shared_engine.state.edit_instance:thy_state_current_instance(&shared_engine.state,(uint16_t)row);
    if(thy_state_edit_bounds(&shared_engine.state,(uint16_t)row,target,&low,&high)!=THY_STATE_OK)return INT32_MIN;
    return field==0?low:field==1?high:INT32_MIN;
}
uintptr_t bridge_state_text(int row) {
    if (row < 0 || row >= (int)THY_MENU_ROW_COUNT) return 0;
    return (uintptr_t)thy_state_get_text(&shared_engine.state, (uint16_t)row);
}
int bridge_state_set(int row, int value) {
    return row < 0 || row >= (int)THY_MENU_ROW_COUNT ? THY_STATE_INVALID :
        thy_state_set(&shared_engine.state, (uint16_t)row, value);
}
static const char *state_input(int length) {
    if (length < 0 || length >= (int)sizeof byte_input) return NULL;
    byte_input[length] = 0;
    return (const char *)byte_input;
}
int bridge_state_set_text(int row, int length) {
    const char *value = state_input(length);
    return value == NULL || row < 0 || row >= (int)THY_MENU_ROW_COUNT ? THY_STATE_INVALID :
        thy_state_set_text(&shared_engine.state, (uint16_t)row, value);
}
int bridge_state_begin(int row) {
    return row < 0 || row >= (int)THY_MENU_ROW_COUNT ? THY_STATE_INVALID :
        thy_state_begin_edit(&shared_engine.state, (uint16_t)row);
}
int bridge_state_stage(int value) { return thy_state_edit_set(&shared_engine.state, value); }
int bridge_state_stage_text(int length) {
    const char *value = state_input(length);
    return value == NULL ? THY_STATE_INVALID : thy_state_edit_text(&shared_engine.state, value);
}
int bridge_state_adjust(int delta) { return thy_state_adjust(&shared_engine.state, delta); }
int bridge_state_commit(void) { return thy_state_commit(&shared_engine.state); }
int bridge_state_cancel(void) { return thy_state_cancel(&shared_engine.state); }
int bridge_state_focus(int group, int row) {
    if (group < 0 || group >= (int)THY_MENU_GROUP_COUNT || row < -1 || row >= (int)THY_MENU_ROW_COUNT)
        return THY_STATE_INVALID;
    int result = thy_state_focus(&shared_engine.state, (uint8_t)group,
        row < 0 ? THY_STATE_NO_ROW : (uint16_t)row);
    if (result == THY_STATE_OK) shared_engine.arp_quick_open = 0u;
    return result;
}
void bridge_state_open(void) {
    thy_engine_calibration_close(&shared_engine);
    if (shared_engine.sequencer_ui.open) (void)thy_engine_sequencer_close(&shared_engine);
    shared_engine.arp_quick_open = 0u;
    thy_state_open(&shared_engine.state);
}
void bridge_state_play(void) {
    thy_engine_calibration_close(&shared_engine);
    if (shared_engine.sequencer_ui.open) (void)thy_engine_sequencer_close(&shared_engine);
    (void)thy_engine_arp_quick_close(&shared_engine);
    thy_state_play(&shared_engine.state);
}
int bridge_state_nav(int action) {
    return action < 0 || action > 4 ? THY_STATE_INVALID :
        thy_engine_navigate(&shared_engine, (uint8_t)action);
}
int bridge_state_cursor(int group) {
    return group >= 0 && group < (int)THY_MENU_GROUP_COUNT ? shared_engine.state.cursors[group] : -1;
}
int bridge_state_info(int field) {
    const thy_state *s = &shared_engine.state;
    switch (field) {
    case 0: return s->level; case 1: return s->group_index;
    case 2: return s->level == THY_STATE_GROUPS ? s->group_index : s->cursors[s->group_index];
    case 3: return s->editing; case 4: return s->edit_row; case 5: return s->edit_value;
    case 6: return (int)s->revision; case 7: return (int)s->transition_revision;
    case 8: return s->transition_direction; case 9: return s->notice;
    case 10: return thy_state_change_count(s); case 11: return sizeof(thy_state);
    case 12: return s->history_count; case 13: return shared_ui.transitioning;
    case 14: return sizeof(thy_engine) + sizeof(thy_studio_ui);
    case 15: return s->about_open;
    case 16: return shared_engine.arp_quick_open;
    case 17: return shared_engine.arp_target;
    case 18: return shared_engine.arp_quick_cursor;
    case 19: return shared_engine.arp_quick_keys;
    case 20: return s->info_open;
    case 21: return (int)shared_ui.transition_elapsed_ms;
    case 22: return shared_engine.sequencer_ui.options_open;
    case 23: return shared_engine.sequencer_ui.options_track;
    case 24: return shared_engine.sensitivity_target;
    case 25: return shared_engine.sensitivity_pick;
    case 26: return shared_engine.sensitivity_picked;
    default: return -1;
    }
}
/* Same versioned settings codec as the MCU; no flash operations are exposed. */
uintptr_t bridge_settings_buffer(void) { return (uintptr_t)settings_input; }
int bridge_settings_capacity(void) { return sizeof settings_input; }
int bridge_settings_version(void) { return THY_SETTINGS_VERSION; }
int bridge_settings_payload_version(int length) {
    return length > 0 && length <= (int)sizeof settings_input ?
        thy_settings_payload_version(settings_input, (size_t)length) : 0;
}
int bridge_settings_encode(void) {
    size_t bytes = 0;
    return thy_settings_encode(&shared_engine, settings_input, sizeof settings_input, &bytes) ? (int)bytes : 0;
}
int bridge_settings_validate(int length) {
    return length > 0 && length <= (int)sizeof settings_input &&
        thy_settings_validate(settings_input, (size_t)length, shared_engine.state.scale_count);
}
int bridge_settings_catalog(int length, int field) {
    uint16_t scale; uint8_t root;
    if (!bridge_settings_validate(length) || !thy_settings_catalog(settings_input, (size_t)length, &scale, &root)) return -1;
    return field == 0 ? scale : field == 1 ? root : -1;
}
uint32_t bridge_settings_crc(int length) {
    return length >= 0 && length <= (int)sizeof settings_input ? thy_settings_crc32(settings_input, (size_t)length) : 0;
}
int bridge_settings_can_apply(void) {
    if (shared_engine.calibration_open || shared_engine.state.editing || engine.sounding_count || engine.queue_count || engine.pending_keys ||
        engine.generated_count || engine.sustain_count || shared_engine.arps[0].held_count ||
        shared_engine.arps[1].held_count || shared_engine.arps[0].output_count ||
        shared_engine.arps[1].output_count) return 0;
    for (unsigned n = 0; n < THY_PAD_COUNT; ++n)
        if (engine.owners[n].held || firmware.sensors[n].held || firmware.context_held[n]) return 0;
    for (unsigned n = 0; n < 4u; ++n) if (firmware.control_masks[n]) return 0;
    return 1;
}
int bridge_settings_apply(int length) {
    if (!bridge_settings_validate(length) || !bridge_settings_can_apply()) return 0;
    uint8_t enabled = firmware.input_enabled;
    firmware.input_enabled = 0;
    int result = thy_settings_apply(&shared_engine, settings_input, (size_t)length);
    firmware.input_enabled = enabled;
    return result;
}
uintptr_t bridge_state_edit_text(void) { return (uintptr_t)shared_engine.state.edit_text; }
uintptr_t bridge_state_notice(void) { return (uintptr_t)thy_state_notice_text(&shared_engine.state); }
uintptr_t bridge_state_musical_name(void) {
    thy_studio_ui_view_line(&shared_engine.state, THY_VIEW_INFO_MUSICAL_NAMES,
        NULL, 1, 1, state_output);
    return (uintptr_t)state_output;
}
int bridge_state_allowed(int row, int value) {
    return row >= 0 && row < (int)THY_MENU_ROW_COUNT &&
        thy_state_option_allowed(&shared_engine.state, (uint16_t)row, value);
}
uintptr_t bridge_state_format(int row, int editing) {
    if (row < 0 || row >= (int)THY_MENU_ROW_COUNT) return 0;
    (void)thy_state_format(&shared_engine.state, (uint16_t)row, editing != 0, state_output, sizeof state_output);
    return (uintptr_t)state_output;
}
uintptr_t bridge_state_compact(int row, int editing) {
    if (row < 0 || row >= (int)THY_MENU_ROW_COUNT) return 0;
    thy_studio_ui_format(&shared_engine.state, (uint16_t)row, editing != 0, state_output);
    return (uintptr_t)state_output;
}
int bridge_state_instances(int row) {
    return row >= 0 && row < (int)THY_MENU_ROW_COUNT ? thy_state_instance_count((uint16_t)row) : 0;
}
int bridge_state_instance_get(int row, int index) {
    int32_t value;
    return row >= 0 && row < (int)THY_MENU_ROW_COUNT && index >= 0 && index < thy_state_instance_count((uint16_t)row) &&
        thy_state_get_instance(&shared_engine.state, (uint16_t)row, (uint8_t)index, &value) == THY_STATE_OK ? value : INT32_MIN;
}
uintptr_t bridge_state_instance_text(int row, int index) {
    return row >= 0 && row < (int)THY_MENU_ROW_COUNT && index >= 0 && index < thy_state_instance_count((uint16_t)row) ?
        (uintptr_t)thy_state_get_text_instance(&shared_engine.state, (uint16_t)row, (uint8_t)index) : 0;
}
int bridge_state_event(int index, int field) {
    const thy_state *s = &shared_engine.state;
    thy_state_event event;
    if (index < 0 || index >= s->history_count ||
        !thy_state_get_event(s, s->revision - s->history_count + 1u + (unsigned)index, &event)) return -1;
    switch (field) {
    case 0: return (int)event.sequence; case 1: return event.row; case 2: return event.kind;
    case 3: return event.value; case 4: return event.instance; default: return -1;
    }
}
uintptr_t bridge_state_event_text(int index) {
    const thy_state *s = &shared_engine.state;
    thy_state_event event;
    if (index < 0 || index >= s->history_count ||
        !thy_state_get_event(s, s->revision - s->history_count + 1u + (unsigned)index, &event)) return 0;
    unsigned n = 0;
    while (n + 1u < sizeof state_output && n < THY_STATE_TEXT_BYTES && event.text[n]) { state_output[n] = event.text[n]; ++n; }
    state_output[n] = 0;
    return (uintptr_t)state_output;
}
int bridge_state_session(int field) {
    switch (field) {
    case 0: return shared_engine.session.system; case 1: return shared_engine.session.playing_mode;
    case 2: return shared_engine.session.transport; case 3: return shared_engine.session.capture;
    case 4: return shared_engine.session.clock; case 5: return (int)shared_engine.session.epoch;
    default: return -1;
    }
}
void bridge_state_navigation_animations(int enabled) {
    thy_studio_ui_set_navigation_animations(&shared_ui, enabled);
}
uint32_t bridge_state_scene_key(void) {
    return thy_studio_ui_scene_key(&shared_engine.state);
}
uintptr_t bridge_state_frame(int length, int din, int usb, uint32_t now, int animations) {
    char name[32];
    if (length < 0 || length > 31) return 0;
    for (int n = 0; n < length; ++n) name[n] = byte_input[n] >= 32 && byte_input[n] <= 126 ? (char)byte_input[n] : '?';
    name[length] = 0;
    shared_ui.animations_enabled = animations != 0;
    thy_studio_ui_render(&shared_ui, &shared_engine.state, name, din, usb, now, firmware_frame);
    firmware_frame_ms = now;
    firmware_frame_valid = 1u;
    return (uintptr_t)firmware_frame;
}
/* The enhanced preview consumes exactly the device companion packet. It
 * reuses the caller's latest C-rendered OLED image; it never renders again,
 * changes menu focus, advances music, or simulates physical pad input. */
static int preview_companion_visual(void *context, uint8_t oled[1024],
    uint8_t rgb[288], uint8_t center[19], uint32_t *frame_ms, uint8_t *visible)
{
    if (!firmware_frame_valid) return 0;
    uint32_t now = *(const uint32_t *)context;
    if (!bridge_firmware_colors(now)) return 0;
    for (unsigned n = 0; n < 1024u; ++n) oled[n] = firmware_frame[n];
    for (unsigned pad = 0; pad < THY_PAD_COUNT; ++pad) {
        uint16_t sequence[3];
        if (thy_engine_sequencer_color(&shared_engine, (uint8_t)pad, sequence)) {
            for (unsigned component = 0; component < 3u; ++component)
                rgb[pad * 3u + component] = (uint8_t)((sequence[component] * 8u + 2047u) / 4095u);
        } else {
            uint32_t color = firmware_colors[pad];
            rgb[pad * 3u] = (uint8_t)(color >> 8u);
            rgb[pad * 3u + 1u] = (uint8_t)(color >> 16u);
            rgb[pad * 3u + 2u] = (uint8_t)color;
        }
    }
    thy_engine_center_levels(&shared_engine, center_levels);
    for (unsigned n = 0; n < 19u; ++n)
        center[n] = (uint8_t)(((uint32_t)center_levels[n] * 255u + 2047u) / 4095u);
    *frame_ms = firmware_frame_ms;
    *visible = 1u;
    return 1;
}
uintptr_t bridge_companion_snapshot(uint32_t now) {
    thy_companion service;
    const thy_companion_identity identity = {
        "0000000000000000", "000000000000000000000000", NULL
    };
    char response[THY_COMPANION_RESPONSE_BYTES];
    thy_companion_init(&service, companion_frame, sizeof companion_frame,
        &identity, preview_companion_visual, &now);
    (void)thy_companion_command(&service, &shared_engine, 1, now,
        "companion snapshot", response, sizeof response);
    return service.active ? (uintptr_t)companion_frame : 0;
}
/* Startup presentation only. Preview replay never resets musical state,
 * routes, settings, acquisition or note ownership. */
void bridge_state_boot_begin(uint32_t now) {
    thy_studio_ui_boot_begin(&shared_ui, now);
}
int bridge_state_boot_progress(int stage) {
    if (stage < THY_STUDIO_BOOT_START || stage > THY_STUDIO_BOOT_MIDI) return -1;
    thy_studio_ui_boot_progress(&shared_ui, (uint8_t)stage);
    return 0;
}
void bridge_state_boot_ready(void) { thy_studio_ui_boot_ready(&shared_ui); }
void bridge_state_boot_skip(void) { thy_studio_ui_boot_skip(&shared_ui); }
int bridge_state_boot_info(int field) {
    switch (field) {
    case 0: return thy_studio_ui_boot_active(&shared_ui);
    case 1: return shared_ui.boot_phase;
    case 2: return shared_ui.boot_stage;
    case 3: return shared_ui.boot_ready;
    case 4: return (int)shared_ui.boot_elapsed_ms;
    case 5: return (int)shared_ui.boot_handoff_elapsed_ms;
    default: return -1;
    }
}

/* Runtime foundation ABI; C owns time conversion, transport and note lifetimes. */
static thy_generated_handle generated_output_handle;
int bridge_rhythm_configure(int tempo, int source, int numerator, int denominator, int quantize) {
    if (tempo < 0 || source < 0 || source > 1 || numerator < 1 || numerator > 16 ||
        denominator < 1 || denominator > 32 || quantize < 0 || quantize > 2)
        return THY_INVALID_ARGUMENT;
    return thy_engine_clock_configure(&shared_engine, (uint32_t)tempo,
        (uint8_t)source, (uint8_t)numerator, (uint8_t)denominator, (uint8_t)quantize);
}
int bridge_rhythm_start(void) { return thy_engine_transport_start(&shared_engine); }
int bridge_rhythm_stop(void) { return thy_engine_transport_stop(&shared_engine); }
int bridge_rhythm_toggle(int track) {
    return track >= 0 && track < (int)THY_TRANSPORT_TRACKS ?
        thy_engine_track_toggle(&shared_engine, (uint8_t)track) : THY_INVALID_ARGUMENT;
}
int bridge_rhythm_loop(int track, uint32_t start, uint32_t end) {
    return track >= 0 && track < (int)THY_TRANSPORT_TRACKS && end > start ?
        thy_engine_track_loop(&shared_engine, (uint8_t)track, start, end - start) : THY_INVALID_ARGUMENT;
}
int bridge_rhythm_midi(int status, int data1, int data2, uint32_t now_ms) {
    if (status < 0 || status > 255 || data1 < 0 || data1 > 127 || data2 < 0 || data2 > 127 ||
        (status != 0xf2 && (data1 || data2))) return THY_INVALID_ARGUMENT;
    const uint8_t message[3] = {(uint8_t)status, (uint8_t)data1, (uint8_t)data2};
    return thy_engine_receive_time(&shared_engine, message, status == 0xf2 ? 3u : 1u, now_ms);
}
uint32_t bridge_rhythm_generation(uint32_t source) {
    return thy_engine_source_generation(&shared_engine, source);
}
int bridge_rhythm_acquire(uint32_t source, uint32_t run, int note, int channel, int mask, int velocity) {
    if (note < 0 || note > 127 || !route_valid(mask, channel) || velocity < 1 || velocity > 127)
        return THY_INVALID_ARGUMENT;
    return thy_engine_generated_acquire(&shared_engine, source, run, (uint8_t)note,
        (uint8_t)channel, (uint8_t)mask, (uint8_t)velocity, &generated_output_handle);
}
uint32_t bridge_rhythm_handle(void) { return generated_output_handle; }
int bridge_rhythm_release(uint32_t handle) { return thy_core_generated_release(&engine, handle); }
int bridge_rhythm_clear(uint32_t source) { return thy_engine_generated_clear(&shared_engine, source); }

int bridge_arp_configure(int index, int enabled, int type, int grouping, int zones,
    int layer, int start, int restart, int rate, int gate, int swing, int steps,
    int pattern, int channel, int mask) {
    if (index < 0 || index > 1 || enabled < 0 || enabled > 1 || type < 0 || type > 1 ||
        grouping < 0 || grouping > 2 || zones < 0 || zones > 63 || layer < 0 || layer > 1 ||
        start < 0 || start > 1 || restart < 0 || restart > 1 || rate < 40 || rate > 3840 ||
        gate < 1 || gate > 100 || swing < 0 || swing > 50 || steps < 0 || steps > 255 ||
        pattern < 0 || pattern > 4 || !route_valid(mask, channel)) return THY_INVALID_ARGUMENT;
    thy_arp_config config = shared_engine.arps[index].config;
    config.enabled = (uint8_t)enabled; config.type = (uint8_t)type;
    config.grouping = (uint8_t)grouping; config.source_zones = (uint8_t)zones;
    config.layer = (uint8_t)layer; config.start = (uint8_t)start;
    config.restart_on_change = (uint8_t)restart; config.rate_ticks = (uint32_t)rate;
    config.gate_percent = (uint8_t)gate; config.swing_percent = (uint8_t)swing;
    config.step_mask = (uint8_t)steps; config.pattern = (uint8_t)pattern;
    config.channel = (uint8_t)channel; config.destinations = (uint8_t)mask;
    for (unsigned n = 0u; n < 8u; ++n) {
        if (note_input[n] < -24 || note_input[n] > 24) return THY_INVALID_ARGUMENT;
        config.offsets[n] = (int8_t)note_input[n];
    }
    return thy_engine_arp_configure(&shared_engine, (uint8_t)index, &config);
}
int bridge_arp_clear(int index) {
    return index >= 0 && index < 2 ? thy_engine_arp_clear(&shared_engine, (uint8_t)index) : THY_INVALID_ARGUMENT;
}
int bridge_arp_select(int index) {
    return index >= 0 && index < 2 ? thy_state_set(&shared_engine.state, THY_ROW_ARPS_TARGET, index) : THY_STATE_INVALID;
}
int bridge_arp_quick_open(int index) {
    int result = bridge_arp_select(index);
    return result == THY_STATE_OK ? thy_engine_arp_quick_open(&shared_engine) : result;
}
int bridge_arp_quick_row(int cursor) {
    return cursor >= 0 && cursor < (int)THY_ENGINE_ARP_QUICK_ROWS ? thy_engine_arp_quick_row((uint8_t)cursor) : -1;
}
int bridge_arp_quick_keys(int enabled) {
    return enabled >= 0 && enabled <= 1 ?
        thy_state_set(&shared_engine.state, THY_ROW_ARPS_QUICKKEYS, enabled) : THY_STATE_INVALID;
}
int bridge_arp_info(int index, int field) {
    if (index < 0 || index > 1) return -1;
    const thy_arp *arp = &shared_engine.arps[index];
    const thy_arp_config *config = &arp->config;
    if (field >= 32 && field < 40) return config->offsets[field - 32];
    switch (field) {
    case 0: return config->enabled; case 1: return config->type;
    case 2: return config->grouping; case 3: return config->source_zones;
    case 4: return config->layer; case 5: return config->start;
    case 6: return config->restart_on_change; case 7: return (int)config->rate_ticks;
    case 8: return config->gate_percent; case 9: return config->swing_percent;
    case 10: return config->step_mask; case 11: return config->pattern;
    case 12: return config->channel; case 13: return config->destinations;
    case 14: return arp->held_count; case 15: return arp->phrase_count;
    case 16: return arp->output_count;
    case 17: case 18: {
        int selected = -1; uint64_t order = 0u;
        for (unsigned n = 0u; n < THY_ARP_INPUTS; ++n)
            if (arp->inputs[n].held && arp->inputs[n].role &&
                (arp->inputs[n].pitches[0] || arp->inputs[n].pitches[1]) &&
                arp->inputs[n].order > order) { selected = (int)n; order = arp->inputs[n].order; }
        if (field == 17) return selected;
        unsigned phrase = config->grouping == THY_ARP_COMBINED || selected < 0 ?
            THY_ARP_POOL : (unsigned)selected;
        if (!arp->phrases[phrase].active)
            for (unsigned n = 0u; n < THY_ARP_PHRASES; ++n)
                if (arp->phrases[n].active) { phrase = n; break; }
        return arp->phrases[phrase].active && arp->phrases[phrase].ordinal ?
            (int)((arp->phrases[phrase].ordinal - 1u) % 8u) : -1;
    }
    case 19: return (int)arp->failures; case 20: return (int)arp->skipped;
    case 21: return (int)sizeof *arp; case 22: return arp->last_error;
    case 23: return (int)arp->attacks; case 24: return (int)arp->run;
    default: return -1;
    }
}
uint32_t bridge_rhythm_info(int field) {
    const thy_clock *clock = &shared_engine.musical_clock;
    const thy_transport *transport = &shared_engine.rhythm_transport;
    switch (field) {
    case 0: return clock->source; case 1: return clock->status;
    case 2: return clock->tempo_milli_bpm; case 3: return THY_CLOCK_PPQN;
    case 4: return (uint32_t)clock->tick; case 5: return (uint32_t)(clock->tick >> 32);
    case 6: return (uint32_t)transport->position; case 7: return (uint32_t)(transport->position >> 32);
    case 8: return transport->state; case 9: return clock->numerator;
    case 10: return clock->denominator; case 11: return transport->launch_quantize;
    case 12: return thy_core_generated_count(&engine); case 13: return THY_GENERATED_OWNER_COUNT;
    case 14: return clock->generation; case 15: return sizeof(thy_clock) + sizeof(thy_transport);
    case 16: return clock->now_ms; case 17: return transport->suspended;
    case 18: return clock->beat_ticks; case 19: return clock->bar_ticks;
    case 20: return clock->last_pulse_interval_ms; case 21: return clock->timeout_ms;
    default: return 0u;
    }
}
uint32_t bridge_rhythm_track(int track, int field) {
    if (track < 0 || track >= (int)THY_TRANSPORT_TRACKS) return 0u;
    const thy_transport_track *run = &shared_engine.rhythm_transport.tracks[track];
    switch (field) {
    case 0: return run->enabled; case 1: return run->running; case 2: return run->pending;
    case 3: return (uint32_t)run->pending_position; case 4: return (uint32_t)(run->pending_position >> 32);
    case 5: return run->playhead; case 6: return run->generation;
    case 7: return run->loop_start; case 8: return run->loop_length;
    default: return 0u;
    }
}

/* Song bytes and sparse events are explicit host ABI buffers. JavaScript never
 * owns a playback clock or duplicates the sequencer's editing semantics. */
uintptr_t bridge_seq_file(void) { return (uintptr_t)sequencer_file; }
uintptr_t bridge_seq_input(void) { return (uintptr_t)sequencer_input; }
uint32_t bridge_seq_info(int field) {
    const thy_sequencer *seq = &shared_engine.sequencer;
    const thy_seq_capture *capture = &seq->capture;
    const thy_sequencer_ui *ui = &shared_engine.sequencer_ui;
    switch (field) {
    case 0: return seq->selected_track; case 1: return seq->count;
    case 2: return seq->revision; case 3: return seq->voice_count;
    case 4: return seq->attacks; case 5: return seq->skipped;
    case 6: return seq->failures; case 7: return seq->last_error;
    case 8: return seq->history_valid && !seq->history_is_redo;
    case 9: return seq->history_valid && seq->history_is_redo;
    case 10: return capture->state; case 11: return capture->track;
    case 12: return capture->source; case 13: return capture->policy;
    case 14: return capture->count; case 15: return capture->growing;
    case 16: return capture->failed; case 17: return capture->region_start;
    case 18: return capture->region_end; case 19: return ui->open;
    case 20: return ui->view; case 21: return ui->layout;
    case 22: return ui->swapped; case 23: return ui->input_layout;
    case 24: return ui->window_tick; case 25: return seq->tracks[seq->selected_track].grid_ticks;
    case 26: return ui->base_pitch; case 27: return ui->region_enabled;
    case 28: return ui->region_start; case 29: return ui->region_end;
    case 30: return ui->selected_pad; case 31: return ui->last_error;
    case 32: return THY_SEQ_EVENTS; case 33: return THY_SEQ_TAKE_EVENTS;
    case 34: return THY_SEQ_SONG_MAX; case 35: return sizeof *seq;
    case 36: return shared_engine.remote_record;
    case 37: return shared_engine.remote_position;
    case 38: return shared_engine.remote_channel;
    case 39: return ui->selected_step;
    case 40: return seq->tracks[seq->selected_track].record_policy == THY_SEQ_OVERDUB;
    case 41: return ui->track_new;
    case 42: return ui->delete_pending;
    case 43: return ui->solo;
    case 44: return ui->help_open;
    case 45: return ui->help_page;
    case 46: return ui->options_open;
    case 47: return ui->options_track;
    default: return 0;
    }
}
uint32_t bridge_seq_track(int track, int field) {
    if (track < 0 || track >= (int)THY_SEQ_TRACKS) return 0;
    const thy_sequencer *seq = &shared_engine.sequencer;
    const thy_seq_track *config = &seq->tracks[track];
    switch (field) {
    case 0: return config->channel; case 1: return config->destinations;
    case 2: return config->velocity; case 3: return config->muted;
    case 4: return config->processor; case 5: return config->record_source;
    case 6: return config->record_policy; case 7: return config->content_end;
    case 8: return config->loop_start; case 9: return config->loop_end;
    case 10: return config->revision;
    case 11: return thy_sequencer_track_count(seq, (uint8_t)track);
    case 12: return config->flags;
    case 13: return config->grid_ticks;
    case 14: return thy_sequencer_track_exists(seq, (uint8_t)track);
    default: return 0;
    }
}
uint32_t bridge_seq_event(int track, int index, int field) {
    if (track < 0 || track >= (int)THY_SEQ_TRACKS || index < 0 || index >= (int)THY_SEQ_EVENTS) return 0;
    const thy_seq_event *event = thy_sequencer_event_at(&shared_engine.sequencer,
        (uint8_t)track, (uint16_t)index);
    if (event == NULL) return 0;
    switch (field) {
    case 0: return event->tick; case 1: return event->duration;
    case 2: return event->id; case 3: return event->group;
    case 4: return event->kind & 127u; case 5: return event->pitch;
    case 6: return event->value; case 7: return (event->kind & 128u) != 0;
    default: return 0;
    }
}
int bridge_seq_select(int track) {
    if (track < 0 || track >= (int)THY_SEQ_TRACKS) return THY_INVALID_ARGUMENT;
    if (shared_engine.sequencer_ui.options_open &&
        track != shared_engine.sequencer_ui.options_track) return THY_SEQ_BUSY;
    if (!thy_sequencer_track_exists(&shared_engine.sequencer, (uint8_t)track)) return THY_SEQ_NOT_FOUND;
    int result = thy_sequencer_select(&shared_engine.sequencer, (uint8_t)track);
    if (result == THY_OK) result = thy_transport_select(&shared_engine.rhythm_transport, (uint8_t)track);
    if (result == THY_OK) {
        shared_engine.sequencer_ui.track_new = 0u;
        result = thy_sequencer_ui_configure(&shared_engine.sequencer_ui,
            THY_SEQ_UI_SELECTED_STEP, shared_engine.sequencer_ui.selected_step);
    }
    return result;
}
int bridge_seq_open(int view) {
    return view >= 0 && view <= THY_SEQ_VIEW_STEPS ?
        thy_engine_sequencer_open(&shared_engine, (uint8_t)view) : THY_INVALID_ARGUMENT;
}
int bridge_seq_close(void) { return thy_engine_sequencer_close(&shared_engine); }
int bridge_seq_action(int command, int track) {
    return command >= 0 && command <= THY_SEQ_UI_OCTAVE_UP && track >= 0 && track < (int)THY_SEQ_TRACKS ?
        thy_engine_sequencer_action(&shared_engine, (uint8_t)command, (uint8_t)track) : THY_INVALID_ARGUMENT;
}
int bridge_seq_record(void) { return thy_engine_sequencer_record(&shared_engine); }
int bridge_seq_finish(int immediate) {
    return immediate >= 0 && immediate <= 1 ?
        thy_engine_sequencer_finish(&shared_engine, immediate) : THY_INVALID_ARGUMENT;
}
int bridge_seq_configure(int track, int channel, int mask, int velocity,
    int muted, int processor, int source, int policy, int flags) {
    if (track < 0 || track >= (int)THY_SEQ_TRACKS || !route_valid(mask, channel) ||
        velocity < 0 || velocity > 127 || muted < 0 || muted > 1 ||
        processor < 0 || processor > 2 || source < 0 || source > 5 || policy < 0 || policy > 1 || flags < 0 || flags > 255)
        return THY_INVALID_ARGUMENT;
    thy_seq_track config = shared_engine.sequencer.tracks[track];
    config.channel = (uint8_t)channel; config.destinations = (uint8_t)mask;
    config.velocity = (uint8_t)velocity; config.muted = (uint8_t)muted;
    config.processor = (uint8_t)processor; config.record_source = (uint8_t)source;
    config.record_policy = (uint8_t)policy;
    config.flags = (uint8_t)flags;
    return thy_engine_sequencer_track_configure(&shared_engine, (uint8_t)track, &config);
}
int bridge_seq_view_configure(int field, uint32_t value) {
    return field >= 0 && field <= THY_SEQ_UI_STEP_COUNT ? thy_sequencer_ui_configure(&shared_engine.sequencer_ui, (uint8_t)field, value) : THY_INVALID_ARGUMENT;
}
int bridge_seq_loop(int track, uint32_t start, uint32_t end) {
    return track >= 0 && track < (int)THY_SEQ_TRACKS ?
        thy_engine_sequencer_loop(&shared_engine, (uint8_t)track, start, end) : THY_INVALID_ARGUMENT;
}
int bridge_seq_remote_configure(int record, int position, int channel) {
    if (record < 0 || record > 1 || position < 0 || position > 1 || channel < 0 || channel > 16)
        return THY_INVALID_ARGUMENT;
    int result = thy_state_set(&shared_engine.state, THY_ROW_MIDI_REMOTECHANNEL, channel);
    if (result == THY_STATE_OK) result = thy_state_set(&shared_engine.state, THY_ROW_MIDI_REMOTERECORD, record);
    if (result == THY_STATE_OK) result = thy_state_set(&shared_engine.state, THY_ROW_MIDI_REMOTEPOSITION, position);
    return result == THY_STATE_OK ? THY_OK : THY_INVALID_ARGUMENT;
}
int bridge_seq_add(int track, int count) {
    if (track < 0 || track >= (int)THY_SEQ_TRACKS || count <= 0 || count > 64) return THY_INVALID_ARGUMENT;
    int result = thy_sequencer_add(&shared_engine.sequencer, (uint8_t)track, sequencer_input, (uint16_t)count);
    if (result == THY_OK) thy_engine_sequencer_changed(&shared_engine, (uint8_t)track);
    return result;
}
int bridge_seq_remove(int id) {
    if (id <= 0 || id > 65535) return THY_INVALID_ARGUMENT;
    const thy_seq_event *event = thy_sequencer_find(&shared_engine.sequencer, (uint16_t)id);
    if (event == NULL) return THY_SEQ_NOT_FOUND;
    uint8_t track = event->track;
    int result = thy_sequencer_remove(&shared_engine.sequencer, (uint16_t)id);
    if (result == THY_OK) thy_engine_sequencer_changed(&shared_engine, track);
    return result;
}
int bridge_seq_update(int id, uint32_t tick, uint32_t duration, int pitch, int velocity) {
    if (id <= 0 || id > 65535 || pitch < 0 || pitch > 127 || velocity < 0 || velocity > 127) return THY_INVALID_ARGUMENT;
    const thy_seq_event *event = thy_sequencer_find(&shared_engine.sequencer, (uint16_t)id);
    if (event == NULL) return THY_SEQ_NOT_FOUND;
    uint8_t track = event->track;
    int result = thy_sequencer_update(&shared_engine.sequencer, (uint16_t)id, tick, duration, (uint8_t)pitch, (uint8_t)velocity);
    if (result == THY_OK) thy_engine_sequencer_changed(&shared_engine, track);
    return result;
}
int bridge_seq_clear(int track) {
    if (track < 0 || track >= (int)THY_SEQ_TRACKS) return THY_INVALID_ARGUMENT;
    int result = thy_sequencer_clear(&shared_engine.sequencer, (uint8_t)track);
    if (result == THY_OK) thy_engine_sequencer_changed(&shared_engine, (uint8_t)track);
    return result;
}
int bridge_seq_duplicate(int track, uint32_t start, uint32_t end, uint32_t destination, int extend) {
    return track >= 0 && track < (int)THY_SEQ_TRACKS && extend >= 0 && extend <= 1 ?
        thy_sequencer_duplicate(&shared_engine.sequencer, (uint8_t)track, start, end, destination, extend) : THY_INVALID_ARGUMENT;
}
int bridge_seq_trim(int track, uint32_t start, uint32_t end) {
    if (track < 0 || track >= (int)THY_SEQ_TRACKS) return THY_INVALID_ARGUMENT;
    int result = thy_sequencer_trim(&shared_engine.sequencer, (uint8_t)track, start, end);
    if (result == THY_OK) thy_engine_sequencer_changed(&shared_engine, (uint8_t)track);
    return result;
}
int bridge_seq_history(int redo) {
    return redo >= 0 && redo <= 1 ? thy_sequencer_history(&shared_engine.sequencer,
        &engine, &shared_engine.rhythm_transport, redo) : THY_INVALID_ARGUMENT;
}
int bridge_seq_song_validate(int length) {
    return length > 0 && length <= (int)sizeof sequencer_file ?
        thy_sequencer_validate(sequencer_file, (size_t)length) : THY_SEQ_CORRUPT;
}
int bridge_seq_song_encode(void) {
    size_t length = 0;
    return thy_sequencer_encode(&shared_engine.sequencer, sequencer_file,
        sizeof sequencer_file, &length) == THY_OK ? (int)length : 0;
}
int bridge_seq_song_decode(int length) {
    return length > 0 && length <= (int)sizeof sequencer_file ?
        thy_engine_sequencer_song_import(&shared_engine, sequencer_file, (size_t)length) : THY_SEQ_CORRUPT;
}
uint32_t bridge_seq_pad(int pad, int field) {
    if (pad < 0 || pad >= 96) return 0;
    const thy_seq_pad_gesture *gesture = &shared_engine.sequencer_ui.pads[pad];
    uint8_t track = 0, pitch = 0; uint32_t tick = 0;
    int role = thy_sequencer_ui_pad_role(&shared_engine.sequencer_ui, (uint8_t)pad, &track, &tick, &pitch);
    uint16_t color[3] = {0, 0, 0};
    switch (field) {
    case 0: return (uint32_t)role; case 1: return gesture->held;
    case 2: return gesture->event_id; case 3: return tick;
    case 4: return pitch;
    case 5: return (uint32_t)thy_sequencer_ui_color(&shared_engine.sequencer_ui, (uint8_t)pad, color);
    case 6: case 7: case 8:
        (void)thy_sequencer_ui_color(&shared_engine.sequencer_ui, (uint8_t)pad, color);
        return color[field - 6];
    case 9: return track;
    default: return 0;
    }
}
int bridge_seq_preview(int pad) {
    preview_count = 0;
    if (pad < 0 || pad >= 96) return THY_INVALID_ARGUMENT;
    uint8_t track, pitch; uint32_t tick;
    int role = thy_sequencer_ui_pad_role(&shared_engine.sequencer_ui,
        (uint8_t)pad, &track, &tick, &pitch);
    if (role != THY_SEQ_PAD_INPUT_NOTES && role != THY_SEQ_PAD_INPUT_CHORDS)
        return THY_INVALID_ARGUMENT;
    uint8_t source = (uint8_t)(pad % 48 + (role == THY_SEQ_PAD_INPUT_NOTES ? 48 : 0));
    return thy_instrument_preview_source(&firmware, source, note_preview, &preview_count);
}
uintptr_t bridge_seq_colors(void) {
    if (!shared_engine.sequencer_ui.open) return 0;
    for (unsigned pad = 0; pad < 96u; ++pad) {
        uint16_t *entry = sequencer_colors + pad * 4u;
        entry[1] = entry[2] = entry[3] = 0;
        entry[0] = (uint16_t)thy_engine_sequencer_color(&shared_engine, (uint8_t)pad, entry + 1);
    }
    return (uintptr_t)sequencer_colors;
}


int bridge_expression_axes(int pad,int pressure,int aftertouch)
{
    return pad>=0&&pad<96&&pressure>=0&&pressure<=127&&aftertouch>=0&&aftertouch<=127?
        thy_engine_expression_pressure_axes(&shared_engine,(uint8_t)pad,(uint8_t)pressure,(uint8_t)aftertouch):THY_INVALID_ARGUMENT;
}
int bridge_expression_usb_reset(uint32_t now)
{return thy_engine_usb_reset(&shared_engine,now);}
int bridge_expression_profile(int scope,int field)
{
    thy_expression_profile p;uint8_t custom=0;
    if(scope==-1||scope==-2)p=firmware.expression_profiles[scope==-2];
    else if(scope<0||scope>=96||thy_instrument_get_expression_profile(&firmware,(uint8_t)scope,&p,&custom)!=THY_OK)return -1;
    switch(field){case 0:return p.enabled;case 1:return p.split;case 2:return p.pressure_curve;case 3:return p.aftertouch_curve;case 4:return custom;default:return -1;}
}
int bridge_set_expression_profile(int scope,int enabled,int split,int pressure_curve,int aftertouch_curve)
{
    if(scope< -2||scope>=96||enabled<0||enabled>1||split<10||split>90||pressure_curve<0||pressure_curve>2||aftertouch_curve<0||aftertouch_curve>2)return THY_INVALID_ARGUMENT;
    thy_expression_profile p={(uint8_t)enabled,(uint8_t)split,(uint8_t)pressure_curve,(uint8_t)aftertouch_curve};
    return scope<0?thy_instrument_set_cluster_expression_profile(&firmware,(uint8_t)(scope==-2),&p):
        thy_instrument_set_pad_expression_profile(&firmware,(uint8_t)scope,&p);
}
int bridge_clear_expression_profile(int pad)
{return pad>=0&&pad<96?thy_instrument_clear_expression_profile(&firmware,(uint8_t)pad):THY_INVALID_ARGUMENT;}
int bridge_expression_profile_axes(int scope,int raw)
{
    if(scope< -2||scope>=96||raw<0||raw>4095)return -1;
    thy_expression_profile p;thy_instrument_response response;
    if(scope<0){p=firmware.expression_profiles[scope==-2];response=firmware.cluster_response[scope==-2];}
    else {
        uint8_t pressure=0,at=0;
        return thy_instrument_expression_axes(&firmware,(uint8_t)scope,(uint16_t)raw,&pressure,&at)==THY_OK?
            (int)((unsigned)pressure*256u+at):-1;
    }
    uint8_t pressure=0,at=0;
    return thy_expression_profile_axes(&p,(uint16_t)raw,response.pressure_floor,response.pressure_full_scale,response.pressure_curve,&pressure,&at)?-1:
        (int)((unsigned)pressure*256u+at);
}
int bridge_channel_range(int destination,int channel)
{
    if(destination<0||destination>2||channel<0||channel>15)return 2;
    for(unsigned logical=0;logical<96u;++logical){uint8_t target=thy_instrument_route_target(&firmware,(uint8_t)logical);
        thy_instrument_route route;if(thy_instrument_get_pad_route(&firmware,target,&route)==THY_OK&&route.channel==channel&&
        (route.destinations&(1u<<destination))&&shared_engine.channel_routes[target].bend)return shared_engine.channel_routes[target].range;}
    return 2;
}

static size_t library_record_size(int kind)
{return kind==0?sizeof(thy_library_scale):kind==1?sizeof(thy_library_chord):kind==2?sizeof(thy_library_six):0u;}
int bridge_library_record(int kind,int slot,int byte)
{
    unsigned limit=kind==0?8u:kind==1?24u:kind==2?3u:0u;
    if(slot<0||(unsigned)slot>=limit||byte<0||(size_t)byte>=library_record_size(kind))return -1;
    const uint8_t *record=kind==0?(const void *)&firmware.library.scales[slot]:
        kind==1?(const void *)&firmware.library.chords[slot]:(const void *)&firmware.library.six[slot];
    return record[byte];
}
int bridge_library_set(int kind,int slot,int length)
{
    return kind>=0&&kind<3&&slot>=0&&slot<24&&length==(int)library_record_size(kind)?
        thy_engine_library_set(&shared_engine,(uint8_t)kind,(uint8_t)slot,byte_input,(size_t)length):THY_INVALID_ARGUMENT;
}
int bridge_library_delete(int kind,int slot)
{return kind>=0&&kind<3&&slot>=0&&slot<24?thy_engine_library_delete(&shared_engine,(uint8_t)kind,(uint8_t)slot):THY_INVALID_ARGUMENT;}
int bridge_library_capture(int kind,int slot)
{
    if(kind<0||kind>2||slot<0||slot>23)return -THY_INVALID_ARGUMENT;
    thy_library_ui_draft draft;int r=thy_engine_library_capture(&shared_engine,(uint8_t)kind,(uint8_t)slot,&draft);
    if(r!=THY_OK)return -r;
    const uint8_t *raw=(const void *)&draft;size_t bytes=library_record_size(kind);
    for(size_t n=0;n<bytes;++n)byte_input[n]=raw[n];return (int)bytes;
}
uintptr_t bridge_library_buffer(void){return (uintptr_t)library_file;}
int bridge_library_export(void)
{size_t count=0;return thy_library_encode(&firmware.library,library_file,sizeof library_file,&count)==THY_LIBRARY_OK?(int)count:0;}
int bridge_library_import(int length)
{return length==(int)sizeof library_file?thy_engine_library_import(&shared_engine,library_file,sizeof library_file):THY_INVALID_ARGUMENT;}
int bridge_library_open(int kind,int slot)
{
    if(kind<0||kind>2||slot<0||slot>23)return THY_INVALID_ARGUMENT;
    int r=thy_engine_library_open(&shared_engine,(uint8_t)kind,(uint8_t)slot);
    return r==THY_STATE_OK?THY_OK:r==THY_STATE_BLOCKED?THY_ENGINE_LIBRARY_BUSY:THY_INVALID_ARGUMENT;
}

/* Advisory scratch belongs to this host adapter, never device/settings state. */
static thy_chord_suggestion guidance_chord_rows[THY_GUIDANCE_LIMIT];
static thy_scale_match guidance_scale_rows[THY_GUIDANCE_LIMIT];
static thy_modulation guidance_modulation;
static size_t guidance_chord_count,guidance_scale_count,guidance_scale_total;
static char guidance_label[THY_HARMONY_TEXT_BYTES];
int bridge_guidance_scale_count(void){return (int)thy_guidance_scale_count();}
uintptr_t bridge_guidance_scale_name(int family)
{return (uintptr_t)(family<0?NULL:thy_guidance_scale_name((unsigned)family));}
int bridge_guidance_scale_mask(int family,int root)
{return family<0||root<0?0:thy_guidance_scale_mask((unsigned)family,(unsigned)root);}
int bridge_guidance_chords(int key,int root,int current)
{
    guidance_chord_count=0u;
    if(key<1||key>4095||root<0||root>11||current<0||current>4095)return -1;
    return thy_guidance_chords((uint16_t)key,(uint8_t)root,(uint16_t)current,
        guidance_chord_rows,THY_GUIDANCE_LIMIT,&guidance_chord_count)==THY_HARMONY_OK?(int)guidance_chord_count:-1;
}
int bridge_guidance_chord_value(int row,int field)
{
    if(row<0||(size_t)row>=guidance_chord_count)return -1;
    const thy_chord_suggestion *r=&guidance_chord_rows[row];
    return field==0?r->mask:field==1?r->root:field==2?r->shared:field==3?r->score:-1;
}
int bridge_guidance_scales(int mask)
{
    guidance_scale_count=guidance_scale_total=0u;
    if(mask<1||mask>4095)return -1;
    return thy_guidance_scales((uint16_t)mask,guidance_scale_rows,THY_GUIDANCE_LIMIT,
        &guidance_scale_count,&guidance_scale_total)==THY_HARMONY_OK?(int)guidance_scale_count:-1;
}
int bridge_guidance_scale_total(void){return (int)guidance_scale_total;}
int bridge_guidance_scale_value(int row,int field)
{
    if(row<0||(size_t)row>=guidance_scale_count)return -1;
    const thy_scale_match *r=&guidance_scale_rows[row];
    return field==0?r->mask:field==1?r->family:field==2?r->root:field==3?r->extra:-1;
}
int bridge_guidance_modulation(int source,int target,int current)
{
    if(source<1||source>4095||target<1||target>4095||current<0||current>4095)return -1;
    return thy_guidance_modulation((uint16_t)source,(uint16_t)target,(uint16_t)current,&guidance_modulation);
}
int bridge_guidance_modulation_value(int field)
{
    const thy_modulation *r=&guidance_modulation;
    return field==0?r->common:field==1?r->added:field==2?r->removed:field==3?r->pivot:
        field==4?r->pivot_root:field==5?r->current_fits:-1;
}
uintptr_t bridge_guidance_label(int mask,int root)
{
    if(mask<0||mask>4095||root<0||(root>11&&root!=255))return 0;
    int16_t notes[12];size_t count=0;thy_harmony harmony;
    for(unsigned n=0;n<12u;++n){unsigned pc=(n+(root==255?0u:(unsigned)root))%12u;
        if(mask&(1u<<pc))notes[count++]=(int16_t)(60u+n+(root==255?0u:(unsigned)root));}
    if(thy_harmony_from_notes(notes,count,(uint8_t)root,&harmony)!=THY_HARMONY_OK ||
        thy_harmony_format(&harmony,guidance_label,sizeof guidance_label)!=THY_HARMONY_OK)return 0;
    return (uintptr_t)guidance_label;
}
