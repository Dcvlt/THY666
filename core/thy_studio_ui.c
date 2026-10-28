/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_studio_ui.h"
#include "thy_harmony.h"
#include "thy_engine.h"
#include "thy_settings.h"
#include "thy_calibration.h"
#include "thy_brand_logo.h"

static unsigned append(char *out, unsigned used, unsigned capacity,
    const char *value)
{
    if (value != NULL)
        while (*value && used + 1u < capacity) out[used++] = *value++;
    out[used] = 0;
    return used;
}

static unsigned number(char *out, unsigned used, unsigned capacity,
    unsigned value)
{
    char reversed[10];
    unsigned count = 0;
    do { reversed[count++] = (char)('0' + value % 10u); value /= 10u; }
    while (value != 0u);
    while (count && used + 1u < capacity) out[used++] = reversed[--count];
    out[used] = 0;
    return used;
}

static unsigned window_start(unsigned cursor, unsigned count)
{
    unsigned start = cursor > 2u ? cursor - 2u : 0u;
    if (count > 5u && start > count - 5u) start = count - 5u;
    return count > 5u ? start : 0u;
}

static const char *notice(const thy_state *state)
{
    /* Results can remain visible until the next input. Always retain the
     * one-touch route to Play rather than replacing the navigation legend. */
    switch (state->notice) {
    case THY_NOTICE_APPLIED: return "APPLIED  O EXIT";
    case THY_NOTICE_DRAFT: return "CONFIG ONLY  O EXIT";
    case THY_NOTICE_INVALID: return "INVALID  O EXIT";
    case THY_NOTICE_UNAVAILABLE: return "UNAVAILABLE  O EXIT";
    case THY_NOTICE_ADAPTER: return "RELEASE PADS RETRY O EXIT";
    case THY_NOTICE_CANCELLED: return "CANCELLED  O EXIT";
    case THY_NOTICE_RANGE: return "OUT OF RANGE  O EXIT";
    case THY_NOTICE_REMOTE_RANGE: return "LOW NOTE > HIGH  O EXIT";
    case THY_NOTICE_PROGRAM_TRIGGER: return "PC NEEDS PRESS  O EXIT";
    case THY_NOTICE_SENSITIVITY: return "CHECK LIMITS  O EXIT";
    case THY_NOTICE_TEXT: return "INVALID TEXT  O EXIT";
    case THY_NOTICE_RELEASE: return "RELEASE ALL PADS  O EXIT";
    case THY_NOTICE_HOLD_ONLY: return "HOLD ONLY / NO LATCH";
    default: return "O EXIT  < BACK";
    }
}

static int equal(const char *left, const char *right)
{
    while (*left && *left == *right) { ++left; ++right; }
    return *left == *right;
}

static int engine_information_row(uint16_t row)
{
    return row == THY_ROW_ENGINES_DRUMS;
}

uint32_t thy_studio_ui_scene_key(const thy_state *state)
{
    if (state == NULL) return UINT32_MAX;
    if(state->reset_open)return UINT32_C(0xE0000)|state->reset_group|((uint32_t)state->reset_track<<8u);
    if(state->instrument&&state->instrument->guidance.open)
        return UINT32_C(0xC0000)|state->instrument->guidance.page;
    if(state->instrument&&state->instrument->library_editor.open)
        return UINT32_C(0xB0000)|((uint32_t)state->instrument->library_editor.kind<<8u)|state->instrument->library_editor.page;
    if(state->engine && state->engine->calibration_open && state->engine->calibration)
        return UINT32_C(0xA0000);
    if (state->engine && state->engine->sequencer_ui.open &&
        state->engine->sequencer_ui.options_open && state->level == THY_STATE_ROWS)
        return state->info_open ? 0x90000u | thy_state_current_row(state) :
            0x80000u | state->engine->sequencer_ui.options_track;
    if (state->engine && state->engine->sequencer_ui.open && state->level == THY_STATE_PLAY)
        return thy_sequencer_ui_scene_key(&state->engine->sequencer_ui);
    unsigned quick = state->engine && state->engine->arp_quick_open &&
        state->level == THY_STATE_PLAY;
    if (state->level == THY_STATE_PLAY && !quick && !state->about_open &&
        thy_engine_harmony_active(state->engine)) {
        unsigned page=state->engine->harmony.page;
        unsigned view=page==THY_HARMONY_CHORDS?1u:0u;
        return UINT32_C(0xD0000) | page | (view<<8u);
    }
    unsigned kind = quick ? 5u : state->about_open ? 4u :
        state->info_open ? 3u : state->level;
    unsigned identity = kind == 3u ? thy_state_current_row(state) :
        kind == THY_STATE_ROWS ? state->group_index :
        kind == THY_STATE_PLAY && state->instrument ?
            state->instrument->display_view : 0u;
    return (uint32_t)(kind << 16u) | identity;
}

static void compact_value(const thy_state *state, uint16_t row_index,
    int editing, char out[32])
{
    static const struct { const char *key, *label; } routes[] = {
        {"usb1-din", "U1+DIN"}, {"usb2-din", "U2+DIN"},
        {"both-usb", "U1+U2"}, {"both-usb-din", "U1U2+DIN"},
        {"usb1", "USB1"}, {"usb2", "USB2"}, {"din", "DIN"}
    };
    const thy_menu_row *row = &thy_menu_rows[row_index];
    /* Information is prose, not a value selector. RIGHT opens its complete
     * text in a separate page instead of a misleading eight-glyph
     * fragment such as "AUTOMATI". */
    if (row->type == THY_MENU_INFO) {
        (void)append(out, 0, 32, engine_information_row(row_index) ? "Open" : "INFO");
        return;
    }
    (void)thy_state_format(state, row_index, editing, out, 32);
    if (row->type != THY_MENU_SELECT || row->dynamic) return;
    int32_t value;
    if (editing && state->edit_row == row_index) value = state->edit_value;
    else if (thy_state_get(state, row_index, &value) != THY_STATE_OK) return;
    if (value < 0 || value >= row->option_count) return;
    const char *key = thy_menu_options[row->option_first + value].key;
    /* Distinguish every approved destination set inside the eight-character
     * OLED value column. Truncating "USB 1 + ..." would hide the DIN choice. */
    for (unsigned i = 0; i < sizeof routes / sizeof routes[0]; ++i)
        if (equal(key, routes[i].key)) {
            (void)append(out, 0, 32, routes[i].label);
            return;
        }
}

void thy_studio_ui_format(const thy_state *state, uint16_t row,
    int use_edit, char out[32])
{
    if (out != NULL && state != NULL && row < THY_MENU_ROW_COUNT)
        compact_value(state, row, use_edit, out);
}

static uint8_t sensitivity_instance(const thy_state *state)
{
    if(state->editing && thy_menu_rows[state->edit_row].instance_kind==THY_MENU_PAD)
        return state->edit_instance;
    if(state->engine && state->engine->sensitivity_target==97u)return 97u;
    return state->engine && state->engine->sensitivity_target?
        (uint8_t)(state->engine->sensitivity_target-1u):96u;
}

static void sensitivity_graph(uint8_t *out,const thy_state *state,int overrides_only)
{
    const thy_engine *e=state->engine;
    if(!e || !state->instrument)return;
    const thy_instrument *i=state->instrument;
    uint8_t instance=sensitivity_instance(state),side=instance==97u || (instance<96u && instance>=48u)?1u:0u;
    int electrical=thy_engine_sensitivity_electrical(e,instance);
    if(electrical == -2)return;
    thy_instrument_response pressure;
    thy_instrument_velocity strike;
    uint8_t pressure_override=0u,velocity_override=0u;
    if(electrical<0){
        if(thy_instrument_get_cluster_response(i,side,&pressure)!=THY_OK ||
            thy_instrument_get_cluster_velocity(i,side,&strike)!=THY_OK)return;
    }
    else if(thy_instrument_get_pad_response(i,(uint8_t)electrical,&pressure,&pressure_override)!=THY_OK)return;
    if(electrical>=0 && thy_instrument_get_velocity(i,electrical,&strike,&velocity_override)!=THY_OK)return;
    if(overrides_only) {
        char text[32];unsigned used;
        if(electrical<0) {
            unsigned pc=0u,vc=0u;
            for(unsigned p=0;p<96u;++p)if((i->mapping[p]>=48u)==(side!=0u)){
                pc+=!!i->sensors[p].response_overridden;vc+=!!i->velocity_overridden[p];
            }
            thy_ui_draw_text(out,2u,13u,side?"RIGHT: CLUSTER DEFAULTS":"LEFT: CLUSTER DEFAULTS",25u,0);
            used=append(text,0u,sizeof text,"AT OVERRIDES ");used=number(text,used,sizeof text,pc);(void)append(text,used,sizeof text,"/48");
            thy_ui_draw_text(out,2u,23u,text,25u,0);
            used=append(text,0u,sizeof text,"STRIKE OVERRIDES ");used=number(text,used,sizeof text,vc);(void)append(text,used,sizeof text,"/48");
            thy_ui_draw_text(out,2u,33u,text,25u,0);
            thy_ui_draw_text(out,2u,43u,"HALF KEEPS OVERRIDES",25u,0);
        } else {
            thy_ui_draw_text(out,2u,13u,pressure_override?"AFTERTOUCH: PAD OVERRIDE":"AFTERTOUCH: USES HALF",25u,0);
            thy_ui_draw_text(out,2u,23u,velocity_override?"STRIKE: PAD OVERRIDE":"STRIKE: USES HALF",25u,0);
            thy_ui_draw_text(out,2u,33u,"EDITS KEEP OTHER PADS",25u,0);
            thy_ui_draw_text(out,2u,43u,"USE HALF CLEARS BOTH",25u,0);
        }
        return;
    }
    thy_ui_draw_text(out,2u,13u,"AFTERTOUCH",12u,0);
    thy_ui_draw_text(out,68u,13u,strike.mode==THY_VELOCITY_STRIKE?"STRIKE":"FIXED",11u,0);
    uint8_t fixed=i->fixed_velocity;
    {
        thy_instrument_route route;
        uint8_t logical=electrical>=0?instance:(uint8_t)(side*48u);
        if(thy_instrument_get_pad_route(i,thy_instrument_route_target(i,logical),&route)==THY_OK)
            fixed=route.velocity;
    }
    for(unsigned graph=0;graph<2u;++graph) {
        unsigned x0=graph?68u:3u;
        thy_ui_draw_rectangle(out,x0,21u,1u,23u,1);
        thy_ui_draw_rectangle(out,x0,43u,56u,1u,1);
        unsigned previous=43u;
        for(unsigned x=0;x<55u;++x) {
            unsigned value=graph?(strike.mode==THY_VELOCITY_STRIKE?
                thy_instrument_velocity_value(&strike,(uint16_t)(x*strike.hard_rate/54u)):fixed):
                thy_instrument_response_pressure(&pressure,(uint16_t)(x*4095u/54u));
            unsigned y=42u-value*20u/127u;
            if(!x)previous=y;
            unsigned first=y<previous?y:previous,last=y>previous?y:previous;
            thy_ui_draw_rectangle(out,x0+1u+x,first,1u,last-first+1u,1);previous=y;
        }
    }
    thy_ui_draw_text(out,2u,47u,pressure_override?"AT:PAD":side?"AT:RIGHT":"AT:LEFT",11u,0);
    thy_ui_draw_text(out,68u,47u,velocity_override?"V:PAD":side?"V:RIGHT":"V:LEFT",11u,0);
}

static void storage_information(uint8_t *out,const thy_state *state)
{
    if(!state->engine)return;
    size_t used=THY_SETTINGS_HEADER_BYTES+THY_SETTINGS_V9_PARAMETER_BYTES+
        thy_sequencer_song_size(&state->engine->sequencer);
    const size_t capacity=THY_SETTINGS_RECORD_BYTES;
    char text[32];unsigned at=append(text,0u,sizeof text,"STATE ");
    at=number(text,at,sizeof text,(unsigned)used);at=append(text,at,sizeof text,"/");
    at=number(text,at,sizeof text,(unsigned)capacity);at=append(text,at,sizeof text,"B ");
    at=number(text,at,sizeof text,(unsigned)(100u*used/capacity));(void)append(text,at,sizeof text,"%");
    thy_ui_draw_text(out,2u,13u,text,25u,0);
    thy_ui_draw_rectangle(out,2u,20u,124u,3u,1);thy_ui_draw_rectangle(out,3u,21u,122u,1u,0);
    unsigned width=used<capacity?(unsigned)(122u*used/capacity):122u;
    if(width)thy_ui_draw_rectangle(out,3u,21u,width,1u,1);
    unsigned count=state->engine->sequencer.count;
    at=append(text,0u,sizeof text,"SONG ");at=number(text,at,sizeof text,count);at=append(text,at,sizeof text,"/");
    at=number(text,at,sizeof text,THY_SEQ_EVENTS);at=append(text,at,sizeof text," EVENTS ");
    at=number(text,at,sizeof text,100u*count/THY_SEQ_EVENTS);(void)append(text,at,sizeof text,"%");
    thy_ui_draw_text(out,2u,26u,text,25u,0);
    thy_ui_draw_rectangle(out,2u,33u,124u,3u,1);thy_ui_draw_rectangle(out,3u,34u,122u,1u,0);
    width=count<THY_SEQ_EVENTS?122u*count/THY_SEQ_EVENTS:122u;
    if(width)thy_ui_draw_rectangle(out,3u,34u,width,1u,1);
    thy_ui_draw_text(out,2u,39u,"JOURNAL 256KiB ROTATING",25u,0);
    thy_ui_draw_text(out,2u,47u,"COLLECTIONS NOT ALLOCATED",25u,0);
}

static void calibration_map(uint8_t *out,const thy_calibration *w,unsigned selected)
{
    for(unsigned p=0;p<96u;++p){unsigned local=p%48u;
        if(p<w->scope_first || p>=w->scope_first+w->scope_count)continue;
        unsigned x=(p<48u?3u:75u)+(local/4u)*4u,y=11u+(local%4u)*3u;
        if(p==selected){
            thy_ui_draw_rectangle(out,x,y,3u,3u,1);
            if(!w->contact_active)thy_ui_draw_pixel(out,x+1u,y+1u,0);
        } else if(w->review_ready && thy_calibration_outlier(w,(uint8_t)p)){
            thy_ui_draw_rectangle(out,x,y,3u,3u,1);thy_ui_draw_pixel(out,x+1u,y+1u,0);
        } else if(thy_calibration_completed(w,(uint8_t)p))thy_ui_draw_rectangle(out,x,y,2u,2u,1);
        else thy_ui_draw_pixel(out,x+1u,y+1u,1);
    }
}

static const char *calibration_action_label(unsigned action)
{
    /* Old Before/After/Retry API IDs remain aliases, not extra controls. */
    static const char *const labels[]={"LEARN","COMPARE","COMPARE","SAVE","LEARN","CANCEL"};
    return action<THY_CALIBRATION_ACTION_COUNT?labels[action]:"LEARN";
}

static void calibration_capture_actions(uint8_t *out,unsigned cursor)
{
    static const uint8_t actions[]={THY_CALIBRATION_LEARN,THY_CALIBRATION_CANCEL};
    if(cursor==THY_CALIBRATION_RETRY_PAD)cursor=THY_CALIBRATION_LEARN;
    for(unsigned n=0;n<2u;++n){unsigned x=2u+n*64u,y=44u;
        int active=actions[n]==cursor;
        if(active)thy_ui_draw_rectangle(out,x,y-1u,60u,7u,1);
        thy_ui_draw_text(out,x+2u,y,calibration_action_label(actions[n]),11u,active);
    }
}

static void calibration_review_page(uint8_t *out,const thy_engine *e,const thy_calibration *w)
{
    char text[32];unsigned used=append(text,0u,sizeof text,"CHECK ");
    (void)number(text,used,sizeof text,w->review_outlier_count);
    thy_ui_draw_text(out,2u,1u,w->scope_first?"RIGHT 48/48":"LEFT 48/48",15u,0);
    thy_ui_draw_text(out,86u,1u,text,8u,0);
    thy_ui_draw_rectangle(out,0u,8u,128u,1u,1);calibration_map(out,w,w->selected);
    for(unsigned row=0u;row<4u;++row){
        unsigned y=25u+row*8u;int selected=row==e->calibration_review_cursor;
        used=0u;
        if(!row){used=append(text,0u,sizeof text,"RESPONSE ");
            (void)append(text,used,sizeof text,w->state==THY_CALIBRATION_PREVIEW_ORIGINAL?"SAVED":"CALIBRATED");
        }else if(row==1u){used=append(text,0u,sizeof text,"SENSITIVITY ");
            used=number(text,used,sizeof text,e->calibration_review_editing?e->calibration_review_draft:w->preview_percent);
            (void)append(text,used,sizeof text,"%");
        }else (void)append(text,0u,sizeof text,row==2u?"APPLY":"DISCARD");
        if(selected){thy_ui_draw_rectangle(out,1u,y-1u,126u,7u,1);thy_ui_draw_text(out,2u,y,">",1u,1);}
        thy_ui_draw_text(out,10u,y,text,23u,selected);
    }
    thy_ui_draw_rectangle(out,0u,55u,128u,1u,1);
    if(w->reason)thy_ui_draw_text(out,2u,58u,thy_calibration_reason_text(w->reason),25u,0);
    else if(e->calibration_review_editing)thy_ui_draw_text(out,2u,58u,"UP/DN >SET <CANCEL",25u,0);
    else if(e->calibration_review_cursor==1u)thy_ui_draw_text(out,2u,58u,"RIGHT ADJUST SENSITIVITY",25u,0);
    else if(e->calibration_review_cursor>1u)thy_ui_draw_text(out,2u,58u,"RELEASE PADS >DO",25u,0);
    else {
        used=append(text,0u,sizeof text,w->selected<48u?"L R":"R R");
        used=number(text,used,sizeof text,w->selected%4u+1u);used=append(text,used,sizeof text," C");
        used=number(text,used,sizeof text,(w->selected%48u)/4u+1u);used=append(text,used,sizeof text," ");
        if(thy_calibration_leak_outlier(w,w->selected))(void)append(text,used,sizeof text,"NEARBY RESPONSE");
        else {used=number(text,used,sizeof text,thy_calibration_strength_percent(w,w->selected));
            (void)append(text,used,sizeof text,"% MEDIAN");}
        thy_ui_draw_text(out,2u,58u,text,25u,0);
    }
}

static void calibration_page(uint8_t *out,const thy_state *state)
{
    const thy_engine *e=state->engine;const thy_calibration *w=e->calibration;
    if(w->scope_count && w->completed_count==w->scope_count && w->state>=THY_CALIBRATION_READY && w->state<=THY_CALIBRATION_APPLIED){
        calibration_review_page(out,e,w);return;
    }
    unsigned selected=w->target;
    char text[32];unsigned used=append(text,0u,sizeof text,"CAL ");
    used=append(text,used,sizeof text,selected<48u?"L":"R");
    used=append(text,used,sizeof text," R");used=number(text,used,sizeof text,selected%4u+1u);
    used=append(text,used,sizeof text," C");
    (void)number(text,used,sizeof text,(selected%48u)/4u+1u);
    thy_ui_draw_text(out,2u,1u,text,19u,0);
    used=number(text,0u,sizeof text,w->completed_count);used=append(text,used,sizeof text,"/");
    (void)number(text,used,sizeof text,w->scope_count);
    thy_ui_draw_text(out,128u-5u*(w->completed_count<10u?4u:5u),1u,text,6u,0);
    thy_ui_draw_rectangle(out,0u,8u,128u,1u,1);
    calibration_map(out,w,selected);
    {
        if(w->state==THY_CALIBRATION_READY){
            thy_ui_draw_text(out,2u,25u,"ALL PADS READY",25u,0);
            thy_ui_draw_text(out,2u,33u,"COMPARE OR SAVE",25u,0);
        }else if(w->state==THY_CALIBRATION_APPLIED){
            thy_ui_draw_text(out,2u,25u,"CHANGES APPLIED",25u,0);
            thy_ui_draw_text(out,2u,33u,"CLOSE TO SAVE",25u,0);
        }else if(w->state==THY_CALIBRATION_APPLY_PENDING)thy_ui_draw_text(out,2u,25u,"RELEASE ALL PADS",25u,0);
        else if(w->state==THY_CALIBRATION_CANCELLED || w->state==THY_CALIBRATION_OFF)thy_ui_draw_text(out,2u,25u,"LEARN TO BEGIN",25u,0);
        else {
            static const char *const stages[]={"SOFT","MID","FIRM","PRESSURE"};
            used=number(text,0u,sizeof text,w->stage+1u);used=append(text,used,sizeof text,"/4 ");
            used=append(text,used,sizeof text,stages[w->stage<4u?w->stage:0u]);
            if(w->stage!=THY_CALIBRATION_PRESSURE){
                used=append(text,used,sizeof text," ");used=number(text,used,sizeof text,w->hits);
                (void)append(text,used,sizeof text,"/3");
            }
            thy_ui_draw_text(out,2u,25u,text,25u,0);
            if(w->stage==THY_CALIBRATION_PRESSURE){
                unsigned full=4095u-w->baseline;
                unsigned raw=w->live_raw>w->baseline?w->live_raw-w->baseline:0u;
                unsigned width=full?raw*120u/full:0u;if(width>120u)width=120u;
                thy_ui_draw_rectangle(out,3u,33u,122u,5u,1);
                thy_ui_draw_rectangle(out,4u,34u,120u,3u,0);
                if(width)thy_ui_draw_rectangle(out,4u,34u,width,3u,1);
            }else thy_ui_draw_text(out,2u,33u,w->contact_active?"RELEASE AFTER STRIKE":"STRIKE THEN RELEASE",25u,0);
        }
    }
    calibration_capture_actions(out,w->action_cursor);
    thy_ui_draw_rectangle(out,0u,55u,128u,1u,1);
    const char *help=e->calibration_pick==1u?"PRESS PAD TO SELECT":
        e->calibration_pick==2u?"RELEASE SELECTED PAD":
        w->state==THY_CALIBRATION_COLLECT && w->stage==THY_CALIBRATION_PRESSURE?
        "HOLD SOFT 1S THEN FIRM":"UP/DN >DO OEXIT";
    thy_ui_draw_text(out,2u,58u,w->reason?thy_calibration_reason_text(w->reason):help,25u,0);
}

static void information(uint8_t *out, const thy_state *state, uint16_t row)
{
    if(state->engine && (row==THY_ROW_SENSITIVITY_CUSTOMCURVE || row==THY_ROW_SENSITIVITY_SCOPE)) {
        sensitivity_graph(out,state,row==THY_ROW_SENSITIVITY_SCOPE);return;
    }
    if(row==THY_ROW_SYSTEM_STORAGE && state->engine){storage_information(out,state);return;}
    char text[97];
    unsigned used = 0u;
    const unsigned char *source = (const unsigned char *)thy_state_get_text(state, row);
    /* The OLED font is ASCII. Collapse UTF-8 punctuation to one space rather
     * than displaying a run of blank glyphs or splitting a multibyte sequence
     * across lines. The browser editor retains the original Unicode text. */
    if (source != NULL) while (*source && used + 1u < sizeof text) {
        if (*source < 128u) text[used++] = (char)*source++;
        else {
            if (used && text[used - 1u] != ' ') text[used++] = ' ';
            ++source;
            while ((*source & 0xc0u) == 0x80u) ++source;
        }
    }
    text[used] = 0;
    thy_ui_draw_text(out, 2, 13, thy_menu_rows[row].short_label, 25, 0);
    unsigned at = 0u;
    for (unsigned line = 0u; line < 3u && at < used; ++line) {
        while (text[at] == ' ') ++at;
        unsigned end = at;
        while (end < used && end - at < 25u) ++end;
        if (end < used && text[end] != ' ') {
            unsigned boundary = end;
            while (boundary > at && text[boundary] != ' ') --boundary;
            if (boundary > at) end = boundary;
        }
        char saved = text[end];
        text[end] = 0;
        thy_ui_draw_text(out, 2, 25u + line * 8u, text + at, 25, 0);
        text[end] = saved;
        at = end;
    }
}

static void section_icon(uint8_t *out, unsigned x, unsigned y, const char *id)
{
    /* Independent seven-pixel symbols. The cursor has its own column: an icon
     * remains a category landmark rather than replacing selection feedback. */
    static const struct { const char *id; uint8_t rows[7]; } icons[] = {
        {"engines", {20,62,99,34,99,62,20}}, /* Toothed gear, hollow shaft. */
        {"reset", {30,33,64,71,65,34,28}}, /* Counterclockwise restore arrow. */
        {"engines.play", {8,12,14,15,14,12,8}},
        {"engines.harmony", {12,10,9,8,8,56,56}}, /* Musical note. */
        {"harmony", {12,10,9,8,8,56,56}},
        {"engines.arp", {1,1,3,6,12,24,48}},
        {"engines.sequencer", {0,85,85,0,85,85,0}},
        {"engines.drums", {28,62,65,65,65,62,28}},
        {"performance", {8,12,14,15,14,12,8}},
        {"chords", {8,28,62,8,28,62,8}},
        {"arps", {1,1,3,6,12,24,48}},
        {"sequencer", {0,85,85,0,85,85,0}},
        {"midi", {28,34,65,85,65,34,28}},
        {"expression", {8,28,42,8,42,28,8}},
        {"sensitivity", {8,8,28,42,73,8,8}},
        {"lights", {8,42,28,127,28,42,8}},
        {"views", {28,34,73,85,73,34,28}},
        {"controls", {8,28,8,107,8,28,8}},
        {"storage", {127,65,93,65,93,65,127}},
        {"system", {8,42,28,99,28,42,8}}
    };
    const uint8_t *rows = icons[0].rows;
    for (unsigned i = 0u; i < sizeof icons / sizeof icons[0]; ++i)
        if (equal(id, icons[i].id)) { rows = icons[i].rows; break; }
    for (unsigned row = 0u; row < 7u; ++row)
        for (unsigned col = 0u; col < 7u; ++col)
            if (rows[row] & (1u << (6u - col)))
                thy_ui_draw_pixel(out, x + col, y + row, 1);
}

static void menu(uint8_t *out, const thy_state *state)
{
    unsigned group_index = state->group_index;
    if (group_index >= THY_MENU_GROUP_COUNT) group_index = 0u;
    const thy_menu_group *group = &thy_menu_groups[group_index];
    int groups = state->level == THY_STATE_GROUPS;
    int track_options = !groups && state->engine && state->engine->sequencer_ui.open &&
        state->engine->sequencer_ui.options_open;
    uint16_t visible[THY_MENU_ROW_COUNT];
    unsigned count=0u,cursor=0u;
    if(groups)for(unsigned g=0;g<THY_MENU_GROUP_COUNT;++g)if(thy_state_group_visible((uint8_t)g)){
        if(g==group_index)cursor=count;visible[count++]=(uint16_t)g;
    }
    if (!groups) {
        for (unsigned item = 0; item < group->row_count; ++item) {
            uint16_t row = (uint16_t)(group->first_row + item);
            if (!thy_state_row_visible(state, row)) continue;
            if (item == state->cursors[group_index]) cursor = count;
            visible[count++] = row;
        }
    }
    if (cursor >= count) cursor = 0u;
    char value[32];
    if (track_options) {
        unsigned title_used = append(value, 0u, sizeof value, "T");
        title_used = number(value, title_used, sizeof value,
            state->engine->sequencer_ui.options_track + 1u);
        (void)append(value, title_used, sizeof value, " SETTINGS");
        thy_ui_draw_text(out, 2, 1, value, 20, 0);
    } else if(!groups && state->engine && group_index==thy_menu_rows[THY_ROW_SENSITIVITY_TARGET].group) {
        unsigned at=append(value,0u,sizeof value,"SENSE ");
        uint8_t instance=sensitivity_instance(state);
        if(instance>=96u)(void)append(value,at,sizeof value,instance==97u?"RIGHT":"LEFT");
        else {
            at=append(value,at,sizeof value,instance<48u?"L":"R");
            at=number(value,at,sizeof value,instance%4u+1u);at=append(value,at,sizeof value," C");
            (void)number(value,at,sizeof value,(instance%48u)/4u+1u);
        }
        thy_ui_draw_text(out,2u,1u,value,20u,0);
    } else thy_ui_draw_text(out, 2, 1, groups ? "SETTINGS" : group->label, 20, 0);
    unsigned used = number(value, 0, sizeof value, count ? cursor + 1u : 0u);
    used = append(value, used, sizeof value, "/");
    used = number(value, used, sizeof value, count);
    unsigned count_x = 128u - used * 5u;
    thy_ui_draw_rectangle(out, count_x - 1u, 0, 128u - count_x + 1u, 8, 0);
    thy_ui_draw_text(out, count_x, 1, value, 6, 0);
    thy_ui_draw_rectangle(out, 0, 9, 128, 1, 1);

    int showing_info = !groups && count &&
        thy_menu_rows[visible[cursor]].type == THY_MENU_INFO && state->info_open;
    if (showing_info) information(out, state, visible[cursor]);
    unsigned start = window_start(cursor, count);
    unsigned section_offset = 0u;
    for (unsigned i = 0; !showing_info && i < 5u && start + i < count; ++i) {
        unsigned item = start + i;
        if (!groups && i && thy_menu_rows[visible[item]].advanced &&
            !thy_menu_rows[visible[item - 1u]].advanced) {
            /* Presentation only: the divider consumes no selectable identity
             * and the five-row viewport still fits above the footer. */
            thy_ui_draw_rectangle(out, 8u, 11u + i * 8u, 118u, 1u, 1);
            section_offset = 2u;
        }
        unsigned y = 13u + i * 8u + section_offset;
        int selected = item == cursor;
        thy_ui_draw_text(out, 1, y, selected ? ">" : "", 1, 0);
        if (groups) {
            const thy_menu_group *item_group = &thy_menu_groups[visible[item]];
            section_icon(out, 8, y - 1u, item_group->id);
            thy_ui_draw_text(out, 19, y, item_group->label, 21, 0);
        } else {
            unsigned row_index = visible[item];
            const thy_menu_row *row = &thy_menu_rows[row_index];
            unsigned label_x = 8u;
            if(thy_state_reset_row((uint16_t)row_index)){
                section_icon(out,8u,y-1u,"reset");label_x=19u;
            }else if (row->group == thy_menu_rows[THY_ROW_ENGINES_PLAY].group) {
                section_icon(out, 8u, y - 1u, row->id);
                label_x = 19u;
            }
            thy_ui_draw_text(out, label_x, y, row->short_label, label_x == 8u ? 15u : 13u, 0);
            compact_value(state, (uint16_t)row_index,
                selected && state->editing, value);
            if (selected && state->editing) {
                thy_ui_draw_rectangle(out, 85, y - 1u, 43, 7, 1);
                thy_ui_draw_text(out, 86, y, value, 8, 1);
            } else thy_ui_draw_text(out, 86, y, value, 8, 0);
        }
    }
    if (!groups && !count) {
        thy_ui_draw_text(out, 2, 22, "FUTURE ENGINE PENDING", 25, 0);
        thy_ui_draw_text(out, 2, 34, "NO LIVE CONTROLS YET", 25, 0);
    }
    thy_ui_draw_rectangle(out, 0, 53, 128, 1, 1);
    const char *footer = state->editing ? "> APPLY O EXIT < CANCEL" :
        groups ? "> OPEN  O EXIT" : "> EDIT  O EXIT  < BACK";
    if (!groups && !count) footer = "O EXIT  < BACK";
    else if (!groups && !state->editing) {
        const thy_menu_row *selected = &thy_menu_rows[visible[cursor]];
        if (selected->type == THY_MENU_INFO) footer =
            !state->info_open ?
            "> INFO O EXIT < BACK" : "READ ONLY O EXIT < BACK";
        else if (selected->type == THY_MENU_ACTION) footer = selected->status == THY_MENU_LIVE ? "> RUN  O EXIT  < BACK" : "UNAVAILABLE O EXIT < BACK";
        else if (selected->status == THY_MENU_DESIGN) footer = "DRAFT >EDIT O EXIT <BACK";
        if (selected->type == THY_MENU_ACTION && selected->status == THY_MENU_LIVE) {
            char action[32];
            if (thy_state_format(state, visible[cursor], 0, action, sizeof action) == THY_STATE_OK) {
                if (equal(action,"Open")) footer="> OPEN  O EXIT  < BACK";
                else if (equal(action,"Apply")) footer="> APPLY O EXIT < BACK";
                else if (equal(action,"Select")) footer="> SELECT O EXIT < BACK";
                else if (equal(action,"Active")) footer="ACTIVE  O EXIT  < BACK";
            }
        }
        if (selected->group == thy_menu_rows[THY_ROW_ENGINES_PLAY].group &&
            selected->type == THY_MENU_INFO && !state->info_open)
            footer = "> OPEN  O EXIT  < BACK";
    }
    if (state->notice) footer = notice(state);
    else if (!groups && count && thy_menu_rows[visible[cursor]].dynamic &&
        thy_menu_rows[visible[cursor]].type == THY_MENU_SELECT) {
        /* Related scale names often share their first eight glyphs. Keep
         * the fixed value column, but show the complete selected/draft name
         * on the bottom line rather than forcing a scrolling animation.
         * Category legends, other editors and notices keep their usual text. */
        if (thy_state_format(state, visible[cursor], state->editing,
            value, sizeof value) == THY_STATE_OK && value[0]) footer = value;
    }
    if (track_options) {
        if (!state->notice) footer = state->editing ?
            "< CANCEL > APPLY O EXIT" : "< TRACK > EDIT O EXIT";
        else if (state->notice == THY_NOTICE_APPLIED) footer = "APPLIED < TRACK O EXIT";
        else if (state->notice == THY_NOTICE_CANCELLED) footer = "CANCELLED < TRACK O EXIT";
    }
    if(state->editing && state->edit_row==THY_ROW_SENSITIVITY_TARGET && !state->notice)
        footer="UP ALL DOWN PAD >APPLY";
    else if(state->editing && !state->notice &&
        thy_menu_rows[state->edit_row].group==thy_menu_rows[THY_ROW_SENSITIVITY_ATTACK].group &&
        thy_menu_rows[state->edit_row].type==THY_MENU_NUMBER) {
        int32_t minimum,maximum;
        if(thy_state_edit_bounds(state,state->edit_row,state->edit_instance,&minimum,&maximum)==THY_STATE_OK &&
           minimum>=0 && maximum>=minimum){
            unsigned at=number(value,0u,sizeof value,(unsigned)minimum);at=append(value,at,sizeof value,"..");
            at=number(value,at,sizeof value,(unsigned)maximum);(void)append(value,at,sizeof value," >APPLY OEXIT");footer=value;
        }
    }
    if(state->engine && state->engine->sensitivity_pick)
        footer=state->engine->sensitivity_picked?"RELEASE PAD / O EXIT":"TAP PAD / < CANCEL O EXIT";
    thy_ui_draw_text(out, 2, 57, footer, 25, 0);
}

static void arp_quick(uint8_t *out, const thy_state *state)
{
    const thy_engine *engine = state->engine;
    unsigned target = engine->arp_target < 2u ? engine->arp_target : 0u;
    const thy_arp *arp = &engine->arps[target];
    char text[32];
    unsigned used = append(text, 0u, sizeof text, "ARP ");
    used = number(text, used, sizeof text, target + 1u);
    used = append(text, used, sizeof text, arp->config.enabled ? " ON  " : " OFF ");
    (void)append(text, used, sizeof text, engine->musical_clock.source == THY_CLOCK_SOURCE_INTERNAL ?
        "INT" : engine->musical_clock.status == THY_CLOCK_LOCKED ? "USB" : "USB WAIT");
    thy_ui_draw_text(out, 2u, 1u, text, 25u, 0);
    thy_ui_draw_rectangle(out, 0u, 9u, 128u, 1u, 1);
    unsigned playhead = 8u;
    for (unsigned n = 0u; n < THY_ARP_PHRASES; ++n)
        if (arp->phrases[n].active && arp->phrases[n].ordinal) {
            playhead = (unsigned)((arp->phrases[n].ordinal - 1u) & 7u); break;
        }
    for (unsigned n = 0u; n < 8u; ++n) {
        unsigned x = 3u + n * 15u;
        int enabled = !!(arp->config.step_mask & (1u << n));
        thy_ui_draw_rectangle(out, x, 13u, 12u, 7u, enabled);
        text[0] = (char)('1' + n); text[1] = 0;
        thy_ui_draw_text(out, x + 4u, 14u, text, 1u, enabled);
        if (n == playhead) thy_ui_draw_rectangle(out, x, 22u, 12u, 1u, 1);
    }
    unsigned cursor = engine->arp_quick_cursor < THY_ENGINE_ARP_QUICK_ROWS ? engine->arp_quick_cursor : 0u;
    unsigned start = cursor ? cursor - 1u : 0u;
    if (start > THY_ENGINE_ARP_QUICK_ROWS - 3u) start = THY_ENGINE_ARP_QUICK_ROWS - 3u;
    for (unsigned line = 0u; line < 3u; ++line) {
        unsigned item = start + line, y = 27u + line * 8u;
        uint16_t row = thy_engine_arp_quick_row((uint8_t)item);
        int selected = item == cursor;
        thy_ui_draw_text(out, 1u, y, selected ? ">" : "", 1u, 0);
        thy_ui_draw_text(out, 8u, y, thy_menu_rows[row].short_label, 14u, 0);
        compact_value(state, row, selected && state->editing, text);
        if (selected && state->editing) {
            thy_ui_draw_rectangle(out, 84u, y - 1u, 44u, 7u, 1);
            thy_ui_draw_text(out, 85u, y, text, 8u, 1);
        } else thy_ui_draw_text(out, 85u, y, text, 8u, 0);
    }
    if (engine->arp_quick_keys) {
        thy_ui_draw_text(out, 1u, 52u, state->notice ? notice(state) : "M1/2 ON  M3/4 HOLD", 25u, 0);
        thy_ui_draw_text(out, 1u, 59u, "M5/6 CLEAR M7 TARGET", 25u, 0);
    } else {
        thy_ui_draw_text(out, 1u, 52u, state->notice ? notice(state) : "> EDIT/APPLY  < BACK", 25u, 0);
        thy_ui_draw_text(out, 1u, 59u, "O EXIT  M KEYS NORMAL", 25u, 0);
    }
}

static const char *seq_capture_name(unsigned value)
{
    static const char *names[] = {"", "ARM", "REC", "REC", "END", "FAIL"};
    return value < sizeof names / sizeof names[0] ? names[value] : "ERROR";
}

static unsigned sequencer_countdown(char *text, unsigned used, unsigned capacity,
    uint64_t target, const thy_sequencer_ui *ui)
{
    uint64_t delta = target > ui->transport->position ? target - ui->transport->position : 0u;
    uint32_t beat = ui->clock->beat_ticks ? ui->clock->beat_ticks : THY_CLOCK_PPQN;
    uint32_t bar = ui->clock->bar_ticks ? ui->clock->bar_ticks : 4u * THY_CLOCK_PPQN;
    int bars = delta && delta % bar == 0u;
    uint64_t units = bars ? delta / bar : delta / beat + (delta % beat != 0u);
    if (units > 999u) units = 999u;
    used = number(text, used, capacity, (unsigned)units);
    return append(text, used, capacity, bars ? "BAR" : "BT");
}

static void sequencer_help(uint8_t *out, const thy_sequencer_ui *ui)
{
    static const struct { const char *title, *lines[5]; } pages[] = {
        {"TRACKS", {"UP/DOWN CHOOSE A TRACK", "RIGHT OPENS / CREATES",
            "LEFT REQUESTS DELETE", "RIGHT CONFIRMS / LEFT NO", "O RETURNS TO EXIT"}},
        {"ENTER NOTES", {"TAP A LEFT STEP", "PLAY RIGHT NOTES / CHORDS",
            "M4 SWITCHES INPUT TYPE", "U1/U2 SHIFT INPUT OCTAVE", "U3 NEXT SCALE / LAYOUT"}},
        {"EDIT A STEP", {"M2 CLEARS SELECTED STEP", "M3 TOGGLES OVERDUB",
            "REPLACE: NEW GESTURE WINS", "OVERDUB: ADD TO THE STEP", "M5 UNDO / M6 REDO"}},
        {"PLAYBACK", {"5 PLAYS / STOPS THE SONG", "M1 PLAYS THIS TRACK SOLO",
            "12 RECORDS / FINISHES", "O RETURNS TO LIVE PLAY", "PLAYBACK KEEPS RUNNING"}},
        {"RECORD + SAVE", {"12 STARTS / FINISHES", "EXISTING LOOP: ONE PASS",
            "12 FINISHES AT NEXT BAR", "STOP + RELEASE TO SAVE", "REBOOT RESTORES STOPPED"}}
    };
    unsigned page = ui->help_page < sizeof pages / sizeof pages[0] ? ui->help_page : 0u;
    char text[32];
    unsigned used = append(text, 0u, sizeof text, "HELP ");
    used = append(text, used, sizeof text, pages[page].title);
    used = append(text, used, sizeof text, " ");
    used = number(text, used, sizeof text, page + 1u);
    (void)append(text, used, sizeof text, "/5");
    thy_ui_draw_text(out, 1u, 1u, text, 25u, 0);
    thy_ui_draw_rectangle(out, 0u, 9u, 128u, 1u, 1);
    for (unsigned line = 0u; line < 5u; ++line)
        thy_ui_draw_text(out, 1u, 13u + line * 8u, pages[page].lines[line], 25u, 0);
    thy_ui_draw_rectangle(out, 0u, 54u, 128u, 1u, 1);
    thy_ui_draw_text(out, 1u, 58u, "UP/DN PAGE < BACK O MENU", 25u, 0);
}

static void sequencer_tracks(uint8_t *out, const thy_sequencer_ui *ui)
{
    const thy_sequencer *seq = ui->sequencer;
    const thy_transport *transport = ui->transport;
    uint8_t tracks[THY_SEQ_TRACKS];
    uint16_t notes[THY_SEQ_TRACKS] = {0};
    for (unsigned n = 0u; n < seq->count; ++n)
        if ((seq->events[n].kind & 0x7fu) == THY_SEQ_NOTE && seq->events[n].track < THY_SEQ_TRACKS)
            ++notes[seq->events[n].track];
    unsigned count = 0u, cursor = 0u;
    for (unsigned track = 0u; track < THY_SEQ_TRACKS; ++track)
        if (thy_sequencer_track_exists(seq, (uint8_t)track)) {
            if (track == seq->selected_track) cursor = count;
            tracks[count++] = (uint8_t)track;
        }
    unsigned rows = count + (count < THY_SEQ_TRACKS ? 1u : 0u);
    if (ui->track_new) cursor = count < THY_SEQ_TRACKS ? count : cursor;
    unsigned start = cursor > 2u ? cursor - 2u : 0u;
    if (rows > 6u && start > rows - 6u) start = rows - 6u;
    if (rows <= 6u) start = 0u;
    for (unsigned line = 0u; line < 6u && start + line < rows; ++line) {
        unsigned at = start + line, y = 12u + line * 7u;
        char text[32];
        unsigned used = append(text, 0u, sizeof text, cursor == at ? ">" : " ");
        if (at == count) {
            (void)append(text, used, sizeof text, "+ NEW TRACK");
            thy_ui_draw_text(out, 1u, y, text, 25u, 0);
            continue;
        }
        unsigned track = tracks[at];
        const thy_transport_track *run = &transport->tracks[track];
        const thy_seq_track *settings = &seq->tracks[track];
        used = append(text, used, sizeof text, "T");
        used = number(text, used, sizeof text, track + 1u);
        used = append(text, used, sizeof text, " N");
        used = number(text, used, sizeof text, notes[track]);
        used = append(text, used, sizeof text, " ");
        if (seq->capture.state && seq->capture.track == track) {
            used = append(text, used, sizeof text, seq_capture_name(seq->capture.state));
            if (seq->capture.state == THY_SEQ_QUEUED || seq->capture.state == THY_SEQ_FINISHING) {
                used = append(text, used, sizeof text, " ");
                (void)sequencer_countdown(text, used, sizeof text,
                    seq->capture.state == THY_SEQ_FINISHING ? seq->capture.end_position : seq->capture.queued_position, ui);
            }
        } else if (run->pending) {
            used = append(text, used, sizeof text, run->pending == THY_TRANSPORT_PENDING_START ? "IN" : "OFF");
            (void)sequencer_countdown(text, used, sizeof text, run->pending_position, ui);
        } else (void)append(text, used, sizeof text, settings->muted ? "MUTE" : run->running ? "PLAY" : "STOP");
        thy_ui_draw_text(out, 1u, y, text, 19u, 0);
        unsigned length = settings->loop_end > settings->loop_start ? settings->loop_end - settings->loop_start : 0u;
        thy_ui_draw_rectangle(out, 99u, y + 1u, 28u, 3u, 1);
        thy_ui_draw_rectangle(out, 100u, y + 2u, 26u, 1u, 0);
        if (length && run->playhead >= settings->loop_start && run->running) {
            unsigned x = 100u + (unsigned)((uint64_t)(run->playhead - settings->loop_start) * 25u / length);
            if (x > 125u) x = 125u;
            thy_ui_draw_pixel(out, x, y + 2u, 1);
        }
    }
    if (!count) {
        thy_ui_draw_text(out, 1u, 29u, "RIGHT CREATES FIRST TRACK", 25u, 0);
        thy_ui_draw_text(out, 1u, 40u, "PADS STAY QUIET HERE", 25u, 0);
    }
}

static void sequencer_steps(uint8_t *out, const thy_sequencer_ui *ui)
{
    const thy_sequencer *seq = ui->sequencer;
    unsigned selected = seq->selected_track;
    const thy_seq_track *track = &seq->tracks[selected];
    uint32_t selected_tick = ui->window_tick + ui->selected_step * ui->grid_ticks;
    uint8_t cells[48] = {0}, notes[128] = {0};
    unsigned note_count = 0u;
    for (unsigned n = 0u; n < seq->count; ++n) {
        const thy_seq_event *event = &seq->events[n];
        if (event->track != selected || (event->kind & 0x7fu) != THY_SEQ_NOTE) continue;
        if (event->tick >= ui->window_tick && event->tick < ui->window_tick + ui->grid_ticks * 48u)
            cells[(event->tick - ui->window_tick) / ui->grid_ticks] = 1u;
        if (event->tick >= selected_tick && event->tick < selected_tick + ui->grid_ticks) {
            ++note_count;
            notes[event->pitch] = 1u;
        }
    }
    char text[32];
    unsigned used = append(text, 0u, sizeof text,
        ui->input_layout == THY_SEQ_INPUT_CHORDS ? "M4 CHORDS / " : "M4 NOTES / ");
    used = append(text, used, sizeof text, ui->overdub ? "OVERDUB" : "REPLACE");
    thy_ui_draw_text(out, 1u, 12u, text, 25u, 0);
    used = number(text, 0u, sizeof text, note_count);
    used = append(text, used, sizeof text, note_count == 1u ? " NOTE  " : " NOTES ");
    unsigned shown = 0u;
    for (unsigned note = 0u; note < 128u && shown < 4u; ++note) if (notes[note]) {
        char name[6];
        thy_ui_note_name((int)note, name);
        if (shown) used = append(text, used, sizeof text, " ");
        used = append(text, used, sizeof text, name);
        ++shown;
    }
    if (note_count > shown) (void)append(text, used, sizeof text, "+");
    else if (!note_count) (void)append(text, used, sizeof text, "TAP RIGHT PADS");
    thy_ui_draw_text(out, 1u, 20u, text, 25u, 0);
    const thy_transport_track *run = &ui->transport->tracks[selected];
    unsigned playing_step = 48u;
    if (run->running && run->playhead >= ui->window_tick)
        playing_step = (run->playhead - ui->window_tick) / ui->grid_ticks;
    for (unsigned step = 0u; step < 48u; ++step) {
        unsigned x = 4u + step % 12u * 10u, y = 29u + step / 12u * 6u;
        int inside = ui->window_tick + step * ui->grid_ticks < track->loop_end;
        if (cells[step]) thy_ui_draw_rectangle(out, x + 2u, y + 1u, 5u, 3u, 1);
        else if (inside) thy_ui_draw_pixel(out, x + 4u, y + 2u, 1);
        if (step == ui->selected_step) {
            thy_ui_draw_rectangle(out, x, y, 9u, 1u, 1);
            thy_ui_draw_rectangle(out, x, y + 4u, 9u, 1u, 1);
            thy_ui_draw_rectangle(out, x, y, 1u, 5u, 1);
            thy_ui_draw_rectangle(out, x + 8u, y, 1u, 5u, 1);
        }
        if (step == playing_step) thy_ui_draw_rectangle(out, x + 1u, y + 5u, 7u, 1u, 1);
    }
}

static void sequencer_canvas(uint8_t *out, const thy_state *state)
{
    const thy_sequencer_ui *ui = &state->engine->sequencer_ui;
    const thy_sequencer *seq = ui->sequencer;
    const thy_transport *transport = ui->transport;
    if (ui->help_open) { sequencer_help(out, ui); return; }
    char text[32];
    unsigned used = append(text, 0u, sizeof text,
        ui->view == THY_SEQ_VIEW_TRACKS ? "TRACKS" : "T");
    if (ui->view == THY_SEQ_VIEW_STEPS) {
        used = number(text, used, sizeof text, seq->selected_track + 1u);
        used = append(text, used, sizeof text, " STEP ");
        used = number(text, used, sizeof text, ui->selected_step + 1u);
        used = append(text, used, sizeof text, "/48");
    }
    used = append(text, used, sizeof text,
        transport->state == THY_TRANSPORT_PLAYING ? (ui->solo ? " SOLO" : " PLAY") : " STOP");
    if (ui->view == THY_SEQ_VIEW_TRACKS) {
        used = append(text, used, sizeof text, " ");
        (void)number(text, used, sizeof text, ui->clock->tempo_milli_bpm / 1000u);
    }
    thy_ui_draw_text(out, 1u, 1u, text, 25u, 0);
    thy_ui_draw_rectangle(out, 0u, 9u, 128u, 1u, 1);
    if (ui->delete_pending) {
        used = append(text, 0u, sizeof text, "DELETE TRACK ");
        used = number(text, used, sizeof text, seq->selected_track + 1u);
        (void)append(text, used, sizeof text, "?");
        thy_ui_draw_text(out, 1u, 18u, text, 25u, 0);
        thy_ui_draw_text(out, 1u, 30u, "NOTES WILL BE REMOVED", 25u, 0);
        thy_ui_draw_text(out, 1u, 42u, "> YES   < NO", 25u, 0);
    } else if (ui->view == THY_SEQ_VIEW_TRACKS) sequencer_tracks(out, ui);
    else sequencer_steps(out, ui);
    thy_ui_draw_rectangle(out, 0u, 54u, 128u, 1u, 1);
    const char *footer = ui->view == THY_SEQ_VIEW_TRACKS ?
        (ui->track_new ? "> CREATE O MENU" : "> OPEN < DELETE O MENU") : "M1 SOLO M2 CLEAR M7 HELP";
    if (ui->delete_pending) footer = "> DELETE < CANCEL O MENU";
    else if (state->engine->song_status) footer = state->engine->song_status == 1u ?
        "SONG SAVE PENDING" : state->engine->song_status == 2u ? "SONG SAVED" : "SONG SAVE FAILED";
    if (ui->last_error) footer = ui->last_error == THY_SEQ_FULL ? "EVENT LIMIT / UNDO OR TRIM" :
        ui->last_error == THY_SEQ_NO_HISTORY ? "NO UNDO / REDO HISTORY" : "EDIT ERROR / CHECK OPTIONS";
    thy_ui_draw_text(out, 1u, 58u, footer, 25u, 0);
}

typedef struct play_snapshot {
    uint8_t pitches[128];
    uint8_t pool[128];
    unsigned notes, pads, pitch_sum;
    char musical_name[THY_HARMONY_TEXT_BYTES];
} play_snapshot;

static play_snapshot play_snapshot_read(const thy_state *state)
{
    play_snapshot result = {0};
    const thy_instrument *instrument = state->instrument;
    const thy_core *core = instrument->core;
    if (core == NULL) return result;
    for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad) {
        int held = core->owners[pad].held || instrument->held_input_valid[pad] ||
            instrument->sensors[pad].held;
        /* An Arp-only input need not own a dry note. Count physical identities,
         * not generated voices, and never count Layer or two Arps twice. */
        if (state->engine != NULL)
            for (unsigned a = 0u; a < 2u; ++a)
                if (state->engine->arps[a].config.enabled &&
                    state->engine->arps[a].inputs[pad].held) held = 1;
        if (held) ++result.pads;
    }
    /* Collapse destination/channel duplicates. These are the ledger's actual
     * sounding pitches, including generated notes and sustain, not pad guesses. */
    for (unsigned key = 0u; key < THY_KEY_COUNT; ++key)
        if (core->refs[key] || thy_core_deferred(core, (uint16_t)key))
            result.pitches[key % 128u] = 1u;
    for (unsigned note = 0u; note < 128u; ++note)
        if (result.pitches[note]) {
            ++result.notes;
            result.pitch_sum += note;
        }
    /* Admitted input pitches remain visible between gates. Read the engine's
     * copied input bitsets, never retain pointers or advance a phrase here.
     * The ownership ledger above alone decides which notes are bright. */
    if (state->engine != NULL) {
        uint64_t pool[2] = {0u, 0u};
        for (unsigned a = 0u; a < 2u; ++a) {
            const thy_arp *arp = &state->engine->arps[a];
            if (!arp->config.enabled) continue;
            for (unsigned input = 0u; input < THY_ARP_INPUTS; ++input)
                if (arp->inputs[input].held) {
                    pool[0] |= arp->inputs[input].pitches[0];
                    pool[1] |= arp->inputs[input].pitches[1];
                }
        }
        for (unsigned note = 0u; note < 128u; ++note)
            result.pool[note] = (uint8_t)((pool[note / 64u] >> (note % 64u)) & 1u);
    }
    /* Recognize the admitted phrase between Arp gates, and the ledger for
     * ordinary/sustained/sequenced notes. Captured roots are hints only: a
     * later key/layout change must never rename a held chord from a preview. */
    uint64_t pitches[2] = {0u, 0u};
    uint8_t root = THY_HARMONY_NO_ROOT;
    int conflicting = 0;
    for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad)
        if (instrument->mapping[pad] < 48u &&
            instrument->held_color_valid[pad] &&
            (core->owners[pad].held || instrument->held_input_valid[pad])) {
            uint8_t candidate = instrument->held_color_root[pad] % 12u;
            if (root != THY_HARMONY_NO_ROOT && root != candidate) conflicting = 1;
            root = candidate;
        }
    for (unsigned note = 0u; note < 128u; ++note)
        if (result.pitches[note] || result.pool[note])
            pitches[note / 64u] |= UINT64_C(1) << (note % 64u);
    thy_harmony harmony;
    if (thy_harmony_recognize(pitches, conflicting ? THY_HARMONY_NO_ROOT : root,
            &harmony) == THY_HARMONY_OK)
        (void)thy_harmony_format(&harmony, result.musical_name,
            sizeof result.musical_name);
    return result;
}

static unsigned signed_number(char *out, unsigned used, unsigned capacity, int value)
{
    if (value < 0) {
        used = append(out, used, capacity, "-");
        return number(out, used, capacity, (unsigned)-value);
    }
    used = append(out, used, capacity, "+");
    return number(out, used, capacity, (unsigned)value);
}

static unsigned route_value(char *out, unsigned used, unsigned capacity,
    const thy_instrument *instrument, uint8_t target, int velocity)
{
    thy_instrument_route route;
    if (thy_instrument_get_pad_route(instrument, target, &route) != THY_OK)
        return append(out, used, capacity, "--");
    return number(out, used, capacity, velocity ? route.velocity : route.channel + 1u);
}

static unsigned route_values(char *out, unsigned used, unsigned capacity,
    const thy_instrument *instrument, int velocity)
{
    if (instrument->routing_profile == THY_ROUTING_WHOLE)
        return route_value(out, used, capacity, instrument, 0u, velocity);
    for (unsigned side = 0u; side < 2u; ++side) {
        used = append(out, used, capacity, side ? " R" : "L");
        unsigned zones = instrument->routing_profile == THY_ROUTING_ZONES ? 3u : 1u;
        for (unsigned zone = 0u; zone < zones; ++zone) {
            if (zone) used = append(out, used, capacity, "/");
            uint8_t target = zones == 3u ? (uint8_t)(3u + side * 3u + zone) :
                (uint8_t)(side + 1u);
            used = route_value(out, used, capacity, instrument, target, velocity);
        }
    }
    return used;
}

static unsigned destination_text(char *out, unsigned used, unsigned capacity, uint8_t routes)
{
    if (routes & THY_ROUTE_USB0) used = append(out, used, capacity, "U1");
    if (routes & THY_ROUTE_USB1) used = append(out, used, capacity, "U2");
    if (routes & THY_ROUTE_DIN) used = append(out, used, capacity, "D");
    return routes ? used : append(out, used, capacity, "OFF");
}

static unsigned side_destinations(const thy_instrument *instrument, unsigned side)
{
    unsigned mask = 0u;
    unsigned count = instrument->routing_profile == THY_ROUTING_ZONES ? 3u : 1u;
    for (unsigned zone = 0u; zone < count; ++zone) {
        thy_instrument_route route;
        uint8_t target = instrument->routing_profile == THY_ROUTING_WHOLE ? 0u :
            count == 3u ? (uint8_t)(3u + side * 3u + zone) : (uint8_t)(side + 1u);
        if (thy_instrument_get_pad_route(instrument, target, &route) == THY_OK)
            mask |= route.destinations;
    }
    return mask;
}

static void view_line(const thy_state *state, const play_snapshot *snapshot,
    unsigned kind, const char *scale_name, int din_ready, int usb_ready, char out[32])
{
    const thy_instrument *instrument = state->instrument;
    const thy_engine *engine = state->engine;
    const thy_instrument_performance *p = &instrument->performance;
    unsigned used = 0u;
    out[0] = 0;
    switch (kind) {
    case THY_VIEW_INFO_MUSICAL_NAMES:
        (void)append(out, used, 32u, snapshot->musical_name);
        break;
    case THY_VIEW_INFO_VELOCITY:
        if (instrument->routing_profile == THY_ROUTING_ZONES) {
            used = append(out, used, 32u, "V");
            for (unsigned zone = 0u; zone < 6u; ++zone) {
                if (zone) used = append(out, used, 32u, zone == 3u ? "|" : "/");
                used = route_value(out, used, 32u, instrument, (uint8_t)(3u + zone), 1);
            }
        } else {
            used = append(out, used, 32u, "VEL ");
            (void)route_values(out, used, 32u, instrument, 1);
        }
        break;
    case THY_VIEW_INFO_COUNTS:
        used = append(out, used, 32u, "PADS ");
        used = number(out, used, 32u, snapshot->pads);
        used = append(out, used, 32u, "  NOTES ");
        (void)number(out, used, 32u, snapshot->notes);
        break;
    case THY_VIEW_INFO_NOTES: {
        used = append(out, used, 32u, "NOTES ");
        unsigned shown = 0u;
        for (unsigned note = 0u; note < 128u && shown < 4u; ++note)
            if (snapshot->pitches[note]) {
                char name[6];
                thy_ui_note_name((int)note, name);
                if (shown) used = append(out, used, 32u, " ");
                used = append(out, used, 32u, name);
                ++shown;
            }
        if (snapshot->notes > shown) (void)append(out, used, 32u, "+");
        else if (!shown) (void)append(out, used, 32u, "--");
        break;
    }
    case THY_VIEW_INFO_ROOT_SCALE:
        used = append(out, used, 32u, "SCALE ");
        (void)append(out, used, 32u, scale_name != NULL ? scale_name : "--");
        break;
    case THY_VIEW_INFO_CHANNELS:
        used = append(out, used, 32u, "CH ");
        (void)route_values(out, used, 32u, instrument, 0);
        break;
    case THY_VIEW_INFO_OUTPUTS:
        used = append(out, used, 32u, instrument->routing_profile == THY_ROUTING_ZONES ?
            "OUT* L:" : "OUT L:");
        used = destination_text(out, used, 32u, (uint8_t)side_destinations(instrument, 0u));
        used = append(out, used, 32u, " R:");
        used = destination_text(out, used, 32u, (uint8_t)side_destinations(instrument, 1u));
        if (!din_ready && !usb_ready) (void)append(out, used, 32u, " --");
        break;
    case THY_VIEW_INFO_MODIFIERS:
        used = append(out, used, 32u, "INV");
        used = number(out, used, 32u, p->inversion);
        used = append(out, used, 32u, " SPR");
        used = number(out, used, 32u, p->spread);
        (void)append(out, used, 32u, p->strum ? " STR ON" : " STR OFF");
        break;
    case THY_VIEW_INFO_PRESSURE: {
        unsigned pressure[2] = {0u, 0u};
        for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad) {
            unsigned side = instrument->mapping[pad] >= 48u;
            const thy_instrument_sensor *sensor = &instrument->sensors[pad];
            if (sensor->held && sensor->pressure > pressure[side])
                pressure[side] = sensor->pressure;
        }
        used = append(out, used, 32u, "PRESS L");
        used = number(out, used, 32u, pressure[0]);
        used = append(out, used, 32u, " R");
        (void)number(out, used, 32u, pressure[1]);
        break;
    }
    case THY_VIEW_INFO_OCTAVES:
        used = append(out, used, 32u, "OCT L");
        used = signed_number(out, used, 32u, p->chord_octave);
        used = append(out, used, 32u, " R");
        used = signed_number(out, used, 32u, p->melody_octave);
        used = append(out, used, 32u, " B");
        (void)signed_number(out, used, 32u, instrument->bass_octave);
        break;
    case THY_VIEW_INFO_CLOCK:
        if (engine == NULL) { (void)append(out, used, 32u, "CLOCK --"); break; }
        if (engine->musical_clock.source == THY_CLOCK_SOURCE_INTERNAL) {
            used = append(out, used, 32u, "INT ");
            used = number(out, used, 32u, engine->musical_clock.tempo_milli_bpm / 1000u);
            used = append(out, used, 32u, " BPM ");
        } else used = append(out, used, 32u,
            engine->musical_clock.status == THY_CLOCK_LOCKED ? "USB LOCK " :
            engine->musical_clock.status == THY_CLOCK_LOST ? "USB LOST " : "USB WAIT ");
        used = number(out, used, 32u, engine->musical_clock.numerator);
        used = append(out, used, 32u, "/");
        (void)number(out, used, 32u, engine->musical_clock.denominator);
        break;
    case THY_VIEW_INFO_ARPS:
        if (engine == NULL) { (void)append(out, used, 32u, "ARPS --"); break; }
        used = append(out, used, 32u, engine->arps[0].config.enabled ? "ARP 1:ON" : "ARP 1:OFF");
        (void)append(out, used, 32u, engine->arps[1].config.enabled ? " 2:ON" : " 2:OFF");
        break;
    default: break;
    }
}

void thy_studio_ui_view_line(const thy_state *state, unsigned kind,
    const char *scale_name, int din_ready, int usb_ready, char out[32])
{
    if (out == NULL) return;
    out[0] = 0;
    if (state == NULL || state->instrument == NULL) return;
    play_snapshot snapshot = play_snapshot_read(state);
    view_line(state, &snapshot, kind, scale_name, din_ready, usb_ready, out);
}

static void draw_view_line(uint8_t *out, unsigned y, const thy_state *state,
    const play_snapshot *snapshot, unsigned kind, const char *scale_name,
    int din_ready, int usb_ready)
{
    char text[32];
    view_line(state, snapshot, kind, scale_name, din_ready, usb_ready, text);
    thy_ui_draw_text(out, 2u, y, text, 21u, 0);
}

static void connection_icons(uint8_t *out, const thy_state *state,
    int din_ready, int usb_ready)
{
    thy_ui_draw_rectangle(out, 116, 1, 1, 8, 1);
    thy_ui_draw_pixel(out, 115, 2, 1);
    thy_ui_draw_pixel(out, 117, 2, 1);
    thy_ui_draw_rectangle(out, 113, 4, 1, 3, 1);
    thy_ui_draw_rectangle(out, 113, 6, 4, 1, 1);
    thy_ui_draw_rectangle(out, 116, 4, 5, 1, 1);
    thy_ui_draw_rectangle(out, 120, 2, 2, 2, 1);
    thy_ui_draw_rectangle(out, 115, 9, 3, 2, 1);
    /* DIN is labelled instead of the ambiguous circular connector glyph.
     * The fixed side rail is shared by every Play view. */
    thy_ui_draw_text(out, 112u, 17u, "DIN", 3u, 0);
    if (!usb_ready) for (unsigned at = 0u; at < 10u; ++at)
        thy_ui_draw_pixel(out, 112u + at, 10u - at, 1);
    if (!din_ready) for (unsigned at = 0u; at < 10u; ++at)
            thy_ui_draw_pixel(out, 112u + at, 24u - at, 1);
    if (state->engine != NULL && state->engine->expression_config.enabled)
        thy_ui_draw_text(out, 112u, 30u, "MPE", 3u, 0);
    if (state->engine != NULL) {
        unsigned arps = (state->engine->arps[0].config.enabled ? 1u : 0u) |
            (state->engine->arps[1].config.enabled ? 2u : 0u);
        if (arps) {
            thy_ui_draw_text(out, 112u, 42u, "ARP", 3u, 0);
            thy_ui_draw_text(out, 112u, 49u,
                arps == 3u ? "1+2" : arps == 1u ? "1" : "2", 3u, 0);
        }
    }
}

static void large_text(uint8_t *out, unsigned x, unsigned y,
    const char *text, unsigned limit)
{
    uint8_t row[THY_UI_WIDTH] = {0};
    thy_ui_draw_text(row, 0, 0, text, limit, 0);
    for (unsigned col = 0u; col < limit * 5u && col < THY_UI_WIDTH; ++col)
        for (unsigned bit = 0u; bit < 5u; ++bit)
            if (row[col] & (1u << bit))
                thy_ui_draw_rectangle(out, x + col * 2u, y + bit * 2u, 2, 2, 1);
}

static void note_text(uint8_t *out, unsigned x, unsigned y,
    const char *text, unsigned length, int bright, unsigned scale)
{
    uint8_t glyphs[THY_UI_WIDTH] = {0};
    thy_ui_draw_text(glyphs, 0u, 0u, text, length, 0);
    for (unsigned col = 0u; col < length * 5u; ++col)
        for (unsigned bit = 0u; bit < 5u; ++bit)
            if (glyphs[col] & (1u << bit))
                for (unsigned dx = 0u; dx < scale; ++dx)
                    for (unsigned dy = 0u; dy < scale; ++dy) {
                        unsigned px = x + col * scale + dx;
                        unsigned py = y + bit * scale + dy;
                        /* The panel is binary. Stable spatial dithering gives
                         * lower apparent brightness without temporal flicker. */
                        if (bright || !((px + py) & 1u))
                            thy_ui_draw_pixel(out, px, py, 1);
                    }
}

static void simple_pattern(uint8_t *out, const play_snapshot *snapshot)
{
    unsigned count = 0u, longest = 0u;
    for (unsigned note = 0u; note < 128u; ++note)
        if (snapshot->pitches[note] || snapshot->pool[note]) {
            char name[6];
            thy_ui_note_name((int)note, name);
            unsigned length = 0u;
            while (name[length]) ++length;
            if (length > longest) longest = length;
            ++count;
        }
    if (!count) { large_text(out, 29u, 22u, "READY", 5u); return; }
    unsigned shown = 0u;
    for (unsigned note = 0u; note < 128u; ++note) {
        if (!snapshot->pitches[note] && !snapshot->pool[note]) continue;
        char name[6];
        thy_ui_note_name((int)note, name);
        unsigned length = 0u;
        while (name[length]) ++length;
        unsigned x, y, scale = count <= 5u ? 2u : 1u;
        if (count == 5u) {
            /* Keep a five-note chord as readable as a triad. Four corner
             * labels follow ascending pitch; the fifth sits in the center.
             * MIDI names are at most four glyphs (C#-1), so their 40-pixel
             * cells fit inside x2..105. Three ten-pixel rows with two blank
             * rows between them leave the header, icons and info slots clear. */
            if (shown < 4u) {
                x = shown & 1u ? 106u - length * 10u : 2u;
                y = shown < 2u ? 12u : 36u;
            } else {
                x = (108u - length * 10u) / 2u;
                y = 24u;
            }
        } else if (scale == 2u) {
            unsigned columns = count <= 3u && longest <= 3u ? count : count == 1u ? 1u : 2u;
            unsigned rows = (count + columns - 1u) / columns;
            unsigned width = 108u / columns;
            x = shown % columns * width + (width - length * 10u) / 2u;
            y = 12u + (30u - rows * 10u - (rows - 1u) * 5u) / 2u + shown / columns * 15u;
        } else {
            /* Keep the side rail clear. Five columns by five rows show 25
             * labels, or 24 with an explicit +N for a denser sounding set. */
            unsigned row = shown / 5u;
            unsigned col = shown % 5u;
            x = 2u + col * 21u; y = 13u + row * 7u;
            if (shown == 24u && count > 25u) {
                char extra[6];
                (void)number(extra, append(extra, 0u, sizeof extra, "+"), sizeof extra, count - shown);
                thy_ui_draw_text(out, x, y, extra, 4u, 0);
                break;
            }
        }
        note_text(out, x, y, name, length, snapshot->pitches[note], scale);
        if (++shown == 25u) break;
    }
}

static int wave(unsigned phase)
{
    static const int8_t sine[32] = {0,25,49,71,90,106,117,125,127,125,117,106,
        90,71,49,25,0,-25,-49,-71,-90,-106,-117,-125,-127,-125,-117,-106,
        -90,-71,-49,-25};
    unsigned index = (phase / 8u) % 32u, fraction = phase % 8u;
    int left = sine[index], right = sine[(index + 1u) % 32u];
    return left + (right - left) * (int)fraction / 8;
}

static void nodal_pattern(uint8_t *out, unsigned pitch_sum, unsigned notes,
    unsigned root, uint32_t now_ms, int motion)
{
    unsigned pitch = notes ? pitch_sum : root;
    unsigned m = 2u + pitch % 5u, n = 1u + (pitch / 5u + notes) % 5u;
    if (m == n) n = n % 6u + 1u;
    unsigned phase = motion && notes ? (now_ms / 30u) % 256u : 0u;
    /* Chladni-inspired note visualization, not sampled audio or a physical
     * simulation. All pictures have fixed work bounds and integer arithmetic. */
    /* These two horizontal waves are invariant across all 29 scanlines.
     * Reuse their exact integer samples (210 bytes of bounded frame scratch)
     * instead of evaluating four interpolated waves at every pixel. */
    int8_t horizontal_m[105], horizontal_n[105];
    for (unsigned x = 0u; x < 105u; ++x) {
        unsigned sx = x * 128u / 104u;
        horizontal_m[x] = (int8_t)wave(sx * m + phase + 64u);
        horizontal_n[x] = (int8_t)wave(sx * n + 64u);
    }
    for (unsigned y = 0u; y < 29u; ++y) {
        unsigned sy = y * 128u / 28u;
        int vertical_n = wave(sy * n + 64u);
        int vertical_m = wave(sy * m + phase + 64u);
        for (unsigned x = 0u; x < 105u; ++x) {
            int a = horizontal_m[x] * vertical_n;
            int b = horizontal_n[x] * vertical_m;
            int distance = a - b;
            if (distance < 0) distance = -distance;
            if (distance < (notes ? 1350 : 650))
                thy_ui_draw_pixel(out, x + 2u, y + 12u, 1);
        }
    }
}

static void plot_line(uint8_t *out, int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int error = dx - dy;
    for (;;) {
        if (x0 >= 1 && x0 < 108 && y0 >= 12 && y0 < 42)
            thy_ui_draw_pixel(out, (unsigned)x0, (unsigned)y0, 1);
        if (x0 == x1 && y0 == y1) break;
        int doubled = error * 2;
        if (doubled > -dy) { error -= dy; x0 += sx; }
        if (doubled < dx) { error += dx; y0 += sy; }
    }
}

static void orbit_pattern(uint8_t *out, const play_snapshot *snapshot,
    uint32_t now_ms, int motion)
{
    /* One point per sounding pitch class, connected to a rotating satellite.
     * Octaves alter satellite phase, while the stable twelve-point ring keeps
     * the harmonic constellation readable. */
    uint8_t classes[12] = {0};
    for (unsigned note = 0u; note < 128u; ++note)
        if (snapshot->pitches[note]) classes[note % 12u] = 1u;
    for (unsigned pc = 0u; pc < 12u; ++pc) {
        unsigned angle = pc * 256u / 12u;
        int x = 54 + wave(angle) * 43 / 127;
        int y = 27 + wave(angle + 64u) * 13 / 127;
        if (classes[pc]) {
            thy_ui_draw_rectangle(out, (unsigned)(x - 1), (unsigned)(y - 1), 3u, 3u, 1);
            unsigned phase = angle + snapshot->pitch_sum * 3u +
                (motion ? (now_ms / 25u) % 256u : 0u);
            int sx = 54 + wave(phase) * 24 / 127;
            int sy = 27 + wave(phase + 64u) * 9 / 127;
            plot_line(out, x, y, sx, sy);
        } else thy_ui_draw_pixel(out, (unsigned)x, (unsigned)y, 1);
    }
    thy_ui_draw_pixel(out, 54u, 27u, 1);
}

static void fifths_pattern(uint8_t *out, const play_snapshot *snapshot,
    unsigned root, uint32_t now_ms, int motion)
{
    static const uint8_t order[12] = {0,7,2,9,4,11,6,1,8,3,10,5};
    static const char *const names[12] = {"C","G","D","A","E","B","F#","C#","G#","D#","A#","F"};
    static const uint8_t xs[12] = {54,72,85,90,85,72,54,36,23,18,23,36};
    static const uint8_t ys[12] = {18,19,23,28,33,36,38,36,33,28,23,19};
    static const uint8_t tx[12] = {52,77,95,101,95,77,49,25,5,0,5,27};
    static const uint8_t ty[12] = {11,13,19,26,34,40,42,40,34,26,19,13};
    uint8_t sounding[12] = {0}, pool[12] = {0};
    for (unsigned note = 0u; note < 128u; ++note) {
        if (snapshot->pitches[note]) sounding[note % 12u] = 1u;
        if (snapshot->pool[note]) pool[note % 12u] = 1u;
    }
    int first = -1, previous = -1;
    for (unsigned step = 0u; step < 12u; ++step)
        if (sounding[order[step]]) {
            if (previous >= 0) plot_line(out, xs[previous], ys[previous], xs[step], ys[step]);
            else first = (int)step;
            previous = (int)step;
        }
    if (previous >= 0 && previous != first)
        plot_line(out, xs[previous], ys[previous], xs[first], ys[first]);
    for (unsigned step = 0u; step < 12u; ++step) {
        unsigned pc = order[step], x = xs[step], y = ys[step];
        note_text(out, tx[step], ty[step], names[step], names[step][1] ? 2u : 1u,
            sounding[pc] || pc == root % 12u, 1u);
        if (sounding[pc]) {
            thy_ui_draw_rectangle(out, x - 1u, y - 1u, 3u, 3u, 1);
            if (motion && (now_ms / 160u) % 3u == 1u) {
                thy_ui_draw_pixel(out, x - 2u, y, 1);
                thy_ui_draw_pixel(out, x + 2u, y, 1);
            }
        } else if (pool[pc]) {
            for (unsigned dx = 0u; dx < 3u; ++dx)
                for (unsigned dy = 0u; dy < 3u; ++dy)
                    if (!((dx + dy) & 1u)) thy_ui_draw_pixel(out, x + dx - 1u, y + dy - 1u, 1);
        } else thy_ui_draw_pixel(out, x, y, 1);
        /* The tuning root has a permanent open square, independent of which
         * Arp gate happens to sound. Pitch order never rotates with animation. */
        if (pc == root % 12u) {
            thy_ui_draw_rectangle(out, x - 3u, y - 3u, 7u, 1u, 1);
            thy_ui_draw_rectangle(out, x - 3u, y + 3u, 7u, 1u, 1);
            thy_ui_draw_rectangle(out, x - 3u, y - 3u, 1u, 7u, 1);
            thy_ui_draw_rectangle(out, x + 3u, y - 3u, 1u, 7u, 1);
        }
    }
}

static void waves_pattern(uint8_t *out, const play_snapshot *snapshot,
    unsigned root, uint32_t now_ms, int motion)
{
    unsigned frequencies[4] = {0}, count = 0u;
    for (unsigned note = 0u; note < 128u && count < 4u; ++note)
        if (snapshot->pitches[note]) frequencies[count++] = 1u + note % 12u;
    if (!count) { frequencies[0] = root % 12u + 1u; count = 1u; }
    unsigned phase = motion && snapshot->notes ? (now_ms / 18u) % 256u : 0u;
    int previous = 27;
    for (unsigned x = 1u; x < 107u; ++x) {
        int sample = 0;
        for (unsigned n = 0u; n < count; ++n)
            sample += wave(x * frequencies[n] * 2u + phase * (n + 1u));
        int y = 27 + sample * 12 / ((int)count * 127);
        if (x > 1u) plot_line(out, (int)x - 1, previous, (int)x, y);
        previous = y;
    }
}

static void piano_pattern(uint8_t *out, const play_snapshot *snapshot)
{
    static const uint8_t natural[] = {0,2,4,5,7,9,11};
    static const uint8_t accidental[] = {1,3,6,8,10};
    static const uint8_t after_white[] = {0,1,3,4,5};
    uint8_t held[12] = {0}, pool[12] = {0};
    for (unsigned note = 0u; note < 128u; ++note) {
        if (snapshot->pitches[note]) held[note % 12u] = 1u;
        if (snapshot->pool[note]) pool[note % 12u] = 1u;
    }
    for (unsigned key = 0u; key < 7u; ++key) {
        unsigned x = 1u + key * 15u;
        thy_ui_draw_rectangle(out, x, 13u, 14u, 29u, 1);
        thy_ui_draw_rectangle(out, x + 1u, 14u, 12u, 27u, 0);
        for (unsigned dx = 1u; dx < 13u; ++dx)
            for (unsigned dy = 1u; dy < 28u; ++dy)
                if (held[natural[key]] || (pool[natural[key]] && !((dx + dy) & 1u)))
                    thy_ui_draw_pixel(out, x + dx, 13u + dy, 1);
        thy_ui_draw_text(out, x + 5u, 35u,
            (const char *[]) {"C","D","E","F","G","A","B"}[key], 1u, held[natural[key]]);
    }
    for (unsigned key = 0u; key < 5u; ++key) {
        unsigned x = 11u + after_white[key] * 15u;
        thy_ui_draw_rectangle(out, x, 13u, 8u, 17u, 0);
        for (unsigned dx = 1u; dx < 7u; ++dx)
            for (unsigned dy = 1u; dy < 16u; ++dy)
                if (held[accidental[key]] || (pool[accidental[key]] && !((dx + dy) & 1u)))
                    thy_ui_draw_pixel(out, x + dx, 13u + dy, 1);
    }
}

/* The workspace reads cached C analysis only. Its stable circle never rotates
 * or repurposes pads; all ordinary attacks/releases remain in the Play engine. */
static void harmony_link(uint8_t *out, int x0, int y0, int x1, int y1, int dotted)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx - dy;
    unsigned step = 0u;
    for (;;) {
        if (x0 >= 1 && x0 < 63 && y0 >= 12 && y0 < 56 &&
            (!dotted || !(step & 1u)))
            thy_ui_draw_pixel(out, (unsigned)x0, (unsigned)y0, 1);
        if (x0 == x1 && y0 == y1) break;
        int doubled = error * 2;
        if (doubled > -dy) { error -= dy; x0 += sx; }
        if (doubled < dx) { error += dx; y0 += sy; }
        ++step;
    }
}

static unsigned harmony_classes(uint16_t mask)
{
    unsigned count = 0u;
    for (unsigned n = 0u; n < 12u; ++n) count += (mask >> n) & 1u;
    return count;
}

static void harmony_ring(uint8_t *out, const thy_harmony_workspace *h,
    uint16_t selected, uint32_t now_ms, int motion)
{
    static const uint8_t order[12] = {0,7,2,9,4,11,6,1,8,3,10,5};
    static const char *const names[12] = {"C","G","D","A","E","B","F#","C#","G#","D#","A#","F"};
    static const uint8_t xs[12] = {30,40,47,50,47,40,30,20,15,12,15,20};
    static const uint8_t ys[12] = {19,21,27,34,41,47,49,47,41,34,27,21};
    static const uint8_t tx[12] = {28,44,53,56,53,44,25,10,1,0,1,11};
    static const uint8_t ty[12] = {12,15,23,32,41,50,52,50,41,32,23,15};
    for (unsigned layer = 0u; layer < 2u; ++layer) {
        uint16_t mask = layer ? h->input_mask : selected;
        int first = -1, previous = -1;
        for (unsigned n = 0u; n < 12u; ++n) if (mask & (1u << order[n])) {
            if (previous >= 0)
                harmony_link(out, xs[previous], ys[previous], xs[n], ys[n], !layer);
            else first = (int)n;
            previous = (int)n;
        }
        if (previous >= 0 && previous != first)
            harmony_link(out, xs[previous], ys[previous], xs[first], ys[first], !layer);
    }
    uint32_t elapsed = now_ms - h->changed_ms;
    for (unsigned n = 0u; n < 12u; ++n) {
        unsigned pc = order[n], x = xs[n], y = ys[n];
        int current = !!(h->input_mask & (1u << pc));
        /* One-bit dither destroys small accidental glyphs. Keep the fixed
         * labels readable; filled/open nodes carry the live distinction. */
        note_text(out, tx[n], ty[n], names[n], names[n][1] ? 2u : 1u, 1, 1u);
        if (current) thy_ui_draw_rectangle(out, x - 1u, y - 1u, 3u, 3u, 1);
        else if (selected & (1u << pc)) {
            thy_ui_draw_rectangle(out, x - 1u, y - 1u, 3u, 3u, 1);
            thy_ui_draw_pixel(out, x, y, 0);
        } else if (h->key_mask & (1u << pc))
            thy_ui_draw_pixel(out, x, y, 1);
        if (pc == h->source_root) {
            thy_ui_draw_rectangle(out, x - 2u, y - 2u, 5u, 1u, 1);
            thy_ui_draw_rectangle(out, x - 2u, y + 2u, 5u, 1u, 1);
            thy_ui_draw_rectangle(out, x - 2u, y - 2u, 1u, 5u, 1);
            thy_ui_draw_rectangle(out, x + 2u, y - 2u, 1u, 5u, 1);
        }
        /* Brief local ripples announce a new physical harmony. They stop
         * after 720 ms; idle text/whole-screen bytes do not churn forever. */
        if (current && motion && elapsed < 720u) {
            unsigned radius = 2u + elapsed / 180u;
            thy_ui_draw_pixel(out, x - radius, y, 1);
            thy_ui_draw_pixel(out, x + radius, y, 1);
        }
    }
}

static int harmony_label_lines(uint8_t *out, unsigned y, const char *label)
{
    unsigned length = 0u;
    while (label[length]) ++length;
    thy_ui_draw_text(out, 68u, y, label, 12u, 0);
    if (length > 12u) thy_ui_draw_text(out, 68u, y + 8u, label + 12u, 12u, 0);
    return length > 12u;
}

static void harmony_base_label(const thy_engine *engine,char label[32])
{
    if(engine->harmony.base_mask)
        thy_engine_harmony_label(engine->harmony.base_mask,engine->harmony.current_root,label);
    else (void)append(label,0u,32u,"Play a chord");
}

static void harmony_status(uint8_t *out,const thy_engine *engine)
{
    const thy_harmony_workspace *h=&engine->harmony;
    const thy_instrument *i=engine->instrument;
    char line[32];unsigned used=append(line,0u,sizeof line,h->control_bank?"OCT ":"SUG ");
    uint8_t physical=thy_instrument_physical_pad(i,h->octave_target?48u:0u);
    used=append(line,used,sizeof line,physical>=96u?"?":physical<48u?"L":"R");
    int shift=h->octave_target?i->performance.melody_octave:i->performance.chord_octave;
    used=append(line,used,sizeof line,shift<0?"-":"+");
    used=number(line,used,sizeof line,(unsigned)(shift<0?-shift:shift));
    (void)append(line,used,sizeof line,h->page==THY_HARMONY_KEY?" KEY PREVIEW":" <BANK >SET O MENU");
    thy_ui_draw_text(out,2u,58u,line,25u,0);
}

static void harmony_short_reason(const thy_harmony_choice *choice,char out[32])
{
    if(choice->reason==THY_HARMONY_PATTERN_NEXT)(void)append(out,0u,32u,"Next step");
    else if(choice->reason==THY_HARMONY_BORROWED)(void)append(out,0u,32u,"Borrowed");
    else if(choice->reason==THY_HARMONY_RESOLVE)(void)append(out,0u,32u,"Resolution");
    else if(choice->reason==THY_HARMONY_SECONDARY){
        static const char *const roots[]={"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
        unsigned used=append(out,0u,32u,"Leads to ");(void)append(out,used,32u,roots[(choice->root+5u)%12u]);
    }
    else if(choice->reason==THY_HARMONY_PARALLEL)(void)append(out,0u,32u,"Parallel");
    else if(choice->reason==THY_HARMONY_EXTENSION)(void)append(out,0u,32u,"Extension");
    else if(choice->reason==THY_HARMONY_IN_KEY)(void)append(out,0u,32u,"In key");
    else {unsigned used=number(out,0u,32u,choice->shared);(void)append(out,used,32u," shared");}
}

static void harmony_workspace_page(uint8_t *out, const thy_state *state,
    uint32_t now_ms, int motion_enabled)
{
    const thy_engine *engine = state->engine;
    const thy_harmony_workspace *h = &engine->harmony;
    unsigned page = h->page <= THY_HARMONY_KEY ? h->page : THY_HARMONY_CHORDS;
    char label[32], line[32];
    thy_ui_draw_text(out,2u,1u,page==THY_HARMONY_CHORDS?"HARMONY":
        page==THY_HARMONY_SCALES?"HARMONY / SCALES":"HARMONY / KEY",25u,0);
    if(page==THY_HARMONY_CHORDS){
        thy_ui_draw_rectangle(out,0u,0u,128u,8u,0);
        thy_engine_harmony_key_label(engine,0,label);thy_ui_draw_text(out,2u,1u,label,17u,0);
        thy_ui_draw_text(out,93u,1u,h->lock_map?"LOCK":h->palette==1u?"BASIC":h->palette==2u?"SUS":h->palette==3u?"COLOUR":h->palette==4u?"EXTEND":h->direction==1u?"HOME":h->direction==2u?"COLOUR":h->direction==3u?"TRAVEL":"AUTO",6u,0);
    }
    thy_ui_draw_rectangle(out, 0u, 9u, 128u, 1u, 1);
    for (unsigned y = 13u; y < 56u; y += 4u) thy_ui_draw_pixel(out, 64u, y, 1);
    uint16_t selected = 0u;
    if (page == THY_HARMONY_CHORDS) {
        thy_ui_draw_text(out, 68u, 13u, "NOW", 12u, 0);
        harmony_base_label(engine,label);
        (void)harmony_label_lines(out, 21u, label);
        if (!h->cache_ready) {
            thy_ui_draw_text(out, 68u, 34u, "MAPPING...", 12u, 0);
        } else if (h->cursor < h->count && h->cursor < THY_HARMONY_WORKSPACE_CHOICES) {
            const thy_harmony_choice *choice = &h->choices[h->cursor];
            selected = choice->mask;
            unsigned used = append(line, 0u, sizeof line, "NEXT ");
            used = number(line, used, sizeof line, h->cursor + 1u);
            used = append(line, used, sizeof line, "/");
            (void)number(line, used, sizeof line, h->count);
            thy_ui_draw_text(out, 68u, 35u, line, 12u, 0);
            thy_engine_harmony_label(choice->mask, choice->root, label);
            if (!harmony_label_lines(out, 43u, label)) {
                harmony_short_reason(choice,label);
                thy_ui_draw_text(out, 68u, 51u, label, 12u, 0);
            }
        } else {
            thy_ui_draw_text(out, 68u, 34u, "NO MATCH", 12u, 0);
            thy_ui_draw_text(out, 68u, 43u, "Play a chord", 12u, 0);
        }
    } else if (page == THY_HARMONY_SCALES) {
        unsigned used = append(line, 0u, sizeof line, "SCALE ");
        used = number(line, used, sizeof line, h->scale_count ? h->cursor + 1u : 0u);
        used = append(line, used, sizeof line, "/");
        (void)number(line, used, sizeof line, h->scale_count);
        thy_ui_draw_text(out, 68u, 13u, line, 12u, 0);
        if (h->cursor < h->scale_count && h->cursor < THY_GUIDANCE_LIMIT) {
            const thy_scale_match *match = &h->scales[h->cursor];
            selected = match->mask;
            static const char *const roots[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            unsigned at = append(label, 0u, sizeof label, roots[match->root % 12u]);
            at = append(label, at, sizeof label, " ");
            (void)append(label, at, sizeof label, thy_guidance_scale_name(match->family));
            harmony_label_lines(out, 22u, label);
            unsigned used_extra = append(line, 0u, sizeof line, "+");
            used_extra = number(line, used_extra, sizeof line, match->extra);
            (void)append(line, used_extra, sizeof line, " notes");
            thy_ui_draw_text(out, 68u, 41u, line, 12u, 0);
            thy_ui_draw_text(out, 68u, 49u, "PREVIEW ONLY", 12u, 0);
        } else harmony_label_lines(out, 24u, "Play notes to discover");
    } else {
        selected = h->target_mask;
        thy_ui_draw_text(out, 68u, 13u, "KEY PREVIEW", 12u, 0);
        thy_engine_harmony_key_label(engine, 1, label);
        harmony_label_lines(out, 22u, label);
        unsigned used = number(line, 0u, sizeof line, harmony_classes(h->comparison.common));
        used = append(line, used, sizeof line, " = +");
        used = number(line, used, sizeof line, harmony_classes(h->comparison.added));
        used = append(line, used, sizeof line, " -");
        (void)number(line, used, sizeof line, harmony_classes(h->comparison.removed));
        thy_ui_draw_text(out, 68u, 41u, line, 12u, 0);
        thy_ui_draw_text(out, 68u, 49u, h->can_apply ? "APPLY: MENU" : "ADVICE ONLY", 12u, 0);
    }
    harmony_ring(out, h, selected, now_ms, motion_enabled && h->motion);
    harmony_status(out,engine);
}

static void guidance_page(uint8_t *out,const thy_instrument *i)
{
    const thy_guidance_ui *u=&i->guidance;char line[32];
    static const char *const titles[]={"HARMONY / SNAPSHOT","CHORD SUGGESTIONS","SCALES / TOP 24","MODULATION / ADVICE"};
    thy_ui_draw_text(out,2,1,titles[u->page],25,0);thy_ui_draw_rectangle(out,0,9,128,1,1);
    unsigned count=thy_guidance_ui_count(u),first=u->cursor>1u?u->cursor-1u:0u;
    if(first+3u>count)first=count>3u?count-3u:0u;
    for(unsigned n=0;n<3u && (first+n<count||(!count&&!n));++n){
        thy_guidance_ui_line(u,first+n,line);
        if(first+n==u->cursor)thy_ui_draw_text(out,1,14+n*11,u->editing?"*":">",1,0);
        thy_ui_draw_text(out,8,14+n*11,line,24,0);
    }
    if(u->page==3u && (u->cursor<2u||u->cursor==5u)){
        thy_ui_draw_text(out,2,48,u->cursor==5u?"Shared pivot triad":u->modulation.current_fits?"Captured notes fit both":"Compare; no key applied",25,0);
        thy_ui_draw_text(out,2,58,"<> BACK/EDIT  C:PLAY",25,0);
    }else {
        uint16_t mask=u->page==3u?(u->cursor==2u?u->modulation.common:u->cursor==3u?u->modulation.added:u->modulation.removed):
            u->page==1u&&u->count?u->rows.chords[u->cursor].mask:u->notes;
        thy_guidance_notes_text(mask,line);unsigned length=0;while(line[length])++length;
        /* All twelve pitch classes must remain readable, including chromatic. */
        thy_ui_draw_text(out,2,48,line,23,0);
        thy_ui_draw_text(out,2,58,length>23u?line+23:"<> BACK/OPEN  C:PLAY",25,0);
    }
}

static void library_page(uint8_t *out,const thy_instrument *instrument)
{
    const thy_library_ui *ui=&instrument->library_editor;char line[32];
    (void)thy_library_ui_title(ui,line,sizeof line);
    thy_ui_draw_text(out,2,1,line,25,0);thy_ui_draw_rectangle(out,0,9,128,1,1);
    unsigned selected=thy_library_ui_selected(ui),count=thy_library_ui_row_count(ui);
    unsigned first=selected>1u?selected-1u:0u;
    if(first+3u>count)first=count>3u?count-3u:0u;
    for(unsigned n=0;n<3u&&first+n<count;++n){
        (void)thy_library_ui_format(ui,&instrument->library,(uint8_t)(first+n),line,sizeof line);
        if(first+n==selected)thy_ui_draw_text(out,1,15+n*11,ui->editing?"*":">",1,0);
        thy_ui_draw_text(out,8,15+n*11,line,24,0);
    }
    if(ui->page==THY_LIBRARY_UI_NAME&&ui->name_cursor<12u)
        thy_ui_draw_rectangle(out,8u+ui->name_cursor*5u,23,4,1,1);
    thy_ui_draw_text(out,2,49,thy_library_ui_status(ui),25,0);
    thy_ui_draw_text(out,2,58,thy_library_ui_legend(ui),25,0);
}
static void reset_confirmation(uint8_t *out,const thy_state *state)
{
    char line[32];unsigned group=state->reset_group;
    if(group>=THY_MENU_GROUP_COUNT)return;
    section_icon(out,2u,1u,"reset");
    unsigned used=append(line,0u,sizeof line,"RESET ");
    used=append(line,used,sizeof line,thy_menu_groups[group].short_label);(void)append(line,used,sizeof line,"?");
    thy_ui_draw_text(out,12u,1u,line,23u,0);thy_ui_draw_rectangle(out,0u,9u,128u,1u,1);
    const char *scope="MENU OPTIONS",*kept="SAVED CONTENT KEPT";
    if(state->reset_track<THY_SEQ_TRACKS){
        used=append(line,0u,sizeof line,"TRACK ");(void)number(line,used,sizeof line,state->reset_track+1u);
        scope=line;kept="NOTES AND SONG KEPT";
    }else if(group==thy_menu_rows[THY_ROW_ARPS_OPEN].group){
        used=append(line,0u,sizeof line,"ARP ");(void)number(line,used,sizeof line,state->reset_targets[THY_MENU_ARP]+1u);scope=line;
    }else if(group==thy_menu_rows[THY_ROW_SENSITIVITY_TARGET].group){
        unsigned pad=state->reset_targets[THY_MENU_PAD];
        scope=pad==97u||(pad<96u&&pad>=48u)?"RIGHT DEFAULTS":"LEFT DEFAULTS";kept="PAD OVERRIDES KEPT";
    }else if(group==thy_menu_rows[THY_ROW_LIGHTS_VIEW].group){
        const thy_menu_row *row=&thy_menu_rows[THY_ROW_LIGHTS_VIEW];unsigned view=state->reset_targets[THY_MENU_VIEW];
        used=append(line,0u,sizeof line,"VIEW ");
        (void)append(line,used,sizeof line,view<row->option_count?thy_menu_options[row->option_first+view].label:"CURRENT");scope=line;
    }else if(group==thy_menu_rows[THY_ROW_MIDI_ROUTINGPROFILE].group){
        unsigned route=state->reset_targets[THY_MENU_ROUTE];
        used=append(line,0u,sizeof line,"ROUTE ");
        if(route<3u)used=append(line,used,sizeof line,route==0u?"WHOLE":route==1u?"LEFT":"RIGHT");
        else {used=append(line,used,sizeof line,"ZONE ");used=number(line,used,sizeof line,route-2u);}
        (void)append(line,used,sizeof line," + GLOBAL");scope=line;
    }else if(group==thy_menu_rows[THY_ROW_PERFORMANCE_VELOCITY].group){
        unsigned side=state->reset_targets[THY_MENU_SIDE],route=state->reset_targets[THY_MENU_ROUTE];
        used=append(line,0u,sizeof line,side==1u?"VEL LEFT / ":side==2u?"VEL RIGHT / ":"VEL BOTH / ");
        (void)append(line,used,sizeof line,route==1u?"LEFT MIDI":route==2u?"RIGHT MIDI":"EDIT ROUTE");
        scope=line;kept="CALIBRATION KEPT";
    }else if(group==thy_menu_rows[THY_ROW_ENGINES_PLAY].group){
        unsigned shortcut=state->reset_targets[THY_MENU_SHORTCUT];
        used=append(line,0u,sizeof line,"KEY + SHORTCUT ");
        if(shortcut<2u)(void)append(line,used,sizeof line,shortcut?"12":"5");
        else {used=append(line,used,sizeof line,shortcut<9u?"M":"U");(void)number(line,used,sizeof line,shortcut<9u?shortcut-1u:shortcut-8u);}
        scope=line;
    }
    thy_ui_draw_text(out,2u,15u,scope,25u,0);thy_ui_draw_text(out,2u,24u,kept,25u,0);
    thy_ui_draw_text(out,4u,36u,state->reset_confirm?"  CANCEL":"> CANCEL",20u,0);
    thy_ui_draw_text(out,4u,45u,state->reset_confirm?"> RESET OPTIONS":"  RESET OPTIONS",24u,0);
    thy_ui_draw_rectangle(out,0u,54u,128u,1u,1);
    thy_ui_draw_text(out,2u,58u,state->notice==THY_NOTICE_RELEASE?"RELEASE PADS / STOP":"UP/DN > OK O EXIT < BACK",25u,0);
}
static void studio_play(uint8_t *out, const thy_state *state,
    const char *scale_name, int din_ready, int usb_ready, uint32_t now_ms,
    int motion_enabled)
{
    const thy_instrument *instrument = state->instrument;
    unsigned view = thy_view_available(instrument->display_view) ?
        instrument->display_view : THY_VIEW_DETAILED;
    const thy_view_config *config = &instrument->display_config[view];
    play_snapshot snapshot = play_snapshot_read(state);
    int musical = snapshot.notes != 0u;
    for (unsigned note = 0u; !musical && note < 128u; ++note)
        musical = snapshot.pool[note] != 0u;
    if (config->header || musical) {
        thy_ui_draw_text(out, 2u, 1u, musical ? snapshot.musical_name :
            scale_name != NULL ? scale_name : "THY666", 21u, 0);
        thy_ui_draw_rectangle(out, 0u, 9u, 108u, 1u, 1);
    }
    connection_icons(out, state, din_ready, usb_ready);
    if (view == THY_VIEW_DETAILED) {
        for (unsigned row = 0u; row < THY_VIEW_DETAIL_ROWS; ++row)
            draw_view_line(out, (config->header || musical ? 12u : 4u) + row * 8u,
                state, &snapshot, config->detail_rows[row], scale_name, din_ready, usb_ready);
        return;
    }
    int motion = motion_enabled && config->motion;
    if (view == THY_VIEW_PIANO) piano_pattern(out, &snapshot);
    else if (view == THY_VIEW_CHLADNI)
        nodal_pattern(out, snapshot.pitch_sum, snapshot.notes,
            instrument->performance.root, now_ms, motion);
    else if (view == THY_VIEW_ORBIT) orbit_pattern(out, &snapshot, now_ms, motion);
    else if (view == THY_VIEW_FIFTHS)
        fifths_pattern(out, &snapshot, instrument->performance.root, now_ms, motion);
    else if (view == THY_VIEW_WAVES)
        waves_pattern(out, &snapshot, instrument->performance.root, now_ms, motion);
    else simple_pattern(out, &snapshot);
    draw_view_line(out, 48u, state, &snapshot, config->slots[0], scale_name, din_ready, usb_ready);
    draw_view_line(out, 58u, state, &snapshot, config->slots[1], scale_name, din_ready, usb_ready);
}

void thy_studio_ui_init(thy_studio_ui *ui)
{
    if (ui == NULL) return;
    *ui = (thy_studio_ui){0};
    ui->animations_enabled = 1u;
    ui->navigation_animations_enabled = 1u;
}

void thy_studio_ui_set_navigation_animations(thy_studio_ui *ui, int enabled)
{
    if (ui == NULL) return;
    ui->navigation_animations_enabled = enabled ? 1u : 0u;
    if (!enabled && ui->transitioning) {
        ui->transitioning = 0u;
        ui->transition_elapsed_ms = THY_STUDIO_UI_TRANSITION_MS;
    }
}

void thy_studio_ui_set_build_identity(thy_studio_ui *ui, const char *identity)
{
    if (ui != NULL) ui->build_identity = identity;
}

int thy_studio_ui_boot_active(const thy_studio_ui *ui)
{
    return ui != NULL && ui->boot_phase >= THY_STUDIO_BOOT_REVEAL &&
        ui->boot_phase <= THY_STUDIO_BOOT_HANDOFF;
}

void thy_studio_ui_boot_begin(thy_studio_ui *ui, uint32_t now_ms)
{
    if (ui == NULL) return;
    ui->boot_phase = THY_STUDIO_BOOT_REVEAL;
    ui->boot_stage = THY_STUDIO_BOOT_START;
    ui->boot_ready = 0u;
    ui->boot_elapsed_ms = 0u;
    ui->boot_handoff_elapsed_ms = 0u;
    ui->last_render_ms = now_ms;
    ui->initialized = 0u;
    ui->transitioning = 0u;
}

void thy_studio_ui_boot_progress(thy_studio_ui *ui, uint8_t stage)
{
    if (thy_studio_ui_boot_active(ui) && stage <= THY_STUDIO_BOOT_MIDI &&
        stage >= ui->boot_stage) ui->boot_stage = stage;
}

void thy_studio_ui_boot_ready(thy_studio_ui *ui)
{
    if (thy_studio_ui_boot_active(ui)) ui->boot_ready = 1u;
}

void thy_studio_ui_boot_skip(thy_studio_ui *ui)
{
    if (!thy_studio_ui_boot_active(ui)) return;
    ui->boot_phase = THY_STUDIO_BOOT_SKIPPED;
    ui->initialized = 0u;
    ui->transitioning = 0u;
}

static unsigned smoothstep(unsigned elapsed, unsigned duration)
{
    unsigned phase = elapsed >= duration ? 1024u : elapsed * 1024u / duration;
    return phase * phase * (3072u - 2u * phase) / 1048576u;
}

static void wordmark(uint8_t *out, unsigned top)
{
    /* Shared original geometric wordmark, independent of the menu font. */
    static const uint8_t glyphs[6][7] = {
        {31, 4, 4, 4, 4, 4, 4}, {17, 17, 17, 31, 17, 17, 17},
        {17, 17, 10, 4, 4, 4, 4}, {14, 16, 16, 30, 17, 17, 14},
        {14, 16, 16, 30, 17, 17, 14}, {14, 16, 16, 30, 17, 17, 14}
    };
    for (unsigned glyph = 0u; glyph < 6u; ++glyph)
        for (unsigned y = 0u; y < 7u; ++y)
            for (unsigned x = 0u; x < 5u; ++x)
                if (glyphs[glyph][y] & (1u << (4u - x)))
                    thy_ui_draw_pixel(out, 70u + glyph * 8u + x, top + y, 1);
}

static void emblem(uint8_t *out,unsigned top)
{
    /* The supplied 48x40 pixel master is copied exactly, not scaled or
     * interpreted as the OLED controller's page-addressed framebuffer. */
    for(unsigned y=0u;y<THY_BRAND_LOGO_HEIGHT;++y)
        for(unsigned x=0u;x<THY_BRAND_LOGO_WIDTH;++x)
            if(thy_brand_logo_bits[y*THY_BRAND_LOGO_STRIDE+x/8u]&(1u<<(7u-x%8u)))
                thy_ui_draw_pixel(out,4u+x,top+y,1);
}

static void credits(uint8_t *out, const thy_studio_ui *ui)
{
    emblem(out,4u);
    wordmark(out, 5u);
    thy_ui_draw_text(out, 83, 20, "OPEN", 16, 0);
    thy_ui_draw_text(out, 70, 28, "INSTRUMENT", 16, 0);
    thy_ui_draw_text(out, 81, 37, "DCVLT", 16, 0);
    thy_ui_draw_text(out, 73, 44, "+ OPENAI", 16, 0);
    if (ui->build_identity != NULL)
        thy_ui_draw_text(out, 2, 50, ui->build_identity, 25, 0);
    thy_ui_draw_text(out, 2, 59, "O EXIT  < BACK", 25, 0);
}

static void boot_brand(uint8_t *out, const thy_studio_ui *ui, int motion,
    int has_state)
{
    static const char *stages[] = {
        "STARTING", "DISPLAY READY", "CONTROLS READY", "PADS READY", "MIDI READY"
    };
    unsigned reveal = motion ? smoothstep(ui->boot_elapsed_ms,
        THY_STUDIO_UI_BOOT_REVEAL_MS) : 1024u;
    unsigned extent = reveal * 64u / 1024u;
    emblem(out,12u);
    wordmark(out, 13u);
    thy_ui_draw_text(out, 83, 30, "OPEN", 16, 0);
    thy_ui_draw_text(out, 70, 38, "INSTRUMENT", 16, 0);
    /* Reveal ink from the center. Captions are always legible and report the
     * caller's actual milestone rather than implying initialization progress. */
    if (extent < 64u)
        for (unsigned y = 0u; y < 53u; ++y)
            for (unsigned x = 0u; x < THY_UI_WIDTH; ++x)
                if (x + extent < 64u || x > 64u + extent)
                    thy_ui_draw_pixel(out, x, y, 0);
    const char *caption = ui->boot_ready && has_state ? "READY TO EXIT" :
        stages[ui->boot_stage];
    unsigned length = 0u;
    while (caption[length]) ++length;
    thy_ui_draw_text(out, (128u - length * 5u) / 2u, 57, caption, 25, 0);
}

static int boot_interrupted(const thy_state *state)
{
    if (state == NULL) return 0;
    if(state->reset_open)return 1;
    if (state->level != THY_STATE_PLAY) return 1;
    if (state->instrument != NULL && state->instrument->core != NULL) {
        const thy_core *core = state->instrument->core;
        if (core->sounding_count || core->pending_keys) return 1;
        for (unsigned pad = 0u; pad < THY_PAD_COUNT; ++pad)
            if (core->owners[pad].held) return 1;
    }
    return 0;
}

static void boot_handoff(uint8_t *out, const uint8_t *brand, unsigned progress)
{
    /* A continuous upward handoff, with exact old/new endpoints. Snapshot
     * each target column before mixing so no frame/page can read its own
     * already-shifted pixels. Eight temporary bytes, no second framebuffer. */
    for (unsigned x = 0u; x < THY_UI_WIDTH; ++x) {
        uint8_t target_column[THY_UI_HEIGHT / 8u];
        for (unsigned page = 0u; page < THY_UI_HEIGHT / 8u; ++page)
            target_column[page] = out[page * THY_UI_WIDTH + x];
        for (unsigned page = 0u; page < THY_UI_HEIGHT / 8u; ++page) {
            uint8_t value = 0u;
            for (unsigned bit = 0u; bit < 8u; ++bit) {
                unsigned source_y = page * 8u + bit + progress;
                int enabled = source_y < THY_UI_HEIGHT ?
                    !!(brand[source_y / 8u * THY_UI_WIDTH + x] & (1u << (source_y % 8u))) :
                    !!(target_column[(source_y - THY_UI_HEIGHT) / 8u] &
                        (1u << (source_y % 8u)));
                if (enabled) value |= (uint8_t)(1u << bit);
            }
            out[page * THY_UI_WIDTH + x] = value;
        }
    }
}

static int render_boot(thy_studio_ui *ui, const thy_state *state,
    int motion_enabled, uint32_t now_ms, uint8_t *out)
{
    if (!thy_studio_ui_boot_active(ui)) return 0;
    if (boot_interrupted(state)) {
        thy_studio_ui_boot_skip(ui);
        return 0;
    }
    uint32_t step = now_ms - ui->last_render_ms;
    if (step > THY_STUDIO_UI_MAX_STEP_MS) step = THY_STUDIO_UI_MAX_STEP_MS;
    ui->last_render_ms = now_ms;
    ui->transitioning = 0u;
    if (state == NULL && ui->boot_phase == THY_STUDIO_BOOT_HANDOFF) {
        ui->boot_phase = THY_STUDIO_BOOT_WAIT;
        ui->boot_handoff_elapsed_ms = 0u;
    }
    if (ui->boot_phase != THY_STUDIO_BOOT_HANDOFF) {
        ui->boot_elapsed_ms += step;
        if (ui->boot_elapsed_ms >= THY_STUDIO_UI_BOOT_REVEAL_MS) {
            ui->boot_elapsed_ms = THY_STUDIO_UI_BOOT_REVEAL_MS;
            ui->boot_phase = THY_STUDIO_BOOT_WAIT;
        }
        if (ui->boot_ready && state != NULL &&
            (!motion_enabled || ui->boot_phase == THY_STUDIO_BOOT_WAIT)) {
            if (!motion_enabled) ui->boot_phase = THY_STUDIO_BOOT_DONE;
            else {
                /* The first handoff frame is the last complete branded
                 * frame, preventing an abrupt caption/position change. */
                for (unsigned i = 0u; i < THY_UI_FRAME_BYTES; ++i)
                    ui->transition_frame[i] = ui->previous_frame[i];
                ui->boot_phase = THY_STUDIO_BOOT_HANDOFF;
            }
        }
    } else {
        ui->boot_handoff_elapsed_ms += step;
        if (!motion_enabled || ui->boot_handoff_elapsed_ms >= THY_STUDIO_UI_BOOT_HANDOFF_MS) {
            ui->boot_handoff_elapsed_ms = THY_STUDIO_UI_BOOT_HANDOFF_MS;
            ui->boot_phase = THY_STUDIO_BOOT_DONE;
        }
    }
    if (ui->boot_phase == THY_STUDIO_BOOT_DONE) return 0;
    if (ui->boot_phase == THY_STUDIO_BOOT_HANDOFF) {
        unsigned progress = smoothstep(ui->boot_handoff_elapsed_ms,
            THY_STUDIO_UI_BOOT_HANDOFF_MS) * THY_UI_HEIGHT / 1024u;
        boot_handoff(out, ui->transition_frame, progress);
    } else {
        for (unsigned i = 0; i < THY_UI_FRAME_BYTES; ++i) out[i] = 0;
        boot_brand(out, ui, motion_enabled, state != NULL);
    }
    for (unsigned i = 0; i < THY_UI_FRAME_BYTES; ++i) ui->previous_frame[i] = out[i];
    ui->initialized = 0u;
    if (state != NULL) ui->transition_revision = state->transition_revision;
    return 1;
}

void thy_studio_ui_render(thy_studio_ui *ui, const thy_state *state,
    const char *scale_name, int din_ready, int usb_ready, uint32_t now_ms,
    uint8_t out[THY_UI_FRAME_BYTES])
{
    if (ui == NULL || out == NULL) return;
    for (unsigned i = 0; i < THY_UI_FRAME_BYTES; ++i) out[i] = 0;
    if (state == NULL) {
        if (render_boot(ui, NULL, ui->animations_enabled, now_ms, out)) return;
        thy_ui_draw_text(out, 2, 20, "THY666 / STARTING", 25, 0);
        ui->initialized = 0u;
        ui->transitioning = 0u;
        return;
    }
    int32_t animations = 1;
    (void)thy_state_get(state, THY_ROW_LIGHTS_ANIMATIONS, &animations);
    int motion_enabled = ui->animations_enabled && animations;
    int navigation_motion_enabled = motion_enabled && ui->navigation_animations_enabled;
    if (state->instrument&&(state->instrument->library_editor.open||state->instrument->guidance.open))thy_studio_ui_boot_skip(ui);
    if (state->engine && ((state->engine->calibration_open && state->engine->calibration) ||
        ((state->engine->arp_quick_open || state->engine->sequencer_ui.open) &&
        state->level == THY_STATE_PLAY))) thy_studio_ui_boot_skip(ui);
    /* Build the current Play frame before the startup handoff. This remains
     * a live projection: settings, readiness and musical ownership never wait
     * for the animation. The normal menu renderer runs immediately on input. */
    if (thy_studio_ui_boot_active(ui)) {
        if (state->instrument != NULL) {
            if (thy_engine_harmony_active(state->engine))
                harmony_workspace_page(out, state, now_ms, motion_enabled);
            else studio_play(out, state, scale_name, din_ready, usb_ready, now_ms,
                motion_enabled);
        } else {
            thy_ui_draw_text(out, 2, 1, "THY666 / PLAY", 25, 0);
            thy_ui_draw_rectangle(out, 0, 9, 128, 1, 1);
            thy_ui_draw_text(out, 2, 23, scale_name, 25, 0);
            thy_ui_draw_text(out, 2, 58, "O MENU", 25, 0);
        }
        if (render_boot(ui, state, motion_enabled, now_ms, out)) return;
        /* Boot completion and interruption begin a coherent normal frame;
         * they must not accidentally start a stale hierarchy transition. */
        ui->initialized = 0u;
        for (unsigned i = 0; i < THY_UI_FRAME_BYTES; ++i) out[i] = 0;
    }
    if (ui->initialized && state->transition_revision != ui->transition_revision &&
        navigation_motion_enabled) {
        for (unsigned i = 0; i < THY_UI_FRAME_BYTES; ++i)
            ui->transition_frame[i] = ui->previous_frame[i];
        ui->transition_started_ms = now_ms;
        ui->transition_elapsed_ms = 0u;
        ui->transition_direction = state->transition_direction > 0 ? 1 : -1;
        ui->transitioning = 1u;
    } else if (ui->transitioning) {
        /* A hierarchy change has a wall-clock deadline. Render the current
         * position after acquisition, LED or display delays; never replay
         * stale intermediate positions and stretch a short slide into a
         * multi-second backlog. Unsigned subtraction handles timer wrap. */
        uint32_t elapsed = now_ms - ui->transition_started_ms;
        ui->transition_elapsed_ms = elapsed < THY_STUDIO_UI_TRANSITION_MS ?
            elapsed : THY_STUDIO_UI_TRANSITION_MS;
    }
    ui->last_render_ms = now_ms;
    ui->transition_revision = state->transition_revision;
    if(state->reset_open)reset_confirmation(out,state);
    else if(state->instrument&&state->instrument->guidance.open)guidance_page(out,state->instrument);
    else if(state->instrument&&state->instrument->library_editor.open)library_page(out,state->instrument);
    else if (state->engine && state->engine->calibration_open && state->engine->calibration)
        calibration_page(out,state);
    else if (state->engine && state->engine->sequencer_ui.open && state->level == THY_STATE_PLAY)
        sequencer_canvas(out, state);
    else if (state->engine && state->engine->arp_quick_open && state->level == THY_STATE_PLAY)
        arp_quick(out, state);
    else if (state->about_open) credits(out, ui);
    else if (state->level == THY_STATE_PLAY) {
        if (state->instrument != NULL) {
            if (thy_engine_harmony_active(state->engine))
                harmony_workspace_page(out, state, now_ms, motion_enabled);
            else studio_play(out, state, scale_name, din_ready, usb_ready, now_ms,
                motion_enabled);
        }
        else {
            thy_ui_draw_text(out, 2, 1, "THY666 / PLAY", 25, 0);
            thy_ui_draw_rectangle(out, 0, 9, 128, 1, 1);
            thy_ui_draw_text(out, 2, 23, scale_name, 25, 0);
            thy_ui_draw_text(out, 2, 58, "O MENU", 25, 0);
        }
    } else menu(out, state);

    /* Unsigned subtraction handles the caller's 32-bit millisecond wrap. */
    uint32_t elapsed = ui->transition_elapsed_ms;
    if (!navigation_motion_enabled || elapsed >= THY_STUDIO_UI_TRANSITION_MS)
        ui->transitioning = 0u;
    if (ui->transitioning) {
        /* Smoothstep in Q10: exact endpoints/halfway, monotonic, no floating
         * point or overshoot. Independent from the legacy 120 ms UI. */
        unsigned eased = smoothstep(elapsed, THY_STUDIO_UI_TRANSITION_MS);
        unsigned progress = eased * THY_UI_WIDTH / 1024u;
        uint8_t row[THY_UI_WIDTH];
        for (unsigned page = 0; page < THY_UI_HEIGHT / 8u; ++page) {
            unsigned offset = page * THY_UI_WIDTH;
            for (unsigned x = 0; x < THY_UI_WIDTH; ++x) row[x] = out[offset + x];
            for (unsigned x = 0; x < THY_UI_WIDTH; ++x) {
                if (ui->transition_direction < 0)
                    out[offset + x] = x < THY_UI_WIDTH - progress ?
                        ui->transition_frame[offset + x + progress] :
                        row[x + progress - THY_UI_WIDTH];
                else out[offset + x] = x >= progress ?
                    ui->transition_frame[offset + x - progress] :
                    row[x + THY_UI_WIDTH - progress];
            }
        }
    }
    for (unsigned i = 0; i < THY_UI_FRAME_BYTES; ++i)
        ui->previous_frame[i] = out[i];
    ui->initialized = 1u;
}
