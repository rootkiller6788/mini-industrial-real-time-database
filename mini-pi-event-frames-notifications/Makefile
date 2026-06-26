# Makefile for mini-pi-event-frames-notifications
# PI Event Frames & Notifications - Real-Time Database Event Management
#
# Build targets:
#   make          - build library (libpievent.a)
#   make test     - build and run tests
#   make examples - build all examples
#   make clean    - remove all build artifacts
#
# Reference: CMU 24-677, Purdue ME 575, MIT 6.302
# Standards: ISA-106, ISA-18.2, IEC 62682

CC      := gcc
CFLAGS  := -Wall -Wextra -std=c11 -O2 -g -I./include
AR      := ar
ARFLAGS := rcs
LDFLAGS := -lm

INC_DIR := include
SRC_DIR := src
TST_DIR := tests
EXA_DIR := examples
BLD_DIR := build

SRCS    := $(SRC_DIR)/event_frame.c $(SRC_DIR)/event_template.c \
           $(SRC_DIR)/notification.c $(SRC_DIR)/trigger_engine.c \
           $(SRC_DIR)/event_correlation.c $(SRC_DIR)/event_archive.c
OBJS    := $(patsubst $(SRC_DIR)/%.c, $(BLD_DIR)/%.o, $(SRCS))
LIB     := $(BLD_DIR)/libpievent.a

TEST_SRCS := $(TST_DIR)/test_event_frame.c
TEST_BINS := $(patsubst $(TST_DIR)/%.c, $(BLD_DIR)/test_%, $(TEST_SRCS))

EXAMPLE_SRCS := $(EXA_DIR)/example_downtime.c $(EXA_DIR)/example_batch.c \
                $(EXA_DIR)/example_notification.c
EXAMPLE_BINS := $(patsubst $(EXA_DIR)/%.c, $(BLD_DIR)/example_%, $(EXAMPLE_SRCS))

.PHONY: all test examples clean check-lines

all: $(LIB)

$(BLD_DIR):
	mkdir -p $(BLD_DIR)

$(BLD_DIR)/%.o: $(SRC_DIR)/%.c | $(BLD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

test: $(LIB) $(TEST_BINS)
	@echo "============================================"
	@echo "Running tests for mini-pi-event-frames..."
	@echo "============================================"
	@passed=0; failed=0; \
	for t in $(TEST_BINS); do \
		echo "--- Running: $$t ---"; \
		if ./$$t; then \
			passed=$$((passed+1)); \
			echo "  PASSED"; \
		else \
			failed=$$((failed+1)); \
			echo "  FAILED"; \
		fi; \
	done; \
	echo "============================================"; \
	echo "Results: $$passed passed, $$failed failed"; \
	test $$failed -eq 0

$(BLD_DIR)/test_%: $(TST_DIR)/%.c $(LIB) | $(BLD_DIR)
	$(CC) $(CFLAGS) $< -L$(BLD_DIR) -lpievent $(LDFLAGS) -o $@

examples: $(EXAMPLE_BINS)

$(BLD_DIR)/example_%: $(EXA_DIR)/%.c $(LIB) | $(BLD_DIR)
	$(CC) $(CFLAGS) $< -L$(BLD_DIR) -lpievent $(LDFLAGS) -o $@

run-examples: examples
	@for e in $(EXAMPLE_BINS); do \
		echo "=== Running $$e ==="; \
		./$$e; \
		echo ""; \
	done

check-lines:
	@lines=$$(cat $(INC_DIR)/*.h $(SRC_DIR)/*.c | wc -l); \
	echo "include/ + src/ total lines: $$lines"; \
	if [ $$lines -ge 3000 ]; then \
		echo "PASS: line count >= 3000"; \
	else \
		echo "FAIL: line count < 3000 (required: >= 3000)"; \
		exit 1; \
	fi

check-filler:
	@echo "Scanning for filler patterns..."
	@grep -rn "_fn[0-9]" $(SRC_DIR) $(INC_DIR) 2>/dev/null || true
	@grep -rn "_aux[0-9]" $(SRC_DIR) $(INC_DIR) 2>/dev/null || true
	@grep -rn "algorithm variant" $(SRC_DIR) 2>/dev/null || true
	@grep -rn "extension point" $(SRC_DIR) 2>/dev/null || true
	@echo "Filler scan complete."

clean:
	rm -rf $(BLD_DIR)
