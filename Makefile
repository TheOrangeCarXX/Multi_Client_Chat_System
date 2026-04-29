CC = gcc
CFLAGS = -Wall -pthread

SRC = src
BUILD = build

all: server client

server:
	$(CC) $(SRC)/server.c $(SRC)/logger.c $(SRC)/auth.c -o $(BUILD)/server $(CFLAGS)

client:
	$(CC) $(SRC)/client.c $(SRC)/auth.c -o $(BUILD)/client $(CFLAGS)

clean:
	rm -f $(BUILD)/server $(BUILD)/client