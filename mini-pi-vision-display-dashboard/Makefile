# mini-pi-vision-display-dashboard — PI Vision Display & Dashboard Engineering
#
# Build:  make
# Test:   make test
# Clean:  make clean

CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
INC_DIR  := include
SRC_DIR  := src
TST_DIR  := tests
EXA_DIR  := examples
INC_FLAGS := -I$(INC_DIR)

SRC := $(wildcard $(SRC_DIR)/*.c)
OBJ := $(SRC:.c=.o)
TST_SRC := $(wildcard $(TST_DIR)/*.c)
TST_BIN := $(TST_SRC:.c=.test)
EXA_SRC := $(wildcard $(EXA_DIR)/*.c)
EXA_BIN := $(EXA_SRC:.c=.exe)

.PHONY: all clean test examples lib

all: lib examples

lib: $(OBJ)
	ar rcs libpv_display.a $(OBJ)
	@echo "[OK] libpv_display.a built"

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(INC_DIR)/*.h
	$(CC) $(CFLAGS) $(INC_FLAGS) -c $< -o $@

test: lib $(TST_BIN)
	@echo "=== Running Tests ==="
	@passed=0; failed=0; \
	for t in $(TST_BIN); do \
		if [ -f "$$t" ]; then \
			echo "--- Running $$t ---"; \
			if ./$$t; then \
				passed=$$((passed+1)); \
				echo "[PASS] $$t"; \
			else \
				failed=$$((failed+1)); \
				echo "[FAIL] $$t"; \
			fi; \
		fi; \
	done; \
	echo "=== Results: $$passed passed, $$failed failed ==="

$(TST_DIR)/%.test: $(TST_DIR)/%.c lib
	$(CC) $(CFLAGS) $(INC_FLAGS) -L. $< -lpv_display -o $@

examples: lib $(EXA_BIN)

$(EXA_DIR)/%.exe: $(EXA_DIR)/%.c lib
	$(CC) $(CFLAGS) $(INC_FLAGS) -L. $< -lpv_display -o $@

clean:
	rm -f $(SRC_DIR)/*.o $(TST_DIR)/*.test $(EXA_DIR)/*.exe libpv_display.a

help:
	@echo "Targets: make / make test / make examples / make clean"
