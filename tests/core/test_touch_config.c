/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "touch_config.h"

static unsigned assertions;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)
typedef struct {
    unsigned calls, config_reads, bank_calls[4];
    int fail_bank, fail_reg, failures_left, wrong_identity, swap_final;
    uint8_t last_register, bank_trace[400], register_trace[400];
} bus;

static uint8_t pattern(unsigned bank, unsigned reg)
{
    return (uint8_t)((bank * 43u + reg * 29u + 7u) & 255u);
}

static int mock_read(void *context, unsigned bank, uint8_t reg, uint16_t *word)
{
    bus *b = context;
    CHECK(bank < 4u);
    CHECK((reg <= 126u && (reg & 1u) == 0u) || reg == 0x8fu || reg == 0x91u);
    CHECK(b->calls < 400u);
    b->bank_trace[b->calls] = (uint8_t)bank;
    b->register_trace[b->calls] = reg;
    ++b->calls;
    ++b->bank_calls[bank];
    b->last_register = reg;
    if ((int)bank == b->fail_bank && (int)reg == b->fail_reg && b->failures_left != 0) {
        --b->failures_left;
        *word = 0xdeadu; /* Callback output must never be trusted after failure. */
        return -3;
    }
    if (reg == 0x8fu) *word = 0x059au;
    else if (reg == 0x91u) *word = (uint16_t)(b->wrong_identity ? 0x010bu :
        b->swap_final && b->bank_calls[bank] >= 68u ? 0x020au : 0x010au);
    else {
        ++b->config_reads;
        *word = (uint16_t)(pattern(bank, reg) | ((uint16_t)pattern(bank, reg + 1u) << 8));
    }
    return 0;
}

static void initialize(thy_touch_capture *capture, bus *b)
{
    const uint32_t uid[3] = {0x00112233u, 0x44556677u, 0x8899aabbu};
    thy_touch_capture_init(capture, uid);
    memset(b, 0, sizeof *b);
    b->fail_bank = b->fail_reg = -1;
}

static void run(thy_touch_capture *capture, bus *b, uint32_t start, unsigned steps)
{
    for (unsigned i = 0; i < steps; ++i)
        (void)thy_touch_capture_step(capture, start + i * 2u, 15u, mock_read, b);
}

static void test_lossless(void)
{
    thy_touch_capture capture;
    bus b;
    initialize(&capture, &b);
    CHECK(thy_touch_capture_begin(&capture, 15u, 100u) == 0);
    run(&capture, &b, 100u, 272u);
    CHECK(b.calls == 272u && b.config_reads == 256u);
    /* Independent Python zlib.crc32 fixture of the tagged provenance bytes. */
    CHECK(capture.bank[0].capture_crc32 == UINT32_C(0x35750aad));
    for (unsigned bank = 0; bank < 4u; ++bank) {
        const thy_touch_snapshot *s = &capture.bank[bank];
        CHECK(s->state == THY_TOUCH_CAPTURE_COMPLETE && s->result == 0);
        CHECK(s->family_id == 0x9au && s->device_id == 0x0a05u && s->device_rev == 1u);
        CHECK(s->stored_crc_valid == 1u);
        CHECK(s->stored_crc16 == (uint16_t)(pattern(bank,126u) | ((uint16_t)pattern(bank,127u) << 8)));
        CHECK(s->capture_crc32 == thy_touch_snapshot_crc32(&capture, bank));
        for (unsigned page = 0; page < 128u; page += 16u) {
            uint8_t bytes[16]; uint16_t valid = 0u;
            CHECK(thy_touch_capture_page(&capture, bank, page, bytes, &valid) == 0);
            CHECK(valid == 0xffffu);
            for (unsigned i = 0; i < 16u; ++i) CHECK(bytes[i] == pattern(bank,page+i));
        }
        CHECK(b.bank_calls[bank] == 68u);
    }
    for (unsigned i = 0; i < 272u; ++i) CHECK(b.bank_trace[i] == i % 4u);
    thy_touch_capture original = capture;
    CHECK(thy_touch_capture_begin(&capture, 15u, 2000u) == 0);
    run(&capture, &b, 2000u, 5u);
    CHECK(b.calls == 272u);
    CHECK(memcmp(capture.bank, original.bank, sizeof capture.bank) == 0);
    uint32_t baseline = capture.bank[0].capture_crc32;
    capture.owner_uid[0] ^= 1u;
    CHECK(thy_touch_snapshot_crc32(&capture, 0u) != baseline);
    capture.owner_uid[0] ^= 1u;
    capture.bank[0].bytes[5] ^= 1u;
    CHECK(thy_touch_snapshot_crc32(&capture, 0u) != baseline);
}

static void test_bounds_and_priority(void)
{
    thy_touch_capture capture;
    bus b;
    initialize(&capture, &b);
    CHECK(thy_touch_capture_begin(&capture, 0u, 0u) == -1);
    CHECK(thy_touch_capture_begin(&capture, 16u, 0u) == -1);
    CHECK(thy_touch_capture_begin(&capture, 1u, 0u) == 0);
    thy_touch_capture before = capture;
    CHECK(thy_touch_capture_begin(&capture, 3u, 1u) == -4);
    CHECK(memcmp(&before, &capture, sizeof capture) == 0);
    CHECK(thy_touch_capture_step(&capture, 0u, 0u, mock_read, &b) == 0);
    CHECK(b.calls == 0u);
    CHECK(thy_touch_capture_step(&capture, 0u, 1u, mock_read, &b) == 1);
    CHECK(thy_touch_capture_step(&capture, 1u, 1u, mock_read, &b) == 0);
    CHECK(b.calls == 1u);
    CHECK(thy_touch_capture_begin(&capture, 2u, 1u) == 0);
    CHECK(thy_touch_capture_step(&capture, 1u, 3u, mock_read, &b) == 0);
    CHECK(b.calls == 1u); /* Adding a bank cannot bypass the global spacing. */
    CHECK(thy_touch_capture_step(&capture, 2u, 1u, mock_read, &b) == 1);
    CHECK(b.calls == 2u);
    uint8_t bytes[16]; uint16_t valid;
    CHECK(thy_touch_capture_page(&capture, 4u, 0u, bytes, &valid) == -1);
    CHECK(thy_touch_capture_page(&capture, 0u, 1u, bytes, &valid) == -1);
    CHECK(thy_touch_capture_page(&capture, 0u, 128u, bytes, &valid) == -1);
    CHECK(thy_touch_capture_page(&capture, 0u, 0u, NULL, &valid) == -1);
    CHECK(thy_touch_capture_page(&capture, 0u, 0u, bytes, &valid) == 0 && valid == 0u);
    for (unsigned i = 0; i < 16u; ++i) CHECK(bytes[i] == 0u);
}

static void test_failed_partial_and_retry(void)
{
    thy_touch_capture capture;
    bus b;
    initialize(&capture, &b);
    b.fail_bank = 0; b.fail_reg = 8; b.failures_left = 3;
    CHECK(thy_touch_capture_begin(&capture, 3u, 0u) == 0);
    run(&capture, &b, 0u, 160u);
    CHECK(capture.bank[0].state == THY_TOUCH_CAPTURE_FAILED && capture.bank[0].result == -3);
    CHECK(capture.bank[0].failed_register == 8u && capture.bank[0].attempts == 9u);
    CHECK(capture.bank[0].capture_crc32 == 0u && capture.bank[0].stored_crc_valid == 0u);
    CHECK(capture.bank[1].state == THY_TOUCH_CAPTURE_COMPLETE);
    uint8_t bytes[16]; uint16_t valid;
    CHECK(thy_touch_capture_page(&capture, 0u, 0u, bytes, &valid) == 0 && valid == 0x00ffu);
    for (unsigned i = 0; i < 8u; ++i) CHECK(bytes[i] == pattern(0u,i));
    for (unsigned i = 8u; i < 16u; ++i) CHECK(bytes[i] == 0u);
    thy_touch_snapshot original = capture.bank[1];
    CHECK(thy_touch_capture_begin(&capture, 3u, 500u) == 0);
    CHECK(capture.bank[0].valid[0] == 0u && capture.bank[0].attempts == 0u);
    run(&capture, &b, 500u, 70u);
    CHECK(capture.bank[0].state == THY_TOUCH_CAPTURE_COMPLETE);
    CHECK(memcmp(&original, &capture.bank[1], sizeof original) == 0);
}

static void test_identity_bound(void)
{
    thy_touch_capture capture;
    bus b;
    initialize(&capture, &b);
    b.wrong_identity = 1;
    CHECK(thy_touch_capture_begin(&capture, 1u, 0u) == 0);
    run(&capture, &b, 0u, 70u);
    CHECK(capture.bank[0].state == THY_TOUCH_CAPTURE_FAILED && b.config_reads == 0u);
    CHECK(capture.bank[0].result == -3 && capture.bank[0].capture_crc32 == 0u);
    initialize(&capture, &b);
    b.swap_final = 1;
    CHECK(thy_touch_capture_begin(&capture, 1u, 0u) == 0);
    run(&capture, &b, 0u, 70u);
    CHECK(capture.bank[0].state == THY_TOUCH_CAPTURE_FAILED && b.config_reads == 64u);
    CHECK(capture.bank[0].failed_register == 0x91u && capture.bank[0].capture_crc32 == 0u);
    CHECK(capture.bank[0].valid[15] == 0xffu); /* Read bytes preserved, not blessed. */
}

static void test_timeout_and_wrap(void)
{
    thy_touch_capture capture;
    bus b;
    initialize(&capture, &b);
    uint32_t start = UINT32_MAX - 30u;
    CHECK(thy_touch_capture_begin(&capture, 1u, start) == 0);
    run(&capture, &b, start, 68u);
    CHECK(capture.bank[0].state == THY_TOUCH_CAPTURE_COMPLETE);
    initialize(&capture, &b);
    CHECK(thy_touch_capture_begin(&capture, 1u, start) == 0);
    CHECK(thy_touch_capture_step(&capture, start + 4999u, 0u, mock_read, &b) == 0);
    CHECK(capture.bank[0].state == THY_TOUCH_CAPTURE_PENDING);
    CHECK(thy_touch_capture_step(&capture, start + 5000u, 0u, mock_read, &b) == 0);
    CHECK(capture.bank[0].state == THY_TOUCH_CAPTURE_FAILED && capture.bank[0].result == -2);
    CHECK(b.calls == 0u);
}

static void test_response_policy(void)
{
    CHECK(thy_touch_poll_period(0u,8u) == 8u);
    CHECK(thy_touch_poll_period(1u,8u) == 8u);
    CHECK(thy_touch_poll_period(2u,8u) == 20u);
    CHECK(thy_touch_poll_period(3u,80u) == 20u);
    CHECK(thy_touch_poll_period(0u,80u) == 80u);
    CHECK(thy_touch_poll_period(0u,7u) == 0u);
    CHECK(thy_touch_poll_period(0u,81u) == 0u);
    CHECK(thy_touch_poll_period(4u,8u) == 0u);
    CHECK(sizeof(thy_touch_capture) <= 1024u);
}

int main(void)
{
    test_lossless(); test_bounds_and_priority(); test_failed_partial_and_retry();
    test_identity_bound(); test_timeout_and_wrap(); test_response_policy();
    printf("6 touch readback/response scenarios passed; %u assertions; snapshot state %zu bytes\n",
           assertions, sizeof(thy_touch_capture));
    return 0;
}
