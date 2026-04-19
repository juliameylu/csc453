/*
 * scheduler.c
 * Implements round-robin scheduler used by the LWP
 * library, manages runnable threads.
 */

#include "scheduler.h"

#define sched_prev sched_one
#define sched_next sched_two

static int scheduler_thread_count = 0;
static thread sched_head = NULL;
static thread sched_tail = NULL;

/*
 * This is to be called before any threads are admitted to the
 * scheduler. It allows the scheduler to set up. This one is
 * allowed to be NULL, so do not call it if it is.
 */
void rr_init(void)
{
        scheduler_thread_count = 0;
        sched_head = NULL;
        sched_tail = NULL;
}

/*
 * This is to be called when the lwp library is done with a
 * scheduler to allow it to clean up. This, too, is allowed to
 * be NULL, so do not call it if it is.
 */
void rr_shutdown(void)
{
        scheduler_thread_count = 0;
        sched_head = NULL;
        sched_tail = NULL;
}

/* Add the passed context to the scheduler’s scheduling pool. */
void rr_admit(thread new)
{
        if (new == NULL) {
                return;
        } else if (sched_head == NULL) {
                new->sched_prev = NULL;
                new->sched_next = NULL;
                sched_head = new;
                sched_tail = new;
        } else {
                sched_tail->sched_next = new;
                new->sched_prev = sched_tail;
                new->sched_next = NULL;
                sched_tail = new;
        }
        scheduler_thread_count += 1;
}

/* Remove the passed context from the scheduler’s scheduling pool. */
void rr_remove(thread victim)
{
        thread prev_thread;
        thread next_thread;
        thread current_thread = sched_head;
        bool found = false;

        if (victim == NULL) {
                return;
        }
        /* check if victim is actually in the queue */
        while (current_thread != NULL) {
                if (current_thread == victim) {
                        found = true;
                        break;
                }
                current_thread = current_thread->sched_next;
        }
        if (!found) {
                return;
        }

        prev_thread = victim->sched_prev;
        next_thread = victim->sched_next;

        if (victim == sched_head && victim == sched_tail) {
                sched_head = NULL;
                sched_tail = NULL;
        } else if (victim == sched_head) {
                next_thread->sched_prev = NULL;
                sched_head = next_thread;
        } else if (victim == sched_tail) {
                prev_thread->sched_next = NULL;
                sched_tail = prev_thread;
        } else {
                prev_thread->sched_next = next_thread;
                next_thread->sched_prev = prev_thread;
        }
        victim->sched_prev = NULL;
        victim->sched_next = NULL;
        scheduler_thread_count -= 1;
}

/* Return the next thread to be run or NULL if there isn’t one. */
thread rr_next(void)
{
        thread old_head;
        thread next_thread;

        if (sched_head == NULL) {
                return NULL;
        }

        if (sched_head == sched_tail) {
                return sched_head;
        }

        old_head = sched_head;
        next_thread = sched_head->sched_next;

        sched_tail->sched_next = sched_head;
        sched_head->sched_prev = sched_tail;
        sched_head->sched_next = NULL;
        next_thread->sched_prev = NULL;

        sched_tail = sched_head;
        sched_head = next_thread;

        return old_head;
}

/*
 * Return the number of runnable threads. This will be useful for
 * lwp_wait() in determining if waiting makes sense.
 */
int rr_qlen(void)
{
        return scheduler_thread_count;
}

static struct scheduler rr_publish = {
        rr_init,
        rr_shutdown,
        rr_admit,
        rr_remove,
        rr_next,
        rr_qlen
};

scheduler RoundRobin = &rr_publish;
