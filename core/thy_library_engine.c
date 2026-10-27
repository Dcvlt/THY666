/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_engine.h"

static int valid(const thy_engine *e){return e&&e->instrument&&e->instrument->core;}
static unsigned limit(unsigned kind){return kind==0u?8u:kind==1u?24u:kind==2u?3u:0u;}
static int result(int r){return r==THY_LIBRARY_OK?THY_OK:r==THY_LIBRARY_IN_USE?THY_ENGINE_LIBRARY_IN_USE:THY_INVALID_ARGUMENT;}
int thy_engine_library_quiet(const thy_engine *e)
{
    if(!valid(e)||e->calibration_open||e->rhythm_transport.state==THY_TRANSPORT_PLAYING||
       e->sequencer.capture.state!=THY_SEQ_CAPTURE_OFF||!thy_engine_channel_quiet(e)||
       (e->expression&&!thy_mpe_quiet(e->expression)))return 0;
    const thy_core *c=e->instrument->core;
    if(c->sounding_count||c->pending_keys||c->generated_count||c->sustain_count)return 0;
    for(unsigned p=0;p<96u;++p)if(c->owners[p].held||e->instrument->sensors[p].held||
        e->instrument->context_held[p])return 0;
    return 1;
}
static void changed(thy_engine *e)
{
    (void)thy_instrument_apply_scale(e->instrument);
    ++e->instrument->revision;
}
int thy_engine_user_scale(thy_engine *e,uint8_t slot)
{
    if(!valid(e)||slot>8u||(slot&&(!e->instrument->library.scales[slot-1u].used||
       !thy_library_scale_valid(&e->instrument->library.scales[slot-1u]))))return THY_INVALID_ARGUMENT;
    if(e->instrument->user_scale!=slot)thy_instrument_key_changed(e->instrument);
    e->instrument->user_scale=slot;changed(e);return THY_OK;
}
static int referenced(const thy_instrument *i,uint8_t kind,uint8_t slot)
{
    if(kind==0u){
        if(i->user_scale==slot+1u)return 1;
        for(unsigned n=0;n<12u;++n)if(i->scale_shortcuts[n]==71u+slot)return 1;
    } else if(kind==1u){
        if(i->chord_layout==THY_CHORD_LAYOUT_USER&&i->library.chords[slot].used)return 1;
        return thy_library_chord_referenced(&i->library,slot);
    } else if(kind==2u && i->chord_layout==THY_CHORD_LAYOUT_SIX_USER && i->six_bank==slot)return 1;
    return 0;
}
int thy_engine_library_delete(thy_engine *e,uint8_t kind,uint8_t slot)
{
    if(!valid(e)||slot>=limit(kind))return THY_INVALID_ARGUMENT;
    if(!thy_engine_library_quiet(e))return THY_ENGINE_LIBRARY_BUSY;
    if(referenced(e->instrument,kind,slot))return THY_ENGINE_LIBRARY_IN_USE;
    thy_library *lib=&e->instrument->library;
    int r=kind==0u?thy_library_delete_scale(lib,slot):kind==1u?
        thy_library_delete_chord(lib,slot):thy_library_delete_six(lib,slot);
    if(r==THY_LIBRARY_OK)changed(e);return result(r);
}
int thy_engine_library_set(thy_engine *e,uint8_t kind,uint8_t slot,const void *record,size_t bytes)
{
    size_t size=kind==0u?sizeof(thy_library_scale):kind==1u?sizeof(thy_library_chord):sizeof(thy_library_six);
    if(!valid(e)||!record||slot>=limit(kind)||bytes!=size)return THY_INVALID_ARGUMENT;
    if(!thy_engine_library_quiet(e))return THY_ENGINE_LIBRARY_BUSY;
    if(!(kind==0u?thy_library_scale_valid(record):kind==1u?thy_library_chord_valid(record):thy_library_six_valid(record)))return THY_INVALID_ARGUMENT;
    const uint8_t *raw=record;if(!raw[size-1u])return thy_engine_library_delete(e,kind,slot);
    thy_library *lib=&e->instrument->library;
    int r=kind==0u?thy_library_set_scale(lib,slot,record):kind==1u?
        thy_library_set_chord(lib,slot,record):thy_library_set_six(lib,slot,record);
    if(r==THY_LIBRARY_OK)changed(e);return result(r);
}
static void name(uint8_t out[13],uint8_t kind,uint8_t slot)
{
    const char *prefix=kind==0u?"Scale ":kind==1u?"Chord ":"Set ";unsigned n=0;
    while(prefix[n]){out[n]=(uint8_t)prefix[n];++n;}
    unsigned value=slot+1u;if(value>=10u)out[n++]=(uint8_t)('0'+value/10u);
    out[n++]=(uint8_t)('0'+value%10u);for(;n<13u;++n)out[n]=0;
}
int thy_engine_library_capture(const thy_engine *e,uint8_t kind,uint8_t slot,thy_library_ui_draft *out)
{
    if(!valid(e)||!out||slot>=limit(kind))return THY_INVALID_ARGUMENT;
    const thy_instrument *i=e->instrument;thy_library_ui_draft draft={0};
    if(kind==0u){
        if(!i->degree_count||i->degree_count>12u)return THY_INVALID_ARGUMENT;
        name(draft.scale.name,kind,slot);draft.scale.used=1u;uint16_t mask=0u;
        for(unsigned n=0;n<i->degree_count;++n){
            if(i->degrees[n]<0||i->degrees[n]>11||(!n&&i->degrees[n])||
               (n&&i->degrees[n]<=i->degrees[n-1u]))return THY_INVALID_ARGUMENT;
            mask|=(uint16_t)(1u<<i->degrees[n]);
        }
        if(thy_library_scale_set_mask(&draft.scale,mask)!=THY_LIBRARY_OK)return THY_INVALID_ARGUMENT;
    } else if(kind==1u){
        thy_chord_cell cell;if(thy_instrument_chord_cell(i,i->selected_chord,&cell)!=THY_OK||
            !cell.valid_mask||cell.root<i->performance.root||cell.root>127)return THY_INVALID_ARGUMENT;
        name(draft.chord.name,kind,slot);draft.chord.used=1u;
        /* User records are authored in C; normalize only sounding tones.
         * Inactive slot literals are source metadata, including zero. */
        draft.chord.root=(uint8_t)(cell.root-i->performance.root);draft.chord.valid_mask=cell.valid_mask;
        for(unsigned n=0;n<7u;++n){int value=cell.notes[n];
            if(cell.valid_mask&(1u<<n))value-=i->performance.root;
            if(value<0||value>127)return THY_INVALID_ARGUMENT;
            draft.chord.notes[n]=(uint8_t)value;}
    } else {
        if(i->six_bank>=3u||!i->library.six[i->six_bank].used)return THY_INVALID_ARGUMENT;
        draft.six=i->library.six[i->six_bank];name(draft.six.name,kind,slot);
    }
    *out=draft;return THY_OK;
}
int thy_engine_library_import(thy_engine *e,const uint8_t *file,size_t count)
{
    if(!valid(e)||!thy_library_validate(file,count))return THY_INVALID_ARGUMENT;
    if(!thy_engine_library_quiet(e)||e->instrument->library_editor.open||e->state.editing)return THY_ENGINE_LIBRARY_BUSY;
    /* Byte-only records have a checked padding-free wire representation. */
    const thy_library *incoming=(const void *)(file+THY_LIBRARY_HEADER_BYTES);
    thy_instrument *i=e->instrument;
    if(i->user_scale>THY_LIBRARY_SCALES||i->six_bank>=THY_LIBRARY_SIX_SETS)return THY_INVALID_ARGUMENT;
    if((i->user_scale&&!incoming->scales[i->user_scale-1u].used)||
       (i->chord_layout==THY_CHORD_LAYOUT_SIX_USER&&!incoming->six[i->six_bank].used))return THY_ENGINE_LIBRARY_IN_USE;
    for(unsigned n=0;n<12u;++n){
        if(i->scale_shortcuts[n]>70u+THY_LIBRARY_SCALES)return THY_INVALID_ARGUMENT;
        if(i->scale_shortcuts[n]>70u&&!incoming->scales[i->scale_shortcuts[n]-71u].used)
            return THY_ENGINE_LIBRARY_IN_USE;
    }
    if(thy_library_decode(&i->library,file,count)!=THY_LIBRARY_OK)return THY_INVALID_ARGUMENT;
    changed(e);return THY_OK;
}
int thy_engine_library_open(thy_engine *e,uint8_t kind,uint8_t slot)
{
    if(!valid(e)||e->calibration_open||e->sequencer_ui.open||e->instrument->guidance.open)return THY_STATE_BLOCKED;
    if(thy_library_ui_open(&e->instrument->library_editor,&e->instrument->library,kind,slot)!=THY_LIBRARY_OK)return THY_STATE_INVALID;
    if(e->state.editing)(void)thy_state_cancel(&e->state);
    e->arp_quick_open=0u;thy_engine_repeat_cancel(e);++e->state.transition_revision;return THY_STATE_OK;
}
void thy_engine_library_close(thy_engine *e)
{if(valid(e)){e->instrument->library_editor=(thy_library_ui){0};++e->state.transition_revision;}}
int thy_engine_library_navigate(thy_engine *e,uint8_t action)
{
    if(!valid(e)||!e->instrument->library_editor.open)return THY_STATE_UNAVAILABLE;
    thy_instrument *i=e->instrument;thy_library_ui *ui=&i->library_editor;
    uint8_t command=thy_library_ui_navigate(ui,&i->library,action);int r=THY_OK;
    if(command==THY_LIBRARY_UI_BACK||command==THY_LIBRARY_UI_PLAY){
        thy_engine_library_close(e);
        if(command==THY_LIBRARY_UI_PLAY){e->state.level=THY_STATE_PLAY;e->state.editing=0u;}
    } else if(command==THY_LIBRARY_UI_SAVE){
        size_t size=ui->kind==0u?sizeof ui->draft.scale:ui->kind==1u?sizeof ui->draft.chord:sizeof ui->draft.six;
        r=thy_engine_library_set(e,ui->kind,ui->slot,&ui->draft,size);
    } else if(command==THY_LIBRARY_UI_DELETE)r=thy_engine_library_delete(e,ui->kind,ui->slot);
    else if(command==THY_LIBRARY_UI_COPY_CURRENT){
        thy_library_ui_draft draft;r=thy_engine_library_capture(e,ui->kind,ui->slot,&draft);
        if(r==THY_OK)r=thy_library_ui_begin_copy(ui,&i->library,&draft)==THY_LIBRARY_OK?THY_OK:THY_INVALID_ARGUMENT;
    }
    if(command==THY_LIBRARY_UI_SAVE||command==THY_LIBRARY_UI_DELETE||
       (command==THY_LIBRARY_UI_COPY_CURRENT&&r!=THY_OK))
        (void)thy_library_ui_ack(ui,&i->library,r==THY_OK?0:r==THY_ENGINE_LIBRARY_IN_USE?2:r==THY_ENGINE_LIBRARY_BUSY?3:1);
    ++e->state.revision;return r==THY_OK?THY_STATE_OK:THY_STATE_BLOCKED;
}
