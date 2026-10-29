/* SPDX-License-Identifier: GPL-3.0-or-later
 * Reuse the real codec/journal fixtures, not a substitute settings model.
 * The older suite's expensive power-cut runner is linked but never invoked. */
#define main settings_fixture_unused_runner
#include "test_settings.c"
#undef main
#include "../../core/thy_settings_transfer.h"

static thy_settings_transfer backup;
static uint8_t backup_file[THY_BACKUP_FILE_MAX], original_file[THY_BACKUP_FILE_MAX];
static char backup_reply[384],backup_line[128];
static uint32_t backup_size;
static unsigned apply_calls;
static const thy_backup_identity identity={"0123456789abcdef01234567","0123456789abcdef",
    THY_MENU_SCHEMA_SHA256,0x413u,THY_BACKUP_COMPATIBILITY};

static int apply_backup(void *context,const uint8_t *bytes,size_t count)
{
    CHECK(context==&engine);++apply_calls;
    return thy_settings_can_apply(&engine,bytes,count)&&thy_settings_apply(&engine,bytes,count);
}
static void setup_backup(void)
{
    defaults();catalog_for_library();memset(flash,255u,sizeof flash);
    thy_settings_io backend=io();CHECK(thy_settings_journal_init(&journal,&backend,70u));
    thy_settings_transfer_init(&backup,journal.record+32u,THY_SETTINGS_PAYLOAD_MAX,&identity,apply_backup,&engine);
    apply_calls=0u;
}
static void request(const char *command,uint32_t now,int available,int quiet,const char *expected)
{
    size_t n=thy_settings_transfer_command(&backup,&engine,&journal,available,quiet,now,
        command,backup_reply,sizeof backup_reply);
    CHECK(n>2u&&n<sizeof backup_reply&&backup_reply[n-2u]=='\r'&&backup_reply[n-1u]=='\n'&&!backup_reply[n]);
    if(strstr(backup_reply,expected)!=backup_reply)fprintf(stderr,"%s -> %s (expected %s)\n",command,backup_reply,expected);
    CHECK(strstr(backup_reply,expected)==backup_reply);
}
static void export_file(void)
{
    request("backup export",1u,1,1,"BACKUP export bytes=");
    CHECK(backup.state==THY_BACKUP_DOWNLOAD&&backup.bytes<=sizeof backup_file);
    backup_size=backup.bytes;memcpy(backup_file,backup.header,64u);
    memcpy(backup_file+64u,backup.buffer,backup.bytes-64u);
    CHECK(thy_settings_crc32(backup_file,backup_size)==backup.crc);
    CHECK(get32(backup_file+60u)==thy_settings_crc32(backup_file,60u));
    request("backup abort",2u,1,1,"BACKUP aborted");
}
static void begin_file(uint32_t crc_value)
{
    (void)snprintf(backup_line,sizeof backup_line,"backup begin %u %u",backup_size,crc_value);
    request(backup_line,10u,1,1,"BACKUP ready bytes=");
}
static void send_file(void)
{
    for(uint32_t at=0u;at<backup_size;at+=32u){
        unsigned count=backup_size-at;if(count>32u)count=32u;
        int prefix=snprintf(backup_line,sizeof backup_line,"backup put %u ",at);
        for(unsigned n=0;n<count;++n)(void)snprintf(backup_line+prefix+n*2u,
            sizeof backup_line-(unsigned)prefix-n*2u,"%02x",backup_file[at+n]);
        request(backup_line,20u+at/32u,1,1,"BACKUP received=");
    }
}
static void upload_file(void)
{begin_file(thy_settings_crc32(backup_file,backup_size));send_file();}
static void advertised_version_matches_exported_codec(void)
{
    ++scenarios;setup_backup();
    /* Exercise a current-only value, rather than just exporting old defaults. */
    instrument.voicing_mode=4u;
    char expected[48];
    (void)snprintf(expected,sizeof expected,"BACKUP version=1 settings=%u max=",THY_SETTINGS_VERSION);
    request("backup caps",0u,1,1,expected);
    request("backup export",1u,1,1,"BACKUP export bytes=");
    (void)snprintf(expected,sizeof expected," settings=%u\r\n",THY_SETTINGS_VERSION);
    CHECK(strstr(backup_reply,expected)!=NULL);
    CHECK(backup.header[16u]==THY_SETTINGS_VERSION&&backup.header[17u]==0u);
    CHECK(thy_settings_payload_version(backup.buffer,backup.payload_bytes)==THY_SETTINGS_VERSION);
    CHECK(thy_settings_validate(backup.buffer,backup.payload_bytes,70u));
    request("backup abort",2u,1,1,"BACKUP aborted");
}
static void full_current_state_and_verified_save(void)
{
    ++scenarios;setup_backup();seed_library();
    instrument.voicing_mode=4u;
    const thy_instrument_response left={820u,797u,1353u,4095u,1u},right={1100u,1060u,900u,3000u,2u};
    const thy_instrument_velocity velocity={3u,100u,THY_VELOCITY_STRIKE,1u,2u,120u};
    const thy_instrument_release_policy lp={23u,1u},rp={40u,1u};
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,0u,&left,&velocity,&lp)==THY_OK);
    CHECK(thy_instrument_set_cluster_sensitivity_policy(&instrument,1u,&right,&velocity,&rp)==THY_OK);
    CHECK(thy_instrument_set_pad_response(&instrument,95u,&left)==THY_OK);
    CHECK(thy_instrument_set_pad_velocity(&instrument,48u,&velocity)==THY_OK);
    instrument.sensitivity_guard[0]=600u;instrument.sensitivity_guard[95]=900u;
    for(unsigned n=0;n<THY_INSTRUMENT_ROUTE_TARGETS;++n)
        engine.channel_routes[n]=(thy_engine_channel_config){(uint8_t)(n%4u),(uint8_t)(n%3u),(uint8_t)(24u-n)};
    instrument.expression_profiles[0]=(thy_expression_profile){1u,65u,1u,2u};
    instrument.expression_profiles[1]=(thy_expression_profile){1u,80u,2u,1u};
    for(unsigned n=0;n<THY_PAD_COUNT;++n){
        instrument.pad_expression_profiles[n]=(thy_expression_profile){(uint8_t)(n%2u),(uint8_t)(10u+n%81u),(uint8_t)(n%3u),(uint8_t)((n+1u)%3u)};
        instrument.expression_profile_overridden[n]=(uint8_t)(n%4u==0u);
    }
    thy_engine_bind_expression(&engine,&expression);
    thy_mpe_config mpe=engine.expression_config;mpe.enabled=1u;mpe.members=8u;mpe.glide=1u;
    CHECK(thy_engine_expression_configure(&engine,&mpe)==THY_OK);
    for(unsigned n=0;n<THY_SEQ_EVENTS;++n){
        thy_seq_event note={.tick=n*120u,.duration=60u,.kind=THY_SEQ_NOTE,.pitch=(uint8_t)(36u+n%48u),.value=93u};
        CHECK(thy_sequencer_add(&engine.sequencer,0u,&note,1u)==THY_OK);
    }
    export_file();CHECK(backup_size==THY_BACKUP_FILE_MAX);
    CHECK(get32(backup_file+8u)==THY_BACKUP_PAYLOAD_MAX);
    setup_backup();thy_engine_bind_expression(&engine,&expression);upload_file();
    request("backup commit",1000u,1,1,"BACKUP applied bytes=");
    CHECK(backup.state==THY_BACKUP_APPLIED&&apply_calls==1u&&thy_settings_transfer_busy(&backup));
    CHECK(encode(roundtrip)==backup_size-64u&&!memcmp(roundtrip,backup_file+64u,backup_size-64u));
    CHECK(!core.queue_count&&!core.sounding_count&&!core.generated_count&&thy_mpe_quiet(&expression));
    CHECK(engine.expression_config.enabled&&engine.expression_config.glide&&engine.sequencer.count==512u);
    CHECK(instrument.voicing_mode==4u);
    CHECK(instrument.sensitivity_guard[95]==900u&&instrument.library.chords[23].used);
    request("backup abort",1001u,1,1,"ERR backup-applied");
    thy_settings_transfer_poll(&backup,&engine,&journal,1,1001u);CHECK(backup.state==THY_BACKUP_QUEUED);
    for(unsigned n=0;n<7000u&&backup.state!=THY_BACKUP_SAVED_VERIFIED;++n){
        thy_settings_journal_task(&journal,&engine,1002u+n,1);
        thy_settings_transfer_poll(&backup,&engine,&journal,1,1002u+n);
    }
    CHECK(backup.state==THY_BACKUP_SAVED_VERIFIED&&!thy_settings_transfer_busy(&backup));
    CHECK(journal.loaded&&journal.status==THY_SETTINGS_SAVED&&journal.saved_crc==get32(backup_file+12u));
    CHECK(!core.queue_count&&!core.sounding_count);
    request("backup status",30000u,0,0,"BACKUP version=1 state=saved-verified");
    thy_settings_io backend=io();CHECK(thy_settings_journal_init(&rebooted,&backend,70u));
    CHECK(rebooted.payload_bytes==backup_size-64u&&!memcmp(rebooted.record+32u,backup_file+64u,backup_size-64u));
}
static void rejected_bytes_are_atomic(void)
{
    ++scenarios;setup_backup();export_file();memcpy(original_file,backup_file,backup_size);
    const unsigned offsets[]={0u,4u,6u,8u,16u,18u,20u,24u,52u,56u,60u,64u+6u,64u+THY_SETTINGS_V12_LIBRARY_OFFSET};
    for(unsigned n=0;n<sizeof offsets/sizeof offsets[0];++n){
        memcpy(backup_file,original_file,backup_size);backup_file[offsets[n]]^=0x80u;
        if(offsets[n]>=64u)put32(backup_file+12u,thy_settings_crc32(backup_file+64u,backup_size-64u));
        if(offsets[n]<60u||offsets[n]>=64u)put32(backup_file+60u,thy_settings_crc32(backup_file,60u));
        setup_backup();size_t before=encode(payload);upload_file();
        request("backup commit",1000u,1,1,"ERR backup-crc-or-format");
        CHECK(!apply_calls&&encode(roundtrip)==before&&!memcmp(payload,roundtrip,before));
    }
    memcpy(backup_file,original_file,backup_size);setup_backup();begin_file(thy_settings_crc32(backup_file,backup_size)^1u);send_file();
    request("backup commit",1000u,1,1,"ERR backup-crc-or-format");CHECK(!apply_calls);
}
static void restore_requires_current_agreeing_header_and_payload(void)
{
    ++scenarios;setup_backup();export_file();memcpy(original_file,backup_file,backup_size);
    uint32_t original_size=backup_size;
    const unsigned versions[][2]={
        {13u,13u},{14u,14u},{15u,15u},{16u,16u},{17u,17u},{14u,THY_SETTINGS_VERSION},
        {THY_SETTINGS_VERSION,14u},{THY_SETTINGS_VERSION+1u,THY_SETTINGS_VERSION+1u},
        {THY_SETTINGS_VERSION,THY_SETTINGS_VERSION+1u},{THY_SETTINGS_VERSION+1u,THY_SETTINGS_VERSION}
    };
    for(unsigned n=0;n<sizeof versions/sizeof versions[0];++n){
        backup_size=original_size;memcpy(backup_file,original_file,backup_size);
        backup_file[16u]=(uint8_t)versions[n][0];backup_file[17u]=(uint8_t)(versions[n][0]>>8u);
        backup_file[64u+6u]=(uint8_t)versions[n][1];
        if(versions[n][1]>=13u&&versions[n][1]<=17u){
            unsigned prefix=versions[n][1]==17u?THY_SETTINGS_V17_PARAMETER_BYTES:versions[n][1]==16u?THY_SETTINGS_V16_PARAMETER_BYTES:THY_SETTINGS_V15_PARAMETER_BYTES;
            memmove(backup_file+64u+prefix,
                backup_file+64u+THY_SETTINGS_V18_PARAMETER_BYTES,
                backup_size-64u-THY_SETTINGS_V18_PARAMETER_BYTES);
            backup_size-=THY_SETTINGS_V18_PARAMETER_BYTES-prefix;
            put32(backup_file+8u,backup_size-64u);
        }
        put32(backup_file+12u,thy_settings_crc32(backup_file+64u,backup_size-64u));
        put32(backup_file+60u,thy_settings_crc32(backup_file,60u));
        /* Supported old codecs are deliberately not applied directly on wire.
         * The host must migrate them using shared C before upload. */
        if(versions[n][1]<THY_SETTINGS_VERSION)
            CHECK(thy_settings_validate(backup_file+64u,backup_size-64u,70u));
        setup_backup();size_t before=encode(payload);upload_file();
        request("backup commit",1000u,1,1,"ERR backup-crc-or-format");
        CHECK(!apply_calls&&backup.state==THY_BACKUP_UPLOAD);
        CHECK(encode(roundtrip)==before&&!memcmp(payload,roundtrip,before));
    }
}
static void changed_applied_settings_cannot_claim_saved_verification(void)
{
    ++scenarios;setup_backup();export_file();upload_file();
    request("backup commit",1000u,1,1,"BACKUP applied bytes=");
    CHECK(backup.state==THY_BACKUP_APPLIED);
    /* A generation advance with a different payload cannot prove this restore. */
    instrument.fixed_velocity=(uint8_t)(instrument.fixed_velocity==80u?81u:80u);
    for(unsigned n=0;n<7000u&&backup.state!=THY_BACKUP_ERROR;++n){
        thy_settings_journal_task(&journal,&engine,1002u+n,1);
        thy_settings_transfer_poll(&backup,&engine,&journal,1,1002u+n);
    }
    CHECK(backup.state==THY_BACKUP_ERROR&&!thy_settings_transfer_busy(&backup));
    CHECK(journal.loaded&&journal.saved_crc!=get32(backup_file+12u));
}
static void bounds_busy_and_quiet_gates(void)
{
    ++scenarios;setup_backup();export_file();size_t before=encode(payload);
    request("backup export",1u,0,1,"ERR backup-busy");request("backup export",1u,1,0,"ERR backup-busy");
    request("backup begin 4294967295 0",1u,1,1,"ERR backup-size");
    request("backup begin 4294967296 0",1u,1,1,"ERR backup-command");
    request("backup begin 64 0",1u,1,1,"ERR backup-size");
    upload_file();request("backup put 0 00",1000u,1,1,"ERR backup-offset");
    request("backup put 0 0g",1000u,1,1,"ERR backup-data");
    request("backup commit",1000u,0,1,"ERR backup-incomplete");
    request("backup commit",1000u,1,0,"ERR backup-busy");
    instrument.input_enabled=1u;request("backup commit",1000u,1,1,"ERR backup-apply");instrument.input_enabled=0u;
    CHECK(encode(roundtrip)==before&&!memcmp(payload,roundtrip,before));
    core.sounding_count=1u;request("backup commit",1000u,1,1,"ERR backup-apply");core.sounding_count=0u;
    CHECK(encode(roundtrip)==before&&!memcmp(payload,roundtrip,before));
    request("backup abort",1001u,1,1,"BACKUP aborted");
    request("backup get 0 32",1002u,1,1,"ERR backup-state");
    request("backup export",1003u,1,1,"BACKUP export bytes=");
    request("backup get 0 33",1004u,1,1,"ERR backup-offset");
    request("backup get 4294967295 32",1004u,1,1,"ERR backup-offset");
    request("backup get 0 32",1004u,0,1,"ERR backup-state");
    request("backup get 0 32",1004u,1,1,"BACKUP data offset=0 hex=54485942");
}
static void expiration_disconnect_and_save_errors_release(void)
{
    ++scenarios;setup_backup();export_file();begin_file(thy_settings_crc32(backup_file,backup_size));
    CHECK(!thy_settings_transfer_expire(&backup,10009u));CHECK(thy_settings_transfer_expire(&backup,10010u));
    CHECK(!thy_settings_transfer_busy(&backup));
    request("backup export",UINT32_MAX-10u,1,1,"BACKUP export bytes=");
    CHECK(!thy_settings_transfer_expire(&backup,5u));CHECK(thy_settings_transfer_expire(&backup,10000u));
    upload_file();request("backup commit",1000u,1,1,"BACKUP applied bytes=");
    journal.status=THY_SETTINGS_ERROR;thy_settings_transfer_poll(&backup,&engine,&journal,1,1001u);
    CHECK(backup.state==THY_BACKUP_ERROR&&!thy_settings_transfer_busy(&backup));
    setup_backup();upload_file();request("backup commit",1000u,1,1,"BACKUP applied bytes=");
    thy_settings_transfer_disconnect(&backup);CHECK(backup.state==THY_BACKUP_ERROR&&!thy_settings_transfer_busy(&backup));
    setup_backup();begin_file(thy_settings_crc32(backup_file,backup_size));thy_settings_transfer_disconnect(&backup);
    CHECK(backup.state==THY_BACKUP_IDLE&&!thy_settings_transfer_staging(&backup));
    char out[8];memset(out,'x',sizeof out);
    CHECK(!thy_settings_transfer_command(&backup,&engine,&journal,1,1,0u,"backup caps",out,sizeof out)&&out[0]=='x');
}
static void source_build_and_schema_are_provenance_not_update_locks(void)
{
    ++scenarios;setup_backup();export_file();backup_file[36u]^=0x55u;backup_file[44u]^=0x33u;
    put32(backup_file+60u,thy_settings_crc32(backup_file,60u));
    setup_backup();upload_file();request("backup commit",1000u,1,1,"BACKUP applied bytes=");
    CHECK(backup.state==THY_BACKUP_APPLIED&&apply_calls==1u);
    CHECK(encode(roundtrip)==backup_size-64u&&!memcmp(roundtrip,backup_file+64u,backup_size-64u));
}
int main(void)
{
    advertised_version_matches_exported_codec();
    full_current_state_and_verified_save();rejected_bytes_are_atomic();bounds_busy_and_quiet_gates();
    restore_requires_current_agreeing_header_and_payload();
    changed_applied_settings_cannot_claim_saved_verification();
    expiration_disconnect_and_save_errors_release();
    source_build_and_schema_are_provenance_not_update_locks();
    printf("%u settings transfer C tests passed (%u assertions)\n",scenarios,checks);return 0;
}
