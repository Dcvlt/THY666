/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <string.h>
#include "thy_harmony.h"
#include "thy_chord_layout.h"

static unsigned assertions;
#define CHECK(x) do { ++assertions; if(!(x)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);return 0; } } while(0)

static int named(const int16_t *notes,size_t count,uint8_t root,const char *expected)
{
    thy_harmony harmony;char text[THY_HARMONY_TEXT_BYTES];
    CHECK(thy_harmony_from_notes(notes,count,root,&harmony)==THY_HARMONY_OK);
    CHECK(thy_harmony_format(&harmony,text,sizeof text)==THY_HARMONY_OK);
    CHECK(!strcmp(text,expected));return 1;
}

static int basic_chords_notes_and_octaves_are_distinct(void)
{
    CHECK(named(NULL,0,THY_HARMONY_NO_ROOT,"READY"));
    const int16_t zero[]={0},sharp[]={1},middle[]={60},highest[]={127};
    CHECK(named(zero,1,THY_HARMONY_NO_ROOT,"C-1"));
    CHECK(named(sharp,1,THY_HARMONY_NO_ROOT,"C#-1"));
    CHECK(named(middle,1,THY_HARMONY_NO_ROOT,"C4"));
    CHECK(named(highest,1,THY_HARMONY_NO_ROOT,"G9"));
    const int16_t octaves[]={0,12,60,72,72};
    CHECK(named(octaves,5,THY_HARMONY_NO_ROOT,"C OCTAVES"));
    const int16_t major[]={60,64,67},minor[]={60,63,67},dominant[]={60,64,67,70};
    const int16_t maj7[]={60,64,67,71},min7[]={60,63,67,70},dim[]={60,63,66};
    CHECK(named(major,3,0,"C MAJ"));CHECK(named(minor,3,0,"C MIN"));
    CHECK(named(dominant,4,0,"C 7"));CHECK(named(maj7,4,0,"C MAJ7"));
    CHECK(named(min7,4,0,"C MIN7"));CHECK(named(dim,3,0,"C DIM"));
    const int16_t power[]={60,67};CHECK(named(power,2,0,"C 5"));
    const int16_t unknown[]={60,61,62};CHECK(named(unknown,3,0,"3 NOTES"));return 1;
}

static int source_root_and_actual_bass_survive_inversion_drop_and_layers(void)
{
    const int16_t inversions[][3]={{60,64,67},{64,67,72},{67,72,76},{60,52,55}};
    const char *expected[]={"C MAJ","C MAJ/E","C MAJ/G","C MAJ/E"};
    for(unsigned n=0;n<4u;++n)CHECK(named(inversions[n],3,0,expected[n]));
    const int16_t layers[]={48,52,55,60,64,67,72,76,79,60,64,67};
    thy_harmony result;
    CHECK(thy_harmony_from_notes(layers,12,0,&result)==THY_HARMONY_OK);
    CHECK(result.notes==9u && result.classes==3u && result.root==0u && result.bass==0u);
    CHECK(!(result.flags&THY_HARMONY_SLASH_BASS));
    CHECK(named(layers,12,0,"C MAJ"));
    const int16_t authored_no5[]={60,64,70};
    CHECK(named(authored_no5,3,0,"3 NOTES")); /* No invented nearest C7. */
    const int16_t actual_min_shell[]={60,63,70};
    CHECK(named(actual_min_shell,3,0,"C MIN7 NO5"));return 1;
}

static int ambiguity_is_deterministic_and_context_is_explicit(void)
{
    const int16_t c6[]={60,64,67,69};thy_harmony result;
    CHECK(thy_harmony_from_notes(c6,4,THY_HARMONY_NO_ROOT,&result)==THY_HARMONY_OK);
    CHECK(result.kind==THY_HARMONY_CHORD && result.matches>1u);
    CHECK(result.confidence==THY_HARMONY_CONFIDENCE_AMBIGUOUS && result.root==0u);
    CHECK(named(c6,4,0,"C 6"));CHECK(named(c6,4,9,"A MIN7/C"));
    CHECK(thy_harmony_from_notes(c6,4,9,&result)==THY_HARMONY_OK);
    CHECK(result.confidence==THY_HARMONY_CONFIDENCE_CONTEXT && (result.flags&THY_HARMONY_ROOT_USED));
    const int16_t am7[]={57,60,64,67};CHECK(named(am7,4,THY_HARMONY_NO_ROOT,"A MIN7"));
    const int16_t aug[]={60,64,68};CHECK(named(aug,3,THY_HARMONY_NO_ROOT,"C AUG"));
    CHECK(named(aug,3,4,"E AUG/C"));
    const int16_t major[]={60,64,67};
    CHECK(thy_harmony_from_notes(major,3,THY_HARMONY_NO_ROOT,&result)==THY_HARMONY_OK);
    CHECK(result.kind==THY_HARMONY_CHORD && result.matches==1u &&
        result.confidence==THY_HARMONY_CONFIDENCE_EXACT);
    CHECK(thy_harmony_from_notes(major,3,2,&result)==THY_HARMONY_OK);
    CHECK(result.root==0u && (result.flags&THY_HARMONY_ROOT_REJECTED) &&
        !(result.flags&THY_HARMONY_ROOT_USED));return 1;
}

static int routing_duplicates_do_not_change_harmony_or_note_counts(void)
{
    uint16_t keys[3u*16u*3u];size_t n=0u;
    const uint8_t pitches[]={60,64,67};
    for(unsigned destination=0;destination<3u;++destination)
        for(unsigned channel=0;channel<16u;++channel)
            for(unsigned pitch=0;pitch<3u;++pitch)
                keys[n++]=(uint16_t)(((destination*16u+channel)*128u)+pitches[pitch]);
    thy_harmony result;
    CHECK(thy_harmony_from_keys(keys,n,0,&result)==THY_HARMONY_OK);
    CHECK(result.notes==3u && result.classes==3u && result.lowest==60u && result.highest==67u);
    char text[THY_HARMONY_TEXT_BYTES];
    CHECK(thy_harmony_format(&result,text,sizeof text)==THY_HARMONY_OK);
    CHECK(!strcmp(text,"C MAJ"));
    const int16_t source[]={67,60,64,60,67};
    thy_harmony actual;
    CHECK(thy_harmony_from_notes(source,5,0,&actual)==THY_HARMONY_OK);
    CHECK(!memcmp(&actual,&result,sizeof result));return 1;
}

static int actual_rootless_and_mislabelled_archive_edges_are_not_repeated(void)
{
    /* All eight actual records whose authored Bb root is absent. Seven
     * distinct archive records share the second pitch list. No missing Bb
     * may be manufactured just because metadata called this Bb7.... */
    const int16_t first[]={57,60,63,66,71},second[]={57,59,62,64,73};
    for(unsigned n=0;n<8u;++n) {
        thy_harmony result;
        CHECK(thy_harmony_from_notes(n?second:first,5,10,&result)==THY_HARMONY_OK);
        CHECK(!(result.pitch_classes&(1u<<10u)));
        CHECK(result.flags&THY_HARMONY_ROOT_REJECTED);
        CHECK(!(result.flags&THY_HARMONY_ROOT_USED));
        CHECK(result.root!=10u && result.confidence!=THY_HARMONY_CONFIDENCE_CONTEXT);
    }
    const int16_t not_maj7[]={60,61,63,67};
    CHECK(named(not_maj7,4,0,"C MIN ADDb9"));
    const int16_t flat5_not_sharp5[]={60,64,66,70};
    CHECK(named(flat5_not_sharp5,4,0,"C 7 b5"));
    const int16_t not_dim7[]={60,63,66,68};
    CHECK(named(not_dim7,4,0,"C DIM ADDb6"));
    const int16_t suspended_six[]={60,62,65,67,69};
    CHECK(named(suspended_six,5,0,"C 6 SUS4 ADD9"));return 1;
}

static int results_are_independent_source_presentations_without_global_scratch(void)
{
    const int16_t c[]={60,64,67},f[]={65,69,72};
    thy_harmony first,second;char text[THY_HARMONY_TEXT_BYTES];
    CHECK(thy_harmony_from_notes(c,3,0,&first)==THY_HARMONY_OK);
    CHECK(thy_harmony_from_notes(f,3,5,&second)==THY_HARMONY_OK);
    CHECK(thy_harmony_format(&second,text,sizeof text)==THY_HARMONY_OK && !strcmp(text,"F MAJ"));
    CHECK(thy_harmony_format(&first,text,sizeof text)==THY_HARMONY_OK && !strcmp(text,"C MAJ"));
    /* An Arp input set names a whole phrase regardless of its current gate. */
    const uint64_t pool[]={UINT64_C(1)<<60u,(UINT64_C(1)<<0u)|(UINT64_C(1)<<3u)};
    thy_harmony phrase;
    CHECK(thy_harmony_recognize(pool,0,&phrase)==THY_HARMONY_OK);
    CHECK(!memcmp(&phrase,&first,sizeof first));
    CHECK(thy_harmony_recognize(pool,0,&phrase)==THY_HARMONY_OK);
    CHECK(thy_harmony_format(&phrase,text,sizeof text)==THY_HARMONY_OK && !strcmp(text,"C MAJ"));
    return 1;
}

static int generated_quality_rows_have_exact_names_for_every_chromatic_root(void)
{
    const int16_t degrees[]={0,2,4,5,7,9,11};
    for(uint8_t quality=0;quality<THY_CHORD_QUALITY_COUNT;++quality)
        for(uint8_t root=0;root<12u;++root)
            for(uint8_t row=0;row<4u;++row) {
                thy_chord_cell cell;int16_t notes[7];size_t count=0u;
                CHECK(thy_chord_layout_resolve(THY_CHORD_LAYOUT_CHROMATIC,quality,row,
                    (int16_t)(60u+root),degrees,7,NULL,&cell)==THY_OK);
                for(unsigned n=0;n<7u;++n)if(cell.valid_mask&(1u<<n))notes[count++]=cell.notes[n];
                thy_harmony result;char text[THY_HARMONY_TEXT_BYTES];
                CHECK(thy_harmony_from_notes(notes,count,root,&result)==THY_HARMONY_OK);
                CHECK(result.kind==THY_HARMONY_CHORD && result.root==root);
                CHECK(result.confidence==THY_HARMONY_CONFIDENCE_CONTEXT &&
                    (result.flags&THY_HARMONY_ROOT_USED));
                CHECK(thy_harmony_format(&result,text,sizeof text)==THY_HARMONY_OK);
            }
    return 1;
}

static int invalid_input_and_short_buffers_leave_outputs_unchanged(void)
{
    thy_harmony result,before;memset(&result,0x5a,sizeof result);before=result;
    const int16_t invalid[][2]={{60,-1},{60,128}};
    for(unsigned n=0;n<2u;++n) {
        CHECK(thy_harmony_from_notes(invalid[n],2,0,&result)==THY_HARMONY_INVALID);
        CHECK(!memcmp(&result,&before,sizeof result));
    }
    const uint16_t bad_keys[]={60,6144};
    CHECK(thy_harmony_from_keys(bad_keys,2,0,&result)==THY_HARMONY_INVALID);
    CHECK(!memcmp(&result,&before,sizeof result));
    CHECK(thy_harmony_recognize(NULL,0,&result)==THY_HARMONY_INVALID);
    const uint64_t bits[]={1u,0u};
    CHECK(thy_harmony_recognize(bits,12,&result)==THY_HARMONY_INVALID);
    CHECK(thy_harmony_recognize(bits,254,&result)==THY_HARMONY_INVALID);
    CHECK(thy_harmony_recognize(bits,0,NULL)==THY_HARMONY_INVALID);
    CHECK(thy_harmony_from_notes(NULL,1,0,&result)==THY_HARMONY_INVALID);
    CHECK(thy_harmony_from_keys(NULL,1,0,&result)==THY_HARMONY_INVALID);
    CHECK(!memcmp(&result,&before,sizeof result));
    char text[THY_HARMONY_TEXT_BYTES],old[THY_HARMONY_TEXT_BYTES];
    memset(text,'!',sizeof text);memcpy(old,text,sizeof text);
    CHECK(thy_harmony_format(&result,text,sizeof text)==THY_HARMONY_INVALID);
    CHECK(!memcmp(text,old,sizeof text));
    const int16_t notes[]={64,67,71,72};
    CHECK(thy_harmony_from_notes(notes,4,0,&result)==THY_HARMONY_OK);
    CHECK(thy_harmony_format(&result,text,0)==THY_HARMONY_TOO_SMALL);
    CHECK(!memcmp(text,old,sizeof text));
    CHECK(thy_harmony_format(&result,NULL,0)==THY_HARMONY_INVALID);
    CHECK(thy_harmony_format(&result,text,strlen("C MAJ7/E"))==THY_HARMONY_TOO_SMALL);
    CHECK(!memcmp(text,old,sizeof text));
    CHECK(thy_harmony_format(&result,text,strlen("C MAJ7/E")+1u)==THY_HARMONY_OK);
    CHECK(!strcmp(text,"C MAJ7/E"));
    result.quality=THY_HARMONY_NO_QUALITY;memcpy(old,text,sizeof text);
    CHECK(thy_harmony_format(&result,text,sizeof text)==THY_HARMONY_INVALID);
    CHECK(!memcmp(text,old,sizeof text));
    CHECK(thy_harmony_quality_name(THY_HARMONY_NO_QUALITY)==NULL);return 1;
}

static int longest_canonical_label_fits_twenty_one_oled_glyphs(void)
{
    const int16_t notes[]={73,64,67,70,63,66};thy_harmony result;
    char text[THY_HARMONY_TEXT_BYTES];
    CHECK(thy_harmony_from_notes(notes,6,1,&result)==THY_HARMONY_OK);
    CHECK(thy_harmony_format(&result,text,sizeof text)==THY_HARMONY_OK);
    CHECK(!strcmp(text,"C# DIM7 ADD9 ADD11/D#") && strlen(text)==21u);
    for(unsigned quality=0;quality<thy_harmony_pattern_count();++quality)
        CHECK(strlen(thy_harmony_quality_name((uint8_t)quality))<=15u);
    return 1;
}

static int all_pitch_class_sets_have_consistent_exact_context_and_format_bounds(void)
{
    for(unsigned mask=0;mask<4096u;++mask) {
        int16_t notes[12];size_t count=0;
        for(unsigned pc=0;pc<12u;++pc)if(mask&(1u<<pc))notes[count++]=(int16_t)(60u+pc);
        thy_harmony result;char text[THY_HARMONY_TEXT_BYTES];
        CHECK(thy_harmony_from_notes(notes,count,THY_HARMONY_NO_ROOT,&result)==THY_HARMONY_OK);
        CHECK(result.pitch_classes==mask && result.notes==count && result.classes==count);
        CHECK(thy_harmony_format(&result,text,sizeof text)==THY_HARMONY_OK);
        CHECK(strlen(text)<THY_HARMONY_TEXT_BYTES);
        unsigned exact_roots=0;
        for(uint8_t root=0;root<12u;++root) {
            thy_harmony context;
            CHECK(thy_harmony_from_notes(notes,count,root,&context)==THY_HARMONY_OK);
            CHECK(context.pitch_classes==mask && context.notes==count);
            CHECK(thy_harmony_format(&context,text,sizeof text)==THY_HARMONY_OK);
            if(context.kind==THY_HARMONY_CHORD && (context.flags&THY_HARMONY_ROOT_USED)) {
                ++exact_roots;CHECK(context.root==root && (mask&(1u<<root)));
                CHECK(context.confidence==THY_HARMONY_CONFIDENCE_CONTEXT);
            } else if(count>1u)CHECK(context.flags&THY_HARMONY_ROOT_REJECTED);
        }
        if(result.kind==THY_HARMONY_CHORD) {
            CHECK(exact_roots==result.matches);
            CHECK(result.confidence==(exact_roots==1u?THY_HARMONY_CONFIDENCE_EXACT:
                THY_HARMONY_CONFIDENCE_AMBIGUOUS));
        } else CHECK(!exact_roots);
    }
    uint64_t every[2]={UINT64_MAX,UINT64_MAX};thy_harmony all;
    CHECK(thy_harmony_recognize(every,THY_HARMONY_NO_ROOT,&all)==THY_HARMONY_OK);
    CHECK(all.notes==128u && all.classes==12u && all.lowest==0u && all.highest==127u);
    char text[THY_HARMONY_TEXT_BYTES];
    CHECK(thy_harmony_format(&all,text,sizeof text)==THY_HARMONY_OK && !strcmp(text,"128 NOTES"));
    return 1;
}

int main(void)
{
    int (*tests[])(void)={
        basic_chords_notes_and_octaves_are_distinct,
        source_root_and_actual_bass_survive_inversion_drop_and_layers,
        ambiguity_is_deterministic_and_context_is_explicit,
        routing_duplicates_do_not_change_harmony_or_note_counts,
        actual_rootless_and_mislabelled_archive_edges_are_not_repeated,
        results_are_independent_source_presentations_without_global_scratch,
        generated_quality_rows_have_exact_names_for_every_chromatic_root,
        invalid_input_and_short_buffers_leave_outputs_unchanged,
        longest_canonical_label_fits_twenty_one_oled_glyphs,
        all_pitch_class_sets_have_consistent_exact_context_and_format_bounds
    };
    for(unsigned n=0;n<sizeof tests/sizeof tests[0];++n)if(!tests[n]())return 1;
    printf("%u harmony scenarios passed (%u assertions); result=%u bytes, patterns=%u, tables=%u bytes\n",
        (unsigned)(sizeof tests/sizeof tests[0]),assertions,(unsigned)sizeof(thy_harmony),
        (unsigned)thy_harmony_pattern_count(),(unsigned)thy_harmony_table_bytes());
    return 0;
}
