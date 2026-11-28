#include "scheduler.h"

void scheduler_init(Scheduler *s) {
    s->count = 0;
}

int scheduler_is_empty(const Scheduler *s) {
    return s->count == 0;
}

int scheduler_push(Scheduler *s, int job_index, int timeout, time_t enqueue_time) {
    for (int i = 0; i < s->count; i++) {
        if (s->items[i].job_index == job_index) return 0;
    }
    if (s->count >= MAX_QUEUE_SIZE) return -1;
    s->items[s->count].job_index = job_index;
    s->items[s->count].timeout = timeout;
    s->items[s->count].enqueue_time = enqueue_time;
    s->count++;
    return 0;
}

int scheduler_pop_sjf(Scheduler *s) {
    if (s->count == 0) return -1;

    time_t now = time(NULL);
    int best_i = 0;
    long best_score = (long)s->items.timeout - (long)(now - s->items.enqueue_time) * AGING_FACTOR;

    for (int i = 1; i < s->count; i++) {
        long waited = (long)(now - s->items[i].enqueue_time);
        long score = (long)s->items[i].timeout - waited * AGING_FACTOR;
        if (score < best_score) {
            best_score = score;
            best_i = i;
        }
    }

    int chosen_job = s->items[best_i].job_index;
    for (int i = best_i; i < s->count - 1; i++) {
        s->items[i] = s->items[i + 1];
    }
    s->count--;
    return chosen_job;
}

void scheduler_remove(Scheduler *s, int job_index) {
    for (int i = 0; i < s->count; i++) {
        if (s->items[i].job_index == job_index) {
            for (int j = i; j < s->count - 1; j++) {
                s->items[j] = s->items[j + 1];
            }
            s->count--;
            return;
        }
    }
}