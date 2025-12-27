CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g -MMD -MP
LDFLAGS =
LIBS =
SRC_DIR = src
BUILD_DIR = build

CLIENT = client
SERVER = server

COMMON_SRC = $(wildcard $(SRC_DIR)/common/*.c)
CLIENT_SRC = $(wildcard $(SRC_DIR)/client/*.c)
SERVER_SRC = $(wildcard $(SRC_DIR)/server/*.c)

CLIENT_BIN = $(BUILD_DIR)/$(CLIENT)
SERVER_BIN = $(BUILD_DIR)/$(SERVER)

DEP_FILES = $(CLIENT_BIN).d $(SERVER_BIN).d

.PHONY: all client server clean

all: client server

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

client: $(CLIENT_BIN)

server: $(SERVER_BIN)

$(CLIENT_BIN): $(BUILD_DIR) $(COMMON_SRC) $(CLIENT_SRC)
	$(CC) $(CFLAGS) $(COMMON_SRC) $(CLIENT_SRC) -o $@ $(LDFLAGS) $(LIBS) -pthread

$(SERVER_BIN): $(BUILD_DIR) $(COMMON_SRC) $(SERVER_SRC)
	$(CC) $(CFLAGS) $(COMMON_SRC) $(SERVER_SRC) -o $@ $(LDFLAGS) $(LIBS) -pthread

-include $(DEP_FILES)

clean:
	rm -rf $(BUILD_DIR)
