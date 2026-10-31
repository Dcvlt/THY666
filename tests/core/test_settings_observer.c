/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual codec/journal and synthetic NOR fixtures; no hardware or wall clock. */
#define main settings_fixture_unused_runner
#include "test_settings.c"
#undef main

typedef struct observer_counts {
    unsigned begins[5],ends[5],active,phase;
    uint32_t stamp;
} observer_counts;
static observer_counts observer_a,observer_b;
static uint8_t observer_enabled;
static uint32_t io_hash;
static unsigned io_reads,io_programs,io_erases,io_failure;
static unsigned last_ops[16],last_count;
static thy_engine engine_before;
static thy_instrument instrument_before;
static thy_core core_before;

static uint32_t timing_begin(void *context,unsigned phase)
{
    observer_counts *counts=context;
    CHECK(counts==&observer_a || counts==&observer_b);
    CHECK(phase<5u && !counts->active);
    counts->active=1u;counts->phase=phase;
    ++counts->begins[phase];counts->stamp+=17u;
    return counts->stamp;
}
static void timing_end(void *context,unsigned phase,uint32_t stamp)
{
    observer_counts *counts=context;
    CHECK(phase<5u && counts->active && counts->phase==phase && stamp==counts->stamp);
    counts->active=0u;++counts->ends[phase];
}
static void trace_word(uint32_t value)
{for(unsigned n=0;n<4u;++n){io_hash=(io_hash^(uint8_t)value)*UINT32_C(16777619);value>>=8u;}}
static void trace_op(unsigned kind,uint32_t offset,uint32_t count)
{
    trace_word(kind);trace_word(offset);trace_word(count);
    if(last_count<sizeof last_ops/sizeof last_ops[0])last_ops[last_count++]=kind;
}
static int observed_read(void *context,uint32_t offset,uint8_t *out,size_t count)
{
    ++io_reads;trace_op(0u,offset,(uint32_t)count);
    if(io_failure==2u && count==4u){trace_word(0u);return 0;}
    int result=read_memory(context,offset,out,count);
    if(io_failure==3u && count==4u)out[0]^=1u;
    if(io_failure==5u && count>THY_SETTINGS_HEADER_BYTES)out[0]^=1u;
    trace_word((uint32_t)result);
    for(size_t n=0;n<count;++n)trace_word(out[n]);
    return result;
}
static int observed_program(void *context,uint32_t offset,const uint8_t word[4])
{
    ++io_programs;trace_op(1u,offset,4u);
    for(unsigned n=0;n<4u;++n)trace_word(word[n]);
    int result=program_memory(context,offset,word);trace_word((uint32_t)result);return result;
}
static int observed_erase(void *context,unsigned sector)
{
    ++io_erases;trace_op(2u,sector,0u);
    int result=erase_memory(context,sector);trace_word((uint32_t)result);return result;
}
static thy_settings_io observed_io(void)
{return(thy_settings_io){flash,observed_read,observed_program,observed_erase};}
static void observer_fixture(unsigned failure)
{
    defaults();memset(flash,255u,sizeof flash);cut_after=-1;fail_erase=0;
    programs=erases=io_reads=io_programs=io_erases=last_count=0u;io_failure=0u;
    io_hash=UINT32_C(2166136261);
    memset(&observer_a,0,sizeof observer_a);memset(&observer_b,0,sizeof observer_b);
    if(failure==4u)memset(flash,0u,THY_SETTINGS_SECTOR_BYTES);
    thy_settings_io backend=observed_io();CHECK(thy_settings_journal_init(&journal,&backend,70u));
    thy_seq_event notes[]={
        {.duration=240u,.group=1u,.kind=128u,.pitch=60u,.value=96u},
        {.duration=240u,.group=1u,.kind=128u,.pitch=64u,.value=96u},
        {.duration=240u,.group=1u,.kind=128u,.pitch=67u,.value=96u}};
    CHECK(thy_sequencer_add(&engine.sequencer,0u,notes,3u)==THY_OK);
    engine.sequencer.tracks[0].channel=3u;
    if(failure==6u)engine.sequencer.count=THY_SEQ_EVENTS+1u;
    engine_before=engine;instrument_before=instrument;core_before=core;
}
static void assert_engine_unchanged(void)
{
    CHECK(!memcmp(&engine,&engine_before,sizeof engine));
    CHECK(!memcmp(&instrument,&instrument_before,sizeof instrument));
    CHECK(!memcmp(&core,&core_before,sizeof core));
}
static void run_save(unsigned failure)
{
    thy_settings_journal_task(&journal,&engine,0u,1);
    if(failure==6u){CHECK(journal.status==THY_SETTINGS_ERROR);assert_engine_unchanged();return;}
    CHECK(journal.status==THY_SETTINGS_PENDING);
    thy_settings_journal_task(&journal,&engine,1499u,1);CHECK(!journal.writing);
    thy_settings_journal_task(&journal,&engine,1500u,1);CHECK(journal.writing);
    io_failure=failure;last_count=0u;
    if(failure==1u)cut_after=0;
    if(failure==4u)fail_erase=1;
    for(unsigned n=0;n<5000u && journal.writing;++n)
        thy_settings_journal_task(&journal,&engine,1501u+n,1);
    CHECK(!journal.writing);
    CHECK(journal.status==(failure?THY_SETTINGS_ERROR:THY_SETTINGS_SAVED));
    assert_engine_unchanged();
}
static void enabled_and_disabled_saves_have_identical_physical_results(void)
{
    static thy_settings_journal baseline,actual;
    static const thy_settings_observer observer={&observer_enabled,&observer_a,timing_begin,timing_end};
    for(unsigned failure=0;failure<=6u;++failure){
        uint32_t expected_hash=0u;unsigned expected_programs=0u,expected_erases=0u,expected_reads=0u;
        for(unsigned enabled=0;enabled<2u;++enabled){
            observer_fixture(failure);observer_enabled=(uint8_t)enabled;journal.observer=&observer;
            run_save(failure);
            for(unsigned phase=0;phase<5u;++phase){
                CHECK(observer_a.begins[phase]==observer_a.ends[phase]);
                if(!enabled)CHECK(!observer_a.begins[phase]);
            }
            CHECK(!observer_a.active);
            if(!enabled){
                memcpy(original,flash,sizeof flash);baseline=journal;baseline.observer=NULL;
                expected_hash=io_hash;expected_programs=io_programs;expected_erases=io_erases;expected_reads=io_reads;
            }else{
                actual=journal;actual.observer=NULL;
                CHECK(!memcmp(flash,original,sizeof flash) && !memcmp(&actual,&baseline,sizeof actual));
                CHECK(io_hash==expected_hash && io_programs==expected_programs && io_erases==expected_erases && io_reads==expected_reads);
                CHECK(observer_a.begins[THY_SETTINGS_OBSERVE_ENCODE]==(failure==6u?1u:3u));
                if(!failure){
                    CHECK(observer_a.begins[THY_SETTINGS_OBSERVE_PROGRAM]==programs);
                    CHECK(observer_a.begins[THY_SETTINGS_OBSERVE_CRC]==6u);
                    CHECK(observer_a.begins[THY_SETTINGS_OBSERVE_VALIDATE]==1u);
                    CHECK(!observer_a.begins[THY_SETTINGS_OBSERVE_ERASE]);
                    size_t bytes=encode(payload);
                    CHECK(bytes==journal.payload_bytes && !memcmp(payload,journal.record+32u,bytes));
                    CHECK(engine.sequencer.count==3u && engine.sequencer.events[1].pitch==64u);
                }else if(failure<=3u){
                    CHECK(observer_a.begins[THY_SETTINGS_OBSERVE_PROGRAM]==1u);
                    CHECK(!observer_a.begins[THY_SETTINGS_OBSERVE_VALIDATE]);
                    CHECK(last_ops[0]==1u && last_count==(failure==1u?1u:2u));
                    if(failure>1u)CHECK(last_ops[1]==0u);
                }else if(failure==4u){
                    CHECK(observer_a.begins[THY_SETTINGS_OBSERVE_ERASE]==1u && !io_programs);
                    CHECK(last_count==1u && last_ops[0]==2u);
                }else if(failure==5u){
                    CHECK(observer_a.begins[THY_SETTINGS_OBSERVE_CRC]==5u);
                    CHECK(!observer_a.begins[THY_SETTINGS_OBSERVE_VALIDATE]);
                }else CHECK(!observer_a.begins[THY_SETTINGS_OBSERVE_CRC] && !io_programs && !io_erases);
            }
        }
    }
}
static void null_disabled_and_incomplete_observers_never_time(void)
{
    observer_fixture(0u);observer_enabled=1u;
    thy_settings_observer partial={&observer_enabled,&observer_a,timing_begin,timing_end};
    for(unsigned variant=0;variant<4u;++variant){
        observer_fixture(0u);partial=(thy_settings_observer){&observer_enabled,&observer_a,timing_begin,timing_end};
        journal.observer=&partial;
        if(variant==0u)journal.observer=NULL;
        else if(variant==1u)partial.enabled=NULL;
        else if(variant==2u)partial.begin=NULL;
        else partial.end=NULL;
        thy_settings_journal_task(&journal,&engine,0u,1);
        CHECK(journal.status==THY_SETTINGS_PENDING);
        for(unsigned phase=0;phase<5u;++phase)CHECK(!observer_a.begins[phase] && !observer_a.ends[phase]);
    }
}
static void observers_are_journal_local_and_initialization_unbinds(void)
{
    observer_fixture(0u);observer_enabled=1u;
    const thy_settings_observer first={&observer_enabled,&observer_a,timing_begin,timing_end};
    const thy_settings_observer second={&observer_enabled,&observer_b,timing_begin,timing_end};
    thy_settings_io backend=observed_io();CHECK(thy_settings_journal_init(&rebooted,&backend,70u));
    journal.observer=&first;rebooted.observer=&second;
    thy_settings_journal_task(&journal,&engine,0u,1);
    CHECK(observer_a.begins[0]==1u && !observer_b.begins[0]);
    thy_settings_journal_task(&rebooted,&engine,0u,1);
    CHECK(observer_a.begins[0]==1u && observer_b.begins[0]==1u);
    journal.observer=&first;run_save(0u);
    unsigned begins=observer_a.begins[THY_SETTINGS_OBSERVE_CRC];
    CHECK(thy_settings_journal_init(&journal,&backend,70u));
    CHECK(journal.observer==NULL && journal.loaded && journal.status==THY_SETTINGS_SAVED);
    CHECK(observer_a.begins[THY_SETTINGS_OBSERVE_CRC]==begins);
    CHECK(rebooted.observer==&second);
}
static void song_readback_uses_only_its_bound_observer(void)
{
    observer_fixture(0u);observer_enabled=1u;run_save(0u);
    const thy_settings_observer observer={&observer_enabled,&observer_a,timing_begin,timing_end};
    journal.observer=&observer;
    CHECK(thy_settings_journal_recall_song(&journal,&engine));
    CHECK(observer_a.begins[THY_SETTINGS_OBSERVE_CRC]==1u && observer_a.ends[THY_SETTINGS_OBSERVE_CRC]==1u);
    CHECK(observer_a.begins[THY_SETTINGS_OBSERVE_VALIDATE]==1u && observer_a.ends[THY_SETTINGS_OBSERVE_VALIDATE]==1u);
    CHECK(!observer_a.begins[THY_SETTINGS_OBSERVE_PROGRAM] && !observer_a.begins[THY_SETTINGS_OBSERVE_ERASE]);
    CHECK(engine.sequencer.count==3u && engine.sequencer.events[0].pitch==60u && engine.sequencer.events[2].pitch==67u);
    memset(&observer_a,0,sizeof observer_a);observer_enabled=0u;
    CHECK(thy_settings_journal_recall_song(&journal,&engine));
    for(unsigned phase=0;phase<5u;++phase)CHECK(!observer_a.begins[phase] && !observer_a.ends[phase]);
}
int main(void)
{
    enabled_and_disabled_saves_have_identical_physical_results();
    null_disabled_and_incomplete_observers_never_time();
    observers_are_journal_local_and_initialization_unbinds();
    song_readback_uses_only_its_bound_observer();
    printf("Journal observers: 4 scenarios, %u assertions; enabled/off bytes, calls, failures and isolated bindings passed\n",checks);
    return 0;
}
