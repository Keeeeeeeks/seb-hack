/**
 * @file telemetry.c
 * @brief NDJSON telemetry serialiser + minimal JSON command parser.
 *
 * The serialise/parse functions are pure C99 (no HAL, no CMSIS) and compile
 * on host and on-target identically.
 *
 * The transport layer (ring buffer + DMA) is compiled only when
 * TELEMETRY_HOST_BUILD is not defined.
 */

#include "telemetry.h"

#ifndef TELEMETRY_HOST_BUILD
#include "stm32g4xx_hal.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>   /* strtol, strtof */
#include <ctype.h>    /* isspace */

/* ═══════════════════════════════════════════════════════════════════════
 * Serialisation
 * ═══════════════════════════════════════════════════════════════════════ */

int telemetry_serialize(char *buf, size_t sz, const TelemetryFrame_t *frame)
{
    if (!buf || !frame || sz == 0) return -1;

    const TelemetrySensorState *s = &frame->sensor;
    const AudioParams          *a = &frame->audio;
    const StatusFlags          *f = &frame->status;

    return snprintf(buf, sz,
        "{"
        "\"t\":%lu,"
        "\"roll\":%.2f,"
        "\"pitch\":%.2f,"
        "\"ax\":%.3f,"
        "\"ay\":%.3f,"
        "\"az\":%.3f,"
        "\"dist_mm\":%lu,"
        "\"temp_c\":%.2f,"
        "\"synth_hz\":%.1f,"
        "\"filt_hz\":%.1f,"
        "\"servo_deg\":%.2f,"
        "\"status\":{"
            "\"mpu\":%d,"
            "\"mcp\":%d,"
            "\"sr04\":%d,"
            "\"audio\":%d,"
            "\"servo\":%d,"
            "\"i2c_err\":%d"
        "}"
        "}\n",
        (unsigned long)s->t_ms,
        s->roll_deg,
        s->pitch_deg,
        s->ax_g,
        s->ay_g,
        s->az_g,
        (unsigned long)(uint32_t)s->dist_filt_mm,
        s->temp_c,
        a->synth_hz,
        a->filt_hz,
        a->servo_deg,
        f->mpu,
        f->mcp,
        f->sr04,
        f->audio,
        f->servo,
        f->i2c_err);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Minimal JSON key scanner (no heap, no external library)
 *
 * Strategy: scan for "key":value patterns using strstr.
 * Handles string values ("...") and numeric/boolean values.
 * ═══════════════════════════════════════════════════════════════════════ */

/**
 * Find the value string for a given key in a flat JSON object.
 * Returns a pointer to the first character of the value (after the colon),
 * or NULL if the key is not found.
 *
 * The search is intentionally simple — it works for the small, well-known
 * command schemas used here.  It is NOT a general JSON parser.
 */
static const char *find_json_value(const char *json, const char *key)
{
    /* Build search pattern: "key": */
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return NULL;

    p += strlen(pattern);

    /* Skip whitespace and colon */
    while (*p && (isspace((unsigned char)*p) || *p == ':')) p++;

    return (*p) ? p : NULL;
}

/**
 * Extract a string value (between double-quotes) from a JSON value pointer.
 * Writes at most dst_sz-1 characters into dst and NUL-terminates.
 * Returns dst, or NULL on error.
 */
static char *extract_string(const char *val, char *dst, size_t dst_sz)
{
    if (!val || *val != '"') return NULL;
    val++;   /* skip opening quote */
    size_t i = 0;
    while (*val && *val != '"' && i < dst_sz - 1) {
        dst[i++] = *val++;
    }
    dst[i] = '\0';
    return dst;
}

CmdCode telemetry_parse_cmd(const char *json, ParsedCmd *out)
{
    if (!json || !out) {
        if (out) { out->code = CMD_ERROR; }
        return CMD_ERROR;
    }

    /* Zero-initialise output */
    out->code    = CMD_NONE;
    out->filt_hz = 0.0f;
    out->bpm     = 0;
    out->on      = 0;

    /* Basic sanity: must start with '{' somewhere */
    const char *p = json;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '{') {
        out->code = CMD_ERROR;
        return CMD_ERROR;
    }

    /* Extract "cmd" value */
    const char *cmd_val = find_json_value(json, "cmd");
    if (!cmd_val) {
        out->code = CMD_ERROR;
        return CMD_ERROR;
    }

    char cmd_str[32];
    if (!extract_string(cmd_val, cmd_str, sizeof(cmd_str))) {
        out->code = CMD_ERROR;
        return CMD_ERROR;
    }

    if (strcmp(cmd_str, "selftest") == 0) {
        out->code = CMD_SELFTEST;

    } else if (strcmp(cmd_str, "set") == 0) {
        out->code = CMD_SET;
        const char *fhz_val = find_json_value(json, "filt_hz");
        if (fhz_val) {
            char *end;
            out->filt_hz = strtof(fhz_val, &end);
            if (end == fhz_val) {
                /* Not a valid number */
                out->code = CMD_ERROR;
                return CMD_ERROR;
            }
        }

    } else if (strcmp(cmd_str, "seq") == 0) {
        out->code = CMD_SEQ;
        const char *bpm_val = find_json_value(json, "bpm");
        if (bpm_val) {
            char *end;
            out->bpm = (int)strtol(bpm_val, &end, 10);
            if (end == bpm_val) {
                out->code = CMD_ERROR;
                return CMD_ERROR;
            }
        }
        const char *on_val = find_json_value(json, "on");
        if (on_val) {
            if (strncmp(on_val, "true", 4) == 0)       out->on = 1;
            else if (strncmp(on_val, "false", 5) == 0) out->on = 0;
            else {
                out->code = CMD_ERROR;
                return CMD_ERROR;
            }
        }

    } else {
        out->code = CMD_UNKNOWN;
    }

    return out->code;
}

#ifndef TELEMETRY_HOST_BUILD

/* ═══════════════════════════════════════════════════════════════════════
 * Minimal on-target transport
 *
 * The full non-blocking DMA/ring transport still lives in Track A's Phase 4
 * backlog. This implementation is intentionally tiny so firmware links now;
 * BIST can print PASS/FAIL lines, and the higher-priority audio ISR remains
 * isolated from telemetry calls. Do not call telemetry_send() from an ISR.
 * ═══════════════════════════════════════════════════════════════════════ */

static UART_HandleTypeDef *s_uart = NULL;
static cmd_callback_t s_cmd_cb = NULL;

void telemetry_init(struct __UART_HandleTypeDef *huart)
{
    s_uart = (UART_HandleTypeDef *)huart;
}

void telemetry_print_raw(const char *line)
{
    if (!s_uart || !line) return;

    size_t len = strlen(line);
    if (len > UINT16_MAX) len = UINT16_MAX;

    (void)HAL_UART_Transmit(s_uart, (uint8_t *)line, (uint16_t)len, 20);
}

void telemetry_send(const TelemetryFrame_t *frame)
{
    char line[256];
    int n = telemetry_serialize(line, sizeof(line), frame);
    if (n > 0 && n < (int)sizeof(line)) {
        telemetry_print_raw(line);
    }
}

void telemetry_poll(void)
{
    /* Reserved for the Phase 4 non-blocking UART ring/DMA transport. */
}

void telemetry_rx_irq(void)
{
    /* Reserved for command receive buffering. */
}

void telemetry_register_cmd_cb(cmd_callback_t cb)
{
    s_cmd_cb = cb;
}

#endif /* TELEMETRY_HOST_BUILD */
