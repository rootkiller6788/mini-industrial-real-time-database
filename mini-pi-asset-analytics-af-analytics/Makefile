# Makefile — mini-pi-asset-analytics-af-analytics
# PI Asset Framework Analytics — Expression Engine, KPIs, Event Frames, Rollup
#
# Targets:
#   make          — build all (library + tests + examples)
#   make test     — build and run all tests
#   make examples — build example programs
#   make clean    — remove build artifacts

CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -Wconversion \
            -Wno-unused-parameter -O2 -g -fPIC
LDFLAGS  := -lm -Wl,--stack,8388608
AR       := ar
ARFLAGS  := rcs

# Directories
SRC_DIR  := src
INC_DIR  := include
TST_DIR  := tests
EXM_DIR  := examples
BLD_DIR  := build

# Source files
SRC_C    := $(wildcard $(SRC_DIR)/*.c)
SRC_OBJ  := $(SRC_C:$(SRC_DIR)/%.c=$(BLD_DIR)/%.o)

# Test files
TST_C    := $(wildcard $(TST_DIR)/*.c)
TST_BIN  := $(TST_C:$(TST_DIR)/%.c=$(BLD_DIR)/%.test)

# Example files
EXM_C    := $(wildcard $(EXM_DIR)/*.c)
EXM_BIN  := $(EXM_C:$(EXM_DIR)/%.c=$(BLD_DIR)/%)

# Library
LIB_NAME := libpiafanalytics
LIB_STATIC := $(BLD_DIR)/$(LIB_NAME).a

# Include flags
INC_FLAGS := -I$(INC_DIR)

# Exclude .lean files from C compilation
SRC_C := $(filter-out %.lean,$(SRC_C))

# ---------------------------------------------------------------------------
# Top-level targets
# ---------------------------------------------------------------------------

.PHONY: all test examples clean

all: $(LIB_STATIC) $(TST_BIN) $(EXM_BIN)
	@echo "=== PI AF Analytics Build Complete ==="
	@echo "  Library:  $(LIB_STATIC)"
	@echo "  Tests:    $(words $(TST_BIN)) built"
	@echo "  Examples: $(words $(EXM_BIN)) built"

test: $(TST_BIN)
	@echo "=== Running PI AF Analytics Tests ==="
	@passed=0; failed=0; \
	for t in $(TST_BIN); do \
		echo "--- $$(basename $$t) ---"; \
		if $$t; then passed=$$((passed+1)); \
		else failed=$$((failed+1)); fi; \
		echo; \
	done; \
	echo "=== Test Summary: $$passed passed, $$failed failed ==="; \
	exit $$failed

examples: $(EXM_BIN)
	@echo "=== PI AF Analytics Examples Built ==="
	@for e in $(EXM_BIN); do echo "  $$(basename $$e)"; done

clean:
	rm -rf $(BLD_DIR)
	@echo "Cleaned."

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------

$(BLD_DIR):
	mkdir -p $(BLD_DIR)

# Compile source files to objects
$(BLD_DIR)/%.o: $(SRC_DIR)/%.c | $(BLD_DIR)
	$(CC) $(CFLAGS) $(INC_FLAGS) -c $< -o $@

# Static library
$(LIB_STATIC): $(SRC_OBJ)
	$(AR) $(ARFLAGS) $@ $^

# Link test executables
$(BLD_DIR)/%.test: $(TST_DIR)/%.c $(LIB_STATIC) | $(BLD_DIR)
	$(CC) $(CFLAGS) $(INC_FLAGS) $< -o $@ -L$(BLD_DIR) -lpiafanalytics $(LDFLAGS)

# Link example executables
$(BLD_DIR)/%: $(EXM_DIR)/%.c $(LIB_STATIC) | $(BLD_DIR)
	$(CC) $(CFLAGS) $(INC_FLAGS) $< -o $@ -L$(BLD_DIR) -lpiafanalytics $(LDFLAGS)

# ---------------------------------------------------------------------------
# Dependency info
# ---------------------------------------------------------------------------

# Ensure library is built before tests/examples
$(TST_BIN): $(LIB_STATIC)
$(EXM_BIN): $(LIB_STATIC)

# Help
help:
	@echo "PI AF Analytics — Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make          — Build all (library + tests + examples)"
	@echo "  make test     — Build and run all tests"
	@echo "  make examples — Build example programs only"
	@echo "  make clean    — Remove all build artifacts"
	@echo ""
	@echo "Source:  src/   ($(words $(SRC_C)) files)"
	@echo "Tests:   tests/ ($(words $(TST_C)) files)"
	@echo "Examples: examples/ ($(words $(EXM_C)) files)"
