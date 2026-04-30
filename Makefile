CC = gcc
CFLAGS = -Wall -pthread -Iinclude

SRC = src
BUILD = build

all: $(BUILD) server client

# ensure build folder exists
$(BUILD):
	mkdir -p $(BUILD)

server: $(BUILD)
	$(CC) $(SRC)/server.c $(SRC)/auth.c $(SRC)/logger.c -o $(BUILD)/server $(CFLAGS)

client: $(BUILD)
	$(CC) $(SRC)/client.c -o $(BUILD)/client $(CFLAGS)

clean:
	rm -rf $(BUILD)