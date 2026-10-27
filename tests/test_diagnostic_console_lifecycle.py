"""Offline regressions for the first-device-test CDC input lifecycle.

Compiles the actual console functions with fake TinyUSB RX/TX FIFOs and DTR.
No USB/serial/device library is imported. This exercises parser/session logic,
including bounded TX/backpressure, rather than USB endpoint or host timing.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]


def function_source(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError("Unterminated console function")


HARNESS = r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static char command[96], response[2048];
static size_t command_length, response_length, response_offset;
static bool line_overflow, line_invalid, console_connected, console_greeting;
static struct { unsigned cdc_commands; bool mounted; } stats;
static struct { unsigned usb_state; } g_diag_mailbox;
static bool fake_dtr;
static unsigned char rx_fifo[4096];
static size_t rx_read, rx_written;
static char tx_fifo[256], wire[8192];
static size_t tx_written, wire_written, write_limit, flush_limit;
static unsigned commands_called, rx_flushes, tx_clears;
static void console_reset_state(void);
static void console_reset(void);
static bool tud_cdc_connected(void) { return fake_dtr; }
static uint32_t tud_cdc_available(void) { return (uint32_t)(rx_written-rx_read); }
static int tud_cdc_read_char(void) {
    return rx_read < rx_written ? rx_fifo[rx_read++] : -1;
}
static void tud_cdc_read_flush(void) { rx_read=rx_written; rx_flushes++; }
static bool tud_cdc_write_clear(void) { tx_written=0; tx_clears++; return true; }
static uint32_t tud_cdc_write(const void *bytes, uint32_t count) {
    if (count>write_limit) count=(uint32_t)write_limit;
    if (count>sizeof tx_fifo-tx_written) count=(uint32_t)(sizeof tx_fifo-tx_written);
    memcpy(tx_fifo + tx_written, bytes, count);
    tx_written += count;
    return count;
}
static void tud_cdc_write_flush(void) {
    size_t count=tx_written<flush_limit?tx_written:flush_limit;
    assert(wire_written+count<sizeof wire);
    memcpy(wire+wire_written,tx_fifo,count);
    wire_written+=count;
    wire[wire_written]=0;
    memmove(tx_fifo,tx_fifo+count,tx_written-count);
    tx_written-=count;
}
static size_t thy_diag_command(const char *line, char *out, size_t capacity) {
    commands_called++;
    assert(strcmp(line,"help") == 0);
    const char *reply="OK THYDIAG/1 mock-help\r\n";
    assert(strlen(reply) + 1 < capacity);
    strcpy(out,reply);
    return strlen(out);
}
static void enqueue(const void *bytes, size_t count) {
    assert(rx_written + count <= sizeof rx_fifo);
    memcpy(rx_fifo+rx_written,bytes,count);
    rx_written += count;
}
static void clear_fixture(void) {
    memset(command,0,sizeof command); memset(response,0,sizeof response);
    command_length=response_length=response_offset=0;
    line_overflow=line_invalid=console_connected=console_greeting=false;
    stats.cdc_commands=commands_called=0;
    fake_dtr=false; rx_read=rx_written=tx_written=wire_written=0; wire[0]=0;
    write_limit=flush_limit=sizeof tx_fifo;
    rx_flushes=tx_clears=0;
}
'''

CASES = r'''
static void drain_tasks(unsigned n) { while (n--) console_task(); }
static void line_state(bool dtr, bool rts) {
    fake_dtr=dtr;
    tud_cdc_line_state_cb(0,dtr,rts);
}
static unsigned count_text(const char *text) {
    unsigned n=0;
    for (const char *at=wire;(at=strstr(at,text))!=NULL;at+=strlen(text)) n++;
    return n;
}
static void open_console(void) {
    line_state(true,false);
    drain_tasks(3);
    assert(!console_greeting && console_connected);
}
int main(void) {
    const char *hello="HELLO THYDIAG/1 diagnostic; type help\r\n";
    const unsigned char stale[] = {0xc0,0xc0,0xc0,0xc0,0xc0,0xc0};

    /* Polling fallback opens cleanly even without a line-state callback. */
    clear_fixture(); fake_dtr=true; drain_tasks(3);
    assert(strcmp(wire,hello)==0);
    enqueue("help\n",5); drain_tasks(3);
    assert(commands_called==1 && stats.cdc_commands==1);
    assert(count_text("OK THYDIAG/1 mock-help\r\n")==1);
    assert(strstr(wire,"ERR ")==NULL);

    /* The six captured C0 bytes are purged while DTR remains low. */
    clear_fixture(); enqueue(stale,sizeof stale); drain_tasks(3);
    assert(tud_cdc_available()==0 && rx_flushes==3 && tx_clears==3);
    assert(!line_overflow && !console_connected && tx_written==0);
    open_console();
    assert(strcmp(wire,hello)==0);
    enqueue("help\n",5); drain_tasks(3);
    assert(commands_called==1 && stats.cdc_commands==1);
    assert(strstr(wire,"ERR ")==NULL);

    /* No closed-session poll is needed: the DTR ACK boundary flushes RX. */
    clear_fixture(); enqueue(stale,sizeof stale); line_state(true,false);
    assert(tud_cdc_available()==0);
    drain_tasks(3); enqueue("help\n",5); drain_tasks(3);
    assert(commands_called==1 && stats.cdc_commands==1);
    assert(strstr(wire,"ERR ")==NULL);

    /* OUT completion after DTR but before the first HELLO write is stale. */
    clear_fixture(); line_state(true,false); enqueue(stale,sizeof stale);
    drain_tasks(3); enqueue("help\n",5); drain_tasks(3);
    assert(commands_called==1 && strstr(wire,"ERR ")==NULL);

    /* Close/reopen can happen entirely between polls, losing a partial line. */
    clear_fixture(); open_console(); enqueue("broken",6); drain_tasks(1);
    assert(command_length==6);
    line_state(false,false); enqueue(stale,sizeof stale); line_state(true,false);
    drain_tasks(3); enqueue("help\n",5); drain_tasks(3);
    assert(commands_called==1 && stats.cdc_commands==1 && count_text(hello)==2);
    assert(strstr(wire,"ERR ")==NULL);

    /* RTS-only/repeated high-DTR notifications preserve an active line. */
    clear_fixture(); open_console(); enqueue("he",2); drain_tasks(1);
    line_state(true,true); tud_cdc_line_state_cb(1,false,false);
    enqueue("lp\n",3); drain_tasks(3);
    assert(commands_called==1 && count_text(hello)==1);

    /* HELLO retries are bounded under short writes and a blocked endpoint.
     * Early bytes are discarded; a command sent after HELLO is retained. */
    clear_fixture(); write_limit=3; flush_limit=0; line_state(true,false);
    drain_tasks(2); assert(response_offset==6 && tx_written==6);
    enqueue(stale,sizeof stale); drain_tasks(1);
    assert(tud_cdc_available()==0 && response_offset==9);
    flush_limit=2; drain_tasks(40);
    while (tx_written) tud_cdc_write_flush();
    assert(strcmp(wire,hello)==0 && !console_greeting);
    enqueue("help\n",5); drain_tasks(40);
    while (tx_written) tud_cdc_write_flush();
    assert(commands_called==1 && count_text(hello)==1);
    assert(count_text("OK THYDIAG/1 mock-help\r\n")==1);

    /* A full TX FIFO causes no parser advance and no duplicate response. */
    clear_fixture(); open_console(); flush_limit=0;
    enqueue("help\nhelp\n",10); drain_tasks(1); drain_tasks(1);
    assert(commands_called==1 && tx_written>0);
    write_limit=0; drain_tasks(1); drain_tasks(3);
    assert(commands_called==2 && response_offset==0);
    enqueue("help\n",5); drain_tasks(3);
    assert(commands_called==2 && tud_cdc_available()==5);
    write_limit=sizeof tx_fifo; flush_limit=sizeof tx_fifo; drain_tasks(8);
    assert(commands_called==3 && stats.cdc_commands==3);
    assert(count_text("OK THYDIAG/1 mock-help\r\n")==3);

    /* A close discards both queued TX and a partially queued application
     * reply. Already delivered bytes remain on the wire and are unavoidable. */
    clear_fixture(); open_console(); enqueue("help\n",5); drain_tasks(1);
    write_limit=5; flush_limit=0; drain_tasks(1);
    assert(tx_written==5 && response_offset==5);
    line_state(false,false); assert(tx_written==0 && response_length==0);
    write_limit=flush_limit=sizeof tx_fifo; line_state(true,false); drain_tasks(3);
    assert(count_text(hello)==2 && strstr(wire,"OK ")==NULL);
    enqueue("help\n",5); drain_tasks(3);
    assert(commands_called==2 && count_text("OK THYDIAG/1 mock-help\r\n")==1);

    /* Actual overlong lines and binary input have distinct errors; each is
     * drained through its newline before the next good command is parsed. */
    clear_fixture(); open_console();
    unsigned char long_line[300]; memset(long_line,'x',sizeof long_line);
    enqueue(long_line,sizeof long_line); enqueue("\nhelp\n",6); drain_tasks(10);
    assert(commands_called==1 && stats.cdc_commands==2);
    assert(count_text("ERR line-too-long\r\n")==1);
    assert(count_text("ERR invalid-character\r\n")==0);
    enqueue(stale,sizeof stale); enqueue("help\nhelp\n",10); drain_tasks(8);
    assert(commands_called==2 && stats.cdc_commands==4);
    assert(count_text("ERR invalid-character\r\n")==1);

    /* CRLF and both backspace forms retain the existing text behavior. */
    clear_fixture(); open_console();
    enqueue("hex\blp\r\n",8); drain_tasks(4);
    enqueue("helx\177p\n",7); drain_tasks(4);
    assert(commands_called==2 && stats.cdc_commands==2);
    assert(strstr(wire,"ERR ")==NULL);

    /* USB unmount/reconfiguration also ends a session without depending on
     * a DTR-low callback. The fake driver resets endpoint FIFOs separately. */
    clear_fixture(); open_console(); enqueue("old",3); drain_tasks(1);
    tud_umount_cb(); assert(!console_connected && command_length==0);
    fake_dtr=false; rx_read=rx_written=tx_written=0;
    tud_mount_cb(); assert(stats.mounted && g_diag_mailbox.usb_state==2);
    line_state(true,false); drain_tasks(3); enqueue("help\n",5); drain_tasks(3);
    assert(commands_called==1 && count_text(hello)==2);
    puts("PASS: actual console source rejects pre-session bytes, preserves session framing, and handles reconnect/backpressure; fake FIFO/DTR only");
    return 0;
}
'''


class ConsoleLifecycleTests(unittest.TestCase):
    def test_console_session_boundaries_and_malformed_line_recovery(self):
        record_path = ROOT / ".tools/config/core-toolchain.json"
        if not record_path.exists():
            self.skipTest("Portable compiler is not prepared")
        record = json.loads(record_path.read_text(encoding="utf-8"))
        compiler = Path(record["executable"])
        if not compiler.exists():
            self.skipTest("Portable compiler executable is absent")
        self.assertEqual(hashlib.sha256(compiler.read_bytes()).hexdigest(),
                         record["executable_sha256"])
        source = (ROOT / "firmware/diagnostic/usb.c").read_text(encoding="utf-8")
        signatures = ["static void queue_error(",
                      "static void console_reset_state(void) {",
                      "static void console_reset(void) {",
                      "static void console_connect(void) {",
                      "void tud_cdc_line_state_cb(",
                      "static void console_task(",
                      "void tud_mount_cb(", "void tud_umount_cb("]
        functions = "\n".join(function_source(source, signature)
                              for signature in signatures)
        output = ROOT / ".tools/tmp" / ("diagnostic-console-lifecycle-" + uuid.uuid4().hex)
        output.mkdir(parents=True, exist_ok=False)
        cfile, binary = output / "console-replay.c", output / "console-replay.exe"
        cfile.write_text(HARNESS + functions + "\n" + CASES,
                         encoding="utf-8")
        env = dict(os.environ)
        env["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / ".tools/zig-cache/global")
        env["ZIG_LOCAL_CACHE_DIR"] = str(output / "zig-cache")
        env["TEMP"] = env["TMP"] = str(ROOT / ".tools/tmp")
        compiled = subprocess.run([str(compiler), "cc", "-std=c11", "-O2", "-Wall",
                                   "-Wextra", "-Werror", "-fno-sanitize=undefined",
                                   str(cfile), "-o", str(binary)],
                                  cwd=ROOT, env=env, check=False, capture_output=True,
                                  text=True, timeout=60)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, check=True,
                                capture_output=True, text=True, timeout=10)
        self.assertIn("PASS: actual console source rejects pre-session bytes", result.stdout)
        print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
