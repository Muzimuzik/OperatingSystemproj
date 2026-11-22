CC     = gcc
CFLAGS = -Wall -Wextra -g
LDLIBS = -pthread

all: server worker client

server: src/server.c src/jobs.c src/jobs.h src/net.h
	$(CC) $(CFLAGS) -o $@ src/server.c src/jobs.c $(LDLIBS)

worker: src/worker.c src/net.h
	$(CC) $(CFLAGS) -o $@ src/worker.c $(LDLIBS)

client: src/client.c src/net.h
	$(CC) $(CFLAGS) -o $@ src/client.c $(LDLIBS)

tests/test_jobs: tests/test_jobs.c src/jobs.c src/jobs.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_jobs.c src/jobs.c $(LDLIBS)

test: tests/test_jobs
	./tests/test_jobs

check:
	./tests/selfcheck.sh

clean:
	rm -f server worker client tests/test_jobs

.PHONY: all test check clean
