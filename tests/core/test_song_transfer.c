/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_song_transfer.h"

static thy_core core;
static thy_instrument instrument;
static thy_engine engine;
static thy_sequencer donor;
static thy_song_transfer transfer;
static uint8_t buffer[THY_SEQ_SONG_MAX],candidate[THY_SEQ_SONG_MAX],saved_buffer[THY_SEQ_SONG_MAX];
static char response[160],line[256];
static size_t candidate_size;
static unsigned assertions;
#define CHECK(x) do {++assertions;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 0;}}while(0)
static uint32_t crc(const uint8_t *bytes,size_t count)
{uint32_t c=UINT32_MAX;for(size_t n=0;n<count;++n){c^=bytes[n];for(unsigned bit=0;bit<8;++bit)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
static int command(const char *text,uint32_t now,int available,const char *expected)
{
    size_t count=thy_song_transfer_command(&transfer,&engine,available,now,text,response,sizeof(response));
    CHECK(count>2 && count<sizeof(response));CHECK(response[count-2]=='\r' && response[count-1]=='\n' && !response[count]);
    CHECK(strstr(response,expected)==response);return 1;
}
static void init(void)
{
    thy_core_init(&core);thy_instrument_init(&instrument,&core);static thy_seq_event undo_workspace_1[THY_SEQ_EVENTS];
    thy_engine_init(&engine,&instrument,0,70, undo_workspace_1);
    (void)thy_engine_advance(&engine,0);
    thy_seq_event original={.tick=0,.duration=960,.kind=THY_SEQ_NOTE,.pitch=64,.value=90};
    (void)thy_sequencer_add(&engine.sequencer,0,&original,1);
    static thy_seq_event undo_workspace_2[THY_SEQ_EVENTS];
    thy_sequencer_init(&donor, undo_workspace_2);original.pitch=67;original.value=101;
    (void)thy_sequencer_add(&donor,1,&original,1);(void)thy_sequencer_encode(&donor,candidate,sizeof(candidate),&candidate_size);
    memset(buffer,0xa5,sizeof(buffer));thy_song_transfer_init(&transfer,buffer,sizeof(buffer));
}
static int begin(uint32_t crc_value)
{
    (void)snprintf(line,sizeof(line),"song begin %u %u",(unsigned)candidate_size,(unsigned)crc_value);
    return command(line,1,1,"SONG ready bytes=");
}
static int put(unsigned offset,unsigned count,uint32_t now)
{
    int prefix=snprintf(line,sizeof(line),"song put %u ",offset);
    for(unsigned n=0;n<count;++n)(void)snprintf(line+prefix+n*2u,sizeof(line)-prefix-n*2u,"%02x",candidate[offset+n]);
    return command(line,now,1,"SONG received=");
}
static int upload(uint32_t start)
{for(unsigned offset=0;offset<candidate_size;offset+=32u){unsigned count=(unsigned)candidate_size-offset;if(count>32)count=32;CHECK(put(offset,count,start+offset));}return 1;}
static int import_stops_tracks_and_preserves_live_pad(void)
{
    init();int16_t live=60;CHECK(thy_engine_press_shaped(&engine,0,&live,1,0,0,1,96,0)==THY_OK);
    CHECK(thy_engine_transport_start(&engine)==THY_OK);CHECK(core.owners[0].held && engine.sequencer.voice_count==1);
    CHECK(begin(crc(candidate,candidate_size)));CHECK(upload(2));
    CHECK(command("song commit",1000,1,"SONG committed bytes="));
    CHECK(transfer.state==THY_SONG_IDLE && engine.sequencer.count==1 && engine.sequencer.events[0].pitch==67 && engine.sequencer.events[0].track==1);
    CHECK(engine.rhythm_transport.state==THY_TRANSPORT_STOPPED && !engine.sequencer.voice_count);
    CHECK(core.owners[0].held && thy_core_refcount(&core,thy_core_key(0,0,60))==1);
    CHECK(engine.song_save_requested && engine.song_status==1);
    CHECK(thy_instrument_release_pad(&instrument,0)==THY_OK);CHECK(!core.sounding_count);return 1;
}
static int malformed_upload_never_mutates_original(void)
{
    init();CHECK(begin(crc(candidate,candidate_size)));memcpy(saved_buffer,buffer,sizeof(buffer));
    CHECK(command("song put 1 0102",2,1,"ERR song-offset"));CHECK(!transfer.received && !memcmp(buffer,saved_buffer,sizeof(buffer)));
    CHECK(command("song put 0 0",3,1,"ERR song-data"));CHECK(command("song put 0 0g",4,1,"ERR song-data"));
    memset(line,'0',sizeof(line));memcpy(line,"song put 0 ",11);line[11+66]=0;
    CHECK(command(line,5,1,"ERR song-data"));CHECK(!transfer.received);
    CHECK(command("song put 4294967296 00",6,1,"ERR song-command"));
    CHECK(command("song put -1 00",7,1,"ERR song-command"));CHECK(command("song commit",8,1,"ERR song-incomplete"));
    CHECK(engine.sequencer.count==1 && engine.sequencer.events[0].pitch==64);
    CHECK(command("song abort",9,1,"SONG aborted"));CHECK(!transfer.state);return 1;
}
static int crc_and_future_format_rejected_atomically(void)
{
    init();CHECK(begin(crc(candidate,candidate_size)^1u));CHECK(upload(2));CHECK(command("song commit",1000,1,"ERR song-crc-or-format"));
    CHECK(engine.sequencer.events[0].pitch==64 && transfer.state==THY_SONG_UPLOAD);
    CHECK(command("song abort",1001,1,"SONG aborted"));candidate[4]=3;
    CHECK(begin(crc(candidate,candidate_size)));CHECK(upload(2));CHECK(command("song commit",1000,1,"ERR song-crc-or-format"));
    CHECK(engine.sequencer.count==1 && engine.sequencer.events[0].pitch==64);return 1;
}
static int download_pins_snapshot_and_bounds(void)
{
    init();CHECK(command("song export",1,1,"SONG export bytes="));CHECK(transfer.state==THY_SONG_DOWNLOAD);
    memcpy(saved_buffer,buffer,transfer.bytes);uint32_t pinned_crc=transfer.crc;
    CHECK(thy_sequencer_update(&engine.sequencer,engine.sequencer.events[0].id,240,480,72,88)==THY_OK);
    CHECK(command("song get 0 32",2,1,"SONG data offset=0 hex="));CHECK(!memcmp(buffer,saved_buffer,transfer.bytes) && transfer.crc==pinned_crc);
    CHECK(command("song get 0 33",3,1,"ERR song-offset"));CHECK(command("song get 0 0",4,1,"ERR song-offset"));
    (void)snprintf(line,sizeof(line),"song get %u 1",transfer.bytes);CHECK(command(line,5,1,"ERR song-offset"));
    CHECK(command("song get 4294967295 32",6,1,"ERR song-offset"));
    CHECK(command("song export",7,1,"ERR song-busy"));CHECK(command("song abort",8,1,"SONG aborted"));return 1;
}
static int availability_and_capture_race(void)
{
    init();CHECK(command("song export",0,0,"ERR song-busy"));
    (void)snprintf(line,sizeof(line),"song begin %u %u",(unsigned)candidate_size,(unsigned)crc(candidate,candidate_size));
    CHECK(command(line,0,0,"ERR song-busy"));CHECK(!transfer.state);
    CHECK(begin(crc(candidate,candidate_size)));CHECK(upload(2));
    CHECK(command("song commit",1000,0,"ERR song-incomplete"));CHECK(engine.sequencer.events[0].pitch==64);
    engine.sequencer_ui.region_enabled=1;engine.sequencer_ui.region_start=0;engine.sequencer_ui.region_end=960;
    CHECK(thy_engine_sequencer_record(&engine)==THY_OK);CHECK(command("song commit",1001,1,"ERR song-busy"));
    CHECK(engine.sequencer.capture.state==THY_SEQ_ARMED && engine.sequencer.events[0].pitch==64);
    CHECK(command("song abort",1002,1,"SONG aborted"));CHECK(command("song export",1003,1,"ERR song-busy"));return 1;
}
static int abort_timeout_wrap_and_short_commands(void)
{
    init();CHECK(begin(crc(candidate,candidate_size)));CHECK(!thy_song_transfer_expire(&transfer,10000));
    CHECK(thy_song_transfer_expire(&transfer,10001) && !transfer.state);
    CHECK(command("song put 0 00",10002,1,"ERR song-state"));CHECK(command("song export",UINT32_MAX-10u,1,"SONG export bytes="));
    CHECK(!thy_song_transfer_expire(&transfer,5));CHECK(thy_song_transfer_expire(&transfer,10000) && !transfer.state);
    const char *short_commands[]={"","s","song","song ","song begin","song begin ","song begin 536", "song get 0", "song put ", "song put 0"};
    for(unsigned n=0;n<sizeof(short_commands)/sizeof(short_commands[0]);++n)CHECK(command(short_commands[n],11000+n,1,"ERR song-command"));
    CHECK(command("song status",12000,0,"SONG version=1 state=idle"));return 1;
}
static int buffer_and_response_safety(void)
{
    init();thy_song_transfer_init(&transfer,0,sizeof(buffer));CHECK(command("song export",0,1,"ERR song-buffer"));
    (void)snprintf(line,sizeof(line),"song begin %u 1",(unsigned)candidate_size);CHECK(command(line,0,1,"ERR song-buffer"));
    thy_song_transfer_init(&transfer,buffer,32);CHECK(command(line,1,1,"ERR song-size"));
    memset(response,'x',sizeof(response));CHECK(!thy_song_transfer_command(&transfer,&engine,1,0,"song status",response,127));
    CHECK(response[0]=='x' && response[127]=='x');return 1;
}
int main(void)
{
    int(*tests[])(void)={import_stops_tracks_and_preserves_live_pad,malformed_upload_never_mutates_original,
        crc_and_future_format_rejected_atomically,download_pins_snapshot_and_bounds,availability_and_capture_race,
        abort_timeout_wrap_and_short_commands,buffer_and_response_safety};
    for(unsigned n=0;n<sizeof(tests)/sizeof(tests[0]);++n)if(!tests[n]())return 1;
    printf("%u song transfer C tests passed (%u assertions)\n",(unsigned)(sizeof(tests)/sizeof(tests[0])),assertions);return 0;
}
