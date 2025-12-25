CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -g
SRC_DIR = src
BUILD_DIR = build

CLIENT = client
SERVER = server

CLIENT_SRC = $(SRC_DIR)/client.c
SERVER_SRC = $(SRC_DIR)/server.c

CLIENT_BIN = $(BUILD_DIR)/$(CLIENT)
SERVER_BIN = $(BUILD_DIR)/$(SERVER)

.PHONY: all client server clean

all: client server

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

client: $(BUILD_DIR) $(CLIENT_SRC)
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(CLIENT_BIN)

server: $(BUILD_DIR) $(SERVER_SRC)
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $(SERVER_BIN)

clean:
	rm -rf $(BUILD_DIR)
