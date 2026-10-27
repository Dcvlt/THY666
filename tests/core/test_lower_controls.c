/* Shared lower-control musical behavior; no board or host I/O. */
#include <stdio.h>
#include <string.h>
#include "../../core/thy_instrument.h"

static thy_core core;
static thy_instrument instrument;
static thy_instrument_pad pads[THY_PAD_COUNT], original[THY_PAD_COUNT];
static const int16_t degrees[] = {0,2,4,5,7,9,11};
static unsigned assertions;

#define CHECK(c) do { ++assertions; if (!(c)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); return 0; \
} } while (0)

static int setup(void)
{
    memset(pads,0,sizeof pads);
    for (unsigned n=0;n<48u;++n) pads[n]=(thy_instrument_pad){{60,64,67},7u,1u};
    for (unsigned n=48u;n<96u;++n) pads[n]=(thy_instrument_pad){{60},1u,1u};
    memcpy(original,pads,sizeof pads);
    thy_core_init(&core);
    thy_instrument_init(&instrument,&core);
    CHECK(thy_instrument_set_layout(&instrument,pads,60,degrees,7u)==THY_OK);
    return 1;
}

static int tap(unsigned lower)
{
    CHECK(thy_instrument_control(&instrument,1u,(uint8_t)(7u+lower),1)==THY_OK);
    CHECK(thy_instrument_control(&instrument,1u,(uint8_t)(7u+lower),0)==THY_OK);
    return 1;
}

static int has(const int16_t *notes,size_t count,int pitch)
{
    for (size_t n=0;n<count;++n) if (notes[n]==pitch) return 1;
    return 0;
}

static void drain(void) { thy_midi_event event; while(thy_core_poll(&core,&event)) {} }

static int all_layers_work_on_sparse_chords(void)
{
    CHECK(setup());
    CHECK(instrument.lower_mode==THY_LOWER_OCTAVE_LAYERS);
    CHECK(instrument.octave_layers==8u && thy_instrument_lower_mask(&instrument)==8u);
    CHECK(thy_instrument_lower_available(&instrument)==127u);
    for(unsigned n=0;n<48u;++n) CHECK(instrument.pad_octave_layers[n]==8u);
    int16_t notes[THY_MAX_NOTES]; size_t count;
    for(unsigned layer=0;layer<7u;++layer) {
        instrument.octave_layers=(uint8_t)(1u<<layer);
        CHECK(thy_instrument_preview_pad(&instrument,0u,notes,&count)==THY_OK && count==3u);
        for(unsigned tone=0;tone<3u;++tone)
            CHECK(notes[tone]==pads[0].notes[tone]+12*((int)layer-3));
        CHECK(thy_instrument_preview_pad(&instrument,48u,notes,&count)==THY_OK && count==1u && notes[0]==60);
    }
    instrument.octave_layers=127u;
    instrument.performance.upper_octaves=3u; /* One layer system, never multiplied. */
    CHECK(thy_instrument_preview_pad(&instrument,0u,notes,&count)==THY_OK && count==21u);
    for(unsigned layer=0;layer<7u;++layer)
        for(unsigned tone=0;tone<3u;++tone)
            CHECK(has(notes,count,pads[0].notes[tone]+12*((int)layer-3)));
    CHECK(memcmp(pads,original,sizeof pads)==0);
    pads[0]=(thy_instrument_pad){{60,61,62,63,64,65,66},127u,1u};
    CHECK(thy_instrument_preview_pad(&instrument,0u,notes,&count)==THY_OK && count==49u);
    CHECK(thy_instrument_set_route(&instrument,0u,0u,THY_ROUTE_ALL,0)==THY_OK);
    CHECK(thy_instrument_press_pad(&instrument,0u)==THY_OK);
    CHECK(core.owners[0].count==147u && core.sounding_count==147u);
    drain();
    CHECK(thy_instrument_release_pad(&instrument,0u)==THY_OK);
    drain(); CHECK(!core.sounding_count && !core.pending_keys);
    return 1;
}

static int switches_scope_and_stored_notes_are_independent(void)
{
    CHECK(setup());
    CHECK(tap(3u)); /* Original-only cannot be switched into a silent board. */
    CHECK(instrument.octave_layers==8u);
    CHECK(tap(6u) && instrument.octave_layers==72u);
    CHECK(tap(3u) && instrument.octave_layers==64u);
    CHECK(tap(6u) && instrument.octave_layers==8u);
    CHECK(tap(0u) && instrument.octave_layers==9u);
    instrument.slot_scope=THY_SLOTS_SELECTED_PAD;
    instrument.selected_chord=12u;
    CHECK(tap(5u));
    CHECK(instrument.pad_octave_layers[12]==40u && instrument.pad_octave_layers[0]==8u);
    CHECK(instrument.octave_layers==9u && thy_instrument_lower_mask(&instrument)==40u);
    CHECK(thy_instrument_set_lower_mode(&instrument,THY_LOWER_STORED_NOTES)==THY_OK);
    CHECK(thy_instrument_lower_available(&instrument)==7u);
    CHECK(tap(6u) && !instrument.excluded_slots[12]); /* Absent note is unavailable. */
    CHECK(tap(1u) && instrument.excluded_slots[12]==2u);
    CHECK(thy_instrument_lower_mask(&instrument)==125u);
    instrument.slot_scope=THY_SLOTS_LEFT_SIDE;
    pads[1]=(thy_instrument_pad){{60,62,64,67,71},31u,1u};
    CHECK(thy_instrument_lower_available(&instrument)==31u);
    CHECK(tap(6u) && !instrument.global_excluded_slots);
    CHECK(tap(4u) && instrument.global_excluded_slots==16u);
    int16_t notes[THY_MAX_NOTES]; size_t count;
    CHECK(thy_instrument_preview_pad(&instrument,1u,notes,&count)==THY_OK && count==4u);
    CHECK(thy_instrument_set_lower_mode(&instrument,THY_LOWER_OCTAVE_LAYERS)==THY_OK);
    CHECK(thy_instrument_lower_mask(&instrument)==9u && instrument.global_excluded_slots==16u);
    CHECK(thy_instrument_preview_pad(&instrument,1u,notes,&count)==THY_OK && count==10u);
    CHECK(thy_instrument_set_lower_mode(&instrument,2u)==THY_INVALID_ARGUMENT);
    CHECK(thy_instrument_set_layout(&instrument,pads,60,degrees,7u)==THY_OK);
    CHECK(instrument.octave_layers==9u && instrument.pad_octave_layers[12]==8u);
    CHECK(instrument.global_excluded_slots==16u && !instrument.excluded_slots[12]);
    return 1;
}

static int range_deduplication_and_capacity_are_safe(void)
{
    CHECK(setup());
    int16_t notes[THY_MAX_NOTES]; size_t count=99u;
    const int16_t extremes[]={0,127};
    thy_shape shape={0}; shape.octave_layers=127u;
    CHECK(thy_core_preview(&core,extremes,2u,0,&shape,notes,&count)==THY_OK && count==8u);
    for(unsigned n=0;n<4u;++n) {
        CHECK(has(notes,count,12*(int)n));
        CHECK(has(notes,count,127-12*(int)n));
    }
    shape.octave_layers=1u;
    CHECK(thy_core_preview(&core,extremes,1u,0,&shape,notes,&count)==THY_OK && !count);
    shape=(thy_shape){0}; shape.upper_octaves=1u;
    notes[0]=-99; count=99u;
    CHECK(thy_core_preview(&core,extremes+1,1u,0,&shape,notes,&count)==THY_INVALID_ARGUMENT);
    CHECK(notes[0]==-99 && count==99u); /* Legacy strictness is retained. */
    int16_t many[THY_MAX_NOTES];
    for(unsigned n=0;n<THY_MAX_NOTES;++n) many[n]=(int16_t)n;
    shape=(thy_shape){0}; shape.octave_layers=24u;
    CHECK(thy_core_preview(&core,many,THY_MAX_NOTES,0,&shape,notes,&count)==THY_INVALID_ARGUMENT);
    CHECK(notes[0]==-99 && count==99u); /* Bounded, atomic output on overflow. */
    for(unsigned n=0;n<THY_MAX_NOTES;++n) many[n]=60;
    shape.octave_layers=127u;
    CHECK(thy_core_preview(&core,many,THY_MAX_NOTES,0,&shape,notes,&count)==THY_OK && count==7u);
    const int16_t repeated[]={60,72,84};
    CHECK(thy_core_preview(&core,repeated,3u,0,&shape,notes,&count)==THY_OK && count==9u);
    return 1;
}

static int held_owners_routes_and_releases_stay_captured(void)
{
    CHECK(setup());
    CHECK(thy_instrument_set_route(&instrument,0u,0u,THY_ROUTE_USB0|THY_ROUTE_DIN,0)==THY_OK);
    CHECK(thy_instrument_press_pad(&instrument,0u)==THY_OK);
    thy_pad_owner owner=core.owners[0];
    CHECK(owner.count==6u && core.sounding_count==6u);
    drain();
    CHECK(tap(6u));
    CHECK(thy_instrument_set_route(&instrument,0u,4u,THY_ROUTE_DIN,0)==THY_OK);
    CHECK(memcmp(&owner,&core.owners[0],sizeof owner)==0);
    CHECK(thy_instrument_release_pad(&instrument,0u)==THY_OK);
    thy_midi_event event; unsigned releases=0u;
    while(thy_core_poll(&core,&event)) {
        CHECK(event.status==0x80u && (event.destination==THY_USB0 || event.destination==THY_DIN));
        CHECK(event.data1==60u || event.data1==64u || event.data1==67u);
        ++releases;
    }
    CHECK(releases==6u && !core.sounding_count);
    CHECK(thy_instrument_press_pad(&instrument,0u)==THY_OK);
    CHECK(core.owners[0].count==6u);
    unsigned attacks=0u;
    while(thy_core_poll(&core,&event)) {
        CHECK(event.status==0x94u && event.destination==THY_DIN);
        ++attacks;
    }
    CHECK(attacks==6u);
    CHECK(thy_instrument_release_pad(&instrument,0u)==THY_OK);
    drain(); CHECK(!core.sounding_count && !core.pending_keys);
    CHECK(memcmp(pads,original,sizeof pads)==0);
    return 1;
}

static int strum_cancellation_and_base_transforms_are_preserved(void)
{
    CHECK(setup());
    instrument.octave_layers=24u;
    instrument.performance.strum=1u;
    instrument.performance.strum_ms=10u;
    instrument.performance.strum_order=THY_STRUM_UP;
    instrument.performance.inversion=1u;
    instrument.performance.spread=1u;
    instrument.bass_octave=-1;
    int16_t notes[THY_MAX_NOTES]; size_t count;
    CHECK(thy_instrument_preview_pad(&instrument,0u,notes,&count)==THY_OK && count==6u);
    static const int16_t expected[]={48,60,64,67,76,79};
    CHECK(memcmp(notes,expected,sizeof expected)==0);
    CHECK(thy_instrument_press_pad(&instrument,0u)==THY_OK);
    CHECK(core.owners[0].count==6u && core.owners[0].activated==1u && core.pending_keys==5u);
    drain();
    CHECK(tap(0u)); /* Does not rewrite the pending old strum. */
    CHECK(thy_instrument_advance(&instrument,10u)==THY_OK);
    thy_midi_event event;
    CHECK(thy_core_poll(&core,&event) && event.status==0x90u && event.data1==60u);
    CHECK(!thy_core_poll(&core,&event));
    CHECK(thy_instrument_release_pad(&instrument,0u)==THY_OK);
    drain();
    CHECK(thy_instrument_advance(&instrument,100u)==THY_OK);
    CHECK(!thy_core_poll(&core,&event) && !core.sounding_count && !core.pending_keys);
    CHECK(memcmp(pads,original,sizeof pads)==0);
    return 1;
}

int main(void)
{
    if(!all_layers_work_on_sparse_chords() ||
       !switches_scope_and_stored_notes_are_independent() ||
       !range_deduplication_and_capacity_are_safe() ||
       !held_owners_routes_and_releases_stay_captured() ||
       !strum_cancellation_and_base_transforms_are_preserved()) return 1;
    printf("5 lower-control scenarios passed (%u assertions).\n",assertions);
    return 0;
}
