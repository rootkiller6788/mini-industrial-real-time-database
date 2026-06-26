# Makefile for mini-pi-integrator-opcua-mqtt
# PI Integrator for OPC UA and MQTT

CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -pedantic -O2 -g
LDFLAGS  = -lm
AR       = ar
ARFLAGS  = rcs

SRCDIR   = src
INCDIR   = include
TESTDIR  = tests
EXAMPLEDIR = examples
BUILDDIR = build
LIBNAME  = libpi_integrator.a

SRCS     = $(SRCDIR)/pi_opcua_bridge.c            $(SRCDIR)/pi_mqtt_stream.c            $(SRCDIR)/pi_integrator_data_model.c            $(SRCDIR)/pi_integrator_transform.c

OBJS     = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

TEST_SRCS = $(wildcard $(TESTDIR)/*.c)
TEST_BINS = $(TEST_SRCS:$(TESTDIR)/%.c=$(BUILDDIR)/test_%)

.PHONY: all clean test examples

all: $(BUILDDIR)/$(LIBNAME)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(BUILDDIR)/$(LIBNAME): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

test: $(BUILDDIR)/$(LIBNAME)
	$(CC) $(CFLAGS) -I$(INCDIR) $(TESTDIR)/test_bridge.c -o $(BUILDDIR)/test_bridge -L$(BUILDDIR) -lpi_integrator $(LDFLAGS)
	$(CC) $(CFLAGS) -I$(INCDIR) $(TESTDIR)/test_mqtt.c -o $(BUILDDIR)/test_mqtt -L$(BUILDDIR) -lpi_integrator $(LDFLAGS)
	$(CC) $(CFLAGS) -I$(INCDIR) $(TESTDIR)/test_data_model.c -o $(BUILDDIR)/test_data_model -L$(BUILDDIR) -lpi_integrator $(LDFLAGS)
	@echo "=== Running tests ==="
	./$(BUILDDIR)/test_bridge
	./$(BUILDDIR)/test_mqtt
	./$(BUILDDIR)/test_data_model
	@echo "=== All tests passed ==="

examples: $(BUILDDIR)/$(LIBNAME)
	$(CC) $(CFLAGS) -I$(INCDIR) $(EXAMPLEDIR)/example_opcua_bridge.c -o $(BUILDDIR)/example_opcua_bridge -L$(BUILDDIR) -lpi_integrator $(LDFLAGS)
	$(CC) $(CFLAGS) -I$(INCDIR) $(EXAMPLEDIR)/example_mqtt_streaming.c -o $(BUILDDIR)/example_mqtt_streaming -L$(BUILDDIR) -lpi_integrator $(LDFLAGS)
	$(CC) $(CFLAGS) -I$(INCDIR) $(EXAMPLEDIR)/example_integrator_pipeline.c -o $(BUILDDIR)/example_integrator_pipeline -L$(BUILDDIR) -lpi_integrator $(LDFLAGS)

clean:
	rm -rf $(BUILDDIR)

stats:
	@echo "Include files:"
	@find $(INCDIR) -name '*.h' -exec wc -l {} +
	@echo "Source files:"
	@find $(SRCDIR) -name '*.c' -exec wc -l {} +
