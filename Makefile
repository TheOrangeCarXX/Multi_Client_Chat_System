CC = gcc
CFLAGS = -Wall -pthread

SRC = src
BUILD = build

all: server client

server:
	$(CC) $(SRC)/server.c $(SRC)/auth.c $(SRC)/logger.c -o $(BUILD)/server $(CFLAGS)

client:
	$(CC) $(SRC)/client.c -o $(BUILD)/client $(CFLAGS)

clean:
	rm -f $(BUILD)/server $(BUILD)/client