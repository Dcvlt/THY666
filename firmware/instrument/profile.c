/* SPDX-License-Identifier: GPL-3.0-or-later
 * Optional RAM-only foreground profiling. No input, MIDI or persistence actions.
 * Cycle costs include interrupts and observer overhead, never optical/wire time. */
#include "profile.h"
#include "board.h"

#ifndef THY_INSTRUMENT_PROFILE_CYCLES
#if defined(__arm__) || defined(__thumb__)
#define THY_INSTRUMENT_PROFILE_CYCLES() (*(volatile uint32_t *)(uintptr_t)0xe0001004u)
#define THY_INSTRUMENT_PROFILE_CLOCK_READY() ((*(volatile uint32_t *)(uintptr_t)0xe0001000u)&1u)
#else
#define THY_INSTRUMENT_PROFILE_CYCLES() 0u
#define THY_INSTRUMENT_PROFILE_CLOCK_READY() 0u
#endif
#endif

static thy_profile capture;
static uint32_t clock_hz,clock_min_cycles,clock_max_cycles;
static thy_instrument_profile_mark gaps[3],journal_marks[5];
static thy_instrument_profile_context context;

void thy_instrument_profile_init(uint32_t hz)
{
    thy_profile_init(&capture);clock_hz=hz;clock_min_cycles=clock_max_cycles=0u;
    for(unsigned n=0;n<3u;++n)gaps[n]=(thy_instrument_profile_mark){0};
    for(unsigned n=0;n<5u;++n)journal_marks[n]=(thy_instrument_profile_mark){0};
    context=(thy_instrument_profile_context){0};
}
thy_profile *thy_instrument_profile_state(void){return &capture;}
uint32_t thy_instrument_profile_now(void *unused){(void)unused;return board_millis();}
void thy_instrument_profile_service(uint32_t now){(void)thy_profile_tick(&capture,now);}
thy_instrument_profile_mark thy_instrument_profile_begin(void)
{
    thy_instrument_profile_mark mark={0};
    if(capture.active && thy_profile_tick(&capture,board_millis())){
        mark.id=capture.id;mark.ms=board_millis();mark.cycles=THY_INSTRUMENT_PROFILE_CYCLES();
    }
    return mark;
}
void thy_instrument_profile_end(unsigned metric,thy_instrument_profile_mark mark)
{
    if(!mark.id || !capture.active || mark.id!=capture.id)return;
    uint32_t cycles=THY_INSTRUMENT_PROFILE_CYCLES(),now=board_millis();
    if(thy_profile_tick(&capture,now))
        (void)thy_profile_record_cycles(&capture,metric,cycles-mark.cycles,now-mark.ms);
}
void thy_instrument_profile_gap(unsigned metric)
{
    if(!capture.active)return;
    unsigned index=metric==THY_PROFILE_LOOP_US?0u:
        metric==THY_PROFILE_INPUT_GAP_US?1u:2u;
    if(metric!=THY_PROFILE_LOOP_US && metric!=THY_PROFILE_INPUT_GAP_US &&
        metric!=THY_PROFILE_ADC_FRAME_GAP_US)return;
    thy_instrument_profile_mark now=thy_instrument_profile_begin(),before=gaps[index];
    if(now.id && before.id==now.id)
        (void)thy_profile_record_cycles(&capture,metric,now.cycles-before.cycles,now.ms-before.ms);
    gaps[index]=now;
}
static uint32_t journal_begin(void *unused,unsigned phase)
{
    (void)unused;if(phase>=5u)return 0u;
    journal_marks[phase]=thy_instrument_profile_begin();return journal_marks[phase].cycles;
}
static void journal_end(void *unused,unsigned phase,uint32_t stamp)
{
    (void)unused;(void)stamp;
    static const uint8_t metrics[]={THY_PROFILE_SETTINGS_ENCODE_US,THY_PROFILE_SETTINGS_VALIDATE_US,
        THY_PROFILE_SETTINGS_CRC_US,THY_PROFILE_SETTINGS_PROGRAM_US,THY_PROFILE_SETTINGS_ERASE_US};
    if(phase<5u)thy_instrument_profile_end(metrics[phase],journal_marks[phase]);
}
const thy_settings_observer *thy_instrument_profile_journal_observer(void)
{
    static const thy_settings_observer observer={&capture.active,NULL,journal_begin,journal_end};
    return &observer;
}

typedef struct {char *out;size_t at,capacity;uint8_t overflow;} writer;
static void text(writer *w,const char *s){
    while(*s && w->at+1u<w->capacity)w->out[w->at++]=*s++;
    if(*s)w->overflow=1u;
}
static void number(writer *w,uint32_t value){char digits[10];unsigned n=0;
    do{digits[n++]=(char)('0'+value%10u);value/=10u;}while(value);
    while(n){char one[2]={digits[--n],0};text(w,one);}}
static void field(writer *w,const char *label,uint32_t value){text(w,label);number(w,value);}
static int same(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static int prefix(const char **s,const char *head){const char *at=*s;while(*head)if(*at++!=*head++)return 0;*s=at;return 1;}
static int decimal(const char **s,uint32_t *value){const char *p=*s;uint32_t n=0;
    if(*p<'0'||*p>'9')return 0;
    do{unsigned digit=(unsigned)(*p++-'0');if(n>(UINT32_MAX-digit)/10u)return 0;n=n*10u+digit;}while(*p>='0'&&*p<='9');
    *s=p;*value=n;return 1;}
static void status(writer *w)
{
    field(w,"PROFILE version=",1u);field(w," id=",capture.id);field(w," active=",capture.active);
    field(w," duration_ms=",capture.duration_ms);field(w," elapsed_ms=",capture.elapsed_ms);
    field(w," hz=",capture.clock_hz);field(w," timed_out=",capture.status==THY_PROFILE_TIMED_OUT);
    field(w," clock_error=",capture.status==THY_PROFILE_CLOCK_ERROR);
    field(w," invalid=",capture.invalid_samples);field(w," dropped=",capture.dropped_samples);
    field(w," saturated=",capture.saturated);
    field(w," core_peak=",capture.peaks[THY_PROFILE_PEAK_CORE]);
    field(w," usb_peak=",capture.peaks[THY_PROFILE_PEAK_USB]);
    field(w," din_peak=",capture.peaks[THY_PROFILE_PEAK_DIN]);
    field(w," din_bytes_peak=",capture.peaks[THY_PROFILE_PEAK_DIN_BYTES]);
    field(w," usb_excluded=",capture.excluded[0]);field(w," din_excluded=",capture.excluded[1]);
    field(w," usb_discarded=",capture.discarded[0]);field(w," din_discarded=",capture.discarded[1]);
    field(w," clock_min_cycles=",clock_min_cycles);field(w," clock_max_cycles=",clock_max_cycles);
    field(w," engine=",context.engine);field(w," view=",context.view);
    field(w," tempo_milli_bpm=",context.tempo_milli_bpm);field(w," settings_crc=",context.settings_crc);
}
size_t thy_instrument_profile_command(const char *line,char *out,size_t capacity,
    int available,const thy_instrument_profile_context *snapshot)
{
    if(!line||!out||capacity<512u)return 0u;
    writer w={out,0u,capacity,0u};const char *p=line;uint32_t first,second;
    (void)thy_profile_tick(&capture,board_millis());
    if(same(line,"profile caps"))text(&w,"PROFILE version=1 supported=1 metrics=19 bins=20 max_seconds=60 queue_unit=ms");
    else if(same(line,"profile status"))status(&w);
    else if(prefix(&p,"profile start ")&&decimal(&p,&first)&&!*p){
        if(!first||first>THY_PROFILE_MAX_SECONDS)text(&w,"ERR profile-duration");
        else if(capture.active)text(&w,"ERR profile-active");
        else if(!available)text(&w,"ERR profile-busy");
        else if(!THY_INSTRUMENT_PROFILE_CLOCK_READY()||clock_hz<1000000u||clock_hz>200000000u)text(&w,"ERR profile-clock");
        else if(!thy_profile_start(&capture,first,board_millis(),clock_hz))text(&w,"ERR profile-start");
        else{
            context=snapshot?*snapshot:(thy_instrument_profile_context){0};
            for(unsigned n=0;n<3u;++n)gaps[n]=(thy_instrument_profile_mark){0};
            for(unsigned n=0;n<5u;++n)journal_marks[n]=(thy_instrument_profile_mark){0};
            clock_min_cycles=UINT32_MAX;clock_max_cycles=0u;
            /* Read-pair cost only. Full observer work and IRQ time remain in
             * collected timings; no guessed overhead is subtracted. */
            for(unsigned n=0;n<32u;++n){uint32_t a=THY_INSTRUMENT_PROFILE_CYCLES();
                uint32_t cost=THY_INSTRUMENT_PROFILE_CYCLES()-a;
                if(cost<clock_min_cycles)clock_min_cycles=cost;if(cost>clock_max_cycles)clock_max_cycles=cost;}
            status(&w);
        }
    }else if((p=line,prefix(&p,"profile stop "))&&decimal(&p,&first)&&!*p){
        if(!first||first!=capture.id)text(&w,"ERR profile-session");
        else{thy_profile_stop(&capture,board_millis());status(&w);}
    }else if((p=line,prefix(&p,"profile metric "))&&decimal(&p,&first)&&*p++==' '&&decimal(&p,&second)&&!*p){
        if(!first||first!=capture.id)text(&w,"ERR profile-session");
        else if(capture.active)text(&w,"ERR profile-active");
        else if(second>=THY_PROFILE_METRIC_COUNT)text(&w,"ERR profile-index");
        else{
            const thy_profile_stat *s=&capture.stats[second];
            field(&w,"PROFILE_METRIC id=",first);field(&w," index=",second);
            text(&w,second==THY_PROFILE_USB_WAIT_MS||second==THY_PROFILE_DIN_WAIT_MS?" unit=ms":" unit=us");
            field(&w," count=",s->count);field(&w," min=",s->min);field(&w," max=",s->max);
            field(&w," mean=",s->count?(uint32_t)(s->sum/s->count):0u);
            field(&w," p50_upper=",thy_profile_percentile_upper(s,50u));
            field(&w," p95_upper=",thy_profile_percentile_upper(s,95u));
            field(&w," p99_upper=",thy_profile_percentile_upper(s,99u));
            field(&w," saturated=",s->saturated);text(&w," bins=");
            for(unsigned n=0;n<THY_PROFILE_HISTOGRAM_BINS;++n){if(n)text(&w,",");number(&w,s->bins[n]);}
        }
    }else text(&w,"ERR profile-command");
    text(&w,"\r\n");
    if(w.overflow){out[0]=0;return 0u;}
    out[w.at]=0;return w.at;
}
