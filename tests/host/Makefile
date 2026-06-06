# Host unit-test build
# Usage:
#   make              — build and run all tests
#   make test_mapping — build and run mapping tests only
#   make test_telemetry — build and run telemetry tests only
#   make clean        — remove build artefacts

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -Wpedantic -g \
           -I../../src/mapping \
           -I../../src/telemetry
LDFLAGS := -lm

SRC_MAPPING   := ../../src/mapping/mapping.c
SRC_TELEMETRY := ../../src/telemetry/telemetry.c

.PHONY: all test_mapping test_telemetry clean

all: test_mapping test_telemetry

# ── mapping tests ──────────────────────────────────────────────────────
test_mapping: test_mapping.c $(SRC_MAPPING)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
	./$@

# ── telemetry tests ────────────────────────────────────────────────────
test_telemetry: test_telemetry.c $(SRC_MAPPING) $(SRC_TELEMETRY)
	$(CC) $(CFLAGS) -DTELEMETRY_HOST_BUILD $^ $(LDFLAGS) -o $@
	./$@

clean:
	rm -f test_mapping test_telemetry
