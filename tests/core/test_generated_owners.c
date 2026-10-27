#include <stdio.h>
#include <string.h>
#include "../../core/thy_core.h"

static unsigned generated_assertions;

#define CHECK(condition) do { \
    ++generated_assertions; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

static void drain(thy_core *core)
{
    thy_midi_event event;
    while (thy_core_poll(core, &event)) { }
}

static int take(thy_core *core, uint8_t destination, uint8_t status,
                uint8_t data1, uint8_t data2)
{
    thy_midi_event event;
    CHECK(thy_core_poll(core, &event));
    CHECK(event.destination == destination && event.status == status);
    CHECK(event.data1 == data1 && event.data2 == data2);
    return 1;
}

static int invariant(const thy_core *core)
{
    uint16_t expected[THY_KEY_COUNT] = {0};
    uint16_t sounding = 0u;
    uint16_t pending = 0u;
    uint16_t sustain = 0u;
    uint16_t generated = 0u;
    unsigned i;
    CHECK(thy_core_queued(core) + thy_core_reserved(core) <= THY_QUEUE_CAPACITY);
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        unsigned j;
        const thy_pad_owner *owner = &core->owners[i];
        CHECK(owner->activated <= owner->count);
        pending = (uint16_t)(pending + owner->count - owner->activated);
        for (j = 0u; j < owner->activated; ++j) ++expected[thy_pad_owner_key(owner, (uint8_t)j)];
    }
    for (i = 0u; i < THY_GENERATED_OWNER_COUNT; ++i) {
        const thy_generated_owner *owner = thy_core_generated_at(core, (uint16_t)i);
        unsigned j;
        if (owner == NULL) continue;
        ++generated;
        CHECK(owner->count > 0u && owner->count <= THY_DEST_COUNT);
        CHECK(owner->source != 0u && owner->run != 0u);
        CHECK(owner->handle <= core->generated_last_handle);
        CHECK(thy_core_generated_find(core, owner->handle) == owner);
        for (j = 0u; j < owner->count; ++j) {
            unsigned k;
            CHECK(owner->keys[j] < THY_KEY_COUNT);
            ++expected[owner->keys[j]];
            for (k = 0u; k < j; ++k) CHECK(owner->keys[j] != owner->keys[k]);
        }
        for (j = 0u; j < i; ++j) {
            CHECK(core->generated[j].handle != owner->handle);
        }
    }
    for (i = 0u; i < THY_KEY_COUNT; ++i) {
        CHECK(expected[i] <= THY_PAD_COUNT + THY_GENERATED_OWNER_COUNT);
        CHECK(thy_core_refcount(core, (uint16_t)i) == expected[i]);
        if (core->refs[i] != 0u || thy_core_deferred(core, (uint16_t)i) != 0u) ++sounding;
        if (thy_core_deferred(core, (uint16_t)i) != 0u) {
            CHECK(core->refs[i] == 0u && core->sustain[i / 128u] != 0u);
        }
    }
    for (i = 0u; i < THY_DEST_COUNT * 16u; ++i) {
        if (core->sustain[i] != 0u) ++sustain;
    }
    CHECK(generated == thy_core_generated_count(core));
    CHECK(sounding == thy_core_sounding(core));
    CHECK(pending == thy_core_pending(core));
    CHECK(thy_core_reserved(core) == sounding + sustain + 2u * pending);
    return 1;
}

static int test_generated_pad_and_source_overlap(void)
{
    thy_core core;
    thy_generated_handle a;
    thy_generated_handle b;
    const int16_t note = 60;
    thy_core_init(&core);
    CHECK(thy_core_press(&core, 0u, &note, 1u, 0, 2u,
                         THY_ROUTE_USB0 | THY_ROUTE_DIN, 90u) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 60u, 2u,
                                     THY_ROUTE_USB0 | THY_ROUTE_DIN, 80u, &a) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 16u, 1u, 60u, 2u,
                                     THY_ROUTE_USB0 | THY_ROUTE_USB1, 70u, &b) == THY_OK);
    CHECK(a != b && thy_core_generated_count(&core) == 2u);
    CHECK(take(&core, THY_USB0, 0x92u, 60u, 90u));
    CHECK(take(&core, THY_DIN, 0x92u, 60u, 90u));
    CHECK(take(&core, THY_USB1, 0x92u, 60u, 70u));
    CHECK(thy_core_generated_release_source(&core, 1u, 1u) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u && thy_core_pad_held(&core, 0u));
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(take(&core, THY_DIN, 0x82u, 60u, 0u));
    CHECK(thy_core_refcount(&core, thy_core_key(THY_USB0, 2u, 60u)) == 1u);
    CHECK(thy_core_generated_release(&core, b) == THY_OK);
    CHECK(take(&core, THY_USB0, 0x82u, 60u, 0u));
    CHECK(take(&core, THY_USB1, 0x82u, 60u, 0u));
    CHECK(thy_core_queued(&core) == 0u && invariant(&core));
    return 1;
}

static int test_stale_handle_and_run_cleanup(void)
{
    thy_core core;
    thy_generated_handle old;
    thy_generated_handle current;
    thy_generated_handle other_run;
    thy_generated_handle other_source;
    const thy_generated_owner *owner;
    thy_core_init(&core);
    CHECK(thy_core_generated_acquire(&core, 16u, 1u, 60u, 0u, THY_ROUTE_DIN, 80u, &old) == THY_OK);
    CHECK(thy_core_generated_release(&core, old) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 16u, 2u, 61u, 5u, THY_ROUTE_USB1, 81u, &current) == THY_OK);
    CHECK(current > old);
    owner = thy_core_generated_at(&core, 0u);
    CHECK(owner != NULL && owner->handle == current);
    CHECK(owner->count == 1u && owner->keys[0] == thy_core_key(THY_USB1, 5u, 61u));
    CHECK(thy_core_generated_release(&core, old) == THY_OK);
    CHECK(thy_core_generated_release_source(&core, 16u, 1u) == THY_OK);
    CHECK(thy_core_generated_count(&core) == 1u);
    CHECK(thy_core_generated_find(&core, current) == owner);
    CHECK(thy_core_generated_acquire(&core, 16u, 1u, 61u, 5u, THY_ROUTE_USB1, 82u, &other_run) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 17u, 2u, 61u, 5u, THY_ROUTE_USB1, 83u, &other_source) == THY_OK);
    drain(&core);
    CHECK(thy_core_generated_source_count(&core, 16u, 2u) == 1u);
    CHECK(thy_core_generated_release_source(&core, 16u, 2u) == THY_OK);
    CHECK(thy_core_generated_count(&core) == 2u && thy_core_queued(&core) == 0u);
    CHECK(thy_core_generated_find(&core, other_run) != NULL);
    CHECK(thy_core_generated_find(&core, other_source) != NULL);
    CHECK(thy_core_generated_release_all(&core) == THY_OK);
    CHECK(take(&core, THY_USB1, 0x85u, 61u, 0u));
    CHECK(invariant(&core));
    return 1;
}

static int test_generated_pool_and_refcount_bound(void)
{
    thy_core core;
    thy_core before;
    thy_generated_handle handle;
    thy_generated_handle unchanged = 123456u;
    const int16_t note = 60;
    unsigned i;
    thy_core_init(&core);
    for (i = 0u; i < THY_PAD_COUNT; ++i) {
        CHECK(thy_core_press(&core, (uint8_t)i, &note, 1u, 0, 0u, THY_ROUTE_ALL, 90u) == THY_OK);
    }
    for (i = 0u; i < THY_GENERATED_OWNER_COUNT; ++i) {
        CHECK(thy_core_generated_acquire(&core, 16u, 1u, 60u, 0u, THY_ROUTE_ALL, 80u, &handle) == THY_OK);
    }
    CHECK(thy_core_generated_count(&core) == THY_GENERATED_OWNER_COUNT);
    CHECK(thy_core_refcount(&core, thy_core_key(THY_DIN, 0u, 60u)) == 224u);
    CHECK(thy_core_queued(&core) == 3u && thy_core_sounding(&core) == 3u);
    before = core;
    CHECK(thy_core_generated_acquire(&core, 17u, 1u, 61u, 0u, THY_ROUTE_ALL, 80u, &unchanged) == THY_OWNER_LIMIT);
    CHECK(unchanged == 123456u && memcmp(&before, &core, sizeof core) == 0);
    CHECK(invariant(&core));
    drain(&core);
    CHECK(thy_core_generated_release_all(&core) == THY_OK);
    CHECK(thy_core_refcount(&core, thy_core_key(THY_DIN, 0u, 60u)) == 96u);
    CHECK(thy_core_queued(&core) == 0u);
    for (i = 0u; i < THY_PAD_COUNT; ++i) CHECK(thy_core_release(&core, (uint8_t)i) == THY_OK);
    CHECK(thy_core_queued(&core) == 3u && invariant(&core));
    return 1;
}

static int test_generated_queue_atomicity_and_reserved_cleanup(void)
{
    thy_core core;
    thy_core before;
    thy_generated_handle first;
    thy_generated_handle overlap;
    thy_generated_handle unchanged = 123456u;
    const int16_t note = 60;
    unsigned i;
    thy_core_init(&core);
    CHECK(thy_core_generated_acquire(&core, 16u, 1u, 60u, 0u, THY_ROUTE_ALL, 90u, &first) == THY_OK);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 6u; ++i) {
        CHECK(thy_core_cc(&core, THY_ROUTE_USB0, 0u, 1u, 0u) == THY_OK);
    }
    before = core;
    CHECK(thy_core_generated_acquire(&core, 17u, 1u, 61u, 0u, THY_ROUTE_ALL, 80u, &unchanged) == THY_QUEUE_FULL);
    CHECK(unchanged == 123456u && memcmp(&before, &core, sizeof core) == 0);
    CHECK(thy_core_generated_acquire(&core, 17u, 1u, 60u, 0u, THY_ROUTE_ALL, 80u, &overlap) == THY_OK);
    CHECK(thy_core_press(&core, 0u, &note, 1u, 0, 0u, THY_ROUTE_USB0, 70u) == THY_OK);
    CHECK(thy_core_generated_release(&core, first) == THY_OK);
    CHECK(thy_core_generated_release_all(&core) == THY_OK);
    CHECK(thy_core_pad_held(&core, 0u));
    CHECK(thy_core_queued(&core) == THY_QUEUE_CAPACITY - 1u);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(thy_core_queued(&core) == THY_QUEUE_CAPACITY && thy_core_reserved(&core) == 0u);
    CHECK(invariant(&core));
    return 1;
}

static int test_generated_sustain_and_local_cleanup(void)
{
    thy_core core;
    thy_generated_handle a;
    thy_generated_handle b;
    const int16_t live = 60;
    unsigned i;
    thy_core_init(&core);
    CHECK(thy_core_sustain(&core, THY_ROUTE_USB0, 0u, 1) == THY_OK);
    CHECK(thy_core_press(&core, 0u, &live, 1u, 0, 0u, THY_ROUTE_USB0, 90u) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 16u, 1u, 60u, 0u, THY_ROUTE_USB0, 80u, &a) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 16u, 1u, 64u, 0u, THY_ROUTE_USB0, 80u, &b) == THY_OK);
    drain(&core);
    CHECK(thy_core_generated_release_source(&core, 16u, 1u) == THY_OK);
    CHECK(thy_core_generated_count(&core) == 0u && thy_core_queued(&core) == 0u);
    CHECK(core.sustain[0] == 1u && thy_core_pad_held(&core, 0u));
    CHECK(thy_core_deferred(&core, thy_core_key(THY_USB0, 0u, 64u)) == 1u);
    CHECK(thy_core_deferred(&core, thy_core_key(THY_USB0, 0u, 60u)) == 0u);
    CHECK(thy_core_reserved(&core) == 3u);
    for (i = 0u; i < THY_QUEUE_CAPACITY - 3u; ++i) {
        CHECK(thy_core_cc(&core, THY_ROUTE_USB0, 0u, 1u, 0u) == THY_OK);
    }
    CHECK(thy_core_sustain(&core, THY_ROUTE_USB0, 0u, 0) == THY_OK);
    CHECK(thy_core_pad_held(&core, 0u) && thy_core_sounding(&core) == 1u);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(thy_core_queued(&core) == THY_QUEUE_CAPACITY && invariant(&core));
    return 1;
}

static int test_generated_deferred_voice_reacquisition(void)
{
    thy_core core;
    thy_generated_handle a;
    thy_generated_handle b;
    thy_core_init(&core);
    CHECK(thy_core_sustain(&core, THY_ROUTE_DIN, 4u, 1) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 16u, 1u, 65u, 4u, THY_ROUTE_DIN, 80u, &a) == THY_OK);
    drain(&core);
    CHECK(thy_core_generated_release(&core, a) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 17u, 1u, 65u, 4u, THY_ROUTE_DIN, 100u, &b) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u && thy_core_sounding(&core) == 1u);
    CHECK(thy_core_deferred(&core, thy_core_key(THY_DIN, 4u, 65u)) == 0u);
    CHECK(thy_core_sustain(&core, THY_ROUTE_DIN, 4u, 0) == THY_OK);
    CHECK(take(&core, THY_DIN, 0xb4u, 64u, 0u));
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_core_generated_release(&core, b) == THY_OK);
    CHECK(take(&core, THY_DIN, 0x84u, 65u, 0u));
    CHECK(invariant(&core));
    return 1;
}

static int test_generated_panic_and_handle_exhaustion(void)
{
    thy_core core;
    thy_core before;
    thy_generated_handle old;
    thy_generated_handle current;
    thy_generated_handle unchanged = 123456u;
    const int16_t chord[] = {60, 64, 67};
    thy_shape shape = {0};
    unsigned i;
    thy_core_init(&core);
    shape.strum_ms = 10u;
    CHECK(thy_core_sustain(&core, THY_ROUTE_ALL, 0u, 1) == THY_OK);
    CHECK(thy_core_press_shaped(&core, 0u, chord, 3u, 0, 0u, THY_ROUTE_ALL, 90u, &shape) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 16u, 1u, 62u, 0u, THY_ROUTE_ALL, 80u, &old) == THY_OK);
    drain(&core);
    for (i = 0u; i < THY_QUEUE_CAPACITY - thy_core_reserved(&core); ++i) {
        CHECK(thy_core_cc(&core, THY_ROUTE_USB0, 0u, 1u, 0u) == THY_OK);
    }
    thy_core_panic(&core);
    CHECK(thy_core_generated_count(&core) == 0u);
    CHECK(thy_core_pending(&core) == 0u && thy_core_reserved(&core) == 0u);
    CHECK(thy_core_generated_find(&core, old) == NULL && invariant(&core));
    before = core;
    thy_core_panic(&core);
    CHECK(memcmp(&before, &core, sizeof core) == 0);
    drain(&core);
    CHECK(thy_core_advance(&core, 100u) == THY_OK && thy_core_queued(&core) == 0u);
    CHECK(thy_core_generated_acquire(&core, 16u, 2u, 62u, 0u, THY_ROUTE_ALL, 80u, &current) == THY_OK);
    CHECK(current > old);
    CHECK(thy_core_generated_release(&core, old) == THY_OK);
    CHECK(thy_core_generated_count(&core) == 1u);
    CHECK(thy_core_generated_release_all(&core) == THY_OK);
    drain(&core);
    /* Simulate the last identity without billions of successful acquisitions. */
    core.generated_last_handle = UINT32_MAX - 1u;
    CHECK(thy_core_generated_acquire(&core, 16u, 3u, 62u, 0u, THY_ROUTE_ALL, 80u, &current) == THY_OK);
    CHECK(current == UINT32_MAX);
    CHECK(thy_core_generated_release(&core, current) == THY_OK);
    before = core;
    CHECK(thy_core_generated_acquire(&core, 16u, 4u, 62u, 0u, THY_ROUTE_ALL, 80u, &unchanged) == THY_HANDLE_EXHAUSTED);
    CHECK(unchanged == 123456u && memcmp(&before, &core, sizeof core) == 0);
    thy_core_panic(&core);
    CHECK(thy_core_generated_acquire(&core, 16u, 5u, 62u, 0u, THY_ROUTE_ALL, 80u, &unchanged) == THY_HANDLE_EXHAUSTED);
    CHECK(invariant(&core));
    return 1;
}

static int test_generated_validation_and_initialization(void)
{
    thy_core core;
    thy_core before;
    thy_generated_handle handle = 123456u;
    thy_core_init(&core);
    before = core;
    CHECK(thy_core_generated_acquire(NULL, 1u, 1u, 60u, 0u, 1u, 80u, &handle) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_acquire(&core, 0u, 1u, 60u, 0u, 1u, 80u, &handle) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_acquire(&core, 1u, 0u, 60u, 0u, 1u, 80u, &handle) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 128u, 0u, 1u, 80u, &handle) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 60u, 16u, 1u, 80u, &handle) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 60u, 0u, 0u, 80u, &handle) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 60u, 0u, 8u, 80u, &handle) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 60u, 0u, 1u, 0u, &handle) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 60u, 0u, 1u, 128u, &handle) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 60u, 0u, 1u, 80u, NULL) == THY_INVALID_ARGUMENT);
    CHECK(handle == 123456u && memcmp(&before, &core, sizeof core) == 0);
    CHECK(thy_core_generated_release(NULL, 1u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_release(&core, 0u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_release_source(NULL, 1u, 1u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_release_source(&core, 0u, 1u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_release_source(&core, 1u, 0u) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_release_all(NULL) == THY_INVALID_ARGUMENT);
    CHECK(thy_core_generated_count(NULL) == 0u);
    CHECK(thy_core_generated_source_count(NULL, 1u, 1u) == 0u);
    CHECK(thy_core_generated_source_count(&core, 0u, 1u) == 0u);
    CHECK(thy_core_generated_find(NULL, 1u) == NULL);
    CHECK(thy_core_generated_find(&core, 0u) == NULL);
    CHECK(thy_core_generated_at(NULL, 0u) == NULL);
    CHECK(thy_core_generated_at(&core, THY_GENERATED_OWNER_COUNT) == NULL);
    CHECK(thy_core_generated_at(&core, 0u) == NULL);
    CHECK(thy_core_generated_acquire(&core, 1u, 1u, 0u, 15u, THY_ROUTE_DIN, 127u, &handle) == THY_OK);
    CHECK(thy_core_generated_release_all(&core) == THY_OK);
    CHECK(thy_core_generated_release_all(&core) == THY_OK);
    thy_core_init(&core);
    CHECK(memcmp(&before, &core, sizeof core) == 0);
    thy_core_init(&core);
    CHECK(memcmp(&before, &core, sizeof core) == 0 && invariant(&core));
    thy_core_init(NULL);
    return 1;
}

static int test_generated_pending_strum_overlap(void)
{
    thy_core core;
    const int16_t notes[] = {60, 64, 67};
    thy_shape shape = {0};
    thy_generated_handle handle;
    thy_core_init(&core);
    shape.strum_ms = 10u;
    CHECK(thy_core_press_shaped(&core, 0u, notes, 3u, 0, 0u, THY_ROUTE_ALL, 90u, &shape) == THY_OK);
    CHECK(thy_core_generated_acquire(&core, 16u, 1u, 64u, 0u, THY_ROUTE_ALL, 80u, &handle) == THY_OK);
    CHECK(invariant(&core));
    drain(&core);
    CHECK(thy_core_advance(&core, 10u) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u);
    CHECK(thy_core_generated_release(&core, handle) == THY_OK);
    CHECK(thy_core_queued(&core) == 0u && thy_core_pending(&core) == 3u);
    CHECK(thy_core_release(&core, 0u) == THY_OK);
    CHECK(thy_core_queued(&core) == 6u);
    drain(&core);
    CHECK(thy_core_advance(&core, 100u) == THY_OK && thy_core_queued(&core) == 0u);
    CHECK(invariant(&core));
    return 1;
}

int run_generated_owner_tests(unsigned *test_count, unsigned *assertion_count)
{
    struct named_test { const char *name; int (*run)(void); };
    const struct named_test tests[] = {
        {"generated and pad route overlap", test_generated_pad_and_source_overlap},
        {"generated stale handles and source runs", test_stale_handle_and_run_cleanup},
        {"generated pool limit and 224-reference bound", test_generated_pool_and_refcount_bound},
        {"generated queue atomicity and reserved cleanup", test_generated_queue_atomicity_and_reserved_cleanup},
        {"generated sustain preserves live pad and pedal", test_generated_sustain_and_local_cleanup},
        {"generated sustained voice reacquisition", test_generated_deferred_voice_reacquisition},
        {"generated panic and identity exhaustion", test_generated_panic_and_handle_exhaustion},
        {"generated validation and init idempotence", test_generated_validation_and_initialization},
        {"generated ownership with pending strums", test_generated_pending_strum_overlap}
    };
    unsigned i;
    generated_assertions = 0u;
    for (i = 0u; i < sizeof tests / sizeof tests[0]; ++i) {
        if (!tests[i].run()) {
            fprintf(stderr, "FAILED: %s\n", tests[i].name);
            return 0;
        }
        ++*test_count;
        printf("PASS: %s\n", tests[i].name);
    }
    *assertion_count += generated_assertions;
    return 1;
}

int main(void)
{
    unsigned tests = 0u;
    unsigned assertions = 0u;
    if (!run_generated_owner_tests(&tests, &assertions)) return 1;
    printf("%u generated-owner tests passed (%u assertions).\n", tests, assertions);
    return 0;
}
