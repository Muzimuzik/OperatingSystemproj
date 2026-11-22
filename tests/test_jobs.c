#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "jobs.h"

static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int status_is(int id, const char *want)
{
    char buf[128];
    return jobs_status(id, buf, sizeof buf) >= 0 && strcmp(buf, want) == 0;
}

static void *adder(void *arg)
{
    (void)arg;
    for (int i = 0; i < 1000; i++)
        jobs_add("echo hi", 10, 0);
    return NULL;
}

int main(void)
{
    char *out;
    size_t len;
    int w, ids[16];

    // normal life
    int a = jobs_add("echo hello", 10, 0);
    CHECK(a == 1);
    CHECK(status_is(a, "T4-1 QUEUED"));
    CHECK(jobs_output(a, &out, &len) == 1);
    CHECK(jobs_set(a, J_RUNNING, 5) == 0);
    CHECK(status_is(a, "T4-1 RUNNING"));
    CHECK(jobs_set(a, J_DONE, 0) == 0);
    CHECK(status_is(a, "T4-1 DONE 0"));
    CHECK(jobs_set(a, J_CANCELLED, 0) < 0);

    // illegal jumps
    int b = jobs_add("exit 3", 10, 0);
    CHECK(jobs_set(b, J_DONE, 3) < 0);
    CHECK(jobs_set(b, J_QUEUED, 0) < 0);
    CHECK(status_is(b, "T4-2 QUEUED"));

    // cancel queued, then again
    CHECK(jobs_cancel(b, NULL) == C_QUEUED);
    CHECK(status_is(b, "T4-2 CANCELLED"));
    CHECK(jobs_cancel(b, NULL) == C_FINISHED);
    CHECK(jobs_cancel(a, NULL) == C_FINISHED);

    // cancel running, late report gets ignored
    int c = jobs_add("sleep 30", 60, 0);
    jobs_set(c, J_RUNNING, 7);
    w = -1;
    CHECK(jobs_cancel(c, &w) == C_RUNNING && w == 7);
    CHECK(jobs_set(c, J_KILLED, 9) < 0);
    CHECK(status_is(c, "T4-3 CANCELLED"));

    // kill reasons
    int d = jobs_add("sleep 100", 2, 0);
    jobs_set(d, J_RUNNING, 7);
    jobs_set(d, J_TIMEOUT, 0);
    CHECK(status_is(d, "T4-4 KILLED TIMEOUT"));

    // worker leaves
    int e = jobs_add("sleep 30", 60, 0), f = jobs_add("sleep 30", 60, 0), g = jobs_add("sleep 30", 60, 0);
    jobs_set(e, J_RUNNING, 7);
    jobs_set(f, J_RUNNING, 7);
    jobs_set(g, J_RUNNING, 8);
    CHECK(jobs_worker_gone(7) == 2);
    CHECK(status_is(e, "T4-5 KILLED SIGNAL 9"));
    CHECK(status_is(f, "T4-6 KILLED SIGNAL 9"));
    CHECK(status_is(g, "T4-7 RUNNING"));
    CHECK(jobs_worker_gone(7) == 0);

    // queue order
    int h = jobs_add("q1", 10, 0), i = jobs_add("q2", 10, 9);
    CHECK(jobs_queued(ids, 16) == 2 && ids[0] == h && ids[1] == i);

    // output
    CHECK(jobs_set_output(a, "hello\nno newline", 16) == 0);
    CHECK(jobs_output(a, &out, &len) == 0 && len == 16 && memcmp(out, "hello\nno newline", 16) == 0);
    free(out);
    char *big = calloc(OUT_MAX + 100, 1);
    jobs_set_output(a, big, OUT_MAX + 100);
    CHECK(jobs_output(a, &out, &len) == 0 && len == OUT_MAX);
    free(out);
    free(big);
    jobs_set_output(b, "", 0);
    CHECK(jobs_output(b, &out, &len) == 0 && len == 0);
    free(out);

    // ids
    CHECK(jobs_parse_id("T4-7") == 7);
    CHECK(jobs_parse_id("T4-99999") == 99999);
    CHECK(jobs_parse_id("T12-7") < 0);
    CHECK(jobs_parse_id("T4-") < 0);
    CHECK(jobs_parse_id("T4-7x") < 0);
    CHECK(jobs_parse_id("T4-0") < 0);
    CHECK(jobs_parse_id("hello") < 0);

    // unknown
    char buf[128];
    CHECK(jobs_status(99999, buf, sizeof buf) < 0);
    CHECK(jobs_cancel(99999, NULL) == C_UNKNOWN);
    CHECK(jobs_output(99999, &out, &len) < 0);
    CHECK(jobs_set(0, J_RUNNING, 1) < 0);
    big = calloc(CMD_MAX + 1, 1);
    memset(big, 'x', CMD_MAX);
    CHECK(jobs_add(big, 10, 0) < 0);
    free(big);

    // one line of text and back
    struct job j1, j2;
    int k = jobs_add("echo 'a  b' | wc -c > \"x y\"", 10, 3);
    jobs_get(k, &j1);
    char line[2048];
    jobs_to_line(&j1, line, sizeof line);
    CHECK(jobs_from_line(line, &j2) == 0);
    CHECK(j2.id == k && j2.state == J_QUEUED && j2.timeout == 10 && j2.prio == 3 &&
          j2.worker == -1 && j2.submitted == j1.submitted && strcmp(j2.cmd, j1.cmd) == 0);
    jobs_get(e, &j1);
    jobs_to_line(&j1, line, sizeof line);
    CHECK(jobs_from_line(line, &j2) == 0 && j2.state == J_KILLED && j2.code == 9);
    CHECK(jobs_from_line("T4-1 NOPE 0 1 0 -1 0 ls", &j2) < 0);
    CHECK(jobs_from_line("T12-1 DONE 0 1 0 -1 0 ls", &j2) < 0);
    CHECK(jobs_from_line("garbage", &j2) < 0);

    // 8 threads adding at once, no id twice
    int before = jobs_add("x", 1, 0);
    pthread_t t[8];
    for (int n = 0; n < 8; n++)
        pthread_create(&t[n], NULL, adder, NULL);
    for (int n = 0; n < 8; n++)
        pthread_join(t[n], NULL);
    CHECK(jobs_add("x", 1, 0) == before + 8001);
    struct job last;
    CHECK(jobs_get(before + 8000, &last) == 0 && last.id == before + 8000);

    if (fails)
        printf("%d failed\n", fails);
    else
        printf("jobs: all good\n");
    return fails != 0;
}
