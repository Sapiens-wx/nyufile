CC = gcc
CFLAGS = -g -pedantic -std=c99 -Wall -Werror -Wextra -lcrypto -I.

SRC_DIR = ./src
BUILD_DIR = ./build
TARGET = nyufile

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# 确保 build 目录存在
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 编译规则：src -> build
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)/*.o $(TARGET)
