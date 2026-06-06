/**
 * @file test_telemetry.c
 * @brief Host unit tests for the telemetry serialiser and command parser.
 *
 * Compile and run on host (no HAL, no CMSIS):
 *   gcc -std=c99 -Wall -Wextra \
 *       -DTELEMETRY_HOST_BUILD \
 *       -I../../src/mapping -I../../src/telemetry \
 *       ../../src/mapping/mapping.c \
 *       ../../src/telemetry/telemetry.c \
 *       test_telemetry.c -lm -o test_telemetry
 *   ./test_telemetry
 *
 * Exit code: 0 = all pass, 1 = any failure.
 *
 * Note: -DTELEMETRY_HOST_BUILD is passed via the Makefile; telemetry.c also
 * defines it internally, so no need to repeat it here.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "telemetry.h"

/* ── Minimal test framework ───────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_TRUE(label, expr)                                            \
    do {                                                                    \
        if (expr) {                                                         \
            printf("[PASS] %s\n", (label));                                 \
            g_pass++;                                                       \
        } else {                                                            \
            printf("[FAIL] %s\n", (label));                                 \
            g_fail++;                                                       \
        }                                                                   \
    } while (0)

#define ASSERT_INT(label, got, expected)                                    \
    do {                                                                    \
        int _g = (int)(got);                                                \
        int _e = (int)(expected);                                           \
        if (_g == _e) {                                                     \
            printf("[PASS] %s  (got=%d)\n", (label), _g);                  \
            g_pass++;                                                       \
        } else {                                                            \
            printf("[FAIL] %s  (got=%d, expected=%d)\n",                   \
                   (label), _g, _e);                                        \
            g_fail++;                                                       \
        }                                                                   \
    } while (0)

#define ASSERT_NEAR(label, got, expected, tol)                              \
    do {                                                                    \
        float _g = (float)(got);                                            \
        float _e = (float)(expected);                                       \
        float _t = (float)(tol);                                            \
        if (fabsf(_g - _e) <= _t) {                                        \
            printf("[PASS] %s  (got=%.4f)\n", (label), (double)_g);        \
            g_pass++;                                                       \
        } else {                                                            \
            printf("[FAIL] %s  (got=%.4f, expected=%.4f, tol=%.4f)\n",    \
                   (label), (double)_g, (double)_e, (double)_t);          \
            g_fail++;                                                       \
        }                                                                   \
    } while (0)

/* ── Helper: build a canonical TelemetryFrame_t ──────────────────────── */

static TelemetryFrame_t make_frame(void)
{
    TelemetryFrame_t f;
    memset(&f, 0, sizeof(f));

    f.sensor.t_ms         = 12345;
    f.sensor.roll_deg     = -12.3f;
    f.sensor.pitch_deg    =   4.5f;
    f.sensor.ax_g         =   0.01f;
    f.sensor.ay_g         =  -0.98f;
    f.sensor.az_g         =   0.12f;
    f.sensor.dist_filt_mm = 350.0f;
    f.sensor.temp_c       =  23.4f;

    f.audio.synth_hz  = 220.0f;
    f.audio.filt_hz   = 215.3f;
    f.audio.servo_deg =  15.75f;

    f.status.mpu     = 0;
    f.status.mcp     = 0;
    f.status.sr04    = 0;
    f.status.audio   = 0;
    f.status.servo   = 0;
    f.status.i2c_err = 0;

    return f;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Serialisation tests
 * ═══════════════════════════════════════════════════════════════════════ */

static void test_serialize_basic(void)
{
    printf("\n--- telemetry_serialize: basic ---\n");

    TelemetryFrame_t f = make_frame();
    char buf[512];
    int n = telemetry_serialize(buf, sizeof(buf), &f);

    /* Must return a positive length */
    ASSERT_TRUE("serialize returns positive length", n > 0);

    /* Must be NUL-terminated */
    ASSERT_TRUE("serialize NUL-terminates", buf[n] == '\0');

    /* Must end with newline (NDJSON) */
    ASSERT_TRUE("serialize ends with newline", buf[n - 1] == '\n');

    /* Must start with '{' */
    ASSERT_TRUE("serialize starts with '{'", buf[0] == '{');

    printf("  serialized: %s", buf);   /* includes trailing \n */
}

static void test_serialize_contains_fields(void)
{
    printf("\n--- telemetry_serialize: required fields present ---\n");

    TelemetryFrame_t f = make_frame();
    char buf[512];
    telemetry_serialize(buf, sizeof(buf), &f);

    /* Check all required JSON keys are present */
    const char *required_keys[] = {
        "\"t\":", "\"roll\":", "\"pitch\":", "\"ax\":", "\"ay\":", "\"az\":",
        "\"dist_mm\":", "\"temp_c\":", "\"synth_hz\":", "\"filt_hz\":",
        "\"servo_deg\":", "\"status\":",
        "\"mpu\":", "\"mcp\":", "\"sr04\":", "\"audio\":", "\"servo\":",
        "\"i2c_err\":",
        NULL
    };

    for (int i = 0; required_keys[i] != NULL; i++) {
        char label[64];
        snprintf(label, sizeof(label), "field '%s' present", required_keys[i]);
        ASSERT_TRUE(label, strstr(buf, required_keys[i]) != NULL);
    }
}

static void test_serialize_values(void)
{
    printf("\n--- telemetry_serialize: value spot-checks ---\n");

    TelemetryFrame_t f = make_frame();
    char buf[512];
    telemetry_serialize(buf, sizeof(buf), &f);

    /* Timestamp */
    ASSERT_TRUE("t=12345 in output", strstr(buf, "12345") != NULL);

    /* Status flags all zero */
    ASSERT_TRUE("mpu:0 in output",   strstr(buf, "\"mpu\":0")   != NULL);
    ASSERT_TRUE("sr04:0 in output",  strstr(buf, "\"sr04\":0")  != NULL);
    ASSERT_TRUE("audio:0 in output", strstr(buf, "\"audio\":0") != NULL);
}

static void test_serialize_fault_flags(void)
{
    printf("\n--- telemetry_serialize: fault flags ---\n");

    TelemetryFrame_t f = make_frame();
    f.status.mpu     = 1;
    f.status.sr04    = 1;
    f.status.i2c_err = 3;

    char buf[512];
    telemetry_serialize(buf, sizeof(buf), &f);

    ASSERT_TRUE("mpu:1 in output",      strstr(buf, "\"mpu\":1")   != NULL);
    ASSERT_TRUE("sr04:1 in output",     strstr(buf, "\"sr04\":1")  != NULL);
    ASSERT_TRUE("i2c_err:3 in output",  strstr(buf, "\"i2c_err\":3") != NULL);
}

static void test_serialize_truncation(void)
{
    printf("\n--- telemetry_serialize: truncation safety ---\n");

    TelemetryFrame_t f = make_frame();
    char buf[16];   /* deliberately too small */
    int n = telemetry_serialize(buf, sizeof(buf), &f);

    /* snprintf returns the number of chars that WOULD have been written;
     * if truncated, n >= sizeof(buf).  Either way, buf must be NUL-terminated. */
    ASSERT_TRUE("truncated output is NUL-terminated",
                buf[sizeof(buf) - 1] == '\0');
    (void)n;
}

static void test_serialize_null_safety(void)
{
    printf("\n--- telemetry_serialize: null safety ---\n");

    TelemetryFrame_t f = make_frame();
    char buf[256];

    int r1 = telemetry_serialize(NULL, sizeof(buf), &f);
    ASSERT_TRUE("NULL buf → negative return", r1 < 0);

    int r2 = telemetry_serialize(buf, sizeof(buf), NULL);
    ASSERT_TRUE("NULL frame → negative return", r2 < 0);

    int r3 = telemetry_serialize(buf, 0, &f);
    ASSERT_TRUE("sz=0 → negative return", r3 < 0);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Command parser tests
 * ═══════════════════════════════════════════════════════════════════════ */

static void test_parse_selftest(void)
{
    printf("\n--- telemetry_parse_cmd: selftest ---\n");

    ParsedCmd cmd;
    CmdCode code = telemetry_parse_cmd("{\"cmd\":\"selftest\"}", &cmd);

    ASSERT_INT("selftest → CMD_SELFTEST", code, CMD_SELFTEST);
    ASSERT_INT("selftest → out.code=CMD_SELFTEST", cmd.code, CMD_SELFTEST);
}

static void test_parse_set(void)
{
    printf("\n--- telemetry_parse_cmd: set ---\n");

    ParsedCmd cmd;
    CmdCode code = telemetry_parse_cmd("{\"cmd\":\"set\",\"filt_hz\":1200}", &cmd);

    ASSERT_INT("set → CMD_SET", code, CMD_SET);
    ASSERT_NEAR("set → filt_hz=1200", cmd.filt_hz, 1200.0f, 0.5f);
}

static void test_parse_set_float(void)
{
    printf("\n--- telemetry_parse_cmd: set with float filt_hz ---\n");

    ParsedCmd cmd;
    CmdCode code = telemetry_parse_cmd("{\"cmd\":\"set\",\"filt_hz\":440.5}", &cmd);

    ASSERT_INT("set float → CMD_SET", code, CMD_SET);
    ASSERT_NEAR("set float → filt_hz=440.5", cmd.filt_hz, 440.5f, 0.1f);
}

static void test_parse_seq_on(void)
{
    printf("\n--- telemetry_parse_cmd: seq on ---\n");

    ParsedCmd cmd;
    CmdCode code = telemetry_parse_cmd(
        "{\"cmd\":\"seq\",\"bpm\":120,\"on\":true}", &cmd);

    ASSERT_INT("seq → CMD_SEQ", code, CMD_SEQ);
    ASSERT_INT("seq → bpm=120", cmd.bpm, 120);
    ASSERT_INT("seq → on=1",    cmd.on,  1);
}

static void test_parse_seq_off(void)
{
    printf("\n--- telemetry_parse_cmd: seq off ---\n");

    ParsedCmd cmd;
    CmdCode code = telemetry_parse_cmd(
        "{\"cmd\":\"seq\",\"bpm\":90,\"on\":false}", &cmd);

    ASSERT_INT("seq off → CMD_SEQ", code, CMD_SEQ);
    ASSERT_INT("seq off → bpm=90",  cmd.bpm, 90);
    ASSERT_INT("seq off → on=0",    cmd.on,  0);
}

static void test_parse_unknown(void)
{
    printf("\n--- telemetry_parse_cmd: unknown command ---\n");

    ParsedCmd cmd;
    CmdCode code = telemetry_parse_cmd("{\"cmd\":\"reboot\"}", &cmd);

    ASSERT_INT("unknown cmd → CMD_UNKNOWN", code, CMD_UNKNOWN);
}

static void test_parse_malformed(void)
{
    printf("\n--- telemetry_parse_cmd: malformed input ---\n");

    ParsedCmd cmd;

    /* Not JSON */
    CmdCode c1 = telemetry_parse_cmd("not json at all", &cmd);
    ASSERT_INT("not-JSON → CMD_ERROR", c1, CMD_ERROR);

    /* Empty string */
    CmdCode c2 = telemetry_parse_cmd("", &cmd);
    ASSERT_INT("empty string → CMD_ERROR", c2, CMD_ERROR);

    /* Missing cmd key */
    CmdCode c3 = telemetry_parse_cmd("{\"foo\":\"bar\"}", &cmd);
    ASSERT_INT("missing cmd key → CMD_ERROR", c3, CMD_ERROR);

    /* NULL input */
    CmdCode c4 = telemetry_parse_cmd(NULL, &cmd);
    ASSERT_INT("NULL input → CMD_ERROR", c4, CMD_ERROR);
}

static void test_parse_null_out(void)
{
    printf("\n--- telemetry_parse_cmd: NULL out pointer ---\n");

    /* Should not crash */
    CmdCode code = telemetry_parse_cmd("{\"cmd\":\"selftest\"}", NULL);
    ASSERT_INT("NULL out → CMD_ERROR (no crash)", code, CMD_ERROR);
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== telemetry unit tests ===\n");

    /* Serialisation */
    test_serialize_basic();
    test_serialize_contains_fields();
    test_serialize_values();
    test_serialize_fault_flags();
    test_serialize_truncation();
    test_serialize_null_safety();

    /* Command parser */
    test_parse_selftest();
    test_parse_set();
    test_parse_set_float();
    test_parse_seq_on();
    test_parse_seq_off();
    test_parse_unknown();
    test_parse_malformed();
    test_parse_null_out();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
