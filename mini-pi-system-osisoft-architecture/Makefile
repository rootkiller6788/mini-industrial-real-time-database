CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 -I./include
LDFLAGS = -lm
SRCDIR = src
INCDIR = include
TESTDIR = tests
EXAMDIR = examples
BUILDDIR = build
SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(filter-out $(SRCDIR)/%.lean,$(SRCS)))

.PHONY: default
default: $(OBJS)
	@echo All source files compiled successfully.
	@echo Run "make test" to run the test suite.
	@echo Run "make examples" to build examples.

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test
test: $(BUILDDIR)/test_pi_system
	./$(BUILDDIR)/test_pi_system

$(BUILDDIR)/test_pi_system: $(TESTDIR)/test_pi_system.c $(filter-out $(SRCDIR)/%.lean,$(SRCS)) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(TESTDIR)/test_pi_system.c $(filter-out $(SRCDIR)/%.lean,$(SRCS)) $(LDFLAGS)

.PHONY: examples
examples: $(BUILDDIR)/ex_snapshot $(BUILDDIR)/ex_buffer $(BUILDDIR)/ex_config

$(BUILDDIR)/ex_snapshot: $(EXAMDIR)/example_snapshot_archive.c $(filter-out $(SRCDIR)/%.lean,$(SRCS)) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(filter-out $(SRCDIR)/%.lean,$(SRCS)) $(LDFLAGS)

$(BUILDDIR)/ex_buffer: $(EXAMDIR)/example_buffer_collective.c $(filter-out $(SRCDIR)/%.lean,$(SRCS)) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(filter-out $(SRCDIR)/%.lean,$(SRCS)) $(LDFLAGS)

$(BUILDDIR)/ex_config: $(EXAMDIR)/example_point_configuration.c $(filter-out $(SRCDIR)/%.lean,$(SRCS)) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< $(filter-out $(SRCDIR)/%.lean,$(SRCS)) $(LDFLAGS)

.PHONY: all
all: test examples

.PHONY: lines
lines:
	@echo include/ lines:
	@find $(INCDIR) -name '*.h' -exec cat {} + | wc -l
	@echo src/ lines:
	@find $(SRCDIR) -name '*.c' -exec cat {} + | wc -l
	@echo Total include+src:
	@find $(INCDIR) $(SRCDIR) \( -name '*.h' -o -name '*.c' \) -exec cat {} + | wc -l

.PHONY: clean
clean:
	rm -rf $(BUILDDIR)

.PHONY: help
help:
	@echo Targets: default test examples all lines clean help
