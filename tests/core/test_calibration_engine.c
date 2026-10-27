/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_engine.h"
#include "thy_calibration.h"
#include "thy_settings.h"
#include <stdio.h>
#include <string.h>
static thy_core core;
static thy_instrument instrument,before;
static thy_engine engine;
static thy_calibration workspace;
static thy_instrument_pad layout[96];
static uint32_t now;
static unsigned assertions;
static uint8_t saved_settings[THY_SETTINGS_PAYLOAD_MAX],observed_settings[THY_SETTINGS_PAYLOAD_MAX];
static size_t saved_settings_bytes;
#define CHECK(x) do{++assertions;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static int setup(void)
{
    thy_core_init(&core);thy_instrument_init(&instrument,&core);now=0;
    for(unsigned p=0;p<96u;++p){layout[p]=(thy_instrument_pad){0};layout[p].notes[0]=60;layout[p].valid_mask=1u;}
    const int16_t degrees[]={0,2,4,5,7,9,11};
    CHECK(thy_instrument_set_layout(&instrument,layout,60,degrees,7u)==THY_OK);
    CHECK(thy_instrument_set_detector_profile(&instrument,THY_DETECTOR_FACTORY_LINEAR)==THY_OK);
    for(unsigned k=0;k<8u;++k)for(unsigned p=0;p<96u;++p)
        CHECK(thy_instrument_sample(&instrument,(uint8_t)p,20u,++now)==THY_OK);
    thy_engine_init(&engine,&instrument,21u,70u);thy_engine_bind_calibration(&engine,&workspace);thy_engine_bind_controls(&engine);
    CHECK(thy_engine_advance(&engine,now)==THY_OK);return 1;
}
static int sample_after(unsigned p,unsigned raw,unsigned elapsed)
{
    now+=elapsed;CHECK(thy_instrument_sample(&instrument,(uint8_t)p,(uint16_t)raw,now)==THY_OK);
    CHECK(thy_engine_advance(&engine,now)==THY_OK);return 1;
}
static int sample(unsigned p,unsigned raw){return sample_after(p,raw,10u);}
static void drain(void){thy_midi_event event;while(thy_core_poll(&core,&event)){} }
static int remember_settings(void)
{CHECK(thy_settings_encode(&engine,saved_settings,sizeof saved_settings,&saved_settings_bytes));return 1;}
static int settings_unchanged(void)
{
    size_t used=0u;CHECK(thy_settings_encode(&engine,observed_settings,sizeof observed_settings,&used));
    CHECK(used==saved_settings_bytes && !memcmp(saved_settings,observed_settings,used));return 1;
}
static int cluster_unchanged(unsigned side,const thy_instrument *saved)
{
    CHECK(!memcmp(&instrument.cluster_response[side],&saved->cluster_response[side],sizeof instrument.cluster_response[side]));
    CHECK(!memcmp(&instrument.cluster_velocity[side],&saved->cluster_velocity[side],sizeof instrument.cluster_velocity[side]));
    CHECK(!memcmp(&instrument.cluster_release_policy[side],&saved->cluster_release_policy[side],sizeof instrument.cluster_release_policy[side]));
    for(unsigned p=0;p<96u;++p)if(instrument.mapping[p]/48u==side){
        CHECK(!memcmp(&instrument.sensors[p].response,&saved->sensors[p].response,sizeof instrument.sensors[p].response));
        CHECK(instrument.sensors[p].response_overridden==saved->sensors[p].response_overridden);
        CHECK(!memcmp(&instrument.pad_velocity[p],&saved->pad_velocity[p],sizeof instrument.pad_velocity[p]));
        CHECK(instrument.velocity_overridden[p]==saved->velocity_overridden[p]);
        CHECK(instrument.sensitivity_guard[p]==saved->sensitivity_guard[p]);
        CHECK(!memcmp(&instrument.pad_release_policy[p],&saved->pad_release_policy[p],sizeof instrument.pad_release_policy[p]));
    }
    return 1;
}
static int preview_mode(int candidate)
{
    workspace.action_cursor=(uint8_t)(candidate?THY_CALIBRATION_AFTER:THY_CALIBRATION_BEFORE);
    return thy_engine_calibration_navigate(&engine,THY_STATE_RIGHT);
}
static unsigned electrical_for(unsigned logical)
{
    for(unsigned p=0;p<96u;++p)if(instrument.mapping[p]==logical)return p;
    return 96u;
}
static int strike(unsigned logical,unsigned delta)
{
    unsigned p=electrical_for(logical);CHECK(p<96u);
    CHECK(sample_after(p,20u+delta/3u,30u));
    CHECK(sample_after(p,20u+delta*2u/3u,30u));
    CHECK(sample_after(p,20u+delta,30u));
    CHECK(sample_after(p,20u,30u));CHECK(sample_after(p,20u,30u));
    return 1;
}
static int learn_four_phases(unsigned logical)
{
    static const unsigned deltas[]={120u,780u,2520u};
    CHECK(workspace.target==logical && workspace.stage==THY_CALIBRATION_SOFT);
    for(unsigned stage=0;stage<3u;++stage){
        for(unsigned tap=0;tap<3u;++tap){
            CHECK(workspace.stage==stage && workspace.hits==tap);
            CHECK(strike(logical,deltas[stage]));
            CHECK(!workspace.reason && !workspace.contact_active);
        }
    }
    CHECK(workspace.stage==THY_CALIBRATION_PRESSURE && !workspace.hits);
    unsigned p=electrical_for(logical);CHECK(p<96u);
    /* One continuous contact: a stable soft start, then a smooth pressure
     * ramp and stable firm end. The rise-rate ordering belongs to the three
     * strike groups, never to this held-pressure gesture. */
    for(unsigned n=0;n<34u;++n)CHECK(sample_after(p,180u,30u));
    for(unsigned n=1;n<=30u;++n)CHECK(sample_after(p,180u+n*100u,30u));
    for(unsigned n=0;n<6u;++n)CHECK(sample_after(p,3180u,30u));
    CHECK(workspace.contact_active && workspace.stage==THY_CALIBRATION_PRESSURE);
    CHECK(workspace.hold_soft==180u && workspace.hold_firm==3180u);
    CHECK(sample_after(p,20u,30u));CHECK(sample_after(p,20u,30u));
    CHECK(thy_calibration_completed(&workspace,(uint8_t)logical));
    CHECK(!workspace.contact_active && !workspace.reason);
    return 1;
}
static void complete_profile(void)
{
    unsigned first=workspace.scope_first;
    (void)thy_calibration_begin_scope(&workspace,(uint8_t)first,48u);
    for(unsigned p=first;p<first+48u;++p){
        workspace.completed[p/8u]|=(uint8_t)(1u<<(p&7u));
        workspace.candidates[p]=(thy_calibration_candidate){(uint16_t)(100u+p),80u,35u,1800u,10u,100u,{200u,800u,1800u},0u,0u};
    }
    workspace.completed_count=48u;workspace.state=THY_CALIBRATION_READY;
}
static int test_raw_wizard_collects_below_old_trigger_and_mutes_music(void)
{
    CHECK(setup());CHECK(thy_engine_calibration_open(&engine)==THY_STATE_OK);
    CHECK(sample(0u,120u));CHECK(workspace.contact_active);
    CHECK(sample(0u,150u));CHECK(sample(0u,160u));
    unsigned samples=workspace.samples;
    CHECK(thy_engine_advance(&engine,now)==THY_OK && workspace.samples==samples);
    CHECK(sample(0u,20u));CHECK(workspace.hits==1u && !workspace.contact_active);
    CHECK(!core.sounding_count && !core.queue_count && instrument.response.attack_raw==1377u);
    CHECK(sample(0u,1600u));CHECK(sample(0u,1800u));CHECK(!core.sounding_count && !core.queue_count);
    CHECK(thy_engine_navigate(&engine,THY_STATE_SELECT)==THY_STATE_OK && !engine.calibration_open);
    CHECK(sample(0u,2200u));CHECK(!core.sounding_count && !core.queue_count);
    CHECK(sample(0u,20u));CHECK(sample(0u,20u));
    CHECK(sample(0u,2200u));CHECK(sample(0u,2200u));CHECK(core.sounding_count==1u);
    CHECK(sample(0u,20u));CHECK(sample(0u,20u));CHECK(!core.sounding_count);
    return 1;
}
static int test_profile_preflight_is_atomic_and_respects_electrical_mapping(void)
{
    CHECK(setup());CHECK(thy_engine_calibration_open(&engine)==THY_STATE_OK);
    complete_profile();workspace.candidates[47].pressure_floor=1800u;
    before=instrument;CHECK(thy_engine_calibration_apply(&engine)!=THY_STATE_OK);
    CHECK(!memcmp(&before,&instrument,sizeof instrument));
    CHECK(workspace.state==THY_CALIBRATION_READY);
    workspace.candidates[47].pressure_floor=35u;
    uint8_t mapping[96];for(unsigned p=0;p<96u;++p)mapping[p]=(uint8_t)(95u-p);
    CHECK(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    before=instrument;
    CHECK(thy_engine_calibration_apply(&engine)==THY_STATE_OK);
    CHECK(instrument.sensors[95].response.attack_raw==100u && instrument.sensors[48].response.attack_raw==147u);
    CHECK(cluster_unchanged(1u,&before));
    for(unsigned p=0;p<96u;++p)if(mapping[p]<48u){
        CHECK(instrument.sensors[p].response_overridden && instrument.velocity_overridden[p]);
        CHECK(instrument.pad_velocity[p].mode==THY_VELOCITY_STRIKE && instrument.pad_release_policy[p].linked);
        CHECK(instrument.pad_release_policy[p].gap==instrument.sensors[p].response.attack_raw-80u);
    }
    CHECK(workspace.state==THY_CALIBRATION_APPLIED && !engine.calibration_open && !core.sounding_count && !core.queue_count);
    return 1;
}
static int test_background_channels_cannot_move_or_feed_the_target(void)
{
    CHECK(setup());CHECK(thy_engine_calibration_open(&engine)==THY_STATE_OK);
    CHECK(workspace.target==0u && workspace.selected==0u);
    /* These are the two unexpected positions observed on the device: left
     * row3/column9 and row4/column5. A small excursion on either channel is
     * not an explicit request to move the calibration target. */
    CHECK(sample(34u,70u));CHECK(sample(19u,90u));
    CHECK(workspace.target==0u && workspace.selected==0u);
    CHECK(!workspace.hits && !workspace.contact_active && !workspace.completed_count);
    CHECK(sample(34u,20u));CHECK(sample(19u,20u));
    CHECK(sample(0u,180u));CHECK(workspace.contact_active);
    unsigned samples=workspace.samples,peak=workspace.peak;
    CHECK(sample(34u,60u));CHECK(sample(19u,50u));
    CHECK(workspace.target==0u && workspace.selected==0u && workspace.contact_active);
    CHECK(workspace.samples==samples && workspace.peak==peak && !workspace.hits);
    CHECK(sample(0u,190u));CHECK(sample(0u,200u));CHECK(sample(0u,20u));
    CHECK(workspace.hits==1u && !workspace.contact_active);
    CHECK(sample(34u,20u));CHECK(sample(19u,20u));
    CHECK(!core.sounding_count && !core.queue_count && !instrument.errors);
    return 1;
}
static int test_other_pad_press_cannot_count_its_coupled_target_excursion(void)
{
    CHECK(setup());CHECK(thy_engine_calibration_open(&engine)==THY_STATE_OK);
    /* An unrelated real press is much larger than the target's coupled
     * background excursion. Only the instructed pad may supply a gesture. */
    CHECK(sample(48u,1500u));CHECK(sample(48u,1800u));
    CHECK(sample(0u,45u));CHECK(sample(0u,55u));CHECK(sample(0u,60u));
    CHECK(sample(0u,20u));
    CHECK(workspace.target==0u && workspace.selected==0u);
    CHECK(!workspace.hits && !workspace.contact_active && !workspace.completed_count);
    CHECK(sample(48u,20u));CHECK(sample(48u,20u));
    CHECK(sample(0u,120u));CHECK(sample(0u,150u));CHECK(sample(0u,180u));
    CHECK(sample(0u,20u));CHECK(workspace.hits==1u);
    CHECK(!core.sounding_count && !core.queue_count && !instrument.errors);
    return 1;
}
static int test_both_clusters_learn_four_phases_and_preview_before_scoped_apply(void)
{
    CHECK(setup());
    uint8_t mapping[96];
    for(unsigned p=0;p<96u;++p)mapping[p]=(uint8_t)((p*37u+11u)%96u);
    CHECK(thy_instrument_set_mapping(&instrument,mapping)==THY_OK);
    for(unsigned side=0u;side<2u;++side){
        engine.sensitivity_target=(uint8_t)(side?97u:0u);
        CHECK(thy_engine_calibration_open(&engine)==THY_STATE_OK);
        CHECK(workspace.scope_first==side*48u && workspace.scope_count==48u);
        CHECK(thy_engine_calibration_apply(&engine)==THY_STATE_UNAVAILABLE);
        before=instrument;CHECK(remember_settings());
        for(unsigned index=0;index<48u;++index){
            unsigned logical=side*48u+(index%12u)*4u+index/12u;
            CHECK(thy_calibration_pad_at(side*48u+index)==logical && workspace.target==logical);
            uint32_t colors[96];CHECK(thy_engine_calibration_lights(&engine,300u,colors));
            CHECK(colors[logical]==0x040800u);
            for(unsigned p=0;p<96u;++p)if(p!=logical){
                uint32_t expected=p/48u!=side?0u:thy_calibration_completed(&workspace,(uint8_t)p)?0x080001u:0x010101u;
                CHECK(colors[p]==expected);
            }
            CHECK(learn_four_phases(logical));
            CHECK(workspace.completed_count==index+1u);
            CHECK(!core.sounding_count && !core.queue_count && !instrument.errors);
            CHECK(instrument.response.attack_raw==1377u);
            if(index+1u<48u){
                unsigned next=index+1u;
                CHECK(workspace.target==side*48u+(next%12u)*4u+next/12u && workspace.stage==THY_CALIBRATION_SOFT);
                CHECK(thy_calibration_preview(&workspace,1)==THY_CALIBRATION_NOT_READY);
            }
        }
        CHECK(workspace.completed_count==48u && workspace.state==THY_CALIBRATION_PREVIEW_CANDIDATE);
        CHECK(settings_unchanged());CHECK(cluster_unchanged(1u-side,&before));
        CHECK(thy_engine_calibration_set_sensitivity(&engine,80)==THY_STATE_OK && workspace.preview_percent==80u);
        CHECK(preview_mode(0)==THY_STATE_OK);
        unsigned p=electrical_for(side*48u);CHECK(p<96u);
        CHECK(sample(p,1200u));CHECK(sample(p,1200u));
        CHECK(!core.owners[p].held && !core.queue_count && !instrument.sensors[p].pressure);
        CHECK(sample(p,20u));CHECK(sample(p,20u));
        CHECK(preview_mode(1)==THY_STATE_OK);
        CHECK(sample(p,1200u));CHECK(sample(p,1200u));
        CHECK(core.owners[p].held && core.owners[p].velocity==127u);
        CHECK(instrument.sensors[p].pressure==43u);
        drain();
        unsigned percent=workspace.preview_percent;
        CHECK(preview_mode(0)==THY_STATE_BLOCKED && workspace.state==THY_CALIBRATION_PREVIEW_CANDIDATE);
        CHECK(thy_engine_calibration_set_sensitivity(&engine,percent?percent-1u:1u)==THY_STATE_BLOCKED);
        CHECK(workspace.preview_percent==percent);
        thy_engine_calibration_close(&engine);CHECK(engine.calibration_open);
        CHECK(settings_unchanged());
        CHECK(sample(p,20u));CHECK(sample(p,20u));drain();
        CHECK(!core.sounding_count && !core.queue_count);
        CHECK(settings_unchanged());
        CHECK(thy_engine_calibration_apply(&engine)==THY_STATE_OK);
        CHECK(workspace.state==THY_CALIBRATION_APPLIED && !engine.calibration_open);
        CHECK(cluster_unchanged(1u-side,&before));
        for(unsigned electrical=0;electrical<96u;++electrical)if(mapping[electrical]/48u==side){
            CHECK(instrument.sensors[electrical].response_overridden && instrument.velocity_overridden[electrical]);
            CHECK(instrument.sensors[electrical].response.pressure_floor==180u);
            CHECK(instrument.sensors[electrical].response.pressure_full_scale==3180u);
            CHECK(instrument.pad_velocity[electrical].mode==THY_VELOCITY_STRIKE);
            CHECK(instrument.pad_velocity[electrical].soft_rate<instrument.pad_velocity[electrical].hard_rate);
            CHECK(instrument.sensitivity_guard[electrical]>=24u);
            CHECK(instrument.sensors[electrical].response.release_raw>=instrument.sensitivity_guard[electrical]);
        }
        size_t used;CHECK(thy_settings_encode(&engine,observed_settings,sizeof observed_settings,&used));
        CHECK(used==saved_settings_bytes && memcmp(saved_settings,observed_settings,used));
        /* Real acquisition qualifies the newly applied detector profiles
         * again before a subsequent basic-sensitivity edit. */
        for(unsigned round=0;round<THY_INSTRUMENT_WARMUP_SAMPLES;++round)
            for(unsigned electrical=0;electrical<96u;++electrical)if(mapping[electrical]/48u==side)
                CHECK(sample_after(electrical,20u,30u));
        CHECK(thy_state_set(&engine.state,THY_ROW_SENSITIVITY_AMOUNT,100)==THY_STATE_OK);
        for(unsigned electrical=0;electrical<96u;++electrical)if(mapping[electrical]/48u==side){
            CHECK(instrument.sensitivity_guard[electrical]>=24u);
            CHECK(instrument.sensors[electrical].response.release_raw>=instrument.sensitivity_guard[electrical]);
        }
        CHECK(cluster_unchanged(1u-side,&before));
    }
    return 1;
}
static int test_global_review_discard_preserves_all_saved_settings(void)
{
    CHECK(setup());
    CHECK(thy_engine_sequencer_open(&engine,THY_SEQ_VIEW_STEPS)==THY_STATE_OK && engine.sequencer_ui.open);
    engine.sensitivity_target=97u;
    CHECK(thy_engine_calibration_open(&engine)==THY_STATE_OK && !engine.sequencer_ui.open);
    CHECK(workspace.scope_first==48u && workspace.scope_count==48u);complete_profile();
    CHECK(remember_settings());before=instrument;
    CHECK(preview_mode(0)==THY_STATE_OK && workspace.state==THY_CALIBRATION_PREVIEW_ORIGINAL);
    CHECK(preview_mode(1)==THY_STATE_OK && workspace.state==THY_CALIBRATION_PREVIEW_CANDIDATE);
    CHECK(thy_engine_calibration_set_sensitivity(&engine,65)==THY_STATE_OK);
    CHECK(settings_unchanged());
    uint32_t colors[96];CHECK(!thy_engine_calibration_lights(&engine,now,colors));
    CHECK(sample(48u,1200u));CHECK(sample(48u,1200u));
    CHECK(core.owners[48].held);drain();
    workspace.action_cursor=THY_CALIBRATION_CANCEL;
    CHECK(thy_engine_calibration_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_BLOCKED);
    CHECK(engine.calibration_open && workspace.state==THY_CALIBRATION_PREVIEW_CANDIDATE);
    CHECK(sample(48u,20u));CHECK(sample(48u,20u));drain();
    workspace.action_cursor=THY_CALIBRATION_CANCEL;
    CHECK(thy_engine_calibration_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(!engine.calibration_open && workspace.state==THY_CALIBRATION_CANCELLED);
    CHECK(settings_unchanged());CHECK(cluster_unchanged(0u,&before));CHECK(cluster_unchanged(1u,&before));
    CHECK(sample(48u,1200u));CHECK(sample(48u,1200u));
    CHECK(!core.owners[48].held && !core.queue_count);
    CHECK(sample(48u,20u));CHECK(sample(48u,20u));
    return 1;
}
static int test_learn_pick_requires_explicit_press_and_does_not_record_selection(void)
{
    CHECK(setup());CHECK(thy_engine_calibration_open(&engine)==THY_STATE_OK);
    workspace.action_cursor=THY_CALIBRATION_LEARN;
    CHECK(thy_engine_calibration_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(engine.calibration_pick==1u);
    CHECK(sample(19u,90u));CHECK(!workspace.hits && workspace.target==0u && engine.calibration_pick==1u);
    CHECK(sample(4u,2000u));CHECK(sample(4u,2000u));
    CHECK(workspace.target==4u && workspace.selected==4u && engine.calibration_pick==2u);
    CHECK(!workspace.hits && !workspace.contact_active);
    CHECK(sample(4u,20u));CHECK(sample(4u,20u));CHECK(!engine.calibration_pick && !workspace.hits);
    CHECK(sample(19u,20u));CHECK(strike(4u,120u));CHECK(workspace.hits==1u);
    /* Learn on a completed pad is also Retry. The explicit selection contact
     * resets that pad, then must be released before any new Soft tap counts. */
    workspace.candidates[4]=(thy_calibration_candidate){100u,80u,180u,3180u,2u,50u,{140u,800u,2540u},0u,0u};
    workspace.completed[0]|=1u<<4u;workspace.completed_count=1u;
    workspace.target=workspace.selected=8u;workspace.stage=workspace.hits=0u;
    workspace.action_cursor=THY_CALIBRATION_LEARN;
    CHECK(thy_engine_calibration_navigate(&engine,THY_STATE_RIGHT)==THY_STATE_OK);
    CHECK(sample(4u,2000u));CHECK(sample(4u,2000u));
    CHECK(workspace.target==4u && !workspace.completed_count && !thy_calibration_completed(&workspace,4u));
    CHECK(!workspace.hits && !workspace.stage && !workspace.contact_active);
    CHECK(sample(4u,20u));CHECK(sample(4u,20u));CHECK(!engine.calibration_pick && !workspace.hits);
    CHECK(!core.sounding_count && !core.queue_count && !instrument.errors);
    return 1;
}
static int test_coupled_outlier_keeps_saved_response_and_other_cluster_plays_normally(void)
{
    CHECK(setup());CHECK(thy_engine_calibration_open(&engine)==THY_STATE_OK);complete_profile();
    workspace.leak_floor[4]=600u;before=instrument;CHECK(remember_settings());
    CHECK(preview_mode(1)==THY_STATE_OK);
    CHECK(thy_calibration_leak_outlier(&workspace,4u) && thy_calibration_outlier(&workspace,4u));
    CHECK(workspace.review_leak_count==1u);
    CHECK(sample(4u,1200u));CHECK(sample(4u,1200u));
    CHECK(!core.owners[4].held && !core.queue_count && instrument.sensors[4].attack_raw==1377);
    CHECK(sample(4u,20u));CHECK(sample(4u,20u));
    CHECK(sample(0u,1200u));CHECK(sample(0u,1200u));CHECK(core.owners[0].held);drain();
    CHECK(sample(0u,20u));CHECK(sample(0u,20u));drain();
    /* Uncalibrated opposite half retains its ordinary channel, detector and
     * Fixed velocity while the selected half auditions its candidate. */
    CHECK(sample(48u,1800u));CHECK(sample(48u,1800u));
    CHECK(core.owners[48].held && core.owners[48].velocity==96u);drain();
    CHECK(thy_engine_calibration_apply(&engine)==THY_STATE_BLOCKED);
    CHECK(sample(48u,20u));CHECK(sample(48u,20u));drain();
    CHECK(settings_unchanged());CHECK(thy_engine_calibration_apply(&engine)==THY_STATE_OK);
    CHECK(!memcmp(&instrument.sensors[4].response,&before.sensors[4].response,sizeof instrument.sensors[4].response));
    thy_instrument_velocity velocity;uint8_t overridden;
    CHECK(thy_instrument_get_velocity(&instrument,4,&velocity,&overridden)==THY_OK);
    CHECK(velocity.mode==THY_VELOCITY_FIXED && velocity.hard_rate==128u);
    CHECK(!memcmp(&instrument.pad_release_policy[4],&before.pad_release_policy[4],sizeof instrument.pad_release_policy[4]));
    /* Explicitly capturing this saved response keeps it unchanged when the
     * cluster's newly fitted inherited default is committed afterward. */
    CHECK(instrument.sensors[4].response_overridden && instrument.velocity_overridden[4]);
    CHECK(instrument.pad_velocity[0].mode==THY_VELOCITY_STRIKE);
    CHECK(cluster_unchanged(1u,&before));
    CHECK(!core.sounding_count && !core.queue_count && !instrument.errors);
    return 1;
}
int main(void)
{
    if(!test_raw_wizard_collects_below_old_trigger_and_mutes_music() ||
       !test_profile_preflight_is_atomic_and_respects_electrical_mapping() ||
       !test_background_channels_cannot_move_or_feed_the_target() ||
       !test_other_pad_press_cannot_count_its_coupled_target_excursion() ||
       !test_both_clusters_learn_four_phases_and_preview_before_scoped_apply() ||
       !test_global_review_discard_preserves_all_saved_settings() ||
       !test_learn_pick_requires_explicit_press_and_does_not_record_selection() ||
       !test_coupled_outlier_keeps_saved_response_and_other_cluster_plays_normally())return 1;
    printf("8 calibration engine scenarios passed (%u assertions), workspace=%zu bytes\n",assertions,sizeof workspace);return 0;
}
