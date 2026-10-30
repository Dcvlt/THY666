/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real portable codec/journal, byte-failing NOR model; no device access. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "thy_settings.h"
#include "fixtures/settings_v4.h"

static thy_core core;
static thy_instrument instrument;
static thy_engine engine;
static thy_mpe expression;
static thy_settings_journal journal, rebooted;
static uint8_t flash[2u*THY_SETTINGS_SECTOR_BYTES], original[sizeof flash];
static uint8_t payload[THY_SETTINGS_PAYLOAD_MAX], roundtrip[THY_SETTINGS_PAYLOAD_MAX];
static thy_seq_event full_song[THY_SEQ_EVENTS];
static unsigned programs, erases, checks, scenarios;
static int cut_after=-1, fail_erase;
#define CHECK(x) do {++checks; assert(x);} while(0)
static int read_memory(void *context,uint32_t offset,uint8_t *out,size_t count) {
    CHECK(context==flash && offset<=sizeof flash && count<=sizeof flash-offset);
    memcpy(out,flash+offset,count); return 1;
}
static int program_memory(void *context,uint32_t offset,const uint8_t word[4]) {
    CHECK(context==flash && !(offset&3u) && offset<=sizeof flash-4u); ++programs;
    for(unsigned n=0;n<4u;++n) {
        if(cut_after==0) return 0;
        CHECK(flash[offset+n]==255u); flash[offset+n]&=word[n]; if(cut_after>0) --cut_after;
    }
    return 1;
}
static int erase_memory(void *context,unsigned sector) {
    CHECK(context==flash && sector<2u); ++erases;
    if(fail_erase) {memset(flash+sector*THY_SETTINGS_SECTOR_BYTES,255u,THY_SETTINGS_SECTOR_BYTES/2u);return 0;}
    memset(flash+sector*THY_SETTINGS_SECTOR_BYTES,255u,THY_SETTINGS_SECTOR_BYTES); return 1;
}
static thy_settings_io io(void) {return (thy_settings_io){flash,read_memory,program_memory,erase_memory};}
static void defaults(void) {
    thy_core_init(&core); thy_instrument_init(&instrument,&core);
    static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&engine,&instrument,21u,70u, undo_workspace_1);
    CHECK(thy_instrument_set_detector_profile(&instrument,THY_DETECTOR_FACTORY_LINEAR)==THY_OK);
    CHECK(thy_instrument_set_route(&instrument,0u,0u,5u,0)==THY_OK);
    CHECK(thy_instrument_set_route(&instrument,1u,1u,5u,0)==THY_OK);
    instrument.input_enabled=0;
}
static size_t encode(uint8_t *out) {size_t bytes=0;CHECK(thy_settings_encode(&engine,out,THY_SETTINGS_PAYLOAD_MAX,&bytes));return bytes;}
static void finish_save(uint32_t time) {
    for(unsigned n=0;n<5000u && (journal.writing || journal.status!=THY_SETTINGS_SAVED);++n)
        thy_settings_journal_task(&journal,&engine,time+n,1);
    CHECK(journal.status==THY_SETTINGS_SAVED && !journal.writing);
}
static void begin_save(uint32_t time) {
    thy_settings_journal_task(&journal,&engine,time,1);
    CHECK(journal.status==THY_SETTINGS_PENDING);
    thy_settings_journal_task(&journal,&engine,time+1499u,1); CHECK(!journal.writing);
    thy_settings_journal_task(&journal,&engine,time+1500u,1); CHECK(journal.writing);
}
static uint32_t get32(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned b=0;b<4u;++b)p[b]=(uint8_t)(n>>(b*8u));}
static void crc_record(uint8_t *record) {put32(record+20,0u);put32(record+20,thy_settings_crc32(record,32u+get32(record+12)));}
/* Current preferences append before the unchanged song. Historical fixtures must
 * remove that tail, not merely relabel a current payload's discriminator. */
static size_t without_v16_preferences(uint8_t *out,size_t bytes) {
    CHECK(bytes>=THY_SETTINGS_V19_PARAMETER_BYTES);
    memmove(out+THY_SETTINGS_V15_PARAMETER_BYTES,out+THY_SETTINGS_V19_PARAMETER_BYTES,
        bytes-THY_SETTINGS_V19_PARAMETER_BYTES);
    return bytes-(THY_SETTINGS_V19_PARAMETER_BYTES-THY_SETTINGS_V15_PARAMETER_BYTES);
}
static size_t encode_legacy_prefix(unsigned version) {
    uint8_t saved[4]; memcpy(saved,instrument.play_nav,sizeof saved);
    unsigned limit=version>=4u?THY_PLAY_ENGINES:version>=3u?THY_PLAY_ARPS:6u;
    for(unsigned n=0u;n<4u;++n) if(instrument.play_nav[n]>limit) instrument.play_nav[n]=THY_PLAY_HIDDEN;
    /* Materialize a valid historical info vocabulary, rather than relabel
     * the new factory Musical names value as an old schema. */
    thy_view_config saved_views[THY_VIEW_COUNT];
    memcpy(saved_views,instrument.display_config,sizeof saved_views);
    for(unsigned view=0;view<THY_VIEW_COUNT;++view) {
        thy_view_config *v=&instrument.display_config[view];
        for(unsigned slot=0;slot<2u;++slot)
            if(v->slots[slot]==THY_VIEW_INFO_MUSICAL_NAMES)v->slots[slot]=slot?THY_VIEW_INFO_NOTES:THY_VIEW_INFO_VELOCITY;
        for(unsigned row=0;row<THY_VIEW_DETAIL_ROWS;++row)
            if(v->detail_rows[row]==THY_VIEW_INFO_MUSICAL_NAMES)v->detail_rows[row]=row?THY_VIEW_INFO_NOTES:THY_VIEW_INFO_ROOT_SCALE;
    }
    size_t bytes=encode(payload);CHECK(bytes>THY_SETTINGS_V6_PARAMETER_BYTES);
    memcpy(instrument.play_nav,saved,sizeof saved);
    memcpy(instrument.display_config,saved_views,sizeof saved_views);
    return without_v16_preferences(payload,bytes);
}
/* Reproduce the frozen v1 wire layout, removing v2's in-band marker and its
 * appended fields. The first three bytes are the unchanged catalog contract. */
static size_t legacy_export(uint8_t *out) {
    encode_legacy_prefix(1u);
    memcpy(out,payload,3u);
    memcpy(out+3u,payload+7u,THY_SETTINGS_V1_PAYLOAD_BYTES-3u);
    CHECK(thy_settings_validate(out,THY_SETTINGS_V1_PAYLOAD_BYTES,70u));
    return THY_SETTINGS_V1_PAYLOAD_BYTES;
}
static size_t v2_export(uint8_t *out) {
    encode_legacy_prefix(2u);
    memcpy(out,payload,THY_SETTINGS_V2_PAYLOAD_BYTES); out[6]=2u;
    CHECK(thy_settings_validate(out,THY_SETTINGS_V2_PAYLOAD_BYTES,70u));
    return THY_SETTINGS_V2_PAYLOAD_BYTES;
}
static size_t v3_export(uint8_t *out) {
    encode_legacy_prefix(3u);
    memcpy(out,payload,THY_SETTINGS_V3_PAYLOAD_BYTES); out[6]=3u;
    CHECK(thy_settings_validate(out,THY_SETTINGS_V3_PAYLOAD_BYTES,70u));
    return THY_SETTINGS_V3_PAYLOAD_BYTES;
}
static size_t v4_export(uint8_t *out) {
    encode_legacy_prefix(4u);
    memcpy(out,payload,THY_SETTINGS_V4_PAYLOAD_BYTES); out[6]=4u;
    /* Exercise the retired ID through journal selection and every-byte
     * upgrades too, not only through direct decoder fixture imports. */
    out[THY_SETTINGS_V3_PAYLOAD_BYTES]=0u;
    CHECK(thy_settings_validate(out,THY_SETTINGS_V4_PAYLOAD_BYTES,70u));
    return THY_SETTINGS_V4_PAYLOAD_BYTES;
}
static size_t v5_export(uint8_t *out) {
    encode_legacy_prefix(5u);
    memcpy(out,payload,THY_SETTINGS_V5_PAYLOAD_BYTES); out[6]=5u;
    CHECK(thy_settings_validate(out,THY_SETTINGS_V5_PAYLOAD_BYTES,70u));
    return THY_SETTINGS_V5_PAYLOAD_BYTES;
}
static size_t v6_export(uint8_t *out) {
    size_t current=encode_legacy_prefix(6u),song=current-THY_SETTINGS_V12_PARAMETER_BYTES;
    memcpy(out,payload,THY_SETTINGS_V6_PARAMETER_BYTES);out[6]=6u;
    memcpy(out+THY_SETTINGS_V6_PARAMETER_BYTES,payload+THY_SETTINGS_V12_PARAMETER_BYTES,song);
    size_t bytes=THY_SETTINGS_V6_PARAMETER_BYTES+song;
    CHECK(thy_settings_payload_version(out,bytes)==6u && thy_settings_validate(out,bytes,70u));
    return bytes;
}
static size_t v7_export(uint8_t *out) {
    size_t current=encode_legacy_prefix(7u),song=current-THY_SETTINGS_V12_PARAMETER_BYTES;
    memcpy(out,payload,THY_SETTINGS_V7_PARAMETER_BYTES);out[6]=7u;
    memcpy(out+THY_SETTINGS_V7_PARAMETER_BYTES,payload+THY_SETTINGS_V12_PARAMETER_BYTES,song);
    size_t bytes=THY_SETTINGS_V7_PARAMETER_BYTES+song;
    CHECK(thy_settings_payload_version(out,bytes)==7u && thy_settings_validate(out,bytes,70u));
    return bytes;
}
static size_t v8_export(uint8_t *out) {
    size_t current=encode_legacy_prefix(8u),song=current-THY_SETTINGS_V12_PARAMETER_BYTES;
    memcpy(out,payload,THY_SETTINGS_V8_PARAMETER_BYTES);out[6]=8u;
    memcpy(out+THY_SETTINGS_V8_PARAMETER_BYTES,payload+THY_SETTINGS_V12_PARAMETER_BYTES,song);
    size_t bytes=THY_SETTINGS_V8_PARAMETER_BYTES+song;
    CHECK(thy_settings_payload_version(out,bytes)==8u && thy_settings_validate(out,bytes,70u));
    return bytes;
}
static size_t v9_export(uint8_t *out) {
    size_t current=encode_legacy_prefix(9u),song=current-THY_SETTINGS_V12_PARAMETER_BYTES;
    memcpy(out,payload,THY_SETTINGS_V9_PARAMETER_BYTES);out[6]=9u;
    memcpy(out+THY_SETTINGS_V9_PARAMETER_BYTES,payload+THY_SETTINGS_V12_PARAMETER_BYTES,song);
    size_t bytes=THY_SETTINGS_V9_PARAMETER_BYTES+song;
    CHECK(thy_settings_payload_version(out,bytes)==9u && thy_settings_validate(out,bytes,70u));
    return bytes;
}
static size_t v10_export(uint8_t *out) {
    size_t current=encode_legacy_prefix(10u),song=current-THY_SETTINGS_V12_PARAMETER_BYTES;
    memcpy(out,payload,THY_SETTINGS_V10_PARAMETER_BYTES);out[6]=10u;
    memcpy(out+THY_SETTINGS_V10_PARAMETER_BYTES,payload+THY_SETTINGS_V12_PARAMETER_BYTES,song);
    size_t bytes=THY_SETTINGS_V10_PARAMETER_BYTES+song;
    CHECK(thy_settings_payload_version(out,bytes)==10u && thy_settings_validate(out,bytes,70u));
    return bytes;
}
static size_t v11_export(uint8_t *out) {
    size_t current=encode_legacy_prefix(11u),song=current-THY_SETTINGS_V12_PARAMETER_BYTES;
    memcpy(out,payload,THY_SETTINGS_V11_PARAMETER_BYTES);out[6]=11u;
    memcpy(out+THY_SETTINGS_V11_PARAMETER_BYTES,payload+THY_SETTINGS_V12_PARAMETER_BYTES,song);
    size_t bytes=THY_SETTINGS_V11_PARAMETER_BYTES+song;
    CHECK(thy_settings_payload_version(out,bytes)==11u && thy_settings_validate(out,bytes,70u));
    return bytes;
}
static size_t v12_export(uint8_t *out) {
    size_t bytes=encode_legacy_prefix(12u);
    memcpy(out,payload,bytes);out[6]=12u;
    CHECK(thy_settings_payload_version(out,bytes)==12u && thy_settings_validate(out,bytes,70u));
    return bytes;
}
static void old_record(unsigned version) {
    memset(flash,255u,sizeof flash);
    size_t bytes=version==1u?legacy_export(roundtrip):version==2u?v2_export(roundtrip):version==3u?v3_export(roundtrip):version==4u?v4_export(roundtrip):version==5u?v5_export(roundtrip):version==6u?v6_export(roundtrip):version==7u?v7_export(roundtrip):version==8u?v8_export(roundtrip):version==9u?v9_export(roundtrip):version==10u?v10_export(roundtrip):version==11u?v11_export(roundtrip):v12_export(roundtrip);
    memset(flash,0u,THY_SETTINGS_HEADER_BYTES);
    put32(flash,UINT32_C(0x36594854)); put32(flash+4u,UINT32_C(0x31535436));
    put32(flash+8u,version); put32(flash+12u,(uint32_t)bytes); put32(flash+16u,7u);
    put32(flash+28u,UINT32_C(0x51afe666)); memcpy(flash+32u,roundtrip,bytes); crc_record(flash);
}
static void legacy_record(void) {old_record(1u);}

static void codec(void) {
    ++scenarios; defaults(); engine.scale=35u; instrument.performance.root=7u;
    instrument.performance.chord_octave=-2; instrument.performance.strum=1u; instrument.performance.strum_ms=73u;
    instrument.transpose[0]=-7; instrument.transpose[1]=9; instrument.light_fade_ms=455u;
    instrument.display_view=5u; instrument.display_counts=0u; instrument.display_pretty_counts=1u;
    instrument.context_controls=1u; instrument.context_mask=19u; instrument.context_location=1u; instrument.bass_octave=-2;
    instrument.slot_scope=1u; instrument.global_excluded_slots=5u; instrument.excluded_slots[37]=65u;
    instrument.lower_mode=1u; instrument.octave_layers=85u;
    for(unsigned n=0;n<48u;++n) instrument.pad_octave_layers[n]=(uint8_t)(1u+(n*17u)%127u);
    instrument.play_nav[0]=6u; instrument.play_nav[3]=0u; instrument.center_brightness=3u;
    instrument.light_key_contrast_percent=18u;
    CHECK(thy_instrument_set_pad_route(&instrument,8u,15u,7u)==THY_OK);
    CHECK(thy_instrument_set_route_expression(&instrument,8u,117u,1u)==THY_OK);
    thy_instrument_response custom={1500u,1430u,1400u,3900u,2u};
    CHECK(thy_instrument_set_pad_response(&instrument,95u,&custom)==THY_OK);
    CHECK(thy_instrument_store_user(&instrument,2u)==THY_OK);
    size_t bytes=encode(payload); CHECK(bytes<THY_SETTINGS_PAYLOAD_MAX);
    instrument.sensors[20].calibration.baseline=999u; instrument.sensors[20].raw=3600u;
    instrument.sensors[20].held=1u; instrument.menu_editing=1u; instrument.selected_pad=67u;
    size_t second=encode(roundtrip); CHECK(second==bytes && !memcmp(payload,roundtrip,bytes));
    defaults(); CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(engine.scale==35u && instrument.performance.root==7u && instrument.display_view==5u);
    CHECK(instrument.bass_octave==-2 && instrument.context_mask==19u && instrument.excluded_slots[37]==65u);
    CHECK(instrument.lower_mode==1u && instrument.octave_layers==85u);
    for(unsigned n=0;n<48u;++n) CHECK(instrument.pad_octave_layers[n]==1u+(n*17u)%127u);
    CHECK(instrument.context_controls && instrument.context_location==1u);
    CHECK(thy_instrument_context_pad(&instrument,3u)==1 && thy_instrument_context_pad(&instrument,7u)==2);
    CHECK(thy_instrument_context_pad(&instrument,51u)==5 && !thy_instrument_context_pad(&instrument,55u));
    CHECK(!thy_instrument_context_pad(&instrument,35u) && !thy_instrument_context_pad(&instrument,95u));
    CHECK(instrument.route_velocity[8]==117u && instrument.route_pressure[8]==2u);
    CHECK(instrument.sensors[95].response_overridden && instrument.sensors[95].response.attack_raw==1500u);
    CHECK(!instrument.sensors[20].held && !instrument.sensors[20].warmup_count);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    for(size_t n=0;n<bytes;++n) CHECK(!thy_settings_validate(payload,n,70u));
    CHECK(!thy_settings_validate(payload,bytes,20u));
    payload[2]=12u; CHECK(!thy_settings_apply(&engine,payload,bytes)); CHECK(instrument.performance.root==7u); payload[2]=7u;
    instrument.input_enabled=1u; CHECK(!thy_settings_apply(&engine,payload,bytes)); instrument.input_enabled=0u;
    CHECK(thy_settings_crc32((const uint8_t *)"123456789",9u)==UINT32_C(0xcbf43926));
}
static void engine_views_and_scale_shortcuts_roundtrip(void) {
    ++scenarios; defaults(); instrument.engine_kind=THY_PLAY_ENGINE_PLAY;
    instrument.display_view=THY_VIEW_FIFTHS;
    instrument.performance.shortcut=11u;
    instrument.users[2].shortcut=10u;
    instrument.play_nav[3]=THY_PLAY_ENGINES;
    for(unsigned n=0;n<THY_SCALE_SHORTCUT_COUNT;++n)
        instrument.scale_shortcuts[n]=(uint8_t)(n==11u?THY_SCALE_SHORTCUT_LAYOUT:(n*7u)%70u);
    for(unsigned n=0;n<THY_VIEW_COUNT;++n) {
        thy_view_config *v=&instrument.display_config[n];
        v->slots[0]=(uint8_t)(n%THY_VIEW_INFO_COUNT);
        v->slots[1]=(uint8_t)((n+4u)%THY_VIEW_INFO_COUNT);
        v->header=(uint8_t)(n&1u); v->motion=(uint8_t)((n+1u)&1u);
        for(unsigned row=0;row<THY_VIEW_DETAIL_ROWS;++row)
            v->detail_rows[row]=(uint8_t)((n+row*3u)%THY_VIEW_INFO_COUNT);
    }
    const thy_instrument expected=instrument;
    size_t bytes=encode(payload); CHECK(bytes>THY_SETTINGS_V6_PARAMETER_BYTES);
    CHECK(thy_settings_payload_version(payload,bytes)==THY_SETTINGS_VERSION);
    defaults(); CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.engine_kind==THY_PLAY_ENGINE_PLAY && instrument.display_view==THY_VIEW_FIFTHS);
    CHECK(engine.session.playing_mode==instrument.engine_kind);
    CHECK(instrument.performance.shortcut==11u && instrument.users[2].shortcut==10u);
    CHECK(instrument.play_nav[3]==THY_PLAY_ENGINES);
    CHECK(!memcmp(instrument.scale_shortcuts,expected.scale_shortcuts,sizeof instrument.scale_shortcuts));
    CHECK(!memcmp(instrument.display_config,expected.display_config,sizeof instrument.display_config));
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    for(size_t at=0;at<bytes;++at) CHECK(!thy_settings_validate(payload,at,70u));
}
static void view_fields_validation_and_legacy_migration(void) {
    ++scenarios; defaults(); size_t bytes=encode(payload);
    const thy_engine before_engine=engine; const thy_instrument before_instrument=instrument;
    const size_t begin=THY_SETTINGS_V3_PAYLOAD_BYTES, views=begin+1u+THY_SCALE_SHORTCUT_COUNT;
    for(size_t at=begin;at<THY_SETTINGS_V5_PAYLOAD_BYTES;++at) {
        uint8_t old=payload[at], invalid;
        if(at==begin) invalid=255u;
        else if(at<views) invalid=THY_SCALE_SHORTCUT_LAYOUT+1u;
        else invalid=((at-views)%10u==2u || (at-views)%10u==3u)?2u:THY_VIEW_INFO_COUNT;
        payload[at]=invalid;
        CHECK(!thy_settings_validate(payload,bytes,70u));
        CHECK(!thy_settings_apply(&engine,payload,bytes));
        CHECK(!memcmp(&engine,&before_engine,sizeof engine) && !memcmp(&instrument,&before_instrument,sizeof instrument));
        payload[at]=old;
    }
    payload[begin]=0u; /* The old Scales workspace is not a v5 active engine. */
    CHECK(!thy_settings_validate(payload,bytes,70u));
    CHECK(!thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&engine,&before_engine,sizeof engine) && !memcmp(&instrument,&before_instrument,sizeof instrument));
    payload[begin]=THY_PLAY_ENGINE_PLAY;
    /* Active retired identities are rejected in current exports, while their
     * reserved per-view preference cells remain stable future-safe storage. */
    instrument.display_view=THY_VIEW_RETIRED_LAYOUTS; size_t rejected=0u;
    CHECK(!thy_settings_encode(&engine,roundtrip,sizeof roundtrip,&rejected));
    defaults(); instrument.display_view=THY_VIEW_RETIRED_ROUTES;
    CHECK(!thy_settings_encode(&engine,roundtrip,sizeof roundtrip,&rejected));
    const unsigned layouts[]={THY_LAYOUT_CHROMATIC,THY_LAYOUT_IN_KEY_LIT,THY_LAYOUT_ALL_LIT,THY_LAYOUT_STORED,THY_LAYOUT_GUITAR};
    for(unsigned version=1u;version<=3u;++version) for(unsigned choice=0;choice<sizeof layouts/sizeof layouts[0];++choice) {
        defaults(); instrument.performance.melody_layout=(uint8_t)layouts[choice];
        instrument.display_counts=0u; instrument.display_pretty_counts=1u; instrument.display_motion=0u;
        size_t old_bytes=version==1u?legacy_export(roundtrip):version==2u?v2_export(roundtrip):v3_export(roundtrip);
        /* display_view is the eleventh field of the frozen CONFIG_BYTES list. */
        const size_t view_at=version==1u?81u:85u;
        CHECK(roundtrip[view_at]==instrument.display_view);
        roundtrip[view_at]=(uint8_t)(choice%3u+2u);
        CHECK(thy_settings_validate(roundtrip,old_bytes,70u));
        defaults(); CHECK(thy_settings_apply(&engine,roundtrip,old_bytes));
        CHECK(instrument.engine_kind==THY_PLAY_ENGINE_PLAY);
        CHECK(instrument.performance.melody_layout==layouts[choice]);
        CHECK(engine.session.playing_mode==instrument.engine_kind);
        CHECK(instrument.display_view==(choice%3u<2u?THY_VIEW_DETAILED:THY_VIEW_SIMPLE));
        CHECK(instrument.display_config[THY_VIEW_SIMPLE].slots[1]==THY_VIEW_INFO_OFF);
        CHECK(instrument.display_config[THY_VIEW_CHLADNI].slots[1]==THY_VIEW_INFO_COUNTS);
        CHECK(!instrument.display_config[THY_VIEW_CHLADNI].motion);
        for(unsigned key=0;key<THY_SCALE_SHORTCUT_COUNT;++key)
            CHECK(instrument.scale_shortcuts[key]==thy_instrument_default_scale_shortcut((uint8_t)key));
        size_t current=encode(payload); CHECK(current>THY_SETTINGS_V6_PARAMETER_BYTES && thy_settings_validate(payload,current,70u));
    }
}
static void frozen_v4_migration_preserves_layout_and_views(void) {
    ++scenarios;
    const unsigned layouts[]={THY_LAYOUT_CHROMATIC,THY_LAYOUT_CONDENSED,THY_LAYOUT_GUITAR};
    const size_t engine_at=THY_SETTINGS_V3_PAYLOAD_BYTES;
    const size_t shortcuts_at=engine_at+1u, views_at=shortcuts_at+THY_SCALE_SHORTCUT_COUNT;
    for(unsigned old_engine=0u;old_engine<2u;++old_engine) for(unsigned layout=0u;layout<3u;++layout) {
        defaults();
        const thy_view_config fifths_default=instrument.display_config[THY_VIEW_FIFTHS];
        memcpy(roundtrip,settings_v4_fixture,sizeof settings_v4_fixture);
        CHECK(thy_settings_payload_version(roundtrip,sizeof settings_v4_fixture)==4u);
        roundtrip[engine_at]=(uint8_t)old_engine;
        roundtrip[8]=(uint8_t)layouts[layout];
        roundtrip[85]=THY_VIEW_WAVES;
        for(unsigned n=0;n<THY_SCALE_SHORTCUT_COUNT;++n)
            roundtrip[shortcuts_at+n]=(uint8_t)(n==11u?THY_SCALE_SHORTCUT_LAYOUT:(n*7u)%70u);
        /* The actual frozen v4 payload has eight independent ten-byte view
         * configurations, not nine current struct instances. */
        for(unsigned n=0;n<8u;++n) {
            uint8_t *v=roundtrip+views_at+n*10u;
            v[0]=(uint8_t)(n%12u);v[1]=(uint8_t)((n+4u)%12u);
            v[2]=(uint8_t)(n&1u);v[3]=(uint8_t)((n+1u)&1u);
            for(unsigned row=0u;row<THY_VIEW_DETAIL_ROWS;++row) v[4u+row]=(uint8_t)((n+row*3u)%12u);
        }
        /* Preserve enabled Arp configuration, without resurrecting a phrase. */
        roundtrip[THY_SETTINGS_V2_PAYLOAD_BYTES+12u]=1u;
        CHECK(thy_settings_validate(roundtrip,sizeof settings_v4_fixture,70u));
        CHECK(thy_settings_apply(&engine,roundtrip,sizeof settings_v4_fixture));
        CHECK(instrument.engine_kind==THY_PLAY_ENGINE_PLAY && engine.session.playing_mode==THY_PLAY_ENGINE_PLAY);
        CHECK(instrument.performance.melody_layout==layouts[layout] && instrument.display_view==THY_VIEW_WAVES);
        CHECK(!memcmp(instrument.scale_shortcuts,roundtrip+shortcuts_at,THY_SCALE_SHORTCUT_COUNT));
        for(unsigned n=0;n<8u;++n) {
            const thy_view_config *v=&instrument.display_config[n]; const uint8_t *old=roundtrip+views_at+n*10u;
            CHECK(v->slots[0]==old[0] && v->slots[1]==old[1] && v->header==old[2] && v->motion==old[3]);
            CHECK(!memcmp(v->detail_rows,old+4u,THY_VIEW_DETAIL_ROWS));
        }
        CHECK(!memcmp(&instrument.display_config[THY_VIEW_FIFTHS],&fifths_default,sizeof fifths_default));
        CHECK(engine.arps[0].config.enabled && !engine.arps[0].held_count && !engine.arps[0].phrase_count && !engine.arps[0].output_count);
        CHECK(!core.sounding_count && !core.generated_count && engine.rhythm_transport.state==THY_TRANSPORT_STOPPED);
        const thy_engine before_engine=engine; const thy_instrument before_instrument=instrument;
        roundtrip[engine_at]=2u;
        CHECK(!thy_settings_validate(roundtrip,sizeof settings_v4_fixture,70u));
        CHECK(!thy_settings_apply(&engine,roundtrip,sizeof settings_v4_fixture));
        roundtrip[engine_at]=(uint8_t)old_engine;roundtrip[85]=THY_VIEW_FIFTHS;
        CHECK(!thy_settings_validate(roundtrip,sizeof settings_v4_fixture,70u));
        CHECK(!thy_settings_apply(&engine,roundtrip,sizeof settings_v4_fixture));
        CHECK(!memcmp(&engine,&before_engine,sizeof engine) && !memcmp(&instrument,&before_instrument,sizeof instrument));
    }
}
static void lower_codec_and_legacy_import(void) {
    ++scenarios; defaults();
    instrument.global_excluded_slots=17u; instrument.excluded_slots[23]=42u; instrument.slot_scope=1u;
    instrument.lower_mode=1u; instrument.octave_layers=127u; instrument.pad_octave_layers[47]=65u;
    size_t bytes=encode(payload); CHECK(bytes>THY_SETTINGS_V6_PARAMETER_BYTES);
    CHECK(thy_settings_payload_version(payload,bytes)==THY_SETTINGS_VERSION);
    CHECK(!thy_settings_payload_version(NULL,bytes));
    CHECK(!thy_settings_validate(payload,bytes-1u,70u)); /* Variable-length v6 needs its full song. */
    uint16_t scale=99u; uint8_t root=99u;
    CHECK(thy_settings_catalog(payload,bytes,&scale,&root)); CHECK(scale==21u && root==0u);
    /* Every truncated v5 length fails, including both exact legacy lengths. */
    for(size_t n=0;n<bytes;++n) CHECK(!thy_settings_validate(payload,n,70u));
    const size_t tail=THY_SETTINGS_V2_PAYLOAD_BYTES-50u;
    payload[tail]=2u; CHECK(!thy_settings_apply(&engine,payload,bytes)); payload[tail]=1u;
    payload[tail+1u]=0u; CHECK(!thy_settings_apply(&engine,payload,bytes)); payload[tail+1u]=127u;
    payload[tail+1u]=128u; CHECK(!thy_settings_validate(payload,bytes,70u)); payload[tail+1u]=127u;
    for(unsigned n=0;n<48u;++n) {
        uint8_t value=payload[tail+2u+n]; payload[tail+2u+n]=0u;
        CHECK(!thy_settings_apply(&engine,payload,bytes));
        payload[tail+2u+n]=128u; CHECK(!thy_settings_validate(payload,bytes,70u));
        payload[tail+2u+n]=value;
    }
    CHECK(instrument.lower_mode==1u && instrument.octave_layers==127u);
    size_t old_bytes=legacy_export(roundtrip); CHECK(old_bytes==2103u);
    CHECK(thy_settings_payload_version(roundtrip,old_bytes)==1);
    /* Reimporting legacy settings must retain their original note-slot
     * behavior even though a clean v2 instrument defaults to octave layers. */
    defaults(); CHECK(thy_settings_apply(&engine,roundtrip,old_bytes));
    CHECK(instrument.lower_mode==0u && instrument.octave_layers==8u);
    CHECK(instrument.global_excluded_slots==17u && instrument.excluded_slots[23]==42u && instrument.slot_scope==1u);
    for(unsigned n=0;n<48u;++n) CHECK(instrument.pad_octave_layers[n]==8u);
    CHECK(encode(payload)>THY_SETTINGS_V6_PARAMETER_BYTES);
    /* Both inactive and active mode preferences survive a fresh restore. */
    instrument.octave_layers=1u; instrument.pad_octave_layers[0]=127u;
    bytes=encode(payload); defaults(); CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.lower_mode==0u && instrument.octave_layers==1u && instrument.pad_octave_layers[0]==127u);
}
static void debounce_and_boot(void) {
    ++scenarios; defaults(); memset(flash,255u,sizeof flash); thy_settings_io backend=io();
    CHECK(thy_settings_journal_init(&journal,&backend,70u)); CHECK(!journal.loaded);
    programs=erases=0; thy_settings_journal_task(&journal,&engine,100u,1); CHECK(journal.status==THY_SETTINGS_PENDING);
    thy_settings_journal_task(&journal,&engine,1700u,0); CHECK(!programs && !erases && !journal.writing);
    instrument.fixed_velocity=102u; thy_settings_journal_task(&journal,&engine,1800u,1);
    thy_settings_journal_task(&journal,&engine,3200u,1); CHECK(!journal.writing);
    thy_settings_journal_task(&journal,&engine,3300u,1); CHECK(journal.writing);
    thy_settings_journal_task(&journal,&engine,3301u,0); CHECK(!programs);
    finish_save(3400u); CHECK(journal.generation==1u && journal.saves==1u && !erases);
    unsigned previous=programs;
    for(unsigned n=0;n<100u;++n) thy_settings_journal_task(&journal,&engine,5000u+n*100u,1);
    CHECK(programs==previous);
    CHECK(thy_settings_journal_init(&rebooted,&backend,70u)); CHECK(rebooted.loaded && rebooted.generation==1u);
    defaults(); CHECK(thy_settings_apply(&engine,rebooted.record+32u,rebooted.payload_bytes)); CHECK(instrument.fixed_velocity==102u);
}
static void every_power_cut(void) {
    ++scenarios; defaults(); memset(flash,255u,sizeof flash); thy_settings_io backend=io();
    CHECK(thy_settings_journal_init(&journal,&backend,70u)); begin_save(0u); finish_save(1600u);
    memcpy(original,flash,sizeof flash); instrument.fixed_velocity=111u;
    size_t bytes=encode(payload); unsigned total=(unsigned)((32u+bytes+3u)&~3u);
    /* Stop after every individual programmed byte, including the final commit.
     * A completed commit may survive even if its acknowledgement was lost. */
    for(unsigned cut=0;cut<=total;++cut) {
        memcpy(flash,original,sizeof flash); cut_after=-1;
        CHECK(thy_settings_journal_init(&journal,&backend,70u)); begin_save(10000u);
        cut_after=(int)cut;
        for(unsigned n=0;n<2000u && journal.writing;++n) thy_settings_journal_task(&journal,&engine,12000u+n,1);
        cut_after=-1; CHECK(thy_settings_journal_init(&rebooted,&backend,70u)); CHECK(rebooted.loaded);
        CHECK(rebooted.generation==1u || rebooted.generation==2u);
        CHECK(thy_settings_validate(rebooted.record+32u,rebooted.payload_bytes,70u));
        defaults(); CHECK(thy_settings_apply(&engine,rebooted.record+32u,rebooted.payload_bytes));
        CHECK(instrument.fixed_velocity==(rebooted.generation==1u?96u:111u)); instrument.fixed_velocity=111u;
    }
}
static void rotation_and_corruption(void) {
    ++scenarios; defaults(); memset(flash,255u,sizeof flash); thy_settings_io backend=io();
    CHECK(thy_settings_journal_init(&journal,&backend,70u));
    const unsigned records_per_sector=THY_SETTINGS_SLOTS_PER_SECTOR/4u;
    for(unsigned n=0;n<records_per_sector+1u;++n) {
        instrument.fixed_velocity=(uint8_t)(60u+n); begin_save(n*10000u); finish_save(n*10000u+1600u);
    }
    CHECK(journal.newest_slot==THY_SETTINGS_SLOTS_PER_SECTOR && journal.generation==records_per_sector+1u);
    CHECK(thy_settings_journal_init(&rebooted,&backend,70u)); CHECK(rebooted.generation==records_per_sector+1u && !rebooted.recovered);
    /* Bad payload CRC falls back atomically to the previous committed slot. */
    flash[journal.newest_slot*THY_SETTINGS_SLOT_BYTES+35u]^=1u;
    CHECK(thy_settings_journal_init(&rebooted,&backend,70u)); CHECK(rebooted.generation==records_per_sector && rebooted.recovered);
    /* Valid CRC cannot make an impossible root value become configuration. */
    uint8_t *last=flash+(THY_SETTINGS_SLOTS_PER_SECTOR-4u)*THY_SETTINGS_SLOT_BYTES; last[34]=255u; crc_record(last);
    CHECK(thy_settings_journal_init(&rebooted,&backend,70u)); CHECK(rebooted.generation==records_per_sector-1u && rebooted.rejected);
    /* Future settings format blocks writes instead of a destructive reset. */
    last=flash+(THY_SETTINGS_SLOTS_PER_SECTOR-8u)*THY_SETTINGS_SLOT_BYTES; put32(last+8,99u); crc_record(last);
    CHECK(!thy_settings_journal_init(&rebooted,&backend,70u)); CHECK(rebooted.status==THY_SETTINGS_UNSUPPORTED);
    unsigned before=programs; thy_settings_journal_task(&rebooted,&engine,900000u,1); CHECK(programs==before);
}
static void erased_inactive_power_loss(void) {
    ++scenarios; defaults(); memset(flash,255u,sizeof flash); thy_settings_io backend=io();
    CHECK(thy_settings_journal_init(&journal,&backend,70u)); begin_save(0u); finish_save(1600u);
    /* The current record owns its complete span, including a V8 payload that
       crosses the first legacy slot even when its song is empty. */
    const size_t committed_span=THY_SETTINGS_WRITE_SLOT_SPAN*THY_SETTINGS_SLOT_BYTES;
    memset(flash+committed_span,0u,THY_SETTINGS_SECTOR_BYTES-committed_span);
    instrument.fixed_velocity=99u; begin_save(10000u); fail_erase=1;
    thy_settings_journal_task(&journal,&engine,12000u,1); fail_erase=0;
    CHECK(journal.status==THY_SETTINGS_ERROR);
    CHECK(thy_settings_journal_init(&rebooted,&backend,70u)); CHECK(rebooted.loaded && rebooted.generation==1u);
    defaults(); CHECK(thy_settings_apply(&engine,rebooted.record+32u,rebooted.payload_bytes)); CHECK(instrument.fixed_velocity==96u);
}
static void legacy_journal_upgrade(void) {
    ++scenarios; defaults(); instrument.fixed_velocity=109u; instrument.global_excluded_slots=33u;
    legacy_record(); memcpy(original,flash,sizeof flash); thy_settings_io backend=io(); programs=erases=0u;
    CHECK(thy_settings_journal_init(&journal,&backend,70u));
    CHECK(journal.loaded && journal.loaded_version==1u && journal.migration_pending && journal.generation==7u);
    CHECK(!programs && !erases && !memcmp(original,flash,sizeof flash));
    defaults(); CHECK(thy_settings_apply(&engine,journal.record+32u,journal.payload_bytes));
    CHECK(instrument.lower_mode==0u && instrument.octave_layers==8u && instrument.fixed_velocity==109u);
    CHECK(instrument.global_excluded_slots==33u);
    thy_settings_journal_task(&journal,&engine,0u,0); CHECK(journal.status==THY_SETTINGS_PENDING);
    thy_settings_journal_task(&journal,&engine,5000u,0); CHECK(!programs && !erases && !journal.writing);
    thy_settings_journal_task(&journal,&engine,5001u,1); CHECK(journal.writing && !programs && !erases);
    for(unsigned n=0;n<2000u && journal.writing;++n) {
        unsigned before=programs, erased=erases;
        thy_settings_journal_task(&journal,&engine,5002u+n,1);
        CHECK(programs-before<=1u && erases-erased<=1u);
        CHECK(!memcmp(flash,original,THY_SETTINGS_SLOT_BYTES));
    }
    CHECK(journal.status==THY_SETTINGS_SAVED && journal.generation==8u && journal.loaded_version==THY_SETTINGS_VERSION && !journal.migration_pending);
    CHECK(journal.saves==1u && !erases);
    CHECK(thy_settings_journal_init(&rebooted,&backend,70u));
    CHECK(rebooted.loaded_version==THY_SETTINGS_VERSION && !rebooted.migration_pending && rebooted.generation==8u);
    defaults(); CHECK(thy_settings_apply(&engine,rebooted.record+32u,rebooted.payload_bytes));
    CHECK(instrument.lower_mode==0u && instrument.octave_layers==8u && instrument.fixed_velocity==109u);
    /* Version/payload mismatch cannot turn a current record into a v1 record. */
    uint8_t *latest=flash+journal.newest_slot*THY_SETTINGS_SLOT_BYTES; put32(latest+8u,1u); crc_record(latest);
    CHECK(thy_settings_journal_init(&rebooted,&backend,70u));
    CHECK(rebooted.loaded_version==1u && rebooted.generation==7u && rebooted.recovered);
    put32(latest+8u,99u); crc_record(latest);
    CHECK(!thy_settings_journal_init(&rebooted,&backend,70u) && rebooted.status==THY_SETTINGS_UNSUPPORTED);
    unsigned before=programs; thy_settings_journal_task(&rebooted,&engine,900000u,1); CHECK(programs==before);
    memcpy(flash,original,sizeof flash); put32(flash+8u,2u); crc_record(flash);
    CHECK(thy_settings_journal_init(&rebooted,&backend,70u)); CHECK(!rebooted.loaded && rebooted.rejected);
}
static void every_legacy_upgrade_power_cut(void) {
    ++scenarios;
    for(unsigned version=1u;version<=12u;++version) {
    defaults(); instrument.fixed_velocity=103u; instrument.excluded_slots[8]=16u;
    old_record(version); memcpy(original,flash,sizeof flash); thy_settings_io backend=io();
    defaults(); CHECK(thy_settings_journal_init(&journal,&backend,70u));
    CHECK(thy_settings_apply(&engine,journal.record+32u,journal.payload_bytes));
    unsigned total=(unsigned)((THY_SETTINGS_HEADER_BYTES+encode(payload)+3u)&~3u);
    for(unsigned cut=0;cut<=total;++cut) {
        memcpy(flash,original,sizeof flash); cut_after=-1; defaults();
        CHECK(thy_settings_journal_init(&journal,&backend,70u));
        CHECK(journal.loaded_version==version && journal.migration_pending);
        CHECK(thy_settings_apply(&engine,journal.record+32u,journal.payload_bytes)); begin_save(10000u);
        cut_after=(int)cut;
        for(unsigned n=0;n<2000u && journal.writing;++n) thy_settings_journal_task(&journal,&engine,12000u+n,1);
        cut_after=-1; CHECK(thy_settings_journal_init(&rebooted,&backend,70u));
        CHECK(rebooted.loaded && (rebooted.generation==7u || rebooted.generation==8u));
        CHECK(rebooted.loaded_version==(rebooted.generation==7u?version:THY_SETTINGS_VERSION));
        CHECK(rebooted.migration_pending==(rebooted.loaded_version!=THY_SETTINGS_VERSION));
        CHECK(!memcmp(flash,original,THY_SETTINGS_SLOT_BYTES));
        defaults(); CHECK(thy_settings_apply(&engine,rebooted.record+32u,rebooted.payload_bytes));
        CHECK(instrument.lower_mode==(version==1u?0u:1u) && instrument.octave_layers==8u && instrument.excluded_slots[8]==16u);
        CHECK(instrument.fixed_velocity==103u);
        CHECK(instrument.release_policy.linked && instrument.release_policy.gap==23u);
    }
    }
}
static void arp_clock_roundtrip_and_fresh_runtime(void) {
    ++scenarios; defaults(); instrument.performance.root=7u;
    thy_arp_config first=engine.arps[0].config, second=engine.arps[1].config;
    first.enabled=1u; first.type=THY_ARP_NOTES; first.grouping=THY_ARP_HYBRID;
    first.source_zones=7u; first.layer=1u; first.start=THY_ARP_GRID;
    first.restart_on_change=1u; first.rate_ticks=320u; first.gate_percent=77u;
    first.swing_percent=31u; first.step_mask=165u; first.pattern=THY_ARP_DOWN_UP;
    first.channel=11u; first.destinations=THY_ROUTE_DIN;
    second.enabled=1u; second.type=THY_ARP_CHORD; second.grouping=THY_ARP_EACH;
    second.source_zones=56u; second.layer=0u; second.start=THY_ARP_IMMEDIATE;
    second.restart_on_change=0u; second.rate_ticks=1440u; second.gate_percent=100u;
    second.swing_percent=0u; second.step_mask=90u; second.pattern=THY_ARP_AS_PLAYED;
    second.channel=15u; second.destinations=THY_ROUTE_USB0 | THY_ROUTE_USB1;
    for(unsigned n=0;n<8u;++n) {first.offsets[n]=(int8_t)((int)n-7); second.offsets[n]=(int8_t)(24-(int)n*6);}
    CHECK(thy_engine_arp_configure(&engine,0u,&first)==THY_OK);
    CHECK(thy_engine_arp_configure(&engine,1u,&second)==THY_OK);
    CHECK(thy_engine_clock_configure(&engine,139125u,THY_CLOCK_SOURCE_USB,5u,8u,THY_TRANSPORT_BEAT)==THY_OK);
    engine.arp_target=1u; engine.arp_quick_keys=0u;
    size_t bytes=encode(payload); CHECK(bytes>THY_SETTINGS_V6_PARAMETER_BYTES);
    /* None of these transient values may participate in the value format. */
    engine.arps[0].held_count=4u; engine.arps[0].phrase_count=2u; engine.arps[0].output_count=3u;
    engine.arps[0].inputs[3].held=1u; engine.arps[0].outputs[7].handle=42u;
    engine.arps[0].attacks=123u; engine.arps[1].failures=456u;
    engine.arps[0].config.root=2u; engine.arps[0].config.scale_count=1u;
    engine.arps[1].config.root=11u; engine.arps[1].last_tick=123456u;
    engine.musical_clock.tick=99999u; engine.musical_clock.remainder=123u;
    engine.musical_clock.status=THY_CLOCK_LOST; engine.musical_clock.now_ms=666u;
    engine.rhythm_transport.position=777u; engine.rhythm_transport.state=THY_TRANSPORT_PLAYING;
    engine.rhythm_transport.tracks[0].running=1u; engine.rhythm_transport.tracks[0].playhead=888u;
    engine.arp_quick_open=1u; engine.arp_quick_cursor=5u;
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    defaults(); thy_engine_bind_music(&engine); thy_engine_bind_controls(&engine);
    thy_instrument_attack_fn attack=instrument.musical_attack;
    thy_instrument_release_fn release=instrument.musical_release;
    thy_instrument_reset_fn reset=instrument.musical_reset;
    thy_instrument_navigation_fn navigation=instrument.navigation;
    instrument.catalog_degree_count=instrument.degree_count=5u;
    const int16_t pentatonic[]={0,2,4,7,9};
    for(unsigned n=0;n<5u;++n) instrument.catalog_degrees[n]=instrument.degrees[n]=pentatonic[n];
    engine.live_generation[0]=7u; engine.rhythm_transport.tracks[0].generation=9u;
    engine.musical_clock.generation=11u; engine.rhythm_transport.generation=13u;
    core.generated_last_handle=1234u; engine.session.system=THY_ENGINE_READY;
    engine.rhythm_transport.state=THY_TRANSPORT_PLAYING; engine.rhythm_transport.position=54321u;
    engine.rhythm_transport.tracks[0].enabled=1u; engine.rhythm_transport.tracks[0].running=1u;
    engine.arp_quick_open=1u; engine.arp_quick_cursor=6u;
    CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.musical_attack==attack && instrument.musical_release==release && instrument.musical_reset==reset);
    CHECK(instrument.navigation==navigation && instrument.musical_context==&engine);
    CHECK(engine.live_generation[0]>7u && engine.rhythm_transport.tracks[0].generation>9u);
    CHECK(engine.musical_clock.generation==12u && engine.rhythm_transport.generation==14u);
    CHECK(core.generated_last_handle==1234u && !core.generated_count && !core.queue_count);
    CHECK(engine.arp_target==1u && !engine.arp_quick_keys && !engine.arp_quick_open && !engine.arp_quick_cursor);
    CHECK(engine.session.system==THY_ENGINE_READY && engine.session.capture==THY_ENGINE_CAPTURE_OFF);
    CHECK(engine.musical_clock.tempo_milli_bpm==139125u && engine.musical_clock.source==THY_CLOCK_SOURCE_USB);
    CHECK(engine.musical_clock.status==THY_CLOCK_WAITING && !engine.musical_clock.tick && !engine.musical_clock.remainder);
    CHECK(engine.musical_clock.numerator==5u && engine.musical_clock.denominator==8u && engine.musical_clock.bar_ticks==2400u);
    CHECK(engine.rhythm_transport.launch_quantize==THY_TRANSPORT_BEAT && engine.rhythm_transport.state==THY_TRANSPORT_STOPPED);
    CHECK(!engine.rhythm_transport.position && !engine.rhythm_epoch_set);
    for(unsigned n=0;n<THY_TRANSPORT_TRACKS;++n) {
        CHECK(!engine.rhythm_transport.tracks[n].running && !engine.rhythm_transport.tracks[n].enabled);
        CHECK(!engine.rhythm_transport.tracks[n].playhead && !engine.rhythm_transport.tracks[n].pending);
    }
    for(unsigned n=0;n<2u;++n) {
        const thy_arp *a=&engine.arps[n];
        CHECK(a->source==n+1u && !a->run && !a->held_count && !a->phrase_count && !a->output_count);
        CHECK(!a->last_tick && !a->press_order && !a->attacks && !a->failures);
        CHECK(a->config.enabled && a->config.root==7u && a->config.scale_count==5u);
        for(unsigned d=0;d<5u;++d) CHECK(a->config.scale[d]==pentatonic[d]);
        for(unsigned d=0;d<THY_ARP_INPUTS;++d) CHECK(!a->inputs[d].held);
        for(unsigned d=0;d<THY_GENERATED_OWNER_COUNT;++d) CHECK(!a->outputs[d].handle);
    }
    CHECK(engine.arps[0].config.rate_ticks==320u && engine.arps[0].config.gate_percent==77u && engine.arps[0].config.swing_percent==31u);
    CHECK(engine.arps[1].config.type==THY_ARP_CHORD && engine.arps[1].config.grouping==THY_ARP_EACH);
    CHECK(engine.arps[1].config.rate_ticks==1440u && engine.arps[1].config.channel==15u && engine.arps[1].config.destinations==3u);
    for(unsigned n=0;n<8u;++n) CHECK(engine.arps[0].config.offsets[n]==first.offsets[n] && engine.arps[1].config.offsets[n]==second.offsets[n]);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
}
static void arp_field_validation_and_atomicity(void) {
    ++scenarios; defaults(); size_t bytes=encode(payload);
    thy_engine before_engine=engine; thy_instrument before_instrument=instrument; thy_core before_core=core;
    const struct {size_t at;uint8_t invalid;} bad[]={
        {0u,0u},{1u,0u},{4u,25u},{5u,231u},{12u,2u},{13u,2u},{14u,3u},
        {15u,64u},{16u,2u},{17u,2u},{18u,2u},{19u,0u},{19u,101u},
        {20u,51u},{22u,5u},{23u,16u},{24u,0u},{24u,8u}
    };
    for(unsigned arp=0;arp<2u;++arp) for(unsigned n=0;n<sizeof bad/sizeof bad[0];++n) {
        size_t at=THY_SETTINGS_V2_PAYLOAD_BYTES+25u*arp+bad[n].at;
        uint8_t old=payload[at]; payload[at]=bad[n].invalid;
        /* Changing one zero rate byte to zero can be a no-op; zero the entire rate instead. */
        uint8_t rate[4]; memcpy(rate,payload+THY_SETTINGS_V2_PAYLOAD_BYTES+25u*arp,4u);
        if(bad[n].at<4u) memset(payload+THY_SETTINGS_V2_PAYLOAD_BYTES+25u*arp,0,4u);
        CHECK(!thy_settings_validate(payload,bytes,70u)); CHECK(!thy_settings_apply(&engine,payload,bytes));
        CHECK(!memcmp(&engine,&before_engine,sizeof engine) && !memcmp(&instrument,&before_instrument,sizeof instrument));
        CHECK(!memcmp(&core,&before_core,sizeof core));
        if(bad[n].at<4u) memcpy(payload+THY_SETTINGS_V2_PAYLOAD_BYTES+25u*arp,rate,4u);
        payload[at]=old;
    }
    const struct {size_t at;uint8_t invalid;} shared[]={
        {54u,2u},{55u,0u},{55u,17u},{56u,3u},{57u,3u},{58u,2u},{59u,2u}
    };
    for(unsigned n=0;n<sizeof shared/sizeof shared[0];++n) {
        size_t at=THY_SETTINGS_V2_PAYLOAD_BYTES+shared[n].at; uint8_t old=payload[at];payload[at]=shared[n].invalid;
        CHECK(!thy_settings_apply(&engine,payload,bytes));
        CHECK(!memcmp(&engine,&before_engine,sizeof engine) && !memcmp(&instrument,&before_instrument,sizeof instrument)); payload[at]=old;
    }
    uint8_t tempo[4]; memcpy(tempo,payload+THY_SETTINGS_V2_PAYLOAD_BYTES+50u,4u);
    put32(payload+THY_SETTINGS_V2_PAYLOAD_BYTES+50u,THY_CLOCK_TEMPO_MIN-1u);
    CHECK(!thy_settings_apply(&engine,payload,bytes));
    put32(payload+THY_SETTINGS_V2_PAYLOAD_BYTES+50u,THY_CLOCK_TEMPO_MAX+1u);
    CHECK(!thy_settings_apply(&engine,payload,bytes));
    memcpy(payload+THY_SETTINGS_V2_PAYLOAD_BYTES+50u,tempo,4u);
    payload[6]=THY_SETTINGS_VERSION+1u; CHECK(!thy_settings_payload_version(payload,bytes) && !thy_settings_apply(&engine,payload,bytes));payload[6]=THY_SETTINGS_VERSION;
    CHECK(!memcmp(&engine,&before_engine,sizeof engine) && !memcmp(&instrument,&before_instrument,sizeof instrument));
    CHECK(thy_core_sustain(&core,THY_ROUTE_USB0,0u,1)==THY_OK);thy_midi_event event;
    while(thy_core_poll(&core,&event)) {}
    CHECK(!thy_settings_apply(&engine,payload,bytes));
    CHECK(thy_core_sustain(&core,THY_ROUTE_USB0,0u,0)==THY_OK);while(thy_core_poll(&core,&event)) {}
    engine.arps[0].held_count=1u; CHECK(!thy_settings_apply(&engine,payload,bytes));engine.arps[0].held_count=0u;
    engine.arps[1].output_count=1u; CHECK(!thy_settings_apply(&engine,payload,bytes));engine.arps[1].output_count=0u;
    CHECK(thy_settings_apply(&engine,payload,bytes));
}
static void v2_journal_upgrade_and_defaults(void) {
    ++scenarios; defaults(); instrument.fixed_velocity=104u; instrument.lower_mode=1u;
    instrument.octave_layers=65u; old_record(2u); memcpy(original,flash,sizeof flash);
    thy_settings_io backend=io(); programs=erases=0u;
    CHECK(thy_settings_journal_init(&journal,&backend,70u));
    CHECK(journal.loaded_version==2u && journal.migration_pending && !programs && !erases);
    defaults(); engine.arps[0].config.enabled=1u; engine.arps[1].config.enabled=1u;
    engine.arp_target=1u; engine.arp_quick_keys=0u;
    CHECK(thy_engine_clock_configure(&engine,170000u,THY_CLOCK_SOURCE_USB,7u,8u,THY_TRANSPORT_IMMEDIATE)==THY_OK);
    CHECK(thy_settings_apply(&engine,journal.record+32u,journal.payload_bytes));
    CHECK(instrument.fixed_velocity==104u && instrument.lower_mode==1u && instrument.octave_layers==65u);
    CHECK(!engine.arps[0].config.enabled && !engine.arps[1].config.enabled);
    CHECK(!engine.arp_target && engine.arp_quick_keys==1u);
    CHECK(engine.musical_clock.source==THY_CLOCK_SOURCE_INTERNAL && engine.musical_clock.tempo_milli_bpm==120000u);
    CHECK(engine.musical_clock.numerator==4u && engine.musical_clock.denominator==4u);
    CHECK(engine.rhythm_transport.launch_quantize==THY_TRANSPORT_BAR);
    thy_settings_journal_task(&journal,&engine,0u,0);thy_settings_journal_task(&journal,&engine,1600u,0);
    CHECK(!programs && !erases && !memcmp(original,flash,sizeof flash));
    thy_settings_journal_task(&journal,&engine,1601u,1);CHECK(journal.writing);
    finish_save(1700u);
    CHECK(journal.loaded_version==THY_SETTINGS_VERSION && !journal.migration_pending && journal.generation==8u);
    CHECK(!memcmp(original,flash,THY_SETTINGS_SLOT_BYTES) && !erases);
    CHECK(thy_settings_journal_init(&rebooted,&backend,70u) && rebooted.loaded_version==THY_SETTINGS_VERSION);
}
static void v3_journal_upgrade_preserves_arps(void) {
    ++scenarios; defaults();
    engine.arps[0].config.enabled=1u; engine.arps[0].config.channel=7u;
    engine.arps[0].config.rate_ticks=320u; engine.arps[1].config.enabled=1u;
    engine.arps[1].config.type=THY_ARP_CHORD; engine.arps[1].config.step_mask=165u;
    engine.arp_target=1u; engine.arp_quick_keys=0u;
    CHECK(thy_engine_clock_configure(&engine,131125u,THY_CLOCK_SOURCE_USB,7u,8u,THY_TRANSPORT_BEAT)==THY_OK);
    old_record(3u); memcpy(original,flash,sizeof flash); thy_settings_io backend=io(); programs=erases=0u;
    CHECK(thy_settings_journal_init(&journal,&backend,70u));
    CHECK(journal.loaded_version==3u && journal.migration_pending && !programs && !erases);
    defaults(); CHECK(thy_settings_apply(&engine,journal.record+32u,journal.payload_bytes));
    CHECK(engine.arps[0].config.enabled && engine.arps[0].config.channel==7u && engine.arps[0].config.rate_ticks==320u);
    CHECK(engine.arps[1].config.enabled && engine.arps[1].config.type==THY_ARP_CHORD && engine.arps[1].config.step_mask==165u);
    CHECK(engine.arp_target==1u && !engine.arp_quick_keys);
    CHECK(engine.musical_clock.tempo_milli_bpm==131125u && engine.musical_clock.source==THY_CLOCK_SOURCE_USB);
    CHECK(engine.musical_clock.numerator==7u && engine.musical_clock.denominator==8u && engine.musical_clock.status==THY_CLOCK_WAITING);
    CHECK(!engine.arps[0].held_count && !engine.arps[1].held_count && !engine.arp_quick_open && !engine.musical_clock.tick);
    begin_save(0u); finish_save(1700u);
    CHECK(journal.loaded_version==THY_SETTINGS_VERSION && !journal.migration_pending && !erases);
    CHECK(!memcmp(flash,original,THY_SETTINGS_SLOT_BYTES));
    defaults(); CHECK(thy_settings_apply(&engine,journal.record+32u,journal.payload_bytes));
    CHECK(engine.arps[0].config.enabled && engine.arps[1].config.enabled && !core.generated_count && !core.queue_count);
}
static void oversized_future_record_preservation(void) {
    ++scenarios; defaults(); memset(flash,255u,sizeof flash); thy_settings_io backend=io();
    CHECK(thy_settings_journal_init(&journal,&backend,70u));begin_save(0u);finish_save(1600u);
    const uint32_t sizes[]={THY_SETTINGS_PAYLOAD_MAX+13u,THY_SETTINGS_SLOT_BYTES-THY_SETTINGS_HEADER_BYTES};
    for(unsigned n=0;n<sizeof sizes/sizeof sizes[0];++n) {
        uint8_t *future=flash+4u*THY_SETTINGS_SLOT_BYTES;memset(future,255u,4u*THY_SETTINGS_SLOT_BYTES);
        memset(future,0u,THY_SETTINGS_HEADER_BYTES);memset(future+32u,90u,sizes[n]);
        put32(future,UINT32_C(0x36594854));put32(future+4u,UINT32_C(0x31535436));
        put32(future+8u,THY_SETTINGS_VERSION+1u);put32(future+12u,sizes[n]);put32(future+16u,2u);
        put32(future+28u,UINT32_C(0x51afe666));crc_record(future);
        programs=erases=0u;memcpy(original,flash,sizeof flash);
        CHECK(!thy_settings_journal_init(&rebooted,&backend,70u));
        CHECK(rebooted.status==THY_SETTINGS_UNSUPPORTED && !programs && !erases);
        thy_settings_journal_task(&rebooted,&engine,9000u,1);thy_settings_journal_task(&rebooted,&engine,12000u,1);
        CHECK(!programs && !erases && !memcmp(original,flash,sizeof flash));
        /* The same recognizable future header is not trusted after CRC damage. */
        future[32u+sizes[n]-1u]^=1u;
        CHECK(thy_settings_journal_init(&rebooted,&backend,70u));
        CHECK(rebooted.loaded_version==THY_SETTINGS_VERSION && rebooted.generation==1u && rebooted.recovered && rebooted.rejected);
    }
}
static void sequencer_song_preferences_and_fresh_runtime(void) {
    ++scenarios; defaults();
    thy_seq_event chord[3] = {
        {0u,960u,0u,1u,2u,0x80u,60u,77u},
        {0u,960u,0u,1u,2u,0x80u,64u,78u},
        {0u,960u,0u,1u,2u,0x80u,67u,79u}};
    CHECK(thy_sequencer_add(&engine.sequencer,2u,chord,3u)==THY_OK);
    CHECK(thy_sequencer_loop(&engine.sequencer,&engine.rhythm_transport,2u,240u,7680u)==THY_OK);
    engine.sequencer.tracks[2].channel=15u;
    engine.sequencer.tracks[2].processor=THY_SEQ_ARP2;
    engine.sequencer.tracks[2].record_source=THY_SEQ_SOURCE_ARP1;
    engine.sequencer.tracks[2].record_policy=THY_SEQ_OVERDUB;
    engine.sequencer.tracks[2].flags=THY_SEQ_FLAG_ONESHOT | THY_SEQ_FLAG_RECORD_CONTROLLERS | THY_SEQ_FLAG_SEND_PRESSURE | (3u<<THY_SEQ_QUANTIZE_SHIFT);
    CHECK(thy_sequencer_create(&engine.sequencer,20u)==THY_OK);
    engine.sequencer.tracks[20].grid_ticks=120u;
    engine.sequencer.selected_track=20u;
    engine.sequencer_ui.layout=THY_SEQ_FULL_PITCH; engine.sequencer_ui.swapped=1u;
    engine.sequencer_ui.input_layout=THY_SEQ_INPUT_CHORDS; engine.sequencer_ui.base_pitch=36u;
    engine.sequencer_ui.grid_ticks=120u; engine.sequencer_ui.window_tick=2880u;
    engine.sequencer_ui.region_enabled=1u; engine.sequencer_ui.region_start=240u; engine.sequencer_ui.region_end=3840u;
    engine.remote_record=1u; engine.remote_position=1u; engine.remote_channel=14u;
    instrument.engine_kind=THY_PLAY_ENGINE_SEQUENCER;
    size_t bytes=encode(payload); CHECK(thy_settings_payload_version(payload,bytes)==THY_SETTINGS_VERSION);
    /* Runtime capture failure and UI gestures are never part of a song save. */
    engine.sequencer.capture.state=THY_SEQ_FAILED;
    engine.sequencer.capture.failed=1u; engine.sequencer.capture.count=11u;
    engine.sequencer_ui.open=1u; engine.sequencer_ui.modifier=1u;
    engine.sequencer_ui.pads[0].held=1u;
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    defaults(); thy_engine_bind_music(&engine); thy_engine_bind_controls(&engine);
    CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(engine.sequencer.count==3u && engine.sequencer.selected_track==20u);
    CHECK(engine.sequencer.events[0].group==engine.sequencer.events[2].group);
    CHECK(engine.sequencer.tracks[2].channel==15u && engine.sequencer.tracks[2].processor==THY_SEQ_ARP2);
    CHECK(engine.sequencer.tracks[2].record_source==THY_SEQ_SOURCE_ARP1 && engine.sequencer.tracks[2].record_policy==THY_SEQ_OVERDUB);
    CHECK(engine.sequencer.tracks[2].loop_start==240u && engine.sequencer.tracks[2].loop_end==7680u);
    CHECK(engine.sequencer.tracks[2].flags & THY_SEQ_FLAG_ONESHOT);
    CHECK(engine.sequencer_ui.layout==THY_SEQ_SPLIT && !engine.sequencer_ui.swapped && engine.sequencer_ui.input_layout==THY_SEQ_INPUT_CHORDS);
    CHECK(engine.sequencer_ui.base_pitch==36u && engine.sequencer_ui.grid_ticks==120u && !engine.sequencer_ui.window_tick);
    CHECK(!engine.sequencer_ui.region_enabled && !engine.sequencer_ui.region_start && engine.sequencer_ui.region_end==3840u);
    CHECK(thy_sequencer_track_exists(&engine.sequencer,20u) && !thy_sequencer_track_count(&engine.sequencer,20u));
    CHECK(engine.remote_record && engine.remote_position && engine.remote_channel==14u);
    CHECK(engine.rhythm_transport.state==THY_TRANSPORT_STOPPED && !engine.rhythm_transport.position);
    CHECK(engine.sequencer.capture.state==THY_SEQ_CAPTURE_OFF && !engine.sequencer.voice_count);
    CHECK(engine.sequencer_ui.open && !engine.sequencer_ui.modifier && !engine.sequencer_ui.pads[0].held);
    CHECK(!core.sounding_count && !core.generated_count && !core.queue_count);
    for(unsigned n=0u;n<THY_SEQ_TRACKS;++n) CHECK(!engine.rhythm_transport.tracks[n].running && !engine.rhythm_transport.tracks[n].pending);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
}
static void v6_preference_validation_is_atomic(void) {
    ++scenarios; defaults(); size_t bytes=encode(payload);
    const thy_engine before_engine=engine;
    const thy_instrument before_instrument=instrument;
    const struct {unsigned at;uint8_t invalid;} bad[] = {
        {0u,3u},{1u,2u},{2u,2u},{3u,121u},{4u,21u},
        {5u,0u},{9u,255u},{13u,255u},{17u,255u},
        {21u,2u},{22u,2u},{23u,2u},{24u,17u}};
    for(unsigned n=0u;n<sizeof bad/sizeof bad[0];++n) {
        unsigned at=THY_SETTINGS_V5_PAYLOAD_BYTES+bad[n].at;
        uint8_t old=payload[at]; payload[at]=bad[n].invalid;
        /* Multi-byte tick fields must be impossible as a complete value. */
        uint8_t saved[4]; unsigned word_at=at;
        int word=bad[n].at==5u || bad[n].at==9u || bad[n].at==13u || bad[n].at==17u;
        if(word) {memcpy(saved,payload+word_at,4u);put32(payload+word_at,UINT32_MAX);}
        CHECK(!thy_settings_validate(payload,bytes,70u));
        CHECK(!thy_settings_apply(&engine,payload,bytes));
        CHECK(!memcmp(&engine,&before_engine,sizeof engine) && !memcmp(&instrument,&before_instrument,sizeof instrument));
        if(word) memcpy(payload+word_at,saved,4u);
        payload[at]=old;
    }
    unsigned song=THY_SETTINGS_V19_PARAMETER_BYTES;
    payload[song+32u]^=1u;
    CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&engine,&before_engine,sizeof engine));
}
static void v5_left_shortcut_migration(void) {
    ++scenarios;
    const unsigned destinations[]={THY_PLAY_HIDDEN,THY_PLAY_LIGHTS,THY_PLAY_VIEWS,THY_PLAY_ARPS,THY_PLAY_ENGINES};
    for(unsigned n=0u;n<sizeof destinations/sizeof destinations[0];++n) {
        defaults(); instrument.play_nav[0]=(uint8_t)destinations[n];
        size_t bytes=v5_export(roundtrip);
        defaults(); CHECK(thy_settings_apply(&engine,roundtrip,bytes));
        CHECK(instrument.play_nav[0]==(destinations[n]==THY_PLAY_HIDDEN?THY_PLAY_ENGINES:destinations[n]));
        CHECK(!engine.sequencer.count && engine.rhythm_transport.state==THY_TRANSPORT_STOPPED);
    }
    defaults(); instrument.play_nav[0]=THY_PLAY_HIDDEN;
    size_t bytes=encode(payload); defaults(); CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.play_nav[0]==THY_PLAY_HIDDEN); /* A deliberate v6 Hidden remains Hidden. */
}
static void full_song_journal_word_and_boundary_power_cuts(void) {
    ++scenarios; defaults(); memset(flash,255u,sizeof flash); thy_settings_io backend=io();
    CHECK(thy_settings_journal_init(&journal,&backend,70u)); begin_save(0u);finish_save(1600u);
    memcpy(original,flash,sizeof flash);
    for(unsigned n=0u;n<THY_SEQ_EVENTS;++n) full_song[n]=(thy_seq_event){
        n*120u,60u,0u,0u,0u,THY_SEQ_NOTE,(uint8_t)(48u+n%36u),(uint8_t)(32u+n%95u)};
    CHECK(thy_sequencer_add(&engine.sequencer,0u,full_song,THY_SEQ_EVENTS)==THY_OK);
    instrument.fixed_velocity=113u;
    thy_instrument_velocity global={2u,128u,THY_VELOCITY_STRIKE,1u,12u,120u};
    thy_instrument_velocity custom={4u,192u,THY_VELOCITY_STRIKE,2u,24u,110u};
    CHECK(thy_instrument_set_velocity(&instrument,&global)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,95u,&custom)==THY_OK);
    CHECK(thy_instrument_set_trigger(&instrument,-1,1300u)==THY_OK);
    thy_instrument_release_policy release_policy={55u,1u};
    CHECK(thy_instrument_set_release_policy(&instrument,95,&release_policy)==THY_OK);
    size_t bytes=encode(payload); CHECK(bytes>8192u && bytes<=THY_SETTINGS_PAYLOAD_MAX);
    unsigned total=(unsigned)((THY_SETTINGS_HEADER_BYTES+bytes+3u)&~3u);
    /* Every word boundary across the maximum song, plus every byte around
     * legacy-slot boundaries and final commit, exercises a multi-slot record. */
    for(unsigned cut=0u;cut<=total;++cut) {
        if(cut%4u && !(cut>=4093u && cut<=4099u) && !(cut>=8189u && cut<=8195u) && cut+8u<total) continue;
        memcpy(flash,original,sizeof flash); cut_after=-1;
        CHECK(thy_settings_journal_init(&journal,&backend,70u)); begin_save(10000u);
        cut_after=(int)cut;
        for(unsigned n=0u;n<5000u && journal.writing;++n) thy_settings_journal_task(&journal,&engine,12000u+n,1);
        cut_after=-1;
        CHECK(thy_settings_journal_init(&rebooted,&backend,70u) && rebooted.loaded);
        CHECK(rebooted.generation==1u || rebooted.generation==2u);
        defaults(); CHECK(thy_settings_apply(&engine,rebooted.record+32u,rebooted.payload_bytes));
        CHECK(engine.sequencer.count==(rebooted.generation==1u?0u:THY_SEQ_EVENTS));
        CHECK(instrument.fixed_velocity==(rebooted.generation==1u?96u:113u));
        CHECK(instrument.velocity_response.mode==(rebooted.generation==1u?THY_VELOCITY_FIXED:THY_VELOCITY_STRIKE));
        CHECK(instrument.velocity_overridden[95u]==(rebooted.generation==2u));
        if(rebooted.generation==2u)CHECK(!memcmp(&instrument.pad_velocity[95u],&custom,sizeof custom));
        CHECK(instrument.response.attack_raw==(rebooted.generation==1u?1377u:1300u));
        CHECK(instrument.response.release_raw==(rebooted.generation==1u?1354u:1277u));
        CHECK(instrument.sensors[95u].response_overridden==(rebooted.generation==2u));
        if(rebooted.generation==2u)CHECK(instrument.pad_release_policy[95u].linked && instrument.pad_release_policy[95u].gap==55u);
        CHECK(!core.sounding_count && engine.rhythm_transport.state==THY_TRANSPORT_STOPPED);
        /* Reconstruct the next write without changing its content. */
        if(!engine.sequencer.count) CHECK(thy_sequencer_add(&engine.sequencer,0u,full_song,THY_SEQ_EVENTS)==THY_OK);
        instrument.fixed_velocity=113u;
        CHECK(thy_instrument_set_velocity(&instrument,&global)==THY_OK);
        CHECK(thy_instrument_set_pad_velocity(&instrument,95u,&custom)==THY_OK);
        CHECK(thy_instrument_set_trigger(&instrument,-1,1300u)==THY_OK);
        CHECK(thy_instrument_set_release_policy(&instrument,95,&release_policy)==THY_OK);
    }
}
static void velocity_roundtrip_overrides_and_clean_strike_runtime(void) {
    ++scenarios;defaults();
    thy_instrument_velocity global={3u,144u,THY_VELOCITY_STRIKE,1u,9u,118u};
    CHECK(thy_instrument_set_velocity(&instrument,&global)==THY_OK);
    for(unsigned n=0u;n<THY_PAD_COUNT;++n) if(n%3u==0u || n==95u) {
        thy_instrument_velocity custom={(uint16_t)(2u+n),(uint16_t)(160u+n),
            (uint8_t)(n%2u),(uint8_t)(n%3u),(uint8_t)(1u+n%32u),(uint8_t)(96u+n%32u)};
        CHECK(thy_instrument_set_pad_velocity(&instrument,(uint8_t)n,&custom)==THY_OK);
    }
    thy_seq_event note={0u,240u,0u,0u,0u,THY_SEQ_NOTE,64u,91u};
    CHECK(thy_sequencer_add(&engine.sequencer,0u,&note,1u)==THY_OK);
    size_t bytes=encode(payload);
    CHECK(THY_SETTINGS_V7_PARAMETER_BYTES==THY_SETTINGS_V6_PARAMETER_BYTES+872u);
    CHECK(bytes==THY_SETTINGS_V19_PARAMETER_BYTES+thy_sequencer_song_size(&engine.sequencer));
    CHECK(thy_settings_payload_version(payload,bytes)==THY_SETTINGS_VERSION);
    for(unsigned n=0u;n<THY_PAD_COUNT;++n) {
        instrument.sensors[n].strike_peak_rate=777u;instrument.sensors[n].strike_peak_ms=333u;
        instrument.sensors[n].strike_velocity=112u;instrument.sensors[n].strike_ready=1u;
    }
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    defaults();CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&instrument.velocity_response,&global,sizeof global));
    for(unsigned n=0u;n<THY_PAD_COUNT;++n) {
        thy_instrument_velocity actual;uint8_t overridden=0u;
        CHECK(thy_instrument_get_velocity(&instrument,(int)n,&actual,&overridden)==THY_OK);
        CHECK(overridden==(n%3u==0u || n==95u));
        if(overridden) {
            CHECK(actual.soft_rate==2u+n && actual.hard_rate==160u+n && actual.mode==n%2u);
            CHECK(actual.curve==n%3u && actual.minimum==1u+n%32u && actual.maximum==96u+n%32u);
        } else CHECK(!memcmp(&actual,&global,sizeof global));
        CHECK(!instrument.sensors[n].strike_ready && !instrument.sensors[n].strike_peak_rate &&
            !instrument.sensors[n].strike_peak_ms && !instrument.sensors[n].strike_velocity);
    }
    CHECK(engine.sequencer.count==1u && engine.sequencer.events[0].pitch==64u);
    CHECK(!core.sounding_count && !core.generated_count && !core.queue_count);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    CHECK(thy_instrument_clear_pad_velocity(&instrument,95u)==THY_OK);
    CHECK(!instrument.velocity_overridden[95u]);
    size_t cleared=encode(roundtrip);defaults();CHECK(thy_settings_apply(&engine,roundtrip,cleared));
    CHECK(!instrument.velocity_overridden[95u] && !memcmp(&instrument.velocity_response,&global,sizeof global));
}
static void velocity_invalid_fields_reject_atomically(void) {
    ++scenarios;defaults();size_t bytes=encode(payload);
    const thy_instrument before_instrument=instrument;const thy_engine before_engine=engine;
    const struct {unsigned field;uint8_t value;} bad[]={{4u,2u},{5u,3u},{6u,0u},{6u,128u},{7u,0u},{7u,128u}};
    for(unsigned target=0u;target<2u;++target) {
        size_t at=THY_SETTINGS_V6_PARAMETER_BYTES+(target?8u+95u*9u+1u:0u);
        for(unsigned n=0u;n<sizeof bad/sizeof bad[0];++n) {
            uint8_t old=payload[at+bad[n].field];payload[at+bad[n].field]=bad[n].value;
            CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
            CHECK(!memcmp(&instrument,&before_instrument,sizeof instrument) && !memcmp(&engine,&before_engine,sizeof engine));
            payload[at+bad[n].field]=old;
        }
        uint8_t rates[4];memcpy(rates,payload+at,4u);memset(payload+at,0,4u);
        CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
        CHECK(!memcmp(&instrument,&before_instrument,sizeof instrument) && !memcmp(&engine,&before_engine,sizeof engine));
        memcpy(payload+at,rates,4u);
    }
    size_t flag=THY_SETTINGS_V6_PARAMETER_BYTES+8u+95u*9u;payload[flag]=2u;
    CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));payload[flag]=0u;
    /* Unselected overrides cannot hide a malformed/noncanonical configuration. */
    payload[flag+1u+6u]=17u;
    CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&instrument,&before_instrument,sizeof instrument) && !memcmp(&engine,&before_engine,sizeof engine));
}
static void v6_song_migration_and_versioned_song_recall(void) {
    ++scenarios;defaults();
    thy_seq_event notes[]={{0u,240u,0u,7u,2u,0x80u,60u,91u},{0u,240u,0u,7u,2u,0x80u,67u,92u}};
    CHECK(thy_sequencer_add(&engine.sequencer,2u,notes,2u)==THY_OK);
    CHECK(thy_sequencer_create(&engine.sequencer,20u)==THY_OK);
    engine.sequencer.tracks[2].channel=8u;engine.sequencer.tracks[2].destinations=THY_ROUTE_DIN;
    thy_instrument_velocity strike={2u,128u,THY_VELOCITY_STRIKE,2u,7u,121u};
    CHECK(thy_instrument_set_velocity(&instrument,&strike)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,95u,&strike)==THY_OK);
    size_t old_bytes=v6_export(roundtrip);defaults();
    CHECK(thy_instrument_set_velocity(&instrument,&strike)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,95u,&strike)==THY_OK);
    CHECK(thy_settings_apply(&engine,roundtrip,old_bytes));
    CHECK(instrument.velocity_response.mode==THY_VELOCITY_FIXED);
    for(unsigned n=0u;n<THY_PAD_COUNT;++n)CHECK(!instrument.velocity_overridden[n]);
    CHECK(engine.sequencer.count==2u && engine.sequencer.events[0].pitch==60u && engine.sequencer.events[1].pitch==67u);
    CHECK(engine.sequencer.tracks[2].channel==8u && engine.sequencer.tracks[2].destinations==THY_ROUTE_DIN);
    CHECK(thy_sequencer_track_exists(&engine.sequencer,20u) && !thy_sequencer_track_count(&engine.sequencer,20u));
    for(unsigned version=6u;version<=13u;++version) {
        if(version<=12u)old_record(version);
        else {memset(flash,255u,sizeof flash);thy_settings_io backend=io();CHECK(thy_settings_journal_init(&journal,&backend,70u));begin_save(0u);finish_save(1600u);}
        thy_settings_io backend=io();CHECK(thy_settings_journal_init(&rebooted,&backend,70u));
        CHECK(thy_sequencer_clear(&engine.sequencer,2u)==THY_OK);
        CHECK(thy_instrument_set_velocity(&instrument,&strike)==THY_OK);
        CHECK(thy_settings_journal_recall_song(&rebooted,&engine));
        CHECK(engine.sequencer.count==2u && engine.sequencer.tracks[2].channel==8u);
        CHECK(!memcmp(&instrument.velocity_response,&strike,sizeof strike));
    }
}
static void release_policy_roundtrip_validation_and_independent_pressure(void) {
    ++scenarios;defaults();
    CHECK(thy_instrument_set_trigger(&instrument,-1,1300u)==THY_OK);
    CHECK(instrument.response.release_raw==1277u && instrument.response.pressure_floor==1353u);
    thy_instrument_release_policy linked={55u,1u},manual={123u,0u};
    CHECK(thy_instrument_set_release_policy(&instrument,0,&linked)==THY_OK);
    CHECK(thy_instrument_set_trigger(&instrument,0,1200u)==THY_OK);
    CHECK(thy_instrument_set_release_policy(&instrument,95,&manual)==THY_OK);
    size_t bytes=encode(payload);
    CHECK(THY_SETTINGS_V8_PARAMETER_BYTES==3508u && THY_SETTINGS_V9_PARAMETER_BYTES==3740u && THY_SETTINGS_RECORD_BYTES==13888u);
    defaults();CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.response.attack_raw==1300u && instrument.response.release_raw==1277u);
    CHECK(instrument.response.pressure_floor==1353u && instrument.release_policy.linked && instrument.release_policy.gap==23u);
    CHECK(instrument.sensors[0].response.attack_raw==1200u && instrument.sensors[0].response.release_raw==1145u);
    CHECK(instrument.pad_release_policy[0].linked && instrument.pad_release_policy[0].gap==55u);
    CHECK(!instrument.pad_release_policy[95].linked && instrument.pad_release_policy[95].gap==123u);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    const thy_instrument before_instrument=instrument;const thy_engine before_engine=engine;
    const struct {size_t offset;uint8_t invalid;} bad[]={
        {THY_SETTINGS_V7_PARAMETER_BYTES,2u},{THY_SETTINGS_V7_PARAMETER_BYTES+1u,0u},
        {THY_SETTINGS_V7_PARAMETER_BYTES+3u,2u},{THY_SETTINGS_V7_PARAMETER_BYTES+4u,0u}};
    for(unsigned n=0;n<sizeof bad/sizeof bad[0];++n){
        uint8_t saved=payload[bad[n].offset];payload[bad[n].offset]=bad[n].invalid;
        CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
        CHECK(!memcmp(&instrument,&before_instrument,sizeof instrument) && !memcmp(&engine,&before_engine,sizeof engine));
        payload[bad[n].offset]=saved;
    }
    /* A valid but inconsistent linked gap must not silently change Release. */
    payload[THY_SETTINGS_V7_PARAMETER_BYTES+1u]=24u;
    CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&instrument,&before_instrument,sizeof instrument));
}
static void v7_frozen_response_rules_and_lossless_linked_migration(void) {
    ++scenarios;defaults();
    thy_instrument_response global={1500u,1400u,1300u,3900u,1u};
    thy_instrument_response local={1700u,1600u,1500u,4000u,2u};
    CHECK(thy_instrument_set_response(&instrument,&global)==THY_OK);
    CHECK(thy_instrument_set_pad_response(&instrument,95u,&local)==THY_OK);
    thy_seq_event note={0u,240u,0u,0u,0u,THY_SEQ_NOTE,61u,92u};
    CHECK(thy_sequencer_add(&engine.sequencer,0u,&note,1u)==THY_OK);
    size_t bytes=v7_export(roundtrip);
    defaults();CHECK(thy_settings_apply(&engine,roundtrip,bytes));
    CHECK(instrument.response.attack_raw==1500u && instrument.response.release_raw==1400u);
    CHECK(instrument.release_policy.linked && instrument.release_policy.gap==100u);
    CHECK(instrument.sensors[95u].response.attack_raw==1700u && instrument.sensors[95u].response.release_raw==1600u);
    CHECK(instrument.pad_release_policy[95u].linked && instrument.pad_release_policy[95u].gap==100u);
    CHECK(engine.sequencer.count==1u && engine.sequencer.events[0].pitch==61u && !core.sounding_count);
    /* Frozen V7 global pressure-floor offset is 63. Make Floor>Release while
     * preserving a valid independent range: V8 allows it, old V7 never did. */
    roundtrip[63u]=0xdcu;roundtrip[64u]=0x05u; /* 1500 */
    CHECK(!thy_settings_validate(roundtrip,bytes,70u));
    const thy_instrument unchanged=instrument;
    CHECK(!thy_settings_apply(&engine,roundtrip,bytes) && !memcmp(&unchanged,&instrument,sizeof instrument));
}
static unsigned preview_probe_calls;
static int transient_sampling_probe(void *context,uint8_t electrical,
    thy_instrument_response *response,thy_instrument_velocity *velocity) {
    (void)context;(void)electrical;(void)response;(void)velocity;++preview_probe_calls;return 0;
}
static void cluster_defaults_roundtrip_and_mapping_safe_inheritance(void) {
    ++scenarios;defaults();
    uint8_t mapping[THY_PAD_COUNT];for(unsigned n=0;n<THY_PAD_COUNT;++n)mapping[n]=(uint8_t)n;
    mapping[0]=95u;mapping[95]=0u;
    CHECK(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    const thy_instrument_response left={820u,797u,1353u,4095u,1u},right={1100u,1060u,900u,3000u,2u};
    const thy_instrument_velocity left_velocity={3u,100u,THY_VELOCITY_STRIKE,1u,2u,120u};
    const thy_instrument_velocity right_velocity={5u,200u,THY_VELOCITY_FIXED,2u,4u,127u};
    const thy_instrument_release_policy left_policy={23u,1u},right_policy={40u,1u};
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,0u,&left,&left_velocity,&left_policy)==THY_OK);
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,1u,&right,&right_velocity,&right_policy)==THY_OK);
    const thy_instrument_response local={900u,870u,400u,3200u,1u};
    const thy_instrument_velocity local_velocity={9u,160u,THY_VELOCITY_STRIKE,2u,5u,110u};
    CHECK(thy_instrument_set_pad_response(&instrument,4u,&local)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,48u,&local_velocity)==THY_OK);
    /* A saved guard is independent of override ownership and raw thresholds;
     * advanced raw edits may intentionally differ while the basic UI guards. */
    instrument.sensitivity_guard[0]=600u;instrument.sensitivity_guard[4]=800u;
    instrument.sensitivity_guard[48]=4094u;instrument.sensitivity_guard[95]=2001u;
    size_t bytes=encode(payload);
    CHECK(THY_SETTINGS_VERSION==19u && THY_SETTINGS_V9_PARAMETER_BYTES==3740u && THY_SETTINGS_RECORD_BYTES==13888u);
    CHECK(bytes==THY_SETTINGS_V19_PARAMETER_BYTES+thy_sequencer_song_size(&engine.sequencer));
    CHECK(thy_settings_payload_version(payload,bytes)==THY_SETTINGS_VERSION);
    /* Runtime providers and their contexts never enter the explicit codec. */
    preview_probe_calls=0u;
    thy_instrument_set_sampling_provider(&instrument,transient_sampling_probe,&preview_probe_calls);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes) && !preview_probe_calls);
    defaults();CHECK(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    thy_instrument_sampling_provider_fn provider_before=instrument.sampling_provider;
    void *context_before=instrument.sampling_provider_context;
    CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.sampling_provider==provider_before && instrument.sampling_provider_context==context_before);
    CHECK(instrument.cluster_response[0].attack_raw==820u && instrument.cluster_response[0].release_raw==797u);
    CHECK(instrument.cluster_response[1].attack_raw==1100u && instrument.cluster_response[1].release_raw==1060u);
    CHECK(instrument.cluster_release_policy[0].gap==23u && instrument.cluster_release_policy[1].gap==40u);
    CHECK(!memcmp(&instrument.cluster_velocity[0],&left_velocity,sizeof left_velocity));
    CHECK(!memcmp(&instrument.cluster_velocity[1],&right_velocity,sizeof right_velocity));
    CHECK(!instrument.sensors[0].response_overridden && instrument.sensors[0].response.attack_raw==1100u);
    CHECK(!instrument.sensors[95].response_overridden && instrument.sensors[95].response.attack_raw==820u);
    CHECK(instrument.sensors[4].response_overridden && instrument.sensors[4].response.attack_raw==900u);
    CHECK(instrument.velocity_overridden[48] && !memcmp(&instrument.pad_velocity[48],&local_velocity,sizeof local_velocity));
    CHECK(!instrument.velocity_overridden[0] && !memcmp(&instrument.pad_velocity[0],&right_velocity,sizeof right_velocity));
    CHECK(!instrument.velocity_overridden[95] && !memcmp(&instrument.pad_velocity[95],&left_velocity,sizeof left_velocity));
    CHECK(instrument.sensitivity_guard[0]==600u && instrument.sensitivity_guard[4]==800u);
    CHECK(instrument.sensitivity_guard[48]==4094u && instrument.sensitivity_guard[95]==2001u);
    CHECK(!core.sounding_count && !core.generated_count && !core.queue_count && !preview_probe_calls);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    thy_instrument_set_sampling_provider(&instrument,transient_sampling_probe,&programs);
    CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.sampling_provider==transient_sampling_probe && instrument.sampling_provider_context==&programs);
    CHECK(!preview_probe_calls);
    const thy_instrument unchanged_instrument=instrument;const thy_engine unchanged_engine=engine;
    const struct {size_t at;uint8_t value;} bad[]={
        {THY_SETTINGS_V8_PARAMETER_BYTES+8u,3u},
        {THY_SETTINGS_V8_PARAMETER_BYTES+17u,2u},
        {THY_SETTINGS_V8_PARAMETER_BYTES+18u,24u},
        {THY_SETTINGS_V8_PARAMETER_BYTES+20u+13u,2u},
        {THY_SETTINGS_V8_PARAMETER_BYTES+20u+15u,0u}
    };
    for(unsigned n=0;n<sizeof bad/sizeof bad[0];++n){
        uint8_t old=payload[bad[n].at];payload[bad[n].at]=bad[n].value;
        CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
        CHECK(!memcmp(&instrument,&unchanged_instrument,sizeof instrument) && !memcmp(&engine,&unchanged_engine,sizeof engine));
        payload[bad[n].at]=old;
    }
    /* Inactive legacy pad entries remain globally canonical in V9; the new
     * tail supplies actual per-cluster inheritance through the runtime map. */
    uint8_t old=payload[189u];payload[189u]^=1u;
    CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));payload[189u]=old;
    size_t guard_at=THY_SETTINGS_V8_PARAMETER_BYTES+40u+95u*2u;
    uint8_t old_guard[2];memcpy(old_guard,payload+guard_at,2u);
    payload[guard_at]=0xffu;payload[guard_at+1u]=0x0fu;
    CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&instrument,&unchanged_instrument,sizeof instrument) && !memcmp(&engine,&unchanged_engine,sizeof engine));
    memcpy(payload+guard_at,old_guard,2u);
}
static void v8_migration_preserves_raw_values_in_both_cluster_defaults(void) {
    ++scenarios;defaults();
    const thy_instrument_response global={1500u,1400u,1510u,3900u,1u},local={1700u,1650u,1000u,4000u,2u};
    const thy_instrument_velocity velocity={4u,170u,THY_VELOCITY_STRIKE,2u,7u,119u};
    CHECK(thy_instrument_set_response(&instrument,&global)==THY_OK);
    CHECK(thy_instrument_set_pad_response(&instrument,95u,&local)==THY_OK);
    CHECK(thy_instrument_set_velocity(&instrument,&velocity)==THY_OK);
    size_t bytes=v8_export(roundtrip);
    CHECK(thy_settings_payload_version(roundtrip,bytes)==8u);
    defaults();instrument.sensitivity_guard[0]=222u;instrument.sensitivity_guard[48]=333u;
    CHECK(thy_settings_apply(&engine,roundtrip,bytes));
    for(unsigned side=0;side<2u;++side){
        CHECK(instrument.cluster_response[side].attack_raw==1500u && instrument.cluster_response[side].release_raw==1400u);
        CHECK(instrument.cluster_response[side].pressure_floor==1510u && instrument.cluster_response[side].pressure_full_scale==3900u);
        CHECK(!instrument.cluster_release_policy[side].linked && instrument.cluster_release_policy[side].gap==100u);
        CHECK(!memcmp(&instrument.cluster_velocity[side],&velocity,sizeof velocity));
    }
    CHECK(instrument.sensors[0].response.attack_raw==1500u && instrument.sensors[48].response.attack_raw==1500u);
    CHECK(instrument.sensors[95].response_overridden && instrument.sensors[95].response.attack_raw==1700u);
    CHECK(!instrument.pad_release_policy[95].linked && instrument.pad_release_policy[95].gap==50u);
    for(unsigned n=0;n<THY_PAD_COUNT;++n)CHECK(!instrument.sensitivity_guard[n]);
    size_t upgraded=encode(payload);CHECK(thy_settings_payload_version(payload,upgraded)==THY_SETTINGS_VERSION);
    CHECK(upgraded==bytes+(THY_SETTINGS_V19_PARAMETER_BYTES-THY_SETTINGS_V8_PARAMETER_BYTES) && thy_settings_validate(payload,upgraded,70u));
    old_record(8u);programs=erases=0u;thy_settings_io backend=io();
    CHECK(thy_settings_journal_init(&journal,&backend,70u));
    CHECK(journal.loaded_version==8u && journal.migration_pending && !programs && !erases);
}
static void mpe_preferences_roundtrip_and_atomic_validation(void) {
    ++scenarios;defaults();thy_engine_bind_expression(&engine,&expression);
    CHECK(THY_SETTINGS_VERSION==19u && THY_SETTINGS_V9_PARAMETER_BYTES==3740u && THY_SETTINGS_V10_PARAMETER_BYTES==3748u && THY_SETTINGS_V11_PARAMETER_BYTES==4263u);
    CHECK(!engine.expression_config.enabled && !engine.expression_config.glide);
    CHECK(engine.expression_config.members==15u && engine.expression_config.bend_range==48u);
    thy_mpe_config config=engine.expression_config;
    config.enabled=1u;config.upper_zone=1u;config.members=7u;config.bend_range=24u;
    config.manager_bend_range=12u;config.glide=1u;config.pad_window=3u;config.semitone_window=7u;
    CHECK(thy_engine_expression_configure(&engine,&config)==THY_OK && !core.queue_count);
    thy_seq_event note={0u,240u,0u,0u,0u,THY_SEQ_NOTE,62u,91u};
    CHECK(thy_sequencer_add(&engine.sequencer,0u,&note,1u)==THY_OK);
    size_t bytes=encode(payload);
    CHECK(bytes==THY_SETTINGS_V19_PARAMETER_BYTES+thy_sequencer_song_size(&engine.sequencer));
    CHECK(thy_settings_payload_version(payload,bytes)==THY_SETTINGS_VERSION);
    const uint8_t expected[8]={1u,1u,7u,24u,12u,1u,3u,7u};
    CHECK(!memcmp(payload+THY_SETTINGS_V9_PARAMETER_BYTES,expected,sizeof expected));
    defaults();const thy_instrument no_workspace_i=instrument;const thy_engine no_workspace_e=engine;
    CHECK(!thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&instrument,&no_workspace_i,sizeof instrument) && !memcmp(&engine,&no_workspace_e,sizeof engine));
    thy_engine_bind_expression(&engine,&expression);
    CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&engine.expression_config,&config,sizeof config));
    CHECK(!memcmp(&expression.config,&config,sizeof config));
    CHECK(!core.queue_count && !core.sounding_count && !core.generated_count && thy_mpe_quiet(&expression));
    CHECK(engine.sequencer.count==1u && engine.sequencer.events[0].pitch==62u);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    const thy_instrument before_i=instrument;const thy_engine before_e=engine;const thy_mpe before_mpe=expression;
    const uint8_t invalid[8]={2u,2u,0u,0u,97u,2u,0u,25u};
    for(unsigned n=0;n<8u;++n){
        size_t offset=THY_SETTINGS_V9_PARAMETER_BYTES+n;uint8_t saved=payload[offset];payload[offset]=invalid[n];
        CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
        CHECK(!memcmp(&instrument,&before_i,sizeof instrument) && !memcmp(&engine,&before_e,sizeof engine));
        CHECK(!memcmp(&expression,&before_mpe,sizeof expression));payload[offset]=saved;
    }
    /* MPE cannot silently claim channels also used by retained generators. */
    uint8_t saved=payload[THY_SETTINGS_V2_PAYLOAD_BYTES+12u];payload[THY_SETTINGS_V2_PAYLOAD_BYTES+12u]=1u;
    CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&engine,&before_e,sizeof engine));payload[THY_SETTINGS_V2_PAYLOAD_BYTES+12u]=saved;
    saved=payload[THY_SETTINGS_V3_PAYLOAD_BYTES];payload[THY_SETTINGS_V3_PAYLOAD_BYTES]=THY_PLAY_ENGINE_SEQUENCER;
    CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&engine,&before_e,sizeof engine));payload[THY_SETTINGS_V3_PAYLOAD_BYTES]=saved;
}
static void v9_migration_keeps_cluster_bytes_and_disables_new_expression(void) {
    ++scenarios;defaults();
    const thy_instrument_response left={820u,797u,1353u,4095u,1u},right={1100u,1060u,900u,3000u,2u};
    const thy_instrument_release_policy left_release={23u,1u},right_release={40u,1u};
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,0u,&left,&instrument.cluster_velocity[0],&left_release)==THY_OK);
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,1u,&right,&instrument.cluster_velocity[1],&right_release)==THY_OK);
    instrument.sensitivity_guard[0]=600u;instrument.sensitivity_guard[48]=900u;
    size_t old_bytes=v9_export(roundtrip);
    defaults();thy_engine_bind_expression(&engine,&expression);
    thy_mpe_config changed=engine.expression_config;changed.enabled=1u;changed.glide=1u;
    CHECK(thy_engine_expression_configure(&engine,&changed)==THY_OK);
    CHECK(thy_settings_apply(&engine,roundtrip,old_bytes));
    thy_mpe_config expected;thy_mpe_defaults(&expected);
    CHECK(!memcmp(&engine.expression_config,&expected,sizeof expected));
    CHECK(!memcmp(&expression.config,&expected,sizeof expected) && !core.queue_count);
    CHECK(instrument.cluster_response[0].attack_raw==820u && instrument.cluster_response[1].attack_raw==1100u);
    CHECK(instrument.sensitivity_guard[0]==600u && instrument.sensitivity_guard[48]==900u);
    size_t upgraded=encode(payload);CHECK(upgraded==old_bytes+(THY_SETTINGS_V19_PARAMETER_BYTES-THY_SETTINGS_V9_PARAMETER_BYTES));
    CHECK(!memcmp(payload+7u,roundtrip+7u,THY_SETTINGS_V9_PARAMETER_BYTES-7u));
    CHECK(!memcmp(payload+THY_SETTINGS_V19_PARAMETER_BYTES,roundtrip+THY_SETTINGS_V9_PARAMETER_BYTES,old_bytes-THY_SETTINGS_V9_PARAMETER_BYTES));
    CHECK(thy_mpe_quiet(&expression) && !core.sounding_count && !core.generated_count);
}
static void ordinary_expression_roundtrip_and_atomic_validation(void) {
    ++scenarios;defaults();
    CHECK(THY_SETTINGS_VERSION==19u && THY_SETTINGS_V11_PARAMETER_BYTES==4263u);
    CHECK(THY_SETTINGS_PAYLOAD_MAX==13856u && THY_SETTINGS_RECORD_BYTES==13888u);
    thy_engine_channel_config expected[THY_INSTRUMENT_ROUTE_TARGETS];
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n){
        CHECK(engine.channel_routes[n].aftertouch==0u && engine.channel_routes[n].bend==0u && engine.channel_routes[n].range==2u);
        expected[n]=(thy_engine_channel_config){(uint8_t)(n%4u),(uint8_t)(n%3u),(uint8_t)(24u-n)};
        engine.channel_routes[n]=expected[n];
    }
    const thy_expression_profile cluster[2]={{1u,65u,1u,2u},{1u,80u,2u,1u}};
    for(unsigned n=0;n<2u;++n)instrument.expression_profiles[n]=cluster[n];
    thy_expression_profile pad_profiles[THY_PAD_COUNT];uint8_t overridden[THY_PAD_COUNT];
    for(unsigned n=0;n<THY_PAD_COUNT;++n){
        pad_profiles[n]=(thy_expression_profile){(uint8_t)(n%2u),(uint8_t)(10u+n%81u),(uint8_t)(n%3u),(uint8_t)((n+1u)%3u)};
        overridden[n]=(uint8_t)(n%4u==0u);
        instrument.pad_expression_profiles[n]=pad_profiles[n];
        instrument.expression_profile_overridden[n]=overridden[n];
    }
    thy_seq_event note={0u,240u,0u,0u,2u,THY_SEQ_NOTE,65u,93u};
    CHECK(thy_sequencer_add(&engine.sequencer,2u,&note,1u)==THY_OK);
    size_t bytes=encode(payload);CHECK(bytes==THY_SETTINGS_V19_PARAMETER_BYTES+thy_sequencer_song_size(&engine.sequencer));
    CHECK(thy_settings_payload_version(payload,bytes)==THY_SETTINGS_VERSION && thy_settings_validate(payload,bytes,70u));
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n){
        size_t offset=THY_SETTINGS_V10_PARAMETER_BYTES+3u*n;
        CHECK(payload[offset]==expected[n].aftertouch && payload[offset+1u]==expected[n].bend && payload[offset+2u]==expected[n].range);
    }
    defaults();CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(engine.channel_routes,expected,sizeof expected));
    CHECK(!memcmp(instrument.expression_profiles,cluster,sizeof cluster));
    CHECK(!memcmp(instrument.pad_expression_profiles,pad_profiles,sizeof pad_profiles));
    CHECK(!memcmp(instrument.expression_profile_overridden,overridden,sizeof overridden));
    CHECK(engine.sequencer.count==1u && engine.sequencer.events[0].pitch==65u);
    CHECK(!core.queue_count && !core.sounding_count && !core.generated_count && thy_engine_channel_quiet(&engine));
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    const thy_engine before_e=engine;const thy_instrument before_i=instrument;
    const uint8_t invalid[][2]={{0u,4u},{1u,3u},{2u,0u},{2u,25u}};
    for(unsigned route=0;route<THY_INSTRUMENT_ROUTE_TARGETS;++route)for(unsigned n=0;n<sizeof invalid/sizeof invalid[0];++n){
        size_t offset=THY_SETTINGS_V10_PARAMETER_BYTES+3u*route+invalid[n][0];
        uint8_t saved=payload[offset];payload[offset]=invalid[n][1];
        CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
        CHECK(!memcmp(&engine,&before_e,sizeof engine) && !memcmp(&instrument,&before_i,sizeof instrument));
        payload[offset]=saved;
    }
    const uint8_t bad_profile[][2]={{0u,2u},{1u,9u},{1u,91u},{2u,3u},{3u,3u}};
    const size_t profiles=THY_SETTINGS_V10_PARAMETER_BYTES+THY_INSTRUMENT_ROUTE_TARGETS*3u;
    for(unsigned target=0;target<2u+THY_PAD_COUNT;++target){
        size_t offset=target<2u?profiles+target*4u:profiles+8u+(target-2u)*5u+1u;
        for(unsigned n=0;n<sizeof bad_profile/sizeof bad_profile[0];++n){
            uint8_t saved=payload[offset+bad_profile[n][0]];payload[offset+bad_profile[n][0]]=bad_profile[n][1];
            CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
            CHECK(!memcmp(&engine,&before_e,sizeof engine) && !memcmp(&instrument,&before_i,sizeof instrument));
            payload[offset+bad_profile[n][0]]=saved;
        }
        if(target>=2u){
            size_t flag=offset-1u;uint8_t saved=payload[flag];payload[flag]=2u;
            CHECK(!thy_settings_validate(payload,bytes,70u) && !thy_settings_apply(&engine,payload,bytes));
            CHECK(!memcmp(&engine,&before_e,sizeof engine) && !memcmp(&instrument,&before_i,sizeof instrument));
            payload[flag]=saved;
        }
    }
    /* Current exports refuse invalid runtime policy rather than narrowing it. */
    engine.channel_routes[8].aftertouch=4u;size_t used=99u;
    CHECK(!thy_settings_encode(&engine,roundtrip,sizeof roundtrip,&used) && used==99u);
}
static void v10_migration_preserves_mpe_song_and_legacy_pressure(void) {
    ++scenarios;defaults();thy_engine_bind_expression(&engine,&expression);
    thy_mpe_config mpe=engine.expression_config;mpe.enabled=1u;mpe.upper_zone=1u;mpe.members=7u;mpe.glide=1u;
    CHECK(thy_engine_expression_configure(&engine,&mpe)==THY_OK);
    instrument.pressure_enabled=1u;instrument.route_pressure[1]=1u;instrument.route_pressure[2]=2u;
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n)
        engine.channel_routes[n]=(thy_engine_channel_config){3u,2u,12u};
    thy_seq_event note={480u,240u,0u,0u,3u,THY_SEQ_NOTE,71u,87u};
    CHECK(thy_sequencer_add(&engine.sequencer,3u,&note,1u)==THY_OK);
    size_t bytes=v10_export(roundtrip);
    defaults();thy_engine_bind_expression(&engine,&expression);
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n)
        engine.channel_routes[n]=(thy_engine_channel_config){2u,1u,24u};
    CHECK(thy_settings_apply(&engine,roundtrip,bytes));
    CHECK(!memcmp(&engine.expression_config,&mpe,sizeof mpe) && !memcmp(&expression.config,&mpe,sizeof mpe));
    CHECK(engine.sequencer.count==1u && engine.sequencer.events[0].pitch==71u);
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n)
        CHECK(engine.channel_routes[n].aftertouch==0u && engine.channel_routes[n].bend==0u && engine.channel_routes[n].range==2u);
    const thy_expression_profile default_profile={0u,70u,0u,0u};
    for(unsigned n=0;n<2u;++n)CHECK(!memcmp(&instrument.expression_profiles[n],&default_profile,sizeof default_profile));
    for(unsigned n=0;n<THY_PAD_COUNT;++n){
        CHECK(!instrument.expression_profile_overridden[n]);
        CHECK(!memcmp(&instrument.pad_expression_profiles[n],&default_profile,sizeof default_profile));
    }
    CHECK(thy_engine_channel_aftertouch(&engine,0u)==1u);
    CHECK(thy_engine_channel_aftertouch(&engine,1u)==0u);
    CHECK(thy_engine_channel_aftertouch(&engine,2u)==1u);
    CHECK(!core.queue_count && !core.sounding_count && !core.generated_count);
    size_t upgraded=encode(payload);CHECK(upgraded==bytes+(THY_SETTINGS_V19_PARAMETER_BYTES-THY_SETTINGS_V10_PARAMETER_BYTES) && thy_settings_payload_version(payload,upgraded)==THY_SETTINGS_VERSION);
    CHECK(!memcmp(payload+7u,roundtrip+7u,THY_SETTINGS_V10_PARAMETER_BYTES-7u));
    CHECK(!memcmp(payload+THY_SETTINGS_V19_PARAMETER_BYTES,roundtrip+THY_SETTINGS_V10_PARAMETER_BYTES,bytes-THY_SETTINGS_V10_PARAMETER_BYTES));
}
static void expression_profile_mapping_span_preflight_is_atomic(void) {
    ++scenarios;defaults();
    instrument.expression_profiles[0]=(thy_expression_profile){1u,70u,0u,0u};
    const thy_instrument_response narrow={1377u,1354u,2000u,2001u,0u};
    CHECK(thy_instrument_set_pad_response(&instrument,95u,&narrow)==THY_OK);
    size_t bytes=encode(payload);CHECK(thy_settings_validate(payload,bytes,70u));
    defaults();CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.sensors[95].response_overridden && !instrument.expression_profiles[1].enabled);
    defaults();uint8_t mapping[THY_PAD_COUNT];
    memcpy(mapping,instrument.mapping,sizeof mapping);uint8_t swap=mapping[0];mapping[0]=mapping[95];mapping[95]=swap;
    CHECK(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    const thy_instrument before_i=instrument;const thy_engine before_e=engine;
    CHECK(thy_settings_validate(payload,bytes,70u));
    CHECK(!thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&instrument,&before_i,sizeof instrument) && !memcmp(&engine,&before_e,sizeof engine));
    CHECK(!core.queue_count && !core.sounding_count && !core.generated_count);
}
static thy_library expected_library;
static thy_instrument before_library_instrument;
static thy_engine before_library_engine;
static void seed_library(void) {
    thy_library_defaults(&instrument.library);
    thy_library_scale scale={0};scale.used=1u;
    CHECK(thy_library_name_set(scale.name,"Minor pent")==THY_LIBRARY_OK);
    CHECK(thy_library_scale_set_mask(&scale,(1u<<0)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<10))==THY_LIBRARY_OK);
    CHECK(thy_library_set_scale(&instrument.library,0u,&scale)==THY_LIBRARY_OK);
    CHECK(thy_library_name_set(scale.name,"All twelve")==THY_LIBRARY_OK);
    CHECK(thy_library_scale_set_mask(&scale,4095u)==THY_LIBRARY_OK);
    CHECK(thy_library_set_scale(&instrument.library,7u,&scale)==THY_LIBRARY_OK);
    thy_library_chord chord={0};chord.used=1u;chord.valid_mask=5u;
    CHECK(thy_library_name_set(chord.name,"Zero and top")==THY_LIBRARY_OK);
    const uint8_t notes[7]={0u,12u,127u,99u,35u,71u,64u};
    memcpy(chord.notes,notes,sizeof notes);
    CHECK(thy_library_set_chord(&instrument.library,0u,&chord)==THY_LIBRARY_OK);
    CHECK(thy_library_name_set(chord.name,"Seven tones")==THY_LIBRARY_OK);
    chord.root=60u;chord.valid_mask=127u;
    for(unsigned n=0;n<7u;++n)chord.notes[n]=(uint8_t)(60u+n*3u);
    CHECK(thy_library_set_chord(&instrument.library,23u,&chord)==THY_LIBRARY_OK);
    thy_library_six set={0};set.used=1u;
    CHECK(thy_library_name_set(set.name,"User six")==THY_LIBRARY_OK);
    const uint8_t refs[6]={0u,23u,255u,0u,23u,255u};memcpy(set.chords,refs,sizeof refs);
    CHECK(thy_library_set_six(&instrument.library,0u,&set)==THY_LIBRARY_OK);
    CHECK(thy_library_name_set(set.name,"Empty set")==THY_LIBRARY_OK);
    memset(set.chords,255u,sizeof set.chords);
    CHECK(thy_library_set_six(&instrument.library,2u,&set)==THY_LIBRARY_OK);
}
static void catalog_for_library(void) {
    static const thy_instrument_pad empty[96]={0};
    const int16_t degrees[7]={0,2,4,5,7,9,11};
    CHECK(thy_instrument_set_layout(&instrument,empty,60,degrees,7u)==THY_OK);
}
static void library_restore_rejected(size_t bytes) {
    CHECK(!thy_settings_validate(payload,bytes,70u));
    CHECK(!thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&instrument,&before_library_instrument,sizeof instrument));
    CHECK(!memcmp(&engine,&before_library_engine,sizeof engine));
    CHECK(!core.queue_count && !core.sounding_count && !core.generated_count);
}
static void chord_library_roundtrip_and_transient_state(void) {
    ++scenarios;defaults();catalog_for_library();seed_library();
    CHECK(sizeof(thy_library)==740u && THY_SETTINGS_V11_PARAMETER_BYTES==4263u);
    CHECK(THY_SETTINGS_V12_LIBRARY_OFFSET==4273u && THY_SETTINGS_V12_PARAMETER_BYTES==5013u);
    CHECK(THY_SETTINGS_PAYLOAD_MAX==13856u && THY_SETTINGS_RECORD_BYTES==13888u);
    instrument.keep_transpose=0u;instrument.live_update=1u;instrument.live_lock_scale=0u;
    instrument.live_mute_chord=1u;instrument.chord_layout=THY_CHORD_LAYOUT_SIX_USER;
    instrument.chord_quality=6u;instrument.six_bank=2u;instrument.voicing_mode=1u;
    instrument.drop_mask=5u;instrument.user_scale=8u;
    instrument.scale_shortcuts[0]=71u;instrument.scale_shortcuts[11]=78u;
    instrument.transpose[0]=5;instrument.transpose[1]=-7;
    CHECK(thy_instrument_apply_scale(&instrument)==THY_OK);
    expected_library=instrument.library;
    thy_seq_event note={240u,240u,0u,0u,1u,THY_SEQ_NOTE,67u,99u};
    CHECK(thy_sequencer_add(&engine.sequencer,1u,&note,1u)==THY_OK);
    const uint8_t policy[10]={0u,1u,0u,1u,4u,6u,2u,1u,5u,8u};
    size_t bytes=encode(payload);
    CHECK(bytes==THY_SETTINGS_V19_PARAMETER_BYTES+thy_sequencer_song_size(&engine.sequencer));
    CHECK(thy_settings_payload_version(payload,bytes)==THY_SETTINGS_VERSION);
    CHECK(!memcmp(payload+THY_SETTINGS_V11_PARAMETER_BYTES,policy,sizeof policy));
    CHECK(!memcmp(payload+THY_SETTINGS_V12_LIBRARY_OFFSET,&expected_library,sizeof expected_library));
    instrument.live_degree_count=3u;instrument.live_anchor=62;
    instrument.live_degrees[0]=0;instrument.live_degrees[1]=3;instrument.live_degrees[2]=7;
    instrument.library_editor.open=1u;instrument.library_editor.kind=THY_LIBRARY_UI_CHORD;
    instrument.library_editor.draft.chord=instrument.library.chords[0];
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    defaults();catalog_for_library();instrument.library_editor.open=1u;
    instrument.live_degree_count=4u;instrument.live_anchor=88;
    CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&instrument.library,&expected_library,sizeof expected_library));
    CHECK(!instrument.keep_transpose && instrument.live_update && !instrument.live_lock_scale && instrument.live_mute_chord);
    CHECK(instrument.chord_layout==THY_CHORD_LAYOUT_SIX_USER && instrument.chord_quality==6u && instrument.six_bank==2u);
    CHECK(instrument.voicing_mode==1u && instrument.drop_mask==5u && instrument.user_scale==8u);
    CHECK(instrument.scale_shortcuts[0]==71u && instrument.scale_shortcuts[11]==78u);
    CHECK(instrument.transpose[0]==5 && instrument.transpose[1]==-7);
    CHECK(instrument.degree_count==12u && instrument.catalog_degree_count==7u);
    for(unsigned n=0;n<12u;++n)CHECK(instrument.degrees[n]==(int16_t)n);
    for(unsigned n=0;n<2u;++n)CHECK(engine.arps[n].config.scale_count==12u && engine.arps[n].config.scale[11]==11u);
    CHECK(!instrument.library_editor.open && !instrument.live_degree_count && !instrument.live_anchor);
    CHECK(engine.sequencer.count==1u && engine.sequencer.events[0].pitch==67u);
    CHECK(!core.queue_count && !core.sounding_count && !core.generated_count);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
}
static void chord_library_invalid_fields_reject_before_mutation(void) {
    ++scenarios;defaults();seed_library();
    instrument.user_scale=8u;instrument.chord_layout=THY_CHORD_LAYOUT_SIX_USER;instrument.six_bank=2u;
    instrument.scale_shortcuts[0]=71u;instrument.scale_shortcuts[11]=78u;
    size_t bytes=encode(payload);
    before_library_instrument=instrument;before_library_engine=engine;
    const uint8_t invalid[10]={2u,2u,2u,2u,6u,7u,3u,5u,8u,9u};
    for(unsigned n=0;n<10u;++n){size_t at=THY_SETTINGS_V11_PARAMETER_BYTES+n;uint8_t saved=payload[at];
        payload[at]=invalid[n];library_restore_rejected(bytes);payload[at]=saved;}
    const size_t library=THY_SETTINGS_V12_LIBRARY_OFFSET;
    for(unsigned n=0;n<THY_LIBRARY_SCALES;++n){size_t at=library+n*16u+15u;uint8_t saved=payload[at];
        payload[at]=2u;library_restore_rejected(bytes);payload[at]=saved;}
    for(unsigned n=0;n<THY_LIBRARY_CHORDS;++n){size_t at=library+128u+n*23u+22u;uint8_t saved=payload[at];
        payload[at]=2u;library_restore_rejected(bytes);payload[at]=saved;}
    for(unsigned n=0;n<THY_LIBRARY_SIX_SETS;++n){size_t at=library+680u+n*20u+19u;uint8_t saved=payload[at];
        payload[at]=2u;library_restore_rejected(bytes);payload[at]=saved;}
    const size_t malformed[]={library+13u,library+14u,library+16u,
        library+128u+13u,library+128u+21u,library+128u+7u,
        library+680u+13u,library+680u+14u};
    const uint8_t malformed_value[]={0u,128u,1u,128u,0u,1u,24u,1u};
    for(unsigned n=0;n<sizeof malformed/sizeof malformed[0];++n){size_t at=malformed[n];uint8_t saved=payload[at];
        payload[at]=malformed_value[n];library_restore_rejected(bytes);payload[at]=saved;}
    for(unsigned n=0;n<7u;++n){size_t at=library+128u+14u+n;uint8_t saved=payload[at];
        payload[at]=128u;library_restore_rejected(bytes);payload[at]=saved;}
    const size_t refs[]={library,library+7u*16u,library+680u+2u*20u};
    const size_t sizes[]={16u,16u,20u};
    for(unsigned n=0;n<3u;++n){uint8_t saved[23];memcpy(saved,payload+refs[n],sizes[n]);
        memset(payload+refs[n],0u,sizes[n]);library_restore_rejected(bytes);memcpy(payload+refs[n],saved,sizes[n]);}
    /* A valid-range reference still cannot name an unused scale. */
    size_t shortcut=THY_SETTINGS_V3_PAYLOAD_BYTES+1u;uint8_t saved=payload[shortcut];
    payload[shortcut]=72u;library_restore_rejected(bytes);payload[shortcut]=saved;
    payload[shortcut]=79u;library_restore_rejected(bytes);payload[shortcut]=saved;
    CHECK(thy_settings_validate(payload,bytes,70u));
    instrument.library.chords[23].notes[6]=128u;memset(roundtrip,90u,sizeof roundtrip);size_t used=99u;
    CHECK(!thy_settings_encode(&engine,roundtrip,sizeof roundtrip,&used) && used==99u);
    for(unsigned n=0;n<sizeof roundtrip;++n)CHECK(roundtrip[n]==90u);
}
static void legacy_migration_clears_chord_library_and_new_policies(void) {
    ++scenarios;
    for(unsigned version=1u;version<=11u;++version){
        defaults();instrument.transpose[0]=-3;instrument.transpose[1]=5;
        old_record(version);size_t bytes=get32(flash+12u);
        seed_library();instrument.keep_transpose=0u;instrument.live_update=1u;instrument.live_lock_scale=0u;
        instrument.live_mute_chord=1u;instrument.chord_layout=THY_CHORD_LAYOUT_USER;instrument.chord_quality=6u;
        instrument.six_bank=2u;instrument.voicing_mode=1u;instrument.drop_mask=7u;instrument.user_scale=8u;
        instrument.library_editor.open=1u;instrument.live_degree_count=3u;instrument.live_anchor=61;
        CHECK(thy_settings_apply(&engine,roundtrip,bytes));
        CHECK(instrument.keep_transpose==1u && !instrument.live_update && instrument.live_lock_scale==1u && !instrument.live_mute_chord);
        CHECK(!instrument.chord_layout && !instrument.chord_quality && !instrument.six_bank && !instrument.voicing_mode && !instrument.drop_mask && !instrument.user_scale);
        const uint8_t *empty=(const uint8_t *)&instrument.library;
        for(unsigned n=0;n<sizeof instrument.library;++n)CHECK(!empty[n]);
        CHECK(!instrument.library_editor.open && !instrument.live_degree_count && !instrument.live_anchor);
        CHECK(instrument.transpose[0]==-3 && instrument.transpose[1]==5);
        size_t upgraded=encode(payload);CHECK(thy_settings_payload_version(payload,upgraded)==THY_SETTINGS_VERSION);
        if(version==11u){
            CHECK(upgraded==bytes+(THY_SETTINGS_V19_PARAMETER_BYTES-THY_SETTINGS_V11_PARAMETER_BYTES));
            CHECK(!memcmp(payload+7u,roundtrip+7u,THY_SETTINGS_V11_PARAMETER_BYTES-7u));
            CHECK(!memcmp(payload+THY_SETTINGS_V19_PARAMETER_BYTES,roundtrip+THY_SETTINGS_V11_PARAMETER_BYTES,bytes-THY_SETTINGS_V11_PARAMETER_BYTES));
            size_t at=THY_SETTINGS_V3_PAYLOAD_BYTES+1u;roundtrip[at]=71u;
            CHECK(!thy_settings_validate(roundtrip,bytes,70u));
        }
    }
}
static void empty_user_grid_and_library_preflight_failures(void) {
    ++scenarios;defaults();instrument.chord_layout=THY_CHORD_LAYOUT_USER;
    size_t bytes=encode(payload);CHECK(thy_settings_validate(payload,bytes,70u));
    defaults();CHECK(thy_settings_apply(&engine,payload,bytes));CHECK(instrument.chord_layout==THY_CHORD_LAYOUT_USER);
    before_library_instrument=instrument;before_library_engine=engine;
    instrument.mapping[1]=0u;before_library_instrument=instrument;
    CHECK(!thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&instrument,&before_library_instrument,sizeof instrument) && !memcmp(&engine,&before_library_engine,sizeof engine));
    defaults();instrument.catalog_degree_count=13u;before_library_instrument=instrument;before_library_engine=engine;
    CHECK(!thy_settings_apply(&engine,payload,bytes));
    CHECK(!memcmp(&instrument,&before_library_instrument,sizeof instrument) && !memcmp(&engine,&before_library_engine,sizeof engine));
    defaults();instrument.chord_layout=THY_CHORD_LAYOUT_SIX_USER;size_t used=99u;
    CHECK(!thy_settings_encode(&engine,roundtrip,sizeof roundtrip,&used) && used==99u);
    thy_library_six empty={0};empty.used=1u;memset(empty.chords,255u,sizeof empty.chords);
    CHECK(thy_library_name_set(empty.name,"Empty six")==THY_LIBRARY_OK);
    CHECK(thy_library_set_six(&instrument.library,2u,&empty)==THY_LIBRARY_OK);instrument.six_bank=2u;
    bytes=encode(payload);CHECK(thy_settings_validate(payload,bytes,70u));
    defaults();CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.chord_layout==THY_CHORD_LAYOUT_SIX_USER && instrument.six_bank==2u);
    for(unsigned n=0;n<THY_LIBRARY_CHORDS;++n)CHECK(!instrument.library.chords[n].used);
}
static void library_journal_restore_and_crc_coverage(void) {
    ++scenarios;defaults();seed_library();instrument.user_scale=1u;
    instrument.scale_shortcuts[11]=71u;instrument.chord_layout=THY_CHORD_LAYOUT_SIX_USER;
    CHECK(thy_instrument_apply_scale(&instrument)==THY_OK);expected_library=instrument.library;
    memset(flash,255u,sizeof flash);thy_settings_io backend=io();
    CHECK(thy_settings_journal_init(&journal,&backend,70u));begin_save(0u);finish_save(1600u);
    CHECK(journal.loaded_version==THY_SETTINGS_VERSION && journal.generation==1u);
    instrument.library.chords[0].notes[0]=1u;
    begin_save(10000u);finish_save(11600u);CHECK(journal.generation==2u);
    /* Keep the musical record structurally valid while changing a stored
     * note: the settings record CRC must cover the entire streamed library. */
    size_t at=journal.newest_slot*THY_SETTINGS_SLOT_BYTES+THY_SETTINGS_HEADER_BYTES+
        THY_SETTINGS_V12_LIBRARY_OFFSET+128u+14u;
    CHECK(flash[at]==1u);flash[at]^=2u;
    const thy_library current=instrument.library;
    CHECK(thy_settings_journal_init(&rebooted,&backend,70u));
    CHECK(rebooted.loaded && rebooted.loaded_version==THY_SETTINGS_VERSION && rebooted.generation==1u && rebooted.recovered && rebooted.rejected);
    CHECK(!memcmp(&instrument.library,&current,sizeof current));
    defaults();CHECK(thy_settings_apply(&engine,rebooted.record+32u,rebooted.payload_bytes));
    CHECK(!memcmp(&instrument.library,&expected_library,sizeof expected_library));
    CHECK(instrument.user_scale==1u && instrument.degree_count==5u && instrument.scale_shortcuts[11]==71u);
    CHECK(instrument.chord_layout==THY_CHORD_LAYOUT_SIX_USER && !core.queue_count);
}
static void v12_migration_preserves_preferences_library_and_song_without_new_defaults(void) {
    ++scenarios;defaults();seed_library();
    CHECK(THY_SETTINGS_VERSION==19u && THY_SETTINGS_V13_PARAMETER_BYTES==THY_SETTINGS_V12_PARAMETER_BYTES);
    CHECK(THY_VIEW_INFO_MUSICAL_NAMES==12u && THY_VIEW_INFO_COUNT==13u);
    CHECK(instrument.display_view==THY_VIEW_SIMPLE && instrument.play_nav[3]==THY_PLAY_CHORDS);
    for(unsigned n=0;n<THY_VIEW_COUNT;++n) {
        CHECK(instrument.display_config[n].slots[0]==THY_VIEW_INFO_MUSICAL_NAMES);
        CHECK(instrument.display_config[n].detail_rows[0]==THY_VIEW_INFO_MUSICAL_NAMES);
        thy_view_config *v=&instrument.display_config[n];
        v->slots[0]=(uint8_t)(n%12u);v->slots[1]=(uint8_t)((n+3u)%12u);
        v->header=(uint8_t)(n&1u);v->motion=(uint8_t)((n+1u)&1u);
        for(unsigned row=0;row<THY_VIEW_DETAIL_ROWS;++row)v->detail_rows[row]=(uint8_t)((n+2u*row)%12u);
    }
    instrument.display_view=THY_VIEW_PIANO;instrument.play_nav[3]=THY_PLAY_MIDI;
    instrument.user_scale=1u;instrument.scale_shortcuts[0]=71u;
    thy_seq_event note={120u,360u,0u,0u,2u,THY_SEQ_NOTE,66u,101u};
    CHECK(thy_sequencer_add(&engine.sequencer,2u,&note,1u)==THY_OK);
    const thy_library saved_library=instrument.library;
    thy_view_config saved_views[THY_VIEW_COUNT];memcpy(saved_views,instrument.display_config,sizeof saved_views);
    size_t bytes=v12_export(roundtrip);
    defaults();
    const uint8_t performance=thy_menu_rows[THY_ROW_PERFORMANCE_VELOCITY].group;
    const uint8_t chords=thy_menu_rows[THY_ROW_CHORDS_LAYOUT].group;
    const uint8_t arps=thy_menu_rows[THY_ROW_ARPS_ENABLED].group;
    engine.state.cursors[performance]=3u;engine.state.cursors[chords]=7u;engine.state.cursors[arps]=2u;
    CHECK(thy_settings_apply(&engine,roundtrip,bytes));
    CHECK(instrument.display_view==THY_VIEW_PIANO && instrument.play_nav[3]==THY_PLAY_MIDI);
    CHECK(!memcmp(instrument.display_config,saved_views,sizeof saved_views));
    CHECK(!memcmp(&instrument.library,&saved_library,sizeof saved_library));
    CHECK(instrument.user_scale==1u && instrument.scale_shortcuts[0]==71u);
    CHECK(engine.sequencer.count==1u && engine.sequencer.events[0].pitch==66u);
    CHECK(engine.sequencer.events[0].duration==360u && engine.sequencer.events[0].track==2u);
    CHECK(!engine.state.cursors[performance] && !engine.state.cursors[chords] && engine.state.cursors[arps]==2u);
    CHECK(!core.queue_count && !core.sounding_count && !core.generated_count);
    CHECK(encode(payload)==bytes+(THY_SETTINGS_V19_PARAMETER_BYTES-THY_SETTINGS_V12_PARAMETER_BYTES) && payload[6]==THY_SETTINGS_VERSION && roundtrip[6]==12u);
    CHECK(!memcmp(payload,roundtrip,6u) && !memcmp(payload+7u,roundtrip+7u,THY_SETTINGS_V12_PARAMETER_BYTES-7u));
    CHECK(!memcmp(payload+THY_SETTINGS_V19_PARAMETER_BYTES,roundtrip+THY_SETTINGS_V12_PARAMETER_BYTES,bytes-THY_SETTINGS_V12_PARAMETER_BYTES));
}
static void legacy_view_info_ranges_reject_new_identity_atomically(void) {
    ++scenarios;
    const size_t views=THY_SETTINGS_V3_PAYLOAD_BYTES+1u+THY_SCALE_SHORTCUT_COUNT;
    for(unsigned version=4u;version<=12u;++version) {
        defaults();old_record(version);const size_t bytes=get32(flash+12u);
        before_library_instrument=instrument;before_library_engine=engine;
        const unsigned count=version==4u?8u:THY_VIEW_COUNT;
        for(unsigned view=0;view<count;++view)for(unsigned field=0;field<10u;++field) {
            if(field==2u || field==3u)continue;
            size_t at=views+view*10u+field;uint8_t saved=roundtrip[at];
            roundtrip[at]=THY_VIEW_INFO_MUSICAL_NAMES;
            CHECK(!thy_settings_validate(roundtrip,bytes,70u));
            CHECK(!thy_settings_apply(&engine,roundtrip,bytes));
            CHECK(!memcmp(&instrument,&before_library_instrument,sizeof instrument));
            CHECK(!memcmp(&engine,&before_library_engine,sizeof engine));
            roundtrip[at]=saved;
        }
        CHECK(thy_settings_validate(roundtrip,bytes,70u));
    }
}
static void automatic_voicing_codec(void) {
    ++scenarios;defaults();instrument.voicing_mode=2u;
    instrument.voicing_previous_mask=7u;instrument.voicing_previous[0]=60;
    size_t bytes=encode(payload);
    defaults();CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.voicing_mode==2u && !instrument.voicing_previous_mask);
    bytes=without_v16_preferences(payload,bytes);payload[6]=13u;
    CHECK(!thy_settings_validate(payload,bytes,70u));
    defaults();bytes=without_v16_preferences(payload,encode(payload));payload[6]=13u;
    CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(!instrument.voicing_mode && !instrument.voicing_previous_mask);
}
static void register_voicing_codec(void) {
    ++scenarios;
    for(unsigned mode=0;mode<=4u;++mode) {
        defaults();instrument.voicing_mode=(uint8_t)mode;
        size_t bytes=encode(payload);CHECK(payload[6]==THY_SETTINGS_VERSION);
        defaults();CHECK(thy_settings_apply(&engine,payload,bytes));
        CHECK(instrument.voicing_mode==mode && !instrument.voicing_previous_mask);
        bytes=without_v16_preferences(payload,bytes);payload[6]=14u;
        CHECK(!!thy_settings_validate(payload,bytes,70u)==(mode<=2u));
        if(mode<=2u){defaults();CHECK(thy_settings_apply(&engine,payload,bytes));CHECK(instrument.voicing_mode==mode);}
    }
}
static void harmony_swap_v16_codec_and_v15_migration(void) {
    ++scenarios;defaults();seed_library();
    CHECK(THY_SETTINGS_V15_PARAMETER_BYTES==5013u && THY_SETTINGS_V16_PARAMETER_BYTES==5025u && THY_SETTINGS_V17_PARAMETER_BYTES==5028u && THY_SETTINGS_V18_PARAMETER_BYTES==5032u && THY_SETTINGS_V19_PARAMETER_BYTES==5042u);
    engine.harmony.pattern=3u;engine.harmony.highlights=0u;engine.harmony.motion=0u;
    engine.harmony.view=3u;engine.harmony.control_bank=1u;engine.harmony.octave_target=0u;
    engine.harmony.right_mode=1u;engine.harmony.analysis=2u;engine.harmony.bass_root=1u;
    engine.harmony.low_note=40u;engine.harmony.high_note=95u;instrument.cluster_swap=1u;
    const uint8_t expected[12]={3u,0u,0u,3u,1u,0u,1u,2u,1u,40u,95u,1u};
    const thy_instrument_response response={900u,870u,400u,3200u,1u};
    CHECK(thy_instrument_set_pad_response(&instrument,48u,&response)==THY_OK);
    thy_seq_event note={120u,240u,0u,0u,2u,THY_SEQ_NOTE,65u,99u};
    CHECK(thy_sequencer_add(&engine.sequencer,2u,&note,1u)==THY_OK);
    const thy_library saved_library=instrument.library;
    size_t bytes=encode(payload);
    CHECK(thy_settings_payload_version(payload,bytes)==THY_SETTINGS_VERSION);
    CHECK(!memcmp(payload+THY_SETTINGS_V15_PARAMETER_BYTES,expected,sizeof expected));
    /* Captures, guidance caches and held-input analysis are runtime only. */
    engine.harmony.input_mask=145u;engine.harmony.base_mask=145u;
    engine.harmony.right_held[7]=4u;engine.harmony.pinned=1u;engine.harmony.preview_scale=1u;
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    defaults();CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(instrument.cluster_swap==1u && instrument.mapping[48]==48u);
    CHECK(instrument.sensors[48].response_overridden && instrument.sensors[48].response.attack_raw==900u);
    CHECK(!instrument.sensors[0].response_overridden);
    CHECK(engine.harmony.pattern==3u && !engine.harmony.highlights && !engine.harmony.motion);
    CHECK(engine.harmony.view==3u && engine.harmony.control_bank==1u && !engine.harmony.octave_target);
    CHECK(engine.harmony.right_mode==1u && engine.harmony.analysis==2u && engine.harmony.bass_root==1u);
    CHECK(engine.harmony.low_note==40u && engine.harmony.high_note==95u);
    CHECK(!engine.harmony.input_mask && !engine.harmony.base_mask && !engine.harmony.right_held[7]);
    CHECK(!engine.harmony.pinned && engine.harmony.preview_scale==UINT8_MAX);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    before_library_instrument=instrument;before_library_engine=engine;
    const uint8_t invalid[12]={4u,2u,2u,4u,2u,2u,2u,3u,2u,128u,128u,2u};
    for(unsigned n=0u;n<sizeof invalid;++n){
        size_t at=THY_SETTINGS_V15_PARAMETER_BYTES+n;uint8_t saved=payload[at];
        payload[at]=invalid[n];library_restore_rejected(bytes);payload[at]=saved;
    }
    /* Each endpoint is in MIDI range, but the combined range is impossible. */
    payload[THY_SETTINGS_V15_PARAMETER_BYTES+9u]=96u;library_restore_rejected(bytes);
    payload[THY_SETTINGS_V15_PARAMETER_BYTES+9u]=40u;
    engine.harmony.low_note=96u;size_t used=99u;memset(roundtrip,90u,sizeof roundtrip);
    CHECK(!thy_settings_encode(&engine,roundtrip,sizeof roundtrip,&used) && used==99u);
    for(unsigned n=0u;n<sizeof roundtrip;++n)CHECK(roundtrip[n]==90u);
    engine.harmony.low_note=40u;
    memcpy(roundtrip,payload,bytes);
    size_t legacy_bytes=without_v16_preferences(roundtrip,bytes);roundtrip[6]=15u;
    CHECK(thy_settings_payload_version(roundtrip,legacy_bytes)==15u);
    CHECK(thy_settings_validate(roundtrip,legacy_bytes,70u));
    /* Import over non-default new preferences: absent V15 fields get defaults. */
    CHECK(thy_settings_apply(&engine,roundtrip,legacy_bytes));
    CHECK(!instrument.cluster_swap && engine.harmony.pattern==1u && engine.harmony.highlights==1u && engine.harmony.motion==1u);
    CHECK(engine.harmony.view==1u && !engine.harmony.control_bank && engine.harmony.octave_target==1u);
    CHECK(!engine.harmony.right_mode && !engine.harmony.analysis && !engine.harmony.bass_root);
    CHECK(!engine.harmony.low_note && engine.harmony.high_note==127u);
    CHECK(!memcmp(&instrument.library,&saved_library,sizeof saved_library));
    CHECK(instrument.sensors[48].response_overridden && instrument.sensors[48].response.attack_raw==900u);
    CHECK(engine.sequencer.count==1u && engine.sequencer.events[0].pitch==65u && engine.sequencer.events[0].track==2u);
    CHECK(encode(payload)==bytes && payload[6]==THY_SETTINGS_VERSION);
    CHECK(!memcmp(payload,roundtrip,6u) && !memcmp(payload+7u,roundtrip+7u,THY_SETTINGS_V15_PARAMETER_BYTES-7u));
    CHECK(!memcmp(payload+THY_SETTINGS_V19_PARAMETER_BYTES,roundtrip+THY_SETTINGS_V15_PARAMETER_BYTES,legacy_bytes-THY_SETTINGS_V15_PARAMETER_BYTES));
    /* V16 retains its chosen view and song, adding only V17 preference defaults. */
    engine.harmony.view=3u;engine.harmony.direction=2u;engine.harmony.pad_map=1u;engine.harmony.hint_contrast=72u;
    bytes=encode(payload);CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(engine.harmony.direction==2u&&engine.harmony.pad_map==1u&&engine.harmony.hint_contrast==72u);
    for(unsigned n=0;n<3u;++n){uint8_t old=payload[THY_SETTINGS_V16_PARAMETER_BYTES+n];
        payload[THY_SETTINGS_V16_PARAMETER_BYTES+n]=(uint8_t[]){4u,2u,101u}[n];
        CHECK(!thy_settings_validate(payload,bytes,70u));payload[THY_SETTINGS_V16_PARAMETER_BYTES+n]=old;}
    memmove(payload+THY_SETTINGS_V16_PARAMETER_BYTES,payload+THY_SETTINGS_V19_PARAMETER_BYTES,bytes-THY_SETTINGS_V19_PARAMETER_BYTES);
    bytes-=17u;payload[6]=16u;CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(engine.harmony.view==1u&&!engine.harmony.direction&&!engine.harmony.pad_map&&engine.harmony.hint_contrast==65u);
    CHECK(engine.sequencer.count==1u&&engine.sequencer.events[0].pitch==65u);
}
static void harmony_v18_preferences_and_v17_migration(void) {
    ++scenarios;defaults();engine.harmony.outside=82u;engine.harmony.stay_in_key=1u;
    engine.harmony.lock_map=1u;engine.harmony.palette=4u;engine.harmony.direction=3u;
    size_t bytes=encode(payload);defaults();CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(engine.harmony.outside==82u&&engine.harmony.stay_in_key&&engine.harmony.lock_map&&engine.harmony.palette==4u);
    CHECK(engine.harmony.direction==3u);
    for(unsigned n=0;n<4u;++n){size_t at=THY_SETTINGS_V17_PARAMETER_BYTES+n;uint8_t old=payload[at];
        payload[at]=(uint8_t[]){101u,2u,2u,5u}[n];CHECK(!thy_settings_validate(payload,bytes,70u));payload[at]=old;}
    memmove(payload+THY_SETTINGS_V17_PARAMETER_BYTES,payload+THY_SETTINGS_V19_PARAMETER_BYTES,bytes-THY_SETTINGS_V19_PARAMETER_BYTES);
    bytes-=14u;payload[6]=17u;
    CHECK(!thy_settings_validate(payload,bytes,70u)); /* v17 rejects direction 3 before migration */
    payload[THY_SETTINGS_V16_PARAMETER_BYTES]=2u;CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(engine.harmony.direction==3u&&engine.harmony.outside==35u&&!engine.harmony.stay_in_key&&!engine.harmony.lock_map&&!engine.harmony.palette);
}
static void playing_v19_roundtrip_validation_and_v18_inheritance(void) {
    ++scenarios;defaults();seed_library();
    CHECK(THY_SETTINGS_VERSION==19u && THY_SETTINGS_V18_PARAMETER_BYTES==5032u && THY_SETTINGS_V19_PARAMETER_BYTES==5042u);
    const thy_seq_event note={120u,240u,0u,0u,2u,THY_SEQ_NOTE,65u,99u};
    CHECK(thy_sequencer_add(&engine.sequencer,2u,&note,1u)==THY_OK);
    const thy_library saved_library=instrument.library;
    size_t bytes=encode(roundtrip);
    engine.performance[0]=(thy_engine_performance_config){127u,2u,2u,24u};
    engine.performance[1]=(thy_engine_performance_config){64u,3u,3u,12u};
    engine.expression_config.pressure_output[0]=THY_MPE_CC74;
    engine.expression_config.pressure_output[1]=THY_MPE_CC1;
    CHECK(encode(payload)==bytes && thy_settings_validate(payload,bytes,70u));
    CHECK(!memcmp(payload,roundtrip,THY_SETTINGS_V18_PARAMETER_BYTES));
    const uint8_t expected[10]={127u,2u,2u,24u,64u,3u,3u,12u,THY_MPE_CC74,THY_MPE_CC1};
    CHECK(!memcmp(payload+THY_SETTINGS_V18_PARAMETER_BYTES,expected,sizeof expected));
    defaults();CHECK(thy_settings_apply(&engine,payload,bytes));
    CHECK(engine.performance[0].velocity==127u && engine.performance[0].aftertouch==2u &&
        engine.performance[0].bend==2u && engine.performance[0].range==24u);
    CHECK(engine.performance[1].velocity==64u && engine.performance[1].aftertouch==3u &&
        engine.performance[1].bend==3u && engine.performance[1].range==12u);
    CHECK(engine.expression_config.pressure_output[0]==THY_MPE_CC74 && engine.expression_config.pressure_output[1]==THY_MPE_CC1);
    CHECK(!memcmp(&instrument.library,&saved_library,sizeof saved_library));
    CHECK(engine.sequencer.count==1u && engine.sequencer.events[0].pitch==65u);
    CHECK(encode(roundtrip)==bytes && !memcmp(payload,roundtrip,bytes));
    CHECK(!core.queue_count && !core.sounding_count);
    before_library_instrument=instrument;before_library_engine=engine;
    const uint8_t invalid[10]={128u,4u,4u,25u,128u,4u,4u,25u,3u,3u};
    for(unsigned n=0;n<sizeof invalid;++n){
        size_t at=THY_SETTINGS_V18_PARAMETER_BYTES+n;uint8_t saved=payload[at];
        payload[at]=invalid[n];library_restore_rejected(bytes);payload[at]=saved;
    }
    /* A legacy import must replace the new overrides with inheritance, while
     * retaining routed behavior, the complete V18 projection and its song. */
    memcpy(roundtrip,payload,bytes);
    memmove(roundtrip+THY_SETTINGS_V18_PARAMETER_BYTES,roundtrip+THY_SETTINGS_V19_PARAMETER_BYTES,
        bytes-THY_SETTINGS_V19_PARAMETER_BYTES);
    const size_t legacy_bytes=bytes-10u;roundtrip[6]=18u;
    CHECK(thy_settings_payload_version(roundtrip,legacy_bytes)==18u && thy_settings_validate(roundtrip,legacy_bytes,70u));
    CHECK(thy_settings_apply(&engine,roundtrip,legacy_bytes));
    for(unsigned side=0;side<2u;++side){
        const thy_engine_performance_config *p=&engine.performance[side];
        CHECK(!p->velocity && !p->aftertouch && !p->bend && !p->range);
        CHECK(engine.expression_config.pressure_output[side]==THY_MPE_PRESSURE);
    }
    CHECK(!memcmp(&instrument.library,&saved_library,sizeof saved_library));
    CHECK(engine.sequencer.count==1u && engine.sequencer.events[0].pitch==65u);
    CHECK(encode(payload)==bytes && payload[6]==19u);
    CHECK(!memcmp(payload,roundtrip,6u) && !memcmp(payload+7u,roundtrip+7u,THY_SETTINGS_V18_PARAMETER_BYTES-7u));
    CHECK(!memcmp(payload+THY_SETTINGS_V19_PARAMETER_BYTES,roundtrip+THY_SETTINGS_V18_PARAMETER_BYTES,legacy_bytes-THY_SETTINGS_V18_PARAMETER_BYTES));
    for(unsigned n=0;n<10u;++n)CHECK(!payload[THY_SETTINGS_V18_PARAMETER_BYTES+n]);
}
int main(void) {
    playing_v19_roundtrip_validation_and_v18_inheritance();
    harmony_v18_preferences_and_v17_migration();harmony_swap_v16_codec_and_v15_migration();
    register_voicing_codec();automatic_voicing_codec();
    codec(); engine_views_and_scale_shortcuts_roundtrip(); view_fields_validation_and_legacy_migration();
    frozen_v4_migration_preserves_layout_and_views();
    lower_codec_and_legacy_import(); debounce_and_boot(); every_power_cut(); rotation_and_corruption(); erased_inactive_power_loss();
    legacy_journal_upgrade(); every_legacy_upgrade_power_cut();
    arp_clock_roundtrip_and_fresh_runtime(); arp_field_validation_and_atomicity(); v2_journal_upgrade_and_defaults(); v3_journal_upgrade_preserves_arps();
    oversized_future_record_preservation();
    sequencer_song_preferences_and_fresh_runtime(); v6_preference_validation_is_atomic();
    v5_left_shortcut_migration(); full_song_journal_word_and_boundary_power_cuts();
    velocity_roundtrip_overrides_and_clean_strike_runtime();velocity_invalid_fields_reject_atomically();
    v6_song_migration_and_versioned_song_recall();
    release_policy_roundtrip_validation_and_independent_pressure();v7_frozen_response_rules_and_lossless_linked_migration();
    cluster_defaults_roundtrip_and_mapping_safe_inheritance();v8_migration_preserves_raw_values_in_both_cluster_defaults();
    mpe_preferences_roundtrip_and_atomic_validation();v9_migration_keeps_cluster_bytes_and_disables_new_expression();
    ordinary_expression_roundtrip_and_atomic_validation();v10_migration_preserves_mpe_song_and_legacy_pressure();
    expression_profile_mapping_span_preflight_is_atomic();
    chord_library_roundtrip_and_transient_state();chord_library_invalid_fields_reject_before_mutation();
    legacy_migration_clears_chord_library_and_new_policies();empty_user_grid_and_library_preflight_failures();
    library_journal_restore_and_crc_coverage();
    v12_migration_preserves_preferences_library_and_song_without_new_defaults();
    legacy_view_info_ranges_reject_new_identity_atomically();
    printf("settings: %u scenarios, %u assertions; v1-v18 migration to v19, Harmony preferences and physical cluster swap, chord policies/named musical library, MPE, ordinary expression and dual-stage profiles, independent clusters/linked release/pressure/velocity/overrides/song/preferences and fresh runtime/hooks, idle/debounce, every-byte empty-record and legacy-upgrade power loss, maximum-song word/boundary cuts, rotation and corrupt/future records passed\n",scenarios,checks);
    return 0;
}
