#ifndef JOBS_H
#define JOBS_H

#include <stddef.h>
#include <time.h>

#define TEAM    4
#define CMD_MAX 1024
#define OUT_MAX (64 * 1024)

// TIMEOUT prints as "KILLED TIMEOUT", KILLED as "KILLED SIGNAL n"
enum jstate { J_QUEUED, J_RUNNING, J_DONE, J_TIMEOUT, J_KILLED, J_CANCELLED };

// what jobs_cancel found
enum cres { C_UNKNOWN = -1, C_QUEUED, C_RUNNING, C_FINISHED };

struct job {
    int id;
    enum jstate state;
    int code;            // exit code (DONE) or signal (KILLED)
    int timeout, prio;
    int worker;          // who runs it, -1 if nobody yet
    time_t submitted;
    char cmd[CMD_MAX];
};

int  jobs_add(const char *cmd, int timeout, int prio);
int  jobs_get(int id, struct job *out);

// every state change goes through here.
// arg = worker for RUNNING, exit code for DONE, signal for KILLED
int  jobs_set(int id, enum jstate st, int arg);

int  jobs_cancel(int id, int *worker);
int  jobs_worker_gone(int worker);
int  jobs_queued(int *ids, int max);

int  jobs_set_output(int id, const char *buf, size_t len);
int  jobs_output(int id, char **buf, size_t *len);

int  jobs_parse_id(const char *s);
int  jobs_status(int id, char *buf, size_t n);

int  jobs_to_line(const struct job *j, char *buf, size_t n);
int  jobs_from_line(const char *line, struct job *j);

#endif
