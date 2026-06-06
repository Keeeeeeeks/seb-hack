/**
 * @file telemetry.h
 * @brief NDJSON telemetry serialiser + LPUART1 command parser.
 *
 * The serialise / parse functions are pure C (no HAL) so they can be
 * compiled and unit-tested on a host.  The UART-transport layer
 * (telemetry_init / telemetry_poll / telemetry_rx_irq) is compiled only
 * when TELEMETRY_HOST_BUILD is NOT defined.
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>
#include <stddef.h>

#include "../mapping/mapping.h"   /* SensorState, AudioParams */

/* ── Status flags ─────────────────────────────────────────────────────── */

/** Per-subsystem fault flags embedded in every telemetry frame. */
typedef struct {
    int mpu;       /**< 1 = MPU-6050 fault */
    int mcp;       /**< 1 = MCP9808 fault  */
    int sr04;      /**< 1 = HC-SR04 timeout */
    int audio;     /**< 1 = DAC/DMA fault  */
    int servo;     /**< 1 = servo fault    */
    int i2c_err;   /**< cumulative I2C error count */
} StatusFlags;

/* ── Extended sensor state (adds timestamp + raw accel) ──────────────── */

/**
 * Full sensor snapshot used by the telemetry serialiser.
 * Extends SensorState with fields that are not needed by the mapping layer.
 */
typedef struct {
    uint32_t t_ms;          /**< HAL_GetTick() timestamp [ms] */
    float    roll_deg;
    float    pitch_deg;
    float    ax_g;
    float    ay_g;
    float    az_g;
    float    dist_filt_mm;
    float    temp_c;
} TelemetrySensorState;

/* ── Telemetry frame (all fields in one struct for test convenience) ──── */

/**
 * Complete telemetry frame.  Used by the host unit test to verify
 * round-trip serialisation.
 */
typedef struct {
    TelemetrySensorState sensor;
    AudioParams          audio;
    StatusFlags          status;
} TelemetryFrame_t;

/* ── Command codes ────────────────────────────────────────────────────── */

typedef enum {
    CMD_NONE     = 0,
    CMD_SELFTEST,        /**< {"cmd":"selftest"} */
    CMD_SET,             /**< {"cmd":"set","filt_hz":N} */
    CMD_SEQ,             /**< {"cmd":"seq","bpm":N,"on":true/false} */
    CMD_UNKNOWN  = -1,
    CMD_ERROR    = -2,   /**< malformed JSON */
} CmdCode;

/** Parsed command payload. */
typedef struct {
    CmdCode  code;
    float    filt_hz;   /**< valid when code == CMD_SET  */
    int      bpm;       /**< valid when code == CMD_SEQ  */
    int      on;        /**< valid when code == CMD_SEQ  */
} ParsedCmd;

/* ── Serialisation / parsing (host-compilable) ────────────────────────── */

/**
 * Serialise a TelemetryFrame_t into a NUL-terminated NDJSON string.
 *
 * @param buf   Output buffer.
 * @param sz    Size of output buffer in bytes.
 * @param frame Frame to serialise.
 * @return      Number of characters written (excluding NUL), or negative on
 *              truncation / error (same semantics as snprintf).
 */
int telemetry_serialize(char *buf, size_t sz, const TelemetryFrame_t *frame);

/**
 * Parse a NUL-terminated JSON command string.
 *
 * Recognised commands:
 *   {"cmd":"selftest"}
 *   {"cmd":"set","filt_hz":N}
 *   {"cmd":"seq","bpm":N,"on":true|false}
 *
 * @param json  Input JSON string (may be modified in place — pass a copy if
 *              the original must be preserved).
 * @param out   Parsed command output.
 * @return      CMD_* code (same as out->code).
 */
CmdCode telemetry_parse_cmd(const char *json, ParsedCmd *out);

/* ── Transport layer (on-target only) ────────────────────────────────── */
#ifndef TELEMETRY_HOST_BUILD

/* Forward-declare HAL type without pulling in the full HAL header here.
 * The .c file includes stm32g4xx_hal.h directly. */
struct __UART_HandleTypeDef;

void telemetry_init(struct __UART_HandleTypeDef *huart);
void telemetry_send(const TelemetryFrame_t *frame);
void telemetry_poll(void);
void telemetry_rx_irq(void);

typedef void (*cmd_callback_t)(const ParsedCmd *cmd);
void telemetry_register_cmd_cb(cmd_callback_t cb);

#endif /* TELEMETRY_HOST_BUILD */

#endif /* TELEMETRY_H */
