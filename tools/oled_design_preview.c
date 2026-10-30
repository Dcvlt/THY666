/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host-only production screen review. Including the renderer gives this harness
 * access to its exact private font, icons, musical projection and Fifths view.
 * Never included in the firmware/WASM source lists. No device access. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/thy_studio_ui.c"

static thy_core music;
static thy_instrument instrument;
static thy_engine engine, saved_engine;
static thy_instrument saved_instrument;
static thy_core saved_music;
static thy_seq_event undo[THY_SEQ_EVENTS];
static thy_studio_ui ui;
static const thy_resources metrics = {
    .valid=1,.timing_valid=1,.stack_valid=1,.cpu_active=100,
    .ram_used=101772,.ram_total=131072,.ccm_used=55656,.ccm_total=65536,
    .flash_used=451052,.flash_total=524288,.stack_used=2048,.stack_total=8192,
    .loop_us=42,.loop_peak_us=90
};
static void init(void) {
    thy_core_init(&music); thy_instrument_init(&instrument,&music);
    thy_engine_init(&engine,&instrument,0,70,undo);
    for(uint8_t side=0u;side<2u;++side)
        if(thy_instrument_set_route(&instrument,side,0u,THY_ROUTE_DIN|THY_ROUTE_USB0,0)!=THY_OK)exit(9);
    thy_studio_ui_init(&ui); ui.animations_enabled=0; ui.resources=&metrics;
}
static void preview_velocity(uint8_t scope,uint8_t mode) {
    for(uint8_t side=0u;side<2u;++side)if(!scope||scope==side+1u){
        thy_instrument_velocity velocity;
        if(thy_instrument_get_cluster_velocity(&instrument,side,&velocity)!=THY_OK)exit(9);
        velocity.mode=mode;
        if(thy_instrument_set_cluster_velocity(&instrument,side,&velocity)!=THY_OK)exit(9);
    }
}
static void setup(unsigned screen) {
    init();
    if(screen>=6u) {
        instrument.display_view=THY_VIEW_SIMPLE;
        preview_velocity(0u,THY_VELOCITY_FIXED);
        if(thy_engine_performance_set(&engine,THY_ROW_PERFORMANCE_PRESSUREMODE,0u,THY_CHANNEL_PRESSURE_OFF)!=THY_STATE_OK)exit(9);
        if(screen==7u)preview_velocity(1u,THY_VELOCITY_STRIKE);
        if(screen==8u && (thy_engine_performance_set(&engine,THY_ROW_PERFORMANCE_PRESSUREMODE,1u,THY_CHANNEL_PRESSURE_POLY)!=THY_STATE_OK ||
            thy_engine_performance_set(&engine,THY_ROW_PERFORMANCE_PRESSUREMODE,2u,THY_CHANNEL_PRESSURE_CHANNEL)!=THY_STATE_OK))exit(9);
        if(screen==9u)engine.expression_config.enabled=1u;
        if(screen==10u)engine.arps[0].config.enabled=1u;
    } else if(screen==0 || screen==4 || screen==5) {
        static const int16_t triad[]={60,64,67};
        static const int16_t five[]={60,62,64,67,71};
        static const int16_t seventh[]={54,57,61,64};
        instrument.display_view=THY_VIEW_SIMPLE;
        if(thy_core_press(&music,0,screen==5?seventh:screen==4?five:triad,screen==5?4:screen==4?5:3,0,1,1,96)!=0)exit(3);
    } else if(screen==1) {
        instrument.engine_kind=THY_PLAY_ENGINE_HARMONY;
        for(unsigned f=0;f<thy_guidance_scale_count();++f)
            if(!strcmp(thy_guidance_scale_name(f),"Major"))engine.scale=(uint16_t)thy_guidance_playable_catalog(f);
        engine.harmony=(thy_harmony_workspace){.key_mask=0xab5,.input_mask=0x891,
            .base_mask=0x891,.source_root=0,.current_root=0,.count=1,.cache_ready=1,
            .view=1,.octave_target=1,.high_note=127};
        engine.harmony.choices[0]=(thy_harmony_choice){.mask=0x224,.root=2,
            .degree=1,.reason=THY_HARMONY_IN_KEY};
    } else if(screen==2) thy_state_open(&engine.state);
    else {
        unsigned row=THY_ROW_SYSTEM_RESOURCES;
        if(thy_state_focus(&engine.state,thy_menu_rows[row].group,row)!=THY_STATE_OK)exit(4);
        engine.state.info_open=1;
    }
}
static void draw(uint8_t *out,unsigned screen) {
    memset(out,0,THY_UI_FRAME_BYTES);
    if(screen==0 || screen>=4u)studio_play(out,&engine.state,"MAJOR C",1,1,2000,0);
    else if(screen==1)harmony_workspace_page(out,&engine.state,2000,0);
    else menu(out,&engine.state,&ui);
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    const char *names[]={"simple","harmony","settings","resources","simple-five","simple-seventh",
        "expression-idle","velocity-left","aftertouch-split","expression-mpe","expression-arp"};
    for(unsigned screen=0;screen<sizeof names/sizeof names[0];screen++) {
        setup(screen);saved_engine=engine;saved_instrument=instrument;saved_music=music;
        struct {uint8_t pre[32],frame[THY_UI_FRAME_BYTES],post[32];} guard;
        memset(&guard,0xa5,sizeof guard);draw(guard.frame,screen);
        for(unsigned n=0;n<32;n++)if(guard.pre[n]!=0xa5||guard.post[n]!=0xa5)return 5;
        if(memcmp(&engine,&saved_engine,sizeof engine)||
           memcmp(&instrument,&saved_instrument,sizeof instrument)||
           memcmp(&music,&saved_music,sizeof music))return 6;
        char path[1024];snprintf(path,sizeof path,"%s/%s-rendered.bin",argv[1],names[screen]);
        FILE *f=fopen(path,"wb");if(!f)return 7;
        if(fwrite(guard.frame,1,THY_UI_FRAME_BYTES,f)!=THY_UI_FRAME_BYTES)return 8;
        fclose(f);
    }
    puts("11 production C frames; framebuffer guards and musical-state immutability passed.");
    return 0;
}
