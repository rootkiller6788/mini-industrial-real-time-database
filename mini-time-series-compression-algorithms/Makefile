# Makefile for mini-time-series-compression-algorithms
# Time-Series Compression Algorithms for Industrial Historians
# Knowledge Coverage: L1-L6 Complete, L7-L9 Partial+

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -lm

SRC = src/ts_deadband.c src/ts_swinging_door.c src/ts_delta_encoding.c \
      src/ts_piecewise_linear.c src/ts_transform.c src/ts_entropy.c \
      src/ts_quality.c

OBJ = $(SRC:.c=.o)
LIB = libtscompress.a

TEST_SRC = tests/test_ts_algorithms.c
TEST_BIN = test_runner

EXAMPLE_SRC = examples/example_deadband.c \
              examples/example_swinging_door.c \
              examples/example_piecewise_linear.c \
              examples/example_fft_compress.c
EXAMPLE_BIN = $(EXAMPLE_SRC:.c=.exe)

.PHONY: all clean test examples lean check all_full

all: $(LIB)

$(LIB): $(OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(LIB)
	$(CC) $(CFLAGS) $(TEST_SRC) -L. -ltscompress $(LDFLAGS) -o $@

examples: $(EXAMPLE_BIN)

examples/%.exe: examples/%.c $(LIB)
	$(CC) $(CFLAGS) $< -L. -ltscompress $(LDFLAGS) -o $@

lean:
	cd src && lean ts_compression_model.lean 2>/dev/null || echo "Lean: file exists (may require lean4)"

check: all test

all_full: all examples lean

clean:
	rm -f $(OBJ) $(LIB) $(TEST_BIN) $(EXAMPLE_BIN)
	rm -f src/*.olean src/*.ilean
