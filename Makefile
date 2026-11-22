# Group 4 — Distributed Job Runner
# `make` builds ./server ./worker ./client in the repo root (spec §5.1).
# Recipe lines must start with a TAB.

CC      = gcc
CFLAGS  = -Wall -Wextra -g
LDFLAGS = -pthread

all: server worker client

server: src/server.c src/net.h
	$(CC) $(CFLAGS) -o server src/server.c $(LDFLAGS)

worker: src/worker.c src/net.h
	$(CC) $(CFLAGS) -o worker src/worker.c $(LDFLAGS)

client: src/client.c src/net.h
	$(CC) $(CFLAGS) -o client src/client.c $(LDFLAGS)

clean:
	rm -f server worker client

.PHONY: all clean
