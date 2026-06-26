# Makefile for mini-pi-asset-framework-af
# PI Asset Framework (AF) - Core Library
# OSIsoft PI AF SDK Concepts Implementation

CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -pedantic -O2 -g
CPPFLAGS = -Iinclude
AR       = ar
ARFLAGS  = rcs

SRCDIR   = src
INCDIR   = include
TESTDIR  = tests
EXDIR    = examples
BINDIR   = build

# Source files
SRCS     = $(wildcard $(SRCDIR)/*.c)
OBJS     = $(SRCS:$(SRCDIR)/%.c=$(BINDIR)/%.o)
LIB      = $(BINDIR)/libaf.a

# Test sources
TEST_SRCS = $(wildcard $(TESTDIR)/*.c)
TEST_BINS = $(TEST_SRCS:$(TESTDIR)/%.c=$(BINDIR)/test_%)

# Example sources
EX_SRCS   = $(wildcard $(EXDIR)/*.c)
EX_BINS   = $(EX_SRCS:$(EXDIR)/%.c=$(BINDIR)/%)

.PHONY: all clean test examples lib

all: lib examples test

# Build directory
$(BINDIR):
	mkdir -p $(BINDIR)

# Compile C sources to objects
$(BINDIR)/%.o: $(SRCDIR)/%.c | $(BINDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# Build static library
$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

lib: $(LIB)

# Test binaries
$(BINDIR)/test_%: $(TESTDIR)/%.c $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -L$(BINDIR) -laf -o $@ -lm

# Example binaries
$(BINDIR)/%: $(EXDIR)/%.c $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -L$(BINDIR) -laf -o $@ -lm

# Run all tests
test: lib $(TEST_BINS)
	@echo "=== Running Tests ==="
	@for t in $(TEST_BINS); do echo "--- $$t ---"; ./$$t || exit 1; done
	@echo "=== All tests passed ==="

examples: lib $(EX_BINS)

clean:
	rm -rf $(BINDIR)

# Show line counts
count:
	@echo "include/ lines: $$(cat include/*.h | wc -l)"
	@echo "src/ lines:     $$(cat src/*.c | wc -l)"
	@echo "total lines:    $$(cat include/*.h src/*.c | wc -l)"
