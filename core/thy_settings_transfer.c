/* SPDX-License-Identifier: GPL-3.0-or-later
 * BACKUP/1, explicit bounded current-state snapshots. No raw-memory, update,
 * reset, note injection or allocation. File header is virtually concatenated
 * with shared journal staging so the largest payload needs no extra RAM copy. */
#include "thy_settings_transfer.h"

typedef struct {char *out;size_t used,capacity;} text;
static void add(text *w,const char *p){while(*p && w->used+1u<w->capacity)w->out[w->used++]=*p++;}
static void number(text *w,uint32_t n){char digits[10];unsigned k=0u;do{digits[k++]=(char)('0'+n%10u);n/=10u;}while(n);while(k){char p[2]={digits[--k],0};add(w,p);}}
static void hex(text *w,uint32_t n,unsigned count){static const char h[]="0123456789abcdef";while(count){char p[2]={h[(n>>(--count*4u))&15u],0};add(w,p);}}
static int same(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static int prefix(const char **p,const char *h){const char *q=*p;while(*h)if(*q++!=*h++)return 0;*p=q;return 1;}
static int decimal(const char **p,uint32_t *out){if(**p<'0'||**p>'9')return 0;uint32_t n=0u;while(**p>='0'&&**p<='9'){unsigned d=(unsigned)(*(*p)++-'0');if(n>(UINT32_MAX-d)/10u)return 0;n=n*10u+d;}*out=n;return 1;}
static int nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n){for(unsigned b=0;b<4u;++b)p[b]=(uint8_t)(n>>(b*8u));}
static unsigned get16(const uint8_t *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
static void put16(uint8_t *p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static int hex_identity(const char *p,unsigned count,uint8_t *out,unsigned retained){
    if(!p)return 0;
    for(unsigned n=0;n<count;++n){if(!p[0]||!p[1])return 0;int a=nibble(*p++),b=nibble(*p++);if(a<0||b<0)return 0;if(n<retained)out[n]=(uint8_t)((unsigned)a*16u+(unsigned)b);}
    return !*p;
}
static uint32_t crc_byte(uint32_t c,uint8_t n){c^=n;for(unsigned b=0;b<8u;++b)c=(c>>1)^((0u-(c&1u))&UINT32_C(0xedb88320));return c;}
static uint8_t file_byte(const thy_settings_transfer *s,uint32_t at){return at<64u?s->header[at]:s->buffer[at-64u];}
static uint32_t file_crc(const thy_settings_transfer *s){uint32_t c=UINT32_MAX;for(uint32_t n=0;n<s->bytes;++n)c=crc_byte(c,file_byte(s,n));return ~c;}
static const char *state_name(unsigned state){static const char *const names[]={"idle","upload","download","applied","queued","saved-verified","error"};return state<7u?names[state]:"error";}
int thy_settings_transfer_staging(const thy_settings_transfer *s){return s && (s->state==THY_BACKUP_UPLOAD||s->state==THY_BACKUP_DOWNLOAD);}
int thy_settings_transfer_busy(const thy_settings_transfer *s){return s && (thy_settings_transfer_staging(s)||s->state==THY_BACKUP_APPLIED||s->state==THY_BACKUP_QUEUED);}
void thy_settings_transfer_init(thy_settings_transfer *s,uint8_t *buffer,size_t capacity,
    const thy_backup_identity *identity,thy_backup_apply_fn apply,void *context)
{if(s){*s=(thy_settings_transfer){.buffer=buffer,.capacity=capacity,.apply=apply,.context=context};if(identity)s->identity=*identity;}}
void thy_settings_transfer_disconnect(thy_settings_transfer *s){if(s){if(s->state==THY_BACKUP_APPLIED||s->state==THY_BACKUP_QUEUED)s->state=THY_BACKUP_ERROR;else s->state=THY_BACKUP_IDLE;}}
int thy_settings_transfer_expire(thy_settings_transfer *s,uint32_t now){
    if(s && thy_settings_transfer_staging(s) && (uint32_t)(now-s->last_ms)>=10000u){s->state=THY_BACKUP_IDLE;return 1;}return 0;
}
static int identity_header(thy_settings_transfer *s){
    for(unsigned n=0;n<64u;++n)s->header[n]=0;
    s->header[0]='T';s->header[1]='H';s->header[2]='Y';s->header[3]='B';
    put16(s->header+4,1u);put16(s->header+6,64u);put32(s->header+8,s->payload_bytes);
    put32(s->header+12,s->payload_crc);put16(s->header+16,THY_SETTINGS_VERSION);
    put16(s->header+18,s->identity.target);put32(s->header+20,s->identity.compatibility);
    if(!hex_identity(s->identity.uid,12u,s->header+24,12u)||
       !hex_identity(s->identity.build,8u,s->header+36,8u)||
       !hex_identity(s->identity.schema,32u,s->header+44,8u))return 0;
    put32(s->header+52,1u);put32(s->header+60,thy_settings_crc32(s->header,60u));return 1;
}
static int file_valid(const thy_settings_transfer *s,const thy_engine *e){
    const uint8_t *h=s->header;uint8_t uid[12];
    if(h[0]!='T'||h[1]!='H'||h[2]!='Y'||h[3]!='B'||get16(h+4)!=1u||get16(h+6)!=64u||
        get16(h+16)!=THY_SETTINGS_VERSION||get16(h+18)!=s->identity.target||
        get32(h+20)!=s->identity.compatibility||get32(h+52)!=1u||get32(h+56)!=0u||
        get32(h+60)!=thy_settings_crc32(h,60u)||get32(h+8)!=s->bytes-64u||
        !hex_identity(s->identity.uid,12u,uid,12u))return 0;
    for(unsigned n=0;n<12u;++n)if(uid[n]!=h[24u+n])return 0;
    size_t bytes=s->bytes-64u;
    return get32(h+12)==thy_settings_crc32(s->buffer,bytes) &&
        thy_settings_payload_version(s->buffer,bytes)==THY_SETTINGS_VERSION &&
        thy_settings_validate(s->buffer,bytes,e->state.scale_count);
}
void thy_settings_transfer_poll(thy_settings_transfer *s,const thy_engine *e,
    const thy_settings_journal *j,int quiet,uint32_t now)
{
    if(!s||!e||!j)return;
    (void)thy_settings_transfer_expire(s,now);
    if(s->state!=THY_BACKUP_APPLIED&&s->state!=THY_BACKUP_QUEUED)return;
    if(j->status==THY_SETTINGS_ERROR||j->status==THY_SETTINGS_UNSUPPORTED){s->state=THY_BACKUP_ERROR;return;}
    s->state=THY_BACKUP_QUEUED;
    if(!quiet||j->writing||j->status!=THY_SETTINGS_SAVED||!j->loaded)return;
    if(j->saved_crc!=s->payload_crc){
        if(j->generation!=s->start_generation)s->state=THY_BACKUP_ERROR;
        return;
    }
    size_t bytes=0u;
    if(!thy_settings_encode(e,s->buffer,s->capacity,&bytes)||bytes!=s->payload_bytes||
        thy_settings_crc32(s->buffer,bytes)!=s->payload_crc||j->saved_crc!=s->payload_crc||
        j->payload_bytes!=s->payload_bytes){s->state=THY_BACKUP_ERROR;return;}
    s->state=THY_BACKUP_SAVED_VERIFIED;
}
size_t thy_settings_transfer_command(thy_settings_transfer *s,const thy_engine *e,
    thy_settings_journal *j,int available,int quiet,uint32_t now,
    const char *line,char *response,size_t capacity)
{
    if(!s||!e||!j||!line||!response||capacity<256u)return 0u;
    text w={response,0u,capacity};const char *p=line;uint32_t first=0u,second=0u;
    (void)thy_settings_transfer_expire(s,now);
    if(same(line,"backup caps")){
        add(&w,"BACKUP version=1 settings=13 max=");number(&w,THY_BACKUP_FILE_MAX);
        add(&w," chunk=32 target=");number(&w,s->identity.target);add(&w," compatibility=");number(&w,s->identity.compatibility);
        add(&w," uid=");add(&w,s->identity.uid?s->identity.uid:"");add(&w," build=");add(&w,s->identity.build?s->identity.build:"");
        add(&w," schema=");add(&w,s->identity.schema?s->identity.schema:"");
    }else if(same(line,"backup status")){
        add(&w,"BACKUP version=1 state=");add(&w,state_name(s->state));
        add(&w," bytes=");number(&w,s->bytes);add(&w," received=");number(&w,s->received);
        add(&w," crc32=");hex(&w,s->crc,8u);add(&w," payload_crc32=");hex(&w,s->payload_crc,8u);
        add(&w," generation=");number(&w,j->generation);
    }else if(same(line,"backup abort")){
        if(s->state==THY_BACKUP_APPLIED||s->state==THY_BACKUP_QUEUED)add(&w,"ERR backup-applied");
        else{s->state=THY_BACKUP_IDLE;j->observed=0u;add(&w,"BACKUP aborted");}
    }else if(same(line,"backup export")){
        size_t bytes=0u;
        if(!available||thy_settings_transfer_busy(s)||!quiet)add(&w,"ERR backup-busy");
        else if(!s->buffer||s->capacity<THY_BACKUP_PAYLOAD_MAX)add(&w,"ERR backup-buffer");
        else if(!thy_settings_encode(e,s->buffer,s->capacity,&bytes))add(&w,"ERR backup-encode");
        else{
            s->payload_bytes=(uint32_t)bytes;s->payload_crc=thy_settings_crc32(s->buffer,bytes);
            if(!identity_header(s))add(&w,"ERR backup-identity");
            else{s->bytes=64u+(uint32_t)bytes;s->received=0u;s->crc=file_crc(s);s->state=THY_BACKUP_DOWNLOAD;s->last_ms=now;
                add(&w,"BACKUP export bytes=");number(&w,s->bytes);add(&w," crc32=");hex(&w,s->crc,8u);add(&w," settings=13");}
        }
    }else if(prefix(&p,"backup begin ")&&decimal(&p,&first)&&*p++==' '&&decimal(&p,&second)&&!*p){
        if(!available||thy_settings_transfer_busy(s)||!quiet||j->status==THY_SETTINGS_ERROR||j->status==THY_SETTINGS_UNSUPPORTED)add(&w,"ERR backup-busy");
        else if(!s->buffer)add(&w,"ERR backup-buffer");
        else if(first<64u+THY_SETTINGS_V13_PARAMETER_BYTES+32u+THY_SEQ_TRACKS*24u||first>THY_BACKUP_FILE_MAX||first-64u>s->capacity)add(&w,"ERR backup-size");
        else{s->bytes=first;s->received=0u;s->crc=second;s->payload_crc=0u;s->payload_bytes=first-64u;s->state=THY_BACKUP_UPLOAD;s->last_ms=now;
            add(&w,"BACKUP ready bytes=");number(&w,first);}
    }else if((p=line,prefix(&p,"backup put "))&&decimal(&p,&first)&&*p++==' '){
        uint8_t bytes[32];unsigned count=0u;int valid=1;
        while(*p&&count<32u){int a=nibble(*p++),b=*p?nibble(*p++):-1;if(a<0||b<0){valid=0;break;}bytes[count++]=(uint8_t)((unsigned)a*16u+(unsigned)b);}
        if(*p||!count||!valid)add(&w,"ERR backup-data");
        else if(!available||s->state!=THY_BACKUP_UPLOAD)add(&w,"ERR backup-state");
        else if(first!=s->received||first>s->bytes||count>s->bytes-first)add(&w,"ERR backup-offset");
        else{for(unsigned n=0;n<count;++n){uint32_t at=first+n;if(at<64u)s->header[at]=bytes[n];else s->buffer[at-64u]=bytes[n];}
            s->received+=count;s->last_ms=now;add(&w,"BACKUP received=");number(&w,s->received);}
    }else if(same(line,"backup commit")){
        if(!available||s->state!=THY_BACKUP_UPLOAD||s->received!=s->bytes)add(&w,"ERR backup-incomplete");
        else if(!quiet||j->status==THY_SETTINGS_ERROR||j->status==THY_SETTINGS_UNSUPPORTED)add(&w,"ERR backup-busy");
        else if(file_crc(s)!=s->crc||!file_valid(s,e))add(&w,"ERR backup-crc-or-format");
        else if(!s->apply||!s->apply(s->context,s->buffer,s->bytes-64u))add(&w,"ERR backup-apply");
        else{s->payload_crc=get32(s->header+12);s->state=THY_BACKUP_APPLIED;s->last_ms=now;s->start_generation=j->generation;j->observed=0u;
            add(&w,"BACKUP applied bytes=");number(&w,s->bytes);add(&w," payload_crc32=");hex(&w,s->payload_crc,8u);}
    }else if((p=line,prefix(&p,"backup get "))&&decimal(&p,&first)&&*p++==' '&&decimal(&p,&second)&&!*p){
        if(!available||s->state!=THY_BACKUP_DOWNLOAD)add(&w,"ERR backup-state");
        else if(!second||second>32u||first>s->bytes||second>s->bytes-first)add(&w,"ERR backup-offset");
        else{s->last_ms=now;add(&w,"BACKUP data offset=");number(&w,first);add(&w," hex=");for(unsigned n=0;n<second;++n)hex(&w,file_byte(s,first+n),2u);}
    }else add(&w,"ERR backup-command");
    add(&w,"\r\n");response[w.used]=0;return w.used;
}
