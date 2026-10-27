/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_companion.h"
#include "thy_studio_ui.h"
#include "thy_views.h"

typedef struct {char *p;size_t used,cap;} writer;
static void add(writer *w,const char *p){while(*p && w->used+1u<w->cap)w->p[w->used++]=*p++;}
static void dec(writer *w,uint32_t n){char b[10];unsigned k=0;do{b[k++]=(char)('0'+n%10u);n/=10u;}while(n);while(k){char c[2]={b[--k],0};add(w,c);}}
static void signed_dec(writer *w,int32_t n){if(n<0){add(w,"-");dec(w,(uint32_t)(-(int64_t)n));}else dec(w,(uint32_t)n);}
static void hex(writer *w,uint32_t n,unsigned digits){static const char h[]="0123456789abcdef";while(digits){char c[2]={h[(n>>(--digits*4u))&15u],0};add(w,c);}}
static void field(writer *w,const char *name,uint32_t n){add(w,name);dec(w,n);}
static int same(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static int prefix(const char **p,const char *head){const char *q=*p;while(*head)if(*q++!=*head++)return 0;*p=q;return 1;}
static int number(const char **p,uint32_t *n){if(**p<'0'||**p>'9')return 0;uint32_t v=0;while(**p>='0'&&**p<='9'){unsigned d=(unsigned)(*(*p)++-'0');if(v>(UINT32_MAX-d)/10u)return 0;v=v*10u+d;}*n=v;return 1;}
static int integer(const char **p,int32_t *n){int minus=**p=='-';if(minus)++*p;uint32_t v;if(!number(p,&v)||v>(minus?UINT32_C(2147483648):INT32_MAX))return 0;*n=minus?(int32_t)(-(int64_t)v):(int32_t)v;return 1;}
static int space(const char **p){if(**p!=' ')return 0;++*p;return 1;}
static int schema(const char **p){const char *s=THY_MENU_SCHEMA_SHA256;for(unsigned n=0;n<64u;++n)if(!**p||*(*p)++!=s[n])return 0;return space(p);}
static void u16(uint8_t *p,unsigned at,uint16_t n){p[at]=(uint8_t)n;p[at+1u]=(uint8_t)(n>>8u);}
static void u32(uint8_t *p,unsigned at,uint32_t n){for(unsigned nbyte=0;nbyte<4u;++nbyte)p[at+nbyte]=(uint8_t)(n>>(8u*nbyte));}
static void text(uint8_t *p,unsigned at,unsigned limit,const char *s){if(s)for(unsigned n=0;n<limit && s[n];++n)p[at+n]=(uint8_t)s[n];}
static void bit(uint8_t *p,unsigned at,unsigned n){p[at+n/8u]|=(uint8_t)(1u<<(n&7u));}
static uint32_t crc32(const uint8_t *p,size_t bytes){uint32_t c=UINT32_MAX;for(size_t n=0;n<bytes;++n){c^=p[n];for(unsigned b=0;b<8u;++b)c=(c>>1u)^((0u-(c&1u))&UINT32_C(0xedb88320));}return ~c;}
void thy_companion_init(thy_companion *s,uint8_t *buffer,size_t cap,const thy_companion_identity *id,thy_companion_visual_fn visual,void *ctx)
{if(s)*s=(thy_companion){.buffer=buffer,.capacity=cap,.identity=id?*id:(thy_companion_identity){0},.visual=visual,.context=ctx};}
int thy_companion_expire(thy_companion *s,uint32_t now)
{if(s && s->active && ((uint32_t)(now-s->last_ms)>=THY_COMPANION_IDLE_MS || (uint32_t)(now-s->captured_ms)>=THY_COMPANION_MAX_AGE_MS)){s->active=0u;return 1;}return 0;}

static int snapshot(thy_companion *s,const thy_engine *e,uint32_t now)
{
    if(!s->buffer || s->capacity<THY_COMPANION_SNAPSHOT_BYTES || !s->visual || s->sequence==UINT32_MAX)return 0;
    uint8_t *p=s->buffer;for(unsigned n=0;n<THY_COMPANION_SNAPSHOT_BYTES;++n)p[n]=0u;
    uint32_t frame_ms=0;uint8_t visible=0;
    if(!s->visual(s->context,p+THY_COMPANION_OLED,p+THY_COMPANION_RGB,p+THY_COMPANION_CENTER,&frame_ms,&visible))return 0;
    const thy_instrument *i=e->instrument;const thy_state *state=&e->state;const thy_core *core=i->core;
    text(p,0,8,"THYCMP1");u16(p,8,1u);u16(p,10,THY_COMPANION_SNAPSHOT_BYTES);u32(p,12,s->sequence+1u);
    u32(p,16,now);u32(p,20,frame_ms);u32(p,24,i->revision);u32(p,28,state->revision);
    text(p,32,16,s->identity.build);text(p,48,24,s->identity.uid);text(p,72,64,THY_MENU_SCHEMA_SHA256);
    for(unsigned n=0;n<THY_KEY_COUNT;++n)if(core->refs[n]||thy_core_deferred(core,(uint16_t)n))bit(p,THY_COMPANION_NOTES,n%128u);
    uint64_t pool[2]={0,0};
    for(unsigned a=0;a<2u;++a)if(e->arps[a].config.enabled)for(unsigned n=0;n<THY_ARP_INPUTS;++n)if(e->arps[a].inputs[n].held){
        pool[0]|=e->arps[a].inputs[n].pitches[0];pool[1]|=e->arps[a].inputs[n].pitches[1];}
    for(unsigned n=0;n<16u;++n)p[THY_COMPANION_POOL+n]=(uint8_t)(pool[n/8u]>>((n%8u)*8u));
    unsigned control=0;const unsigned counts[]={5u,14u,12u,12u};
    for(unsigned bank=0;bank<4u;++bank)for(unsigned n=0;n<counts[bank];++n,++control)if(i->control_masks[bank]&(1u<<n))bit(p,THY_COMPANION_CONTROLS,control);
    uint8_t seen[2]={0},fixed[2]={0},modes[2]={0};
    for(unsigned n=0;n<THY_PAD_COUNT;++n){
        unsigned logical=i->mapping[n];if(logical>=96u)return 0;unsigned side=logical/48u;
        const thy_instrument_sensor *sensor=&i->sensors[n];
        int held=sensor->held||core->owners[n].held||i->held_input_valid[n];
        for(unsigned a=0;a<2u;++a)if(e->arps[a].config.enabled && e->arps[a].inputs[n].held)held=1;
        if(held){bit(p,THY_COMPANION_HELD,logical);++p[1618];if(e->input_velocity[n]>p[1616+side])p[1616+side]=e->input_velocity[n];}
        if(sensor->pressure>p[1608+side])p[1608+side]=sensor->pressure;
        thy_instrument_velocity velocity;thy_instrument_route route;uint8_t custom;
        if(thy_instrument_get_velocity(i,(int)n,&velocity,&custom)!=THY_OK ||
           thy_instrument_get_pad_route(i,thy_instrument_route_target(i,(uint8_t)n),&route)!=THY_OK)return 0;
        if(!seen[side]){fixed[side]=route.velocity;modes[side]=velocity.mode;seen[side]=1u;}
        else {if(fixed[side]!=route.velocity)fixed[side]=0u;if(modes[side]!=velocity.mode)modes[side]=2u;}
        if(logical==s->selected_pad){p[1632]=(uint8_t)logical;p[1633]=(uint8_t)n;u16(p,1634,sensor->raw);u16(p,1636,(uint16_t)sensor->signed_delta);
            p[1638]=sensor->pressure;p[1639]=(uint8_t)(sensor->held|(sensor->blocked<<1u)|(sensor->warmup_count>=THY_INSTRUMENT_WARMUP_SAMPLES?4u:0u));
            p[1640]=sensor->strike_velocity;p[1641]=sensor->detector_profile;u16(p,1642,(uint16_t)sensor->attack_raw);u16(p,1644,(uint16_t)sensor->release_raw);u32(p,1646,sensor->last_sample_ms);}
    }
    for(unsigned n=0;n<128u;++n)if(p[THY_COMPANION_NOTES+n/8u]&(1u<<(n&7u)))++p[1619];
    const char *scale=i->user_scale && i->user_scale<=8u?(const char *)i->library.scales[i->user_scale-1u].name:
        e->scale<THY_MENU_DEFAULT_SCALE_COUNT?thy_menu_scale_names[e->scale]:"";
    thy_studio_ui_view_line(state,THY_VIEW_INFO_MUSICAL_NAMES,scale,1,1,(char *)p+THY_COMPANION_HARMONY);
    text(p,THY_COMPANION_SCALE_NAME,31,scale);
    p[1581]=state->group_index;p[1582]=state->level;p[1583]=state->editing;p[1584]=state->notice;
    p[1585]=state->info_open;p[1586]=state->about_open;p[1587]=state->design_editing;p[1588]=e->arp_quick_open;p[1589]=e->sequencer_ui.open;p[1590]=e->sequencer_ui.options_open;
    uint16_t row=thy_state_current_row(state);u16(p,1591,row);u16(p,1593,state->edit_row);p[1595]=state->edit_instance;
    p[1596]=row<THY_MENU_ROW_COUNT?thy_state_current_instance(state,row):UINT8_MAX;
    p[1597]=e->sensitivity_target;p[1598]=e->velocity_target;p[1599]=e->route_target;u32(p,1600,(uint32_t)state->edit_value);
    p[1604]=(uint8_t)(!!e->expression_config.enabled|(e->arps[0].config.enabled?2u:0u)|(e->arps[1].config.enabled?4u:0u)|
        (e->rhythm_transport.state==THY_TRANSPORT_PLAYING?8u:0u)|(e->sequencer.capture.state!=THY_SEQ_CAPTURE_OFF?16u:0u));
    p[1605]=i->performance.root;u16(p,1606,e->scale);p[1610]=i->expression_profiles[0].enabled;p[1611]=i->expression_profiles[1].enabled;
    p[1612]=fixed[0];p[1613]=fixed[1];p[1614]=modes[0];p[1615]=modes[1];u16(p,1620,thy_core_sounding(core));u16(p,1622,thy_core_generated_count(core));
    u32(p,1624,e->musical_clock.tempo_milli_bpm);p[1628]=e->musical_clock.source;p[1629]=e->musical_clock.status;p[1630]=e->rhythm_transport.state;p[1631]=e->sequencer.capture.state;
    u32(p,1650,i->layout_revision);u32(p,1654,s->identity.saved_generation?*s->identity.saved_generation:0u);p[1658]=!!visible;
    p[1659]=state->group_index<THY_MENU_GROUP_COUNT?state->cursors[state->group_index]:0u;
    p[1660]=e->sequencer.selected_track;p[1661]=i->display_view;p[1662]=i->engine_kind;
    int32_t current=INT32_MIN;if(row<THY_MENU_ROW_COUNT)(void)thy_state_get(state,row,&current);u32(p,1664,(uint32_t)current);
    s->crc=crc32(p,THY_COMPANION_SNAPSHOT_BYTES);++s->sequence;s->active=s->captured=1u;s->captured_ms=s->last_ms=now;return 1;
}

size_t thy_companion_command(thy_companion *s,thy_engine *e,int available,uint32_t now,const char *line,char *response,size_t capacity)
{
    if(!s||!e||!e->instrument||!e->instrument->core||!line||!response||capacity<THY_COMPANION_RESPONSE_BYTES)return 0;
    const char *p=line;uint32_t first=0,second=0,third=0;int32_t value=0;writer w={response,0,capacity};
    (void)thy_companion_expire(s,now);add(&w,"COMPANION/1 ");
    if(same(line,"companion caps")){
        add(&w,"CAPS build=");add(&w,s->identity.build?s->identity.build:"unknown");add(&w," uid=");add(&w,s->identity.uid?s->identity.uid:"unknown");
        add(&w," schema=");add(&w,THY_MENU_SCHEMA_SHA256);field(&w," bytes=",THY_COMPANION_SNAPSHOT_BYTES);field(&w," chunk=",THY_COMPANION_CHUNK_BYTES);field(&w," rows=",THY_MENU_ROW_COUNT);field(&w," writable=",!!available);
        add(&w," role=instrument backup=1 song=1 companion=1");
    }else if(same(line,"companion status")){field(&w,"STATUS active=",s->active);field(&w," seq=",s->sequence);field(&w," selected=",s->selected_pad);field(&w," available=",!!available);}
    else if(prefix(&p,"companion release ")&&number(&p,&first)&&!*p){
        if(first!=s->sequence)add(&w,"ERR generation");else{s->active=0u;add(&w,"RELEASED");}
    }else if(!available)add(&w,"ERR busy");
    else if(same(line,"companion snapshot")){
        if(s->active)add(&w,"ERR snapshot-active");
        else if(s->captured&&(uint32_t)(now-s->captured_ms)<THY_COMPANION_MIN_CAPTURE_MS)add(&w,"ERR rate");
        else if(!snapshot(s,e,now))add(&w,"ERR snapshot-unavailable");
        else{field(&w,"SNAPSHOT seq=",s->sequence);field(&w," bytes=",THY_COMPANION_SNAPSHOT_BYTES);add(&w," crc32=");hex(&w,s->crc,8u);}
    }else if((p=line,prefix(&p,"companion get "))&&number(&p,&first)&&space(&p)&&number(&p,&second)&&space(&p)&&number(&p,&third)&&!*p){
        if(!s->active||first!=s->sequence)add(&w,"ERR generation");
        else if(!third||third>THY_COMPANION_CHUNK_BYTES||second>THY_COMPANION_SNAPSHOT_BYTES||third>THY_COMPANION_SNAPSHOT_BYTES-second)add(&w,"ERR offset");
        else{s->last_ms=now;field(&w,"DATA seq=",first);field(&w," offset=",second);add(&w," hex=");for(unsigned n=0;n<third;++n)hex(&w,s->buffer[second+n],2u);}
    }else if((p=line,prefix(&p,"companion pad "))&&number(&p,&first)&&!*p){
        if(first>=96u)add(&w,"ERR pad");else{s->selected_pad=(uint8_t)first;field(&w,"PAD selected=",first);}
    }else if((p=line,prefix(&p,"companion read "))&&schema(&p)&&number(&p,&first)&&space(&p)&&number(&p,&second)&&!*p){
        if(first>=THY_MENU_ROW_COUNT||second>255u)add(&w,"ERR row");
        else{uint8_t instance=second==255u?thy_state_current_instance(&e->state,(uint16_t)first):(uint8_t)second;
            int result=thy_state_get_instance(&e->state,(uint16_t)first,instance,&value);
            if(result!=THY_STATE_OK)add(&w,"ERR read");else{field(&w,"VALUE row=",first);field(&w," instance=",instance);add(&w," value=");signed_dec(&w,value);}}
    }else if((p=line,prefix(&p,"companion set "))&&schema(&p)&&number(&p,&first)&&space(&p)&&number(&p,&second)&&space(&p)&&integer(&p,&value)&&!*p){
        if(first>=THY_MENU_ROW_COUNT||second>=thy_state_instance_count((uint16_t)first)||thy_menu_rows[first].status!=THY_MENU_LIVE||!thy_state_row_visible(&e->state,(uint16_t)first))add(&w,"ERR row");
        else if(e->state.editing)add(&w,"ERR draft-active");
        else{int result=thy_state_set_instance(&e->state,(uint16_t)first,(uint8_t)second,value);add(&w,"SET result=");signed_dec(&w,result);field(&w," row=",first);field(&w," instance=",second);}
    }else if((p=line,prefix(&p,"companion nav "))&&schema(&p)&&number(&p,&first)&&!*p){
        if(first>THY_STATE_RIGHT)add(&w,"ERR action");else{int result=thy_engine_navigate(e,(uint8_t)first);add(&w,"NAV result=");signed_dec(&w,result);}
    }else if((p=line,prefix(&p,"companion control "))&&schema(&p)&&number(&p,&first)&&space(&p)&&number(&p,&second)&&!*p){
        static const uint8_t counts[]={5u,14u,12u,12u};
        if(first>=4u||second>=counts[first])add(&w,"ERR control");
        else if(e->instrument->control_masks[first]&(1u<<second))add(&w,"ERR control-held");
        else{int result=thy_instrument_control(e->instrument,(uint8_t)first,(uint8_t)second,1);
            int released=thy_instrument_control(e->instrument,(uint8_t)first,(uint8_t)second,0);
            add(&w,"CONTROL result=");signed_dec(&w,result==THY_OK?released:result);}
    }else if((p=line,prefix(&p,"companion action "))&&schema(&p)&&number(&p,&first)&&space(&p)&&number(&p,&second)&&!*p){
        if(first>=THY_MENU_ROW_COUNT||thy_menu_rows[first].type!=THY_MENU_ACTION||thy_menu_rows[first].status!=THY_MENU_LIVE||!thy_state_row_visible(&e->state,(uint16_t)first)||second!=thy_state_current_instance(&e->state,(uint16_t)first))add(&w,"ERR row");
        else if(e->state.editing)add(&w,"ERR draft-active");
        else{int result=thy_state_begin_edit(&e->state,(uint16_t)first);add(&w,"ACTION result=");signed_dec(&w,result);}
    }else add(&w,"ERR command-or-schema");
    add(&w,"\r\n");response[w.used]=0;return w.used;
}
