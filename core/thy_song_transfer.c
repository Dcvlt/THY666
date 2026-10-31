/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "thy_song_transfer.h"
#include "thy_crc32.h"
typedef struct { char *out; size_t used,capacity; } text;
static void add(text *w,const char *p){while(*p && w->used+1u<w->capacity)w->out[w->used++]=*p++;}
static void decimal(text *w,uint32_t n){char digits[10];unsigned k=0u;do{digits[k++]=(char)('0'+n%10u);n/=10u;}while(n);while(k){char p[2]={digits[--k],0};add(w,p);}}
static void hex(text *w,uint32_t n,unsigned count){static const char h[]="0123456789abcdef";while(count){char p[2]={h[(n>>(--count*4u))&15u],0};add(w,p);}}
static int same(const char *a,const char *b){while(*a && *a==*b){++a;++b;}return *a==*b;}
static int prefix(const char **p,const char *head){const char *q=*p;while(*head)if(*q++!=*head++)return 0;*p=q;return 1;}
static int number(const char **p,uint32_t *out){if(**p<'0'||**p>'9')return 0;uint32_t n=0u;while(**p>='0'&&**p<='9'){unsigned d=(unsigned)(*(*p)++-'0');if(n>(UINT32_MAX-d)/10u)return 0;n=n*10u+d;}*out=n;return 1;}
static int nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
void thy_song_transfer_init(thy_song_transfer *s,uint8_t *buffer,size_t capacity){if(s)*s=(thy_song_transfer){.buffer=buffer,.capacity=capacity};}
int thy_song_transfer_expire(thy_song_transfer *s,uint32_t now){if(s && s->state && (uint32_t)(now-s->last_ms)>=10000u){s->state=THY_SONG_IDLE;return 1;}return 0;}
size_t thy_song_transfer_command(thy_song_transfer *s,thy_engine *e,int available,
    uint32_t now,const char *line,char *response,size_t capacity)
{
    if(!s || !e || !line || !response || capacity<128u)return 0u;
    text w={response,0u,capacity};uint32_t first=0u,second=0u;const char *p=line;
    (void)thy_song_transfer_expire(s,now);
    if(same(line,"song status")) {
        add(&w,"SONG version=1 state=");add(&w,s->state==THY_SONG_UPLOAD?"upload":s->state==THY_SONG_DOWNLOAD?"download":"idle");
        add(&w," bytes=");decimal(&w,s->bytes);add(&w," received=");decimal(&w,s->received);add(&w," crc32=");hex(&w,s->crc,8u);
    } else if(same(line,"song abort")) {s->state=THY_SONG_IDLE;add(&w,"SONG aborted");}
    else if(prefix(&p,"song begin ") && number(&p,&first) && *p++==' ' && number(&p,&second) && !*p) {
        if(!available || s->state || e->sequencer.capture.state!=THY_SEQ_CAPTURE_OFF) add(&w,"ERR song-busy");
        else if(!s->buffer) add(&w,"ERR song-buffer");
        else if(first<32u+THY_SEQ_TRACKS*24u || first>THY_SEQ_SONG_MAX || first>s->capacity) add(&w,"ERR song-size");
        else {s->state=THY_SONG_UPLOAD;s->bytes=first;s->received=0u;s->crc=second;s->last_ms=now;add(&w,"SONG ready bytes=");decimal(&w,first);}
    } else if((p=line,prefix(&p,"song put ")) && number(&p,&first) && *p++==' ') {
        uint8_t bytes[32];unsigned count=0u;int valid=1;
        while(*p && count<sizeof bytes){int a=nibble(*p++);int b=*p?nibble(*p++):-1;if(a<0||b<0){valid=0;break;}bytes[count++]=(uint8_t)((unsigned)a*16u+(unsigned)b);}
        if(*p || !count || !valid) add(&w,"ERR song-data");
        else if(s->state!=THY_SONG_UPLOAD || !available) add(&w,"ERR song-state");
        else if(first!=s->received || first>s->bytes || count>s->bytes-first) add(&w,"ERR song-offset");
        else {for(unsigned n=0;n<count;++n)s->buffer[first+n]=bytes[n];s->received+=count;s->last_ms=now;add(&w,"SONG received=");decimal(&w,s->received);}
    } else if(same(line,"song commit")) {
        if(!available || s->state!=THY_SONG_UPLOAD || s->received!=s->bytes) add(&w,"ERR song-incomplete");
        else if(e->sequencer.capture.state!=THY_SEQ_CAPTURE_OFF) add(&w,"ERR song-busy");
        else if(thy_crc32(s->buffer,s->bytes)!=s->crc || thy_sequencer_validate(s->buffer,s->bytes)!=THY_OK) add(&w,"ERR song-crc-or-format");
        else if(thy_engine_sequencer_song_import(e,s->buffer,s->bytes)!=THY_OK) add(&w,"ERR song-import");
        else {s->state=THY_SONG_IDLE;e->song_save_requested=1u;e->song_status=1u;add(&w,"SONG committed bytes=");decimal(&w,s->bytes);}
    } else if(same(line,"song export")) {
        size_t bytes=0u;
        if(!available || s->state || e->sequencer.capture.state!=THY_SEQ_CAPTURE_OFF) add(&w,"ERR song-busy");
        else if(!s->buffer) add(&w,"ERR song-buffer");
        else if(thy_sequencer_encode(&e->sequencer,s->buffer,s->capacity,&bytes)!=THY_OK) add(&w,"ERR song-encode");
        else {s->state=THY_SONG_DOWNLOAD;s->bytes=(uint32_t)bytes;s->received=0u;s->crc=thy_crc32(s->buffer,bytes);s->last_ms=now;add(&w,"SONG export bytes=");decimal(&w,s->bytes);add(&w," crc32=");hex(&w,s->crc,8u);}
    } else if((p=line,prefix(&p,"song get ")) && number(&p,&first) && *p++==' ' && number(&p,&second) && !*p) {
        if(s->state!=THY_SONG_DOWNLOAD || !available) add(&w,"ERR song-state");
        else if(!second || second>32u || first>s->bytes || second>s->bytes-first) add(&w,"ERR song-offset");
        else {s->last_ms=now;add(&w,"SONG data offset=");decimal(&w,first);add(&w," hex=");for(unsigned n=0;n<second;++n)hex(&w,s->buffer[first+n],2u);}
    } else add(&w,"ERR song-command");
    add(&w,"\r\n");response[w.used]=0;return w.used;
}
