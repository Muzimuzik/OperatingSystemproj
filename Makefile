CC = gcc
CFLAGS = -Wall -Wextra

all: server worker client

server: src/server.c src/scheduler.c src/scheduler.h src/net.h src/protocol.h src/journal.h
	$(CC) $(CFLAGS) -o server src/server.c src/scheduler.c

worker: src/worker.c src/protocol.h
	$(CC) $(CFLAGS) -o worker src/worker.c

client: src/client.c src/protocol.h
	$(CC) $(CFLAGS) -o client src/client.c

clean:
	rm -f server worker client