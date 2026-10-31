/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual adapter with synthetic counters; no device, clock or peripheral I/O. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static uint32_t fake_ms,fake_cycle_value,fake_cycle_step;
static unsigned cycle_reads,millisecond_reads,ready_reads;
static int clock_ready;
static uint32_t read_cycles(void)
{++cycle_reads;uint32_t value=fake_cycle_value;fake_cycle_value+=fake_cycle_step;return value;}
static int read_ready(void){++ready_reads;return clock_ready;}
uint32_t board_millis(void){++millisecond_reads;return fake_ms;}
#define THY_INSTRUMENT_PROFILE_CYCLES() read_cycles()
#define THY_INSTRUMENT_PROFILE_CLOCK_READY() read_ready()
#include "../../firmware/instrument/profile.c"

static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static char reply[2048];
static void reset(uint32_t hz)
{
    fake_ms=fake_cycle_value=cycle_reads=millisecond_reads=ready_reads=0u;
    fake_cycle_step=7u;clock_ready=1;thy_instrument_profile_init(hz);
}
static int command(const char *line,int available,const char *expected)
{
    size_t count=thy_instrument_profile_command(line,reply,sizeof reply,available,NULL);
    CHECK(count==strlen(reply) && count>=2u && count<sizeof reply);
    CHECK(reply[count-2u]=='\r' && reply[count-1u]=='\n');
    CHECK(strstr(reply,expected)==reply);
    return 1;
}
static int inactive_marks_and_command_buffer_guards(void)
{
    reset(168000000u);thy_profile before=*thy_instrument_profile_state();
    thy_instrument_profile_mark mark=thy_instrument_profile_begin();
    CHECK(!mark.id && !mark.cycles && !mark.ms);
    thy_instrument_profile_end(THY_PROFILE_ENGINE_US,mark);
    thy_instrument_profile_gap(THY_PROFILE_LOOP_US);thy_instrument_profile_gap(999u);
    const thy_settings_observer *observer=thy_instrument_profile_journal_observer();
    CHECK(observer && observer->enabled==&capture.active && !*observer->enabled);
    uint32_t stamp=observer->begin(observer->context,THY_SETTINGS_OBSERVE_CRC);
    observer->end(observer->context,THY_SETTINGS_OBSERVE_CRC,stamp);
    CHECK(!cycle_reads && !millisecond_reads && !ready_reads);
    CHECK(!memcmp(&before,&capture,sizeof capture));
    memset(reply,'x',sizeof reply);
    CHECK(!thy_instrument_profile_command(NULL,reply,sizeof reply,1,NULL) && reply[0]=='x');
    CHECK(!thy_instrument_profile_command("profile caps",NULL,sizeof reply,1,NULL));
    CHECK(!thy_instrument_profile_command("profile start 1",reply,511u,1,NULL) && reply[0]=='x');
    CHECK(!capture.active && !cycle_reads && !millisecond_reads);
    CHECK(command("profile caps",0,"PROFILE version=1 supported=1 metrics=19 bins=20 max_seconds=60 queue_unit=ms"));
    CHECK(command("profile status",0,"PROFILE version=1 id=0 active=0"));
    CHECK(strstr(reply," duration_ms=0 elapsed_ms=0 hz=0 "));
    CHECK(!cycle_reads && !ready_reads);
    fake_ms=77u;CHECK(thy_instrument_profile_now(NULL)==77u);
    return 1;
}
static int strict_commands_reject_malformed_and_stale_requests(void)
{
    reset(168000000u);
    const char *invalid[]={"profile"," profile caps","profile caps ","profile CAPS",
        "profile start","profile start ","profile start +1","profile start -1",
        "profile start 1 ","profile start 1x","profile start  1","profile start 4294967296",
        "profile stop","profile stop -1","profile stop 1x","profile metric 1",
        "profile metric 1 ","profile metric 1 0 ","profile metric 1  0",
        "profile metric 1 -1","profile metric 1 4294967296","profile unknown"};
    for(unsigned n=0;n<sizeof invalid/sizeof invalid[0];++n)
        CHECK(command(invalid[n],1,"ERR profile-command"));
    CHECK(command("profile start 0",1,"ERR profile-duration"));
    CHECK(command("profile start 61",1,"ERR profile-duration"));
    CHECK(command("profile start 4294967295",1,"ERR profile-duration"));
    CHECK(command("profile stop 0",1,"ERR profile-session"));
    CHECK(command("profile metric 0 0",1,"ERR profile-session"));
    CHECK(!capture.id && !capture.active && !cycle_reads);
    return 1;
}
static int admission_context_and_clock_probe_are_explicit(void)
{
    reset(168000000u);CHECK(command("profile start 1",0,"ERR profile-busy"));
    CHECK(!cycle_reads && !ready_reads);
    clock_ready=0;CHECK(command("profile start 1",1,"ERR profile-clock"));
    CHECK(!capture.id && !cycle_reads && ready_reads==1u);
    reset(999999u);CHECK(command("profile start 1",1,"ERR profile-clock"));
    reset(200000001u);CHECK(command("profile start 1",1,"ERR profile-clock"));
    reset(168000000u);
    thy_instrument_profile_context snapshot={2u,4u,123000u,UINT32_C(0x12345678)};
    CHECK(thy_instrument_profile_command("profile start 2",reply,sizeof reply,1,&snapshot)>0u);
    CHECK(capture.id==1u && capture.active && capture.duration_ms==2000u);
    CHECK(cycle_reads==64u && ready_reads==1u);
    CHECK(strstr(reply," clock_min_cycles=7 clock_max_cycles=7 engine=2 view=4 tempo_milli_bpm=123000 settings_crc=305419896"));
    snapshot.engine=9u;snapshot.settings_crc=0u;
    CHECK(command("profile status",0,"PROFILE version=1 id=1 active=1"));
    CHECK(strstr(reply," engine=2 view=4 ") && strstr(reply," settings_crc=305419896"));
    CHECK(command("profile start 1",1,"ERR profile-active"));
    CHECK(command("profile metric 1 0",1,"ERR profile-active"));
    CHECK(command("profile stop 2",1,"ERR profile-session") && capture.active);
    CHECK(cycle_reads==64u);
    fake_ms=50u;CHECK(command("profile stop 1",0,"PROFILE version=1 id=1 active=0"));
    CHECK(capture.status==THY_PROFILE_STOPPED && capture.elapsed_ms==50u);
    CHECK(command("profile metric 1 19",1,"ERR profile-index"));
    CHECK(command("profile metric 1 0",0,"PROFILE_METRIC id=1 index=0 unit=us count=0 min=0 max=0 mean=0 p50_upper=0"));
    CHECK(command("profile stop 1",0,"PROFILE version=1 id=1 active=0"));
    CHECK(cycle_reads==64u);
    capture.id=UINT32_MAX;
    CHECK(command("profile start 1",1,"ERR profile-start") && !capture.active);
    CHECK(capture.id==UINT32_MAX && cycle_reads==64u);
    return 1;
}
static int timeout_clock_wrap_and_read_only_freeze(void)
{
    reset(168000000u);fake_ms=UINT32_MAX-20u;
    CHECK(command("profile start 1",1,"PROFILE version=1 id=1 active=1"));
    uint32_t start=fake_ms;fake_ms=start+999u;thy_instrument_profile_service(fake_ms);
    CHECK(capture.active && capture.elapsed_ms==999u);
    fake_ms=start+1000u;CHECK(command("profile status",0,"PROFILE version=1 id=1 active=0"));
    CHECK(strstr(reply," timed_out=1 clock_error=0 "));
    CHECK(capture.elapsed_ms==1000u && capture.status==THY_PROFILE_TIMED_OUT);
    thy_profile frozen=capture;unsigned reads=cycle_reads;
    thy_instrument_profile_mark mark=thy_instrument_profile_begin();
    thy_instrument_profile_end(THY_PROFILE_ENGINE_US,mark);thy_instrument_profile_gap(THY_PROFILE_INPUT_GAP_US);
    thy_instrument_profile_service(start+2000u);
    CHECK(!memcmp(&capture,&frozen,sizeof capture) && cycle_reads==reads);
    fake_ms=start+2000u;CHECK(command("profile start 1",1,"PROFILE version=1 id=2 active=1"));
    CHECK(command("profile stop 1",1,"ERR profile-session"));
    CHECK(command("profile metric 1 0",1,"ERR profile-session"));
    fake_ms-=1u;thy_instrument_profile_service(fake_ms);
    CHECK(!capture.active && capture.status==THY_PROFILE_CLOCK_ERROR);
    CHECK(command("profile status",0,"PROFILE version=1 id=2 active=0"));
    CHECK(strstr(reply," clock_error=1 "));
    return 1;
}
static int marks_and_gaps_do_not_cross_session_boundaries(void)
{
    reset(168000000u);CHECK(command("profile start 2",1,"PROFILE version=1 id=1 active=1"));
    fake_cycle_step=0u;fake_cycle_value=UINT32_MAX-100u;
    thy_instrument_profile_mark old=thy_instrument_profile_begin();
    fake_cycle_value=old.cycles+1680u;fake_ms=1u;
    thy_instrument_profile_end(THY_PROFILE_ENGINE_US,old);
    CHECK(capture.stats[THY_PROFILE_ENGINE_US].count==1u && capture.stats[THY_PROFILE_ENGINE_US].sum==10u);
    thy_instrument_profile_gap(THY_PROFILE_LOOP_US);
    fake_cycle_value+=336u;thy_instrument_profile_gap(THY_PROFILE_LOOP_US);
    CHECK(capture.stats[THY_PROFILE_LOOP_US].count==1u && capture.stats[THY_PROFILE_LOOP_US].sum==2u);
    unsigned reads=cycle_reads;thy_instrument_profile_gap(THY_PROFILE_USB_US);
    CHECK(cycle_reads==reads);
    CHECK(command("profile stop 1",1,"PROFILE version=1 id=1 active=0"));
    CHECK(command("profile start 2",1,"PROFILE version=1 id=2 active=1"));
    reads=cycle_reads;thy_instrument_profile_end(THY_PROFILE_ENGINE_US,old);
    CHECK(cycle_reads==reads && !capture.stats[THY_PROFILE_ENGINE_US].count);
    thy_instrument_profile_gap(THY_PROFILE_LOOP_US);CHECK(!capture.stats[THY_PROFILE_LOOP_US].count);
    fake_cycle_value+=168u;thy_instrument_profile_gap(THY_PROFILE_LOOP_US);
    CHECK(capture.stats[THY_PROFILE_LOOP_US].sum==1u);
    const unsigned gap_ids[]={THY_PROFILE_INPUT_GAP_US,THY_PROFILE_ADC_FRAME_GAP_US};
    for(unsigned n=0;n<2u;++n){
        thy_instrument_profile_gap(gap_ids[n]);fake_cycle_value+=504u;thy_instrument_profile_gap(gap_ids[n]);
        CHECK(capture.stats[gap_ids[n]].count==1u && capture.stats[gap_ids[n]].sum==3u);
    }
    thy_instrument_profile_mark mark=thy_instrument_profile_begin();
    thy_instrument_profile_end(THY_PROFILE_METRIC_COUNT,mark);CHECK(capture.invalid_samples==1u);
    return 1;
}
static int journal_binding_uses_only_active_session_marks(void)
{
    reset(168000000u);const thy_settings_observer *observer=thy_instrument_profile_journal_observer();
    CHECK(observer==thy_instrument_profile_journal_observer());
    CHECK(command("profile start 2",1,"PROFILE version=1 id=1 active=1"));fake_cycle_step=0u;
    const unsigned metrics[]={THY_PROFILE_SETTINGS_ENCODE_US,THY_PROFILE_SETTINGS_VALIDATE_US,
        THY_PROFILE_SETTINGS_CRC_US,THY_PROFILE_SETTINGS_PROGRAM_US,THY_PROFILE_SETTINGS_ERASE_US};
    for(unsigned phase=0;phase<5u;++phase){
        uint32_t stamp=observer->begin(observer->context,phase);fake_cycle_value+=168u*(phase+1u);
        observer->end(observer->context,phase,stamp);
        CHECK(capture.stats[metrics[phase]].count==1u && capture.stats[metrics[phase]].sum==phase+1u);
    }
    unsigned reads=cycle_reads;CHECK(!observer->begin(observer->context,5u));observer->end(observer->context,5u,0u);
    CHECK(cycle_reads==reads);
    uint32_t old=observer->begin(observer->context,0u);
    CHECK(command("profile stop 1",1,"PROFILE version=1 id=1 active=0"));
    CHECK(command("profile start 2",1,"PROFILE version=1 id=2 active=1"));
    reads=cycle_reads;observer->end(observer->context,0u,old);
    CHECK(cycle_reads==reads && !capture.stats[THY_PROFILE_SETTINGS_ENCODE_US].count);
    CHECK(command("profile stop 2",1,"PROFILE version=1 id=2 active=0"));
    reads=cycle_reads;
    for(unsigned phase=0;phase<5u;++phase){uint32_t stamp=observer->begin(observer->context,phase);observer->end(observer->context,phase,stamp);}
    CHECK(!*observer->enabled && cycle_reads==reads);
    return 1;
}
static int serializers_have_complete_framing_or_explicit_overflow(void)
{
    reset(168000000u);
    const thy_instrument_profile_context snapshot={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    CHECK(thy_instrument_profile_command("profile start 1",reply,sizeof reply,1,&snapshot)>0u);
    CHECK(command("profile stop 1",1,"PROFILE version=1 id=1 active=0"));
    capture.id=capture.duration_ms=capture.elapsed_ms=capture.clock_hz=UINT32_MAX;
    capture.invalid_samples=capture.dropped_samples=capture.saturated=UINT32_MAX;
    for(unsigned n=0;n<4u;++n)capture.peaks[n]=UINT32_MAX;
    for(unsigned n=0;n<2u;++n)capture.excluded[n]=capture.discarded[n]=UINT32_MAX;
    clock_min_cycles=clock_max_cycles=UINT32_MAX;
    size_t full=thy_instrument_profile_command("profile status",reply,sizeof reply,0,NULL);
    CHECK(full>=512u && full==strlen(reply) && reply[full-2u]=='\r' && reply[full-1u]=='\n');
    struct{uint8_t before;char bytes[512];uint8_t after;} guarded;
    memset(&guarded,0xa5,sizeof guarded);
    CHECK(!thy_instrument_profile_command("profile status",guarded.bytes,sizeof guarded.bytes,0,NULL));
    CHECK(!guarded.bytes[0] && guarded.before==0xa5u && guarded.after==0xa5u);
    for(unsigned metric=0;metric<THY_PROFILE_METRIC_COUNT;++metric){
        thy_profile_stat *stat=&capture.stats[metric];
        *stat=(thy_profile_stat){.sum=(uint64_t)UINT32_MAX*UINT32_MAX,.count=UINT32_MAX,
            .min=UINT32_MAX,.max=UINT32_MAX,.saturated=1u};stat->bins[19]=UINT32_MAX;
        char line[80];(void)snprintf(line,sizeof line,"profile metric %u %u",UINT32_MAX,metric);
        size_t count=thy_instrument_profile_command(line,guarded.bytes,sizeof guarded.bytes,0,NULL);
        CHECK(count>0u && count<sizeof guarded.bytes && count==strlen(guarded.bytes));
        CHECK(strstr(guarded.bytes," mean=4294967295 p50_upper=4294967295 p95_upper=4294967295 p99_upper=4294967295 saturated=1 bins="));
        CHECK(strstr(guarded.bytes,(metric==15u||metric==16u)?" unit=ms ":" unit=us "));
        CHECK(guarded.bytes[count-2u]=='\r' && guarded.bytes[count-1u]=='\n' && guarded.before==0xa5u && guarded.after==0xa5u);
    }
    return 1;
}
static int emit_fixture_lines(void)
{
    reset(168000000u);
    CHECK(command("profile caps",1,"PROFILE version=1"));fputs(reply,stdout);
    const thy_instrument_profile_context snapshot={2u,4u,123000u,UINT32_C(0x12345678)};
    CHECK(thy_instrument_profile_command("profile start 2",reply,sizeof reply,1,&snapshot)>0u);
    for(unsigned metric=0;metric<THY_PROFILE_METRIC_COUNT;++metric){
        CHECK(thy_profile_record(&capture,metric,2u*(metric+1u)));
        CHECK(thy_profile_record(&capture,metric,4u*(metric+1u)));
    }
    thy_profile_peak(&capture,THY_PROFILE_PEAK_CORE,3u);thy_profile_peak(&capture,THY_PROFILE_PEAK_USB,2u);
    thy_profile_peak(&capture,THY_PROFILE_PEAK_DIN,1u);thy_profile_peak(&capture,THY_PROFILE_PEAK_DIN_BYTES,6u);
    fake_ms=123u;CHECK(command("profile stop 1",1,"PROFILE version=1"));fputs(reply,stdout);
    for(unsigned metric=0;metric<THY_PROFILE_METRIC_COUNT;++metric){
        char line[40];(void)snprintf(line,sizeof line,"profile metric 1 %u",metric);
        CHECK(command(line,1,"PROFILE_METRIC id=1"));fputs(reply,stdout);
    }
    return 1;
}
static void fixture_string(const char *value)
{
    putchar('"');
    for(const unsigned char *p=(const unsigned char *)value;*p;++p){
        if(*p=='"' || *p=='\\'){putchar('\\');putchar(*p);}
        else if(*p=='\r')fputs("\\r",stdout);
        else if(*p=='\n')fputs("\\n",stdout);
        else if(*p<32u)printf("\\u%04x",(unsigned)*p);
        else putchar(*p);
    }
    putchar('"');
}
static int emit_fixture_json(void)
{
    reset(168000000u);CHECK(command("profile caps",1,"PROFILE version=1"));
    fputs("{\"caps\":",stdout);fixture_string(reply);
    CHECK(command("profile status",0,"PROFILE version=1"));
    fputs(",\"idle\":",stdout);fixture_string(reply);
    const thy_instrument_profile_context snapshot={2u,4u,123000u,UINT32_C(0x12345678)};
    CHECK(thy_instrument_profile_command("profile start 2",reply,sizeof reply,1,&snapshot)>0u);
    fputs(",\"active\":",stdout);fixture_string(reply);
    for(unsigned metric=0;metric<THY_PROFILE_METRIC_COUNT;++metric){
        CHECK(thy_profile_record(&capture,metric,2u*(metric+1u)));
        CHECK(thy_profile_record(&capture,metric,4u*(metric+1u)));
    }
    thy_profile_peak(&capture,THY_PROFILE_PEAK_CORE,3u);thy_profile_peak(&capture,THY_PROFILE_PEAK_USB,2u);
    thy_profile_peak(&capture,THY_PROFILE_PEAK_DIN,1u);thy_profile_peak(&capture,THY_PROFILE_PEAK_DIN_BYTES,6u);
    fake_ms=123u;CHECK(command("profile stop 1",1,"PROFILE version=1"));
    fputs(",\"stopped\":",stdout);fixture_string(reply);
    fputs(",\"metrics\":[",stdout);
    for(unsigned metric=0;metric<THY_PROFILE_METRIC_COUNT;++metric){
        char line[40];(void)snprintf(line,sizeof line,"profile metric 1 %u",metric);
        CHECK(command(line,1,"PROFILE_METRIC id=1"));if(metric)putchar(',');fixture_string(reply);
    }
    putchar(']');capture.stats[0]=(thy_profile_stat){0};
    CHECK(command("profile metric 1 0",1,"PROFILE_METRIC id=1"));
    fputs(",\"empty_metric\":",stdout);fixture_string(reply);
    capture.stats[0]=(thy_profile_stat){.sum=262144u,.bins={[19]=1u},.count=1u,.min=262144u,.max=262144u};
    CHECK(command("profile metric 1 0",1,"PROFILE_METRIC id=1"));
    fputs(",\"overflow_metric\":",stdout);fixture_string(reply);
    capture.stats[0]=(thy_profile_stat){.sum=(uint64_t)UINT32_MAX*UINT32_MAX,
        .bins={[19]=UINT32_MAX},.count=UINT32_MAX,.min=UINT32_MAX,.max=UINT32_MAX,.saturated=1u};
    CHECK(command("profile metric 1 0",1,"PROFILE_METRIC id=1"));
    fputs(",\"saturated_metric\":",stdout);fixture_string(reply);fputs("}\n",stdout);
    return 1;
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--fixture-lines"))return emit_fixture_lines()?0:1;
    if(argc==2 && !strcmp(argv[1],"--fixture-json"))return emit_fixture_json()?0:1;
    int(*tests[])(void)={inactive_marks_and_command_buffer_guards,
        strict_commands_reject_malformed_and_stale_requests,admission_context_and_clock_probe_are_explicit,
        timeout_clock_wrap_and_read_only_freeze,marks_and_gaps_do_not_cross_session_boundaries,
        journal_binding_uses_only_active_session_marks,serializers_have_complete_framing_or_explicit_overflow};
    for(unsigned n=0;n<sizeof tests/sizeof tests[0];++n)if(!tests[n]())return 1;
    printf("Instrument profiling: %u scenarios, %u assertions; actual commands, clocks, sessions and observer binding passed\n",
        (unsigned)(sizeof tests/sizeof tests[0]),checks);
    return 0;
}
