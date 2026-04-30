CC = gcc
CFLAGS = -Wall -pthread -Iinclude

SRC = src
BUILD = build

all: $(BUILD) server client logger_process

# ensure build folder exists
$(BUILD):
	mkdir -p $(BUILD)

server: $(BUILD)
	$(CC) $(SRC)/server.c $(SRC)/auth.c $(SRC)/logger.c $(SRC)/ipc.c -o $(BUILD)/server $(CFLAGS)

client: $(BUILD)
	$(CC) $(SRC)/client.c -o $(BUILD)/client $(CFLAGS)

logger_process: $(BUILD)
	$(CC) $(SRC)/logger_process.c -o $(BUILD)/logger_process $(CFLAGS)

clean:
	rm -rf $(BUILD)