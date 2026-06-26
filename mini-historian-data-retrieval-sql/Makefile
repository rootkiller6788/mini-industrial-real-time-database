# Makefile for mini-historian-data-retrieval-sql
CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -pedantic -O2 -g
INCLUDES := -Iinclude
LDFLAGS  := -lm

BUILD_DIR := build
SRC_DIR   := src
INC_DIR   := include
TEST_DIR  := tests
EXAM_DIR  := examples

SRCS := $(SRC_DIR)/historian_model.c         $(SRC_DIR)/historian_retrieval.c         $(SRC_DIR)/historian_aggregate.c         $(SRC_DIR)/historian_compression.c         $(SRC_DIR)/historian_interpolation.c         $(SRC_DIR)/historian_windowing.c

OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
LIB := $(BUILD_DIR)/libhistorian.a
TEST_SRC := $(TEST_DIR)/test_historian.c
TEST_EXE := $(BUILD_DIR)/test_historian.exe

.PHONY: all clean test examples lines

all: $(LIB) $(TEST_EXE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LIB): $(BUILD_DIR) $(OBJS)
	ar rcs $@ $(OBJS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TEST_EXE): $(TEST_SRC) $(LIB) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(BUILD_DIR) -lhistorian $(LDFLAGS) -o $@

test: $(TEST_EXE)
	./$(TEST_EXE)

lines:
	@echo include/ + src/ line count:
	@find include -name *.h -exec cat {} + | wc -l
	@find src -name *.c -exec cat {} + | wc -l

clean:
	rm -rf $(BUILD_DIR)

format:
	clang-format -i include/*.h src/*.c tests/*.c
