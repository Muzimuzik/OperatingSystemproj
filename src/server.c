#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "net.h"
#include "protocol.h"
#include "scheduler.h"
#include "journal.h"

#define MAX_JOBS 1024
#define JOURNAL_PATH "jobs.log"
#define OUT_DIR "out"

typedef enum {
    ST_QUEUED,
    ST_RUNNING,
    ST_DONE,
    ST_KILLED_TIMEOUT,
    ST_KILLED_SIGNAL,
    ST_CANCELLED
} JobState;

typedef struct {
    char id[MAX_ID_LEN];
    int seq;
    char command[MAX_CMD_LEN];
    int timeout;
    int priority;
    JobState state;
    int detail_code;        // exit code для DONE или номер сигнала для KILLED_SIGNAL
    int worker_fd;          // fd воркера, если RUNNING
    time_t created_at;
} JobRecord;

static JobRecord job_table[MAX_JOBS];
static int job_count = 0;
static int max_seq_seen = 0;
static Scheduler sched;
static int journal_fd = -1;
static volatile sig_atomic_t running = 1;

static void handle_sigterm(int sig) {
    (void)sig;
    running = 0;
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

int net_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }
    if (listen(fd, 32) < 0) {
        perror("listen"); close(fd); return -1;
    }
    return fd;
}

void net_reset_conn(struct pollfd *pfd, ConnState *st) {
    if (pfd->fd != -1) {
        close(pfd->fd);
        pfd->fd = -1;
    }
    if (st->out_buf) {
        free(st->out_buf);
        st->out_buf = NULL;
    }
    memset(st, 0, sizeof(*st));
    st->role = CONN_UNKNOWN;
}

static int find_job_by_id(const char *id) {
    for (int i = 0; i < job_count; i++) {
        if (strcmp(job_table[i].id, id) == 0) return i;
    }
    return -1;
}

static int is_finished_state(JobState st) {
    return (st == ST_DONE || st == ST_KILLED_TIMEOUT ||
            st == ST_KILLED_SIGNAL || st == ST_CANCELLED);
}

static const char *state_to_str(JobState st) {
    switch (st) {
        case ST_QUEUED: return "QUEUED";
        case ST_RUNNING: return "RUNNING";
        case ST_DONE: return "DONE";
        case ST_KILLED_TIMEOUT: return "KILLED_TIMEOUT";
        case ST_KILLED_SIGNAL: return "KILLED_SIGNAL";
        case ST_CANCELLED: return "CANCELLED";
    }
    return "UNKNOWN";
}

static JobState str_to_state(const char *s) {
    if (strcmp(s, "QUEUED") == 0) return ST_QUEUED;
    if (strcmp(s, "RUNNING") == 0) return ST_RUNNING;
    if (strcmp(s, "DONE") == 0) return ST_DONE;
    if (strcmp(s, "KILLED_TIMEOUT") == 0) return ST_KILLED_TIMEOUT;
    if (strcmp(s, "KILLED_SIGNAL") == 0) return ST_KILLED_SIGNAL;
    if (strcmp(s, "CANCELLED") == 0) return ST_CANCELLED;
    return ST_QUEUED;
}

// Единая функция применения записи журнала к таблице в памяти (никогда не пишет в файл!)
static void apply_journal_line(const char *line) {
    if (strncmp(line, "SUBMIT ", 7) == 0) {
        char id[MAX_ID_LEN], enc_cmd[MAX_CMD_LEN * 2];
        int timeout = 0, prio = 0;
        if (sscanf(line + 7, "%31s %d %d %[^\n]", id, &timeout, &prio, enc_cmd) == 4) {
            int idx = find_job_by_id(id);
            if (idx == -1 && job_count < MAX_JOBS) {
                idx = job_count++;
                memset(&job_table[idx], 0, sizeof(JobRecord));
                strncpy(job_table[idx].id, id, MAX_ID_LEN - 1);
                int tnum = 0, seq = 0;
                if (sscanf(id, "T%d-%d", &tnum, &seq) == 2) {
                    job_table[idx].seq = seq;
                    if (seq > max_seq_seen) max_seq_seen = seq;
                }
                decode_newlines(enc_cmd, job_table[idx].command, MAX_CMD_LEN);
                job_table[idx].timeout = timeout;
                job_table[idx].priority = prio;
                job_table[idx].state = ST_QUEUED;
                job_table[idx].worker_fd = -1;
                job_table[idx].created_at = time(NULL);
            }
        }
    } else if (strncmp(line, "STATE ", 6) == 0) {
        char id[MAX_ID_LEN], st_str[32];
        int code = 0;
        if (sscanf(line + 6, "%31s %31s %d", id, st_str, &code) == 3) {
            int idx = find_job_by_id(id);
            if (idx != -1) {
                job_table[idx].state = str_to_state(st_str);
                job_table[idx].detail_code = code;
                if (job_table[idx].state != ST_RUNNING) {
                    job_table[idx].worker_fd = -1;
                }
            }
        }
    }
}

// Единая точка изменения состояния: сначала journal_append(), затем apply_journal_line()
static int record_submit(const char *id, int timeout, int prio, const char *cmd) {
    char enc_cmd[MAX_CMD_LEN * 2];
    encode_newlines(cmd, enc_cmd, sizeof(enc_cmd));

    char line[JOURNAL_LINE];
    snprintf(line, sizeof(line), "SUBMIT %s %d %d %s", id, timeout, prio, enc_cmd);
    if (journal_append(journal_fd, line) != 0) return -1;
    apply_journal_line(line);
    return 0;
}

static int record_state_change(const char *id, JobState new_state, int code) {
    char line[JOURNAL_LINE];
    snprintf(line, sizeof(line), "STATE %s %s %d", id, state_to_str(new_state), code);
    if (journal_append(journal_fd, line) != 0) return -1;
    apply_journal_line(line);
    return 0;
}

static void save_job_output_file(const char *id, const char *data, size_t len) {
    mkdir(OUT_DIR, 0755);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.out", OUT_DIR, id);
    FILE *f = fopen(path, "wb");
    if (f) {
        if (len > 0) fwrite(data, 1, len, f);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
    }
}

// Восстановление состояния из jobs.log при старте
static void recover_from_journal(void) {
    mkdir(OUT_DIR, 0755);
    int rfd = journal_read_open(JOURNAL_PATH);
    if (rfd >= 0) {
        char line[JOURNAL_LINE];
        while (journal_next(rfd, line, sizeof(line)) == 1) {
            apply_journal_line(line);
        }
        journal_read_close(rfd);
    }

    // Все RUNNING задачи при краше возвращаются в QUEUED, все QUEUED ставятся в планировщик
    for (int i = 0; i < job_count; i++) {
        if (job_table[i].state == ST_RUNNING) {
            job_table[i].state = ST_QUEUED;
            job_table[i].worker_fd = -1;
        }
        if (job_table[i].state == ST_QUEUED) {
            scheduler_push(&sched, i, job_table[i].timeout, job_table[i].created_at);
        }
    }

    journal_fd = journal_open(JOURNAL_PATH);
    if (journal_fd < 0) {
        perror("journal_open");
        exit(EXIT_FAILURE);
    }
}

// Раздача задач свободным слотам воркеров по SJF
static void dispatch_jobs(struct pollfd *fds, ConnState *conns, int nfds) {
    while (!scheduler_is_empty(&sched)) {
        int chosen_conn = -1;
        for (int i = 1; i < nfds; i++) {
            if (fds[i].fd != -1 && conns[i].role == CONN_WORKER &&
                conns[i].used_slots < conns[i].max_slots) {
                chosen_conn = i;
                break;
            }
        }
        if (chosen_conn == -1) break; // Нет свободных слотов

        int j_idx = scheduler_pop_sjf(&sched);
        if (j_idx < 0 || job_table[j_idx].state != ST_QUEUED) continue;

        if (record_state_change(job_table[j_idx].id, ST_RUNNING, 0) != 0) {
            scheduler_push(&sched, j_idx, job_table[j_idx].timeout, job_table[j_idx].created_at);
            break;
        }
        job_table[j_idx].worker_fd = fds[chosen_conn].fd;

        char enc_cmd[MAX_CMD_LEN * 2];
        encode_newlines(job_table[j_idx].command, enc_cmd, sizeof(enc_cmd));

        char msg[LINE_BUF_SIZE];
        int len = snprintf(msg, sizeof(msg), "RUN %s %d %s\n",
                           job_table[j_idx].id, job_table[j_idx].timeout, enc_cmd);

        if (send_all(fds[chosen_conn].fd, msg, (size_t)len) == 0) {
            conns[chosen_conn].used_slots++;
            printf("job sent: %s to worker fd=%d\n", job_table[j_idx].id, fds[chosen_conn].fd);
            fflush(stdout);
        } else {
            record_state_change(job_table[j_idx].id, ST_QUEUED, 0);
            scheduler_push(&sched, j_idx, job_table[j_idx].timeout, job_table[j_idx].created_at);
        }
    }
}

static void handle_line_message(int idx, struct pollfd *fds, ConnState *conns, int nfds, const char *line) {
    int fd = fds[idx].fd;

    if (strncmp(line, "WORKER ", 7) == 0) {
        int k = atoi(line + 7);
        if (k < 1) k = 1;
        conns[idx].role = CONN_WORKER;
        conns[idx].max_slots = k;
        conns[idx].used_slots = 0;
        printf("worker connected: fd=%d K=%d\n", fd, k);
        fflush(stdout);
        return;
    }

    if (strncmp(line, "SUBMIT ", 7) == 0) {
        conns[idx].role = CONN_CLIENT;
        int timeout = 0, prio = 0;
        char enc_cmd[MAX_CMD_LEN * 2];
        if (sscanf(line + 7, "%d %d %[^\n]", &timeout, &prio, enc_cmd) == 3 && timeout >= 1) {
            if (job_count >= MAX_JOBS) {
                send_all(fd, "ERR QUEUE_FULL\n", 15);
                return;
            }
            int next_seq = max_seq_seen + 1;
            char new_id[MAX_ID_LEN];
            snprintf(new_id, sizeof(new_id), "T%d-%d", TEAM_NUMBER, next_seq);

            char cmd[MAX_CMD_LEN];
            decode_newlines(enc_cmd, cmd, sizeof(cmd));

            if (record_submit(new_id, timeout, prio, cmd) == 0) {
                send_all(fd, "OK\n", 3);
            } else {
                send_all(fd, "ERR SUBMIT_FAILED\n", 20);
            }
            return;
        }
    }

    if (strncmp(line, "STATUS ", 7) == 0) {
        conns[idx].role = CONN_CLIENT;
        char id[MAX_ID_LEN];
        if (sscanf(line + 7, "%31s", id) == 1) {    
            int idx = find_job_by_id(id);
            if (idx != -1) {
                send_all(fd, "OK\n", 3);
            } else {
                send_all(fd, "ERR JOB_NOT_FOUND\n", 21);
            }
            return;
        }
    }

    if (strncmp(line, "CANCEL ", 7) == 0) {
        conns[idx].role = CONN_CLIENT;
        char id[MAX_ID_LEN];
        if (sscanf(line + 7, "%31s", id) == 1) {
            int idx = find_job_by_id(id);
            if (idx != -1) {
                send_all(fd, "OK\n", 3);
            } else {
                send_all(fd, "ERR JOB_NOT_FOUND\n", 21);
            }
            return;
        }
    }   
    send_all(fd, "ERR UNKNOWN_COMMAND\n", 21);
}

static void handle_worker_message(int idx, struct pollfd *fds, ConnState *conns, int nfds, const char *line) {
    int fd = fds[idx].fd;

    if (strncmp(line, "RUN ", 4) == 0) {
        conns[idx].role = CONN_WORKER;
        char id[MAX_ID_LEN], enc_cmd[MAX_CMD_LEN * 2];
        int timeout = 0;
        if (sscanf(line + 4, "%31s %d %[^\n]", id, &timeout, enc_cmd) == 3 && timeout >= 1) {
            int idx = find_job_by_id(id);
            if (idx != -1) {
                job_table[idx].state = ST_RUNNING;
                job_table[idx].worker_fd = fd;
                job_table[idx].created_at = time(NULL);
            }
        }
        return;
    }

    if (strncmp(line, "DONE ", 5) == 0) {
        conns[idx].role = CONN_WORKER;
        char id[MAX_ID_LEN];
        if (sscanf(line + 5, "%31s", id) == 1) {
            int idx = find_job_by_id(id);
            if (idx != -1) {
                job_table[idx].state = ST_DONE;
                job_table[idx].worker_fd = -1;
                job_table[idx].detail_code = 0;
                record_state_change(id, ST_DONE, 0);
                return;
            }
        }
    }

    if (strncmp(line, "KILLED ", 7) == 0) {
        conns[idx].role = CONN_WORKER;
        char id[MAX_ID_LEN];
        if (sscanf(line + 7, "%31s", id) == 1) {
            int idx = find_job_by_id(id);
            if (idx != -1) {
                job_table[idx].state = ST_KILLED_SIGNAL;
                job_table[idx].worker_fd = -1;
                job_table[idx].detail_code = atoi(line + 7);
                record_state_change(id, ST_KILLED_SIGNAL, job_table[idx].detail_code);
                return;
            }
        }
    }
    send_all(fd, "ERR UNKNOWN_COMMAND\n", 21);
}