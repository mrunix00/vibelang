CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Ilibs/Unity/src -Isrc -O3
LDFLAGS =

BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

SRC_DIR = src
TEST_DIR = test
UNITY_DIR = libs/Unity

SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
TEST_FILES = $(wildcard $(TEST_DIR)/*.c)
UNITY_FILES = $(UNITY_DIR)/src/unity.c

MAIN_SOURCE = $(SRC_DIR)/main.c
LIB_SRC_FILES = $(filter-out $(MAIN_SOURCE), $(SRC_FILES))

MAIN_OBJ = $(OBJ_DIR)/main.o
LIB_OBJ = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(LIB_SRC_FILES))
TEST_OBJ = $(patsubst $(TEST_DIR)/%.c, $(OBJ_DIR)/%.o, $(TEST_FILES)) $(patsubst $(UNITY_DIR)/src/%.c, $(OBJ_DIR)/%.o, $(UNITY_FILES)) $(LIB_OBJ)

.PHONY: all clean test

all: $(BUILD_DIR)/main

$(BUILD_DIR)/main: $(MAIN_OBJ) $(LIB_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(MAIN_OBJ) $(LIB_OBJ)

$(BUILD_DIR)/test_runner: $(TEST_OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(TEST_OBJ)

test: $(BUILD_DIR)/test_runner
	./$(BUILD_DIR)/test_runner

clean:
	rm -rf $(BUILD_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(TEST_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(UNITY_DIR)/src/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)
