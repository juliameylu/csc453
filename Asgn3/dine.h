#ifndef DINE_H
#define DINE_H

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef DAWDLEFACTOR
#define DAWDLEFACTOR 1000
#endif

/*
 * All philosophers start out in the changing state, and start out
 * hungry. That is, they will all try to eat before beginning to think.
 */
#define CHANGING 0
#define EATING 1
#define THINKING 2

/* indicates semaphore is shared between threads of a process */
#define SHARED_BETWEEN_THREADS_FLAG 0

#define DEFAULT_CYCLES 1
#define FORK_INIT_VAL 1
#define PRINTING_INIT_VAL 1
#define THREAD_FAILURE ((void *)-1)
#define THREAD_SUCCESS NULL

#ifndef NUM_PHILOSOPHERS
#define NUM_PHILOSOPHERS 5
#endif

/* at least 2 cuz phils need at least 2 forks to eat */
#if NUM_PHILOSOPHERS < 2
#error "NUM_PHILOSOPHERS must be at least 2"
#endif

/* printing stuff */
#define NULL_CHAR_LEN 1
#define LEADING_SPACE_LEN 1

typedef struct philosopherinfo_st {
        pthread_t tid;
        int id;
        int left_fork_index;
        int right_fork_index;
        int cycles_left;
        int state;
        sem_t *left_fork;
        sem_t *right_fork;
        sem_t *printing_lock;
        bool holding_left;
        bool holding_right;
        struct philosopherinfo_st *all_philosophers;
} philinfo;

void print_status_change(philinfo *philosophers);
void dawdle(void);
void *philosopher(void *arg);
void build_table_border(char *header_border, int header_border_len);
void print_table_border(char *header_border);
void print_phil_labels(void);
void print_table_header(char *header_border);

#endif
