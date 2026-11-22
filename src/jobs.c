#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "jobs.h"

struct slot {
    struct job j;
    char *out;
    size_t outlen;
};

static struct slot *tab;
static int njobs, cap;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

static const char *names[] = { "QUEUED", "RUNNING", "DONE", "TIMEOUT", "KILLED", "CANCELLED" };

static struct slot *find(int id)
{
    return (id >= 1 && id <= njobs) ? &tab[id - 1] : NULL;
}

static int finished(enum jstate s)
{
    return s >= J_DONE;
}

// finished jobs never change again, so a late worker report cant undo a cancel
static int set_locked(struct job *j, enum jstate st, int arg)
{
    int ok = (j->state == J_QUEUED && (st == J_RUNNING || st == J_CANCELLED)) ||
             (j->state == J_RUNNING && finished(st));
    if (!ok)
        return -1;
    j->state = st;
    if (st == J_RUNNING)
        j->worker = arg;
    else
        j->code = arg;
    return 0;
}

int jobs_add(const char *cmd, int timeout, int prio)
{
    if (strlen(cmd) >= CMD_MAX)
        return -1;

    pthread_mutex_lock(&mu);
    if (njobs == cap) {
        int ncap = cap ? cap * 2 : 64;
        struct slot *t = realloc(tab, ncap * sizeof *t);
        if (!t) {
            pthread_mutex_unlock(&mu);
            return -1;
        }
        tab = t;
        cap = ncap;
    }
    struct slot *s = &tab[njobs];
    memset(s, 0, sizeof *s);
    s->j.id = ++njobs;
    s->j.state = J_QUEUED;
    s->j.timeout = timeout;
    s->j.prio = prio;
    s->j.worker = -1;
    s->j.submitted = time(NULL);
    strcpy(s->j.cmd, cmd);
    int id = s->j.id;
    pthread_mutex_unlock(&mu);
    return id;
}

int jobs_get(int id, struct job *out)
{
    pthread_mutex_lock(&mu);
    struct slot *s = find(id);
    if (s)
        *out = s->j;
    pthread_mutex_unlock(&mu);
    return s ? 0 : -1;
}

int jobs_set(int id, enum jstate st, int arg)
{
    pthread_mutex_lock(&mu);
    struct slot *s = find(id);
    int r = s ? set_locked(&s->j, st, arg) : -1;
    pthread_mutex_unlock(&mu);
    return r;
}

// on C_RUNNING the server has to tell *worker to kill the job
int jobs_cancel(int id, int *worker)
{
    pthread_mutex_lock(&mu);
    struct slot *s = find(id);
    int r;
    if (!s) {
        r = C_UNKNOWN;
    } else if (finished(s->j.state)) {
        r = C_FINISHED;
    } else {
        r = s->j.state == J_RUNNING ? C_RUNNING : C_QUEUED;
        if (worker)
            *worker = s->j.worker;
        set_locked(&s->j, J_CANCELLED, 0);
    }
    pthread_mutex_unlock(&mu);
    return r;
}

// worker left without reporting: whatever it was running is dead
int jobs_worker_gone(int worker)
{
    int n = 0;
    pthread_mutex_lock(&mu);
    for (int i = 0; i < njobs; i++) {
        struct job *j = &tab[i].j;
        if (j->state == J_RUNNING && j->worker == worker && set_locked(j, J_KILLED, 9) == 0)
            n++;
    }
    pthread_mutex_unlock(&mu);
    return n;
}

// oldest first
int jobs_queued(int *ids, int max)
{
    int n = 0;
    pthread_mutex_lock(&mu);
    for (int i = 0; i < njobs && n < max; i++)
        if (tab[i].j.state == J_QUEUED)
            ids[n++] = tab[i].j.id;
    pthread_mutex_unlock(&mu);
    return n;
}

int jobs_set_output(int id, const char *buf, size_t len)
{
    if (len > OUT_MAX)
        len = OUT_MAX;
    char *copy = malloc(len ? len : 1);
    if (!copy)
        return -1;
    memcpy(copy, buf, len);

    pthread_mutex_lock(&mu);
    struct slot *s = find(id);
    if (s) {
        free(s->out);
        s->out = copy;
        s->outlen = len;
    }
    pthread_mutex_unlock(&mu);
    if (!s)
        free(copy);
    return s ? 0 : -1;
}

// 0 = ok (free *buf), 1 = not finished, -1 = unknown
int jobs_output(int id, char **buf, size_t *len)
{
    int r;
    pthread_mutex_lock(&mu);
    struct slot *s = find(id);
    if (!s) {
        r = -1;
    } else if (!finished(s->j.state)) {
        r = 1;
    } else {
        *len = s->outlen;
        *buf = malloc(s->outlen + 1);
        r = *buf ? 0 : -1;
        if (*buf) {
            if (s->outlen)
                memcpy(*buf, s->out, s->outlen);
            (*buf)[s->outlen] = '\0';
        }
    }
    pthread_mutex_unlock(&mu);
    return r;
}

// "T4-7" -> 7, anything else -> -1
int jobs_parse_id(const char *s)
{
    int team, id, end = 0;
    if (sscanf(s, "T%d-%d%n", &team, &id, &end) != 2 || s[end] || team != TEAM || id < 1)
        return -1;
    return id;
}

int jobs_status(int id, char *buf, size_t n)
{
    struct job j;
    if (jobs_get(id, &j) < 0)
        return -1;

    switch (j.state) {
    case J_DONE:
        return snprintf(buf, n, "T%d-%d DONE %d", TEAM, id, j.code);
    case J_TIMEOUT:
        return snprintf(buf, n, "T%d-%d KILLED TIMEOUT", TEAM, id);
    case J_KILLED:
        return snprintf(buf, n, "T%d-%d KILLED SIGNAL %d", TEAM, id, j.code);
    default:
        return snprintf(buf, n, "T%d-%d %s", TEAM, id, names[j.state]);
    }
}

// cmd goes last cuz it has spaces
int jobs_to_line(const struct job *j, char *buf, size_t n)
{
    return snprintf(buf, n, "T%d-%d %s %d %d %d %d %ld %s", TEAM, j->id, names[j->state],
                    j->code, j->timeout, j->prio, j->worker, (long)j->submitted, j->cmd);
}

int jobs_from_line(const char *line, struct job *j)
{
    char name[16];
    int team, end = 0;
    long t;
    if (sscanf(line, "T%d-%d %15s %d %d %d %d %ld %n", &team, &j->id, name, &j->code,
               &j->timeout, &j->prio, &j->worker, &t, &end) != 8 || !end || team != TEAM)
        return -1;
    if (strlen(line + end) >= CMD_MAX)
        return -1;

    int st = -1;
    for (int i = 0; i <= J_CANCELLED; i++)
        if (strcmp(name, names[i]) == 0)
            st = i;
    if (st < 0)
        return -1;

    j->state = st;
    j->submitted = t;
    strcpy(j->cmd, line + end);
    return 0;
}
