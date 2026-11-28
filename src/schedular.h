#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <time.h>

#define MAX_QUEUE_SIZE 512
#define AGING_FACTOR 1 // Каждая секунда ожидания уменьшает эффективный timeout на 1с

typedef struct {
    int job_index;      // Индекс задачи в общей таблице сервера
    int timeout;        // Исходный таймаут (оценка длины задачи для SJF)
    time_t enqueue_time;
} QueueEntry;

typedef struct {
    QueueEntry items[MAX_QUEUE_SIZE];
    int count;
} Scheduler;

void scheduler_init(Scheduler *s);
int scheduler_push(Scheduler *s, int job_index, int timeout, time_t enqueue_time);
int scheduler_pop_sjf(Scheduler *s);
void scheduler_remove(Scheduler *s, int job_index);
int scheduler_is_empty(const Scheduler *s);

#endif