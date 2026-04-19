/*
 * lwp.c
 * Implements the LWP library, including thread
 * creation, scheduling, yielding, termination,
 * waiting, and internal thread bookkeeping.
 */

#include "lwp.h"
#include "scheduler.h"

#include <stddef.h>
#include <stdbool.h>
#include <sys/resource.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>

#define live_next lib_one
#define wait_next lib_two
#define sched_prev sched_one
#define sched_next sched_two

/* NO_THREAD is defined as 0 so can't use 0 */
#define FIRST_TID 1
/* 8 MB */
#define DEFAULT_STACK_SIZE (8 * 1024 * 1024)
#define LWP_EXIT_RETURN_OFFSET (-1)
#define WRAPPER_RETURN_OFFSET (-2)
#define SAVED_BP_OFFSET (-3)

#define FAKE_SAVED_BP 0
#define STACK_ALIGNMENT 16
#define DEBUG_MSG_BUF_SIZE 256

/* Tuple that describes a zombie */
typedef struct zombie {
  thread zombie_thread;
  struct zombie *next;
} *zombie;

static scheduler current_scheduler = NULL;
static thread curr_thread = NULL;

/* queue of waiting threads */
static thread waiting_head = NULL;
static thread waiting_tail = NULL;

/* queue of live threads */
static thread live_head = NULL;
static thread live_tail = NULL;

/* queue of zombie threads */
static zombie zombies_head = NULL;
static zombie zombies_tail = NULL;

static tid_t curr_tid = FIRST_TID;

static void lwp_wrap(lwpfun fun, void *arg);
static tid_t assign_tid(void);
static size_t get_stack_size(void);
static size_t round_up_page_size(size_t size, size_t page_size);
static size_t round_down_16(size_t size);
static tid_t reap_and_remove(thread dead_thread, int *status);
static void insert_zombie(thread new);
static zombie find_zombie(thread curr);
static void remove_zombie(thread victim);
static void insert_live(thread new);
static void remove_live(thread victim);
static void insert_waiting(thread new);
static void remove_waiting(thread victim);
static void print_custom_debug_msg(const char *msg);

/*
 * Makes a new lightweight thread, initializes its context, and
 * returns its thread ID. When the scheduler later selects it,
 * lwp_yield() restores that context so the thread begins
 * executing its function.
 */
tid_t lwp_create(lwpfun function, void *argument)
{
        thread new_thread;
        size_t stack_size;
        void *stack_base;
        char *stack_top;
        unsigned long *aligned_stack_top;
        unsigned long *lwp_exit_return_pointer;
        unsigned long *wrapper_return_pointer;
        unsigned long *saved_bp_pointer;
        scheduler scheduler_in_use;
        char msg[DEBUG_MSG_BUF_SIZE];
        int len;

        /* defensive: works if thread type changes */
        new_thread = malloc(sizeof(*new_thread));
        /* return NO THREAD if the thread cannot be created */
        if (new_thread == NULL) {
                print_custom_debug_msg(
                        "failure in lwp_create(): malloc thread\n"
                );
                return NO_THREAD;
        }
        new_thread->tid = assign_tid();

        stack_size = get_stack_size();
        if (stack_size == 0) {
                free(new_thread);
                print_custom_debug_msg(
                        "failure in lwp_create(): stack size is 0\n"
                );
                return NO_THREAD;
        }

        new_thread->stacksize = stack_size;
        stack_base = mmap(
                NULL,
                stack_size,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK,
                -1,
                0
        );
        if (stack_base == MAP_FAILED) {
                free(new_thread);
                print_custom_debug_msg("failure in lwp_create(): mmap\n");
                return NO_THREAD;
        }
        new_thread->stack = stack_base;

        /* Note: pop moves rsp up to higher addresses, flipped */
        /*
         * Stack structure:
         * top: somewhere (old BP) = NULL
         * middle: return address = lwp_wrap
         * bottom: return address = lwp_exit
         */
        /* start of stack is at the top of stack memory block */
        stack_top = (char *)stack_base + stack_size;
        /* block's top must be 16 byte aligned */
        aligned_stack_top =
                (unsigned long *)round_down_16((size_t)stack_top);

        /*
         * defensive: if lwp_wrap returns unexpectedly,
         * safely return to lwp_exit.
         */
        lwp_exit_return_pointer = aligned_stack_top + LWP_EXIT_RETURN_OFFSET;
        wrapper_return_pointer = aligned_stack_top + WRAPPER_RETURN_OFFSET;
        saved_bp_pointer = aligned_stack_top + SAVED_BP_OFFSET;

        *lwp_exit_return_pointer = (unsigned long) lwp_exit;
        /* return to wrapper */
        *wrapper_return_pointer = (unsigned long) lwp_wrap;
        *saved_bp_pointer = FAKE_SAVED_BP;

        /* Pass in the wrapper's args. */
        new_thread->state.rdi = (unsigned long) function;
        new_thread->state.rsi = (unsigned long) argument;

        new_thread->state.rsp = (unsigned long) saved_bp_pointer;
        new_thread->state.rbp = (unsigned long) saved_bp_pointer;
        new_thread->state.fxsave = FPU_INIT;

        /* status */
        new_thread->status = LWP_LIVE;

        new_thread->live_next = NULL;
        new_thread->wait_next = NULL;
        new_thread->sched_prev = NULL;
        new_thread->sched_next = NULL;
        new_thread->exited = NULL;

        insert_live(new_thread);

        /* admit to scheduler */
        /* default to round robin */
        scheduler_in_use = lwp_get_scheduler();
        if (scheduler_in_use == NULL) {
                scheduler_in_use = RoundRobin;
                current_scheduler = RoundRobin;
        }
        scheduler_in_use->admit(new_thread);

        /* debug msg */
        len = snprintf(msg, sizeof(msg),
                "success in lwp_create(): thread %lu has been created\n",
                (unsigned long)new_thread->tid);
        if (len >= 0) {
                print_custom_debug_msg(msg);
        }

        return new_thread->tid;
}

/*
 * Starts the LWP system. Converts the calling thread into a LWP,
 * adds it to the scheduler, and yields to whichever thread the
 * scheduler chooses.
 */
void lwp_start(void)
{
        thread new_thread;
        scheduler scheduler_in_use;

        new_thread = malloc(sizeof(*new_thread));
        if (new_thread == NULL) {
                print_custom_debug_msg(
                        "failure in lwp_start(): malloc thread\n"
                );
                exit(EXIT_FAILURE);
        }
        /* already has its own stack */
        new_thread->tid = assign_tid();
        new_thread->stack = NULL;
        new_thread->stacksize = 0;
        new_thread->status = LWP_LIVE;
        new_thread->live_next = NULL;
        new_thread->wait_next = NULL;
        new_thread->sched_prev = NULL;
        new_thread->sched_next = NULL;
        new_thread->exited = NULL;

        insert_live(new_thread);

        /* admit to scheduler */
        /* default to round robin */
        scheduler_in_use = lwp_get_scheduler();
        if (scheduler_in_use == NULL) {
                scheduler_in_use = RoundRobin;
                current_scheduler = RoundRobin;
        }
        scheduler_in_use->admit(new_thread);

        curr_thread = new_thread;
        print_custom_debug_msg(
                "success in lwp_start(): lwp system started\n"
        );
        lwp_yield();
}

/*
 * Yields control to the next thread as indicated by the scheduler.
 * If there is no next thread, calls exit(3) with the termination
 * status of the calling thread.
 */
void lwp_yield(void)
{
        scheduler scheduler_in_use;
        thread next_thread;
        thread prev_thread;
        char msg[DEBUG_MSG_BUF_SIZE];
        int len;

        scheduler_in_use = lwp_get_scheduler();
        if (scheduler_in_use == NULL) {
                scheduler_in_use = RoundRobin;
                current_scheduler = RoundRobin;
                /*
                 * Don't init or else it will override previous
                 * round robin state.
                 */
        }

        next_thread = scheduler_in_use->next();
        if (curr_thread == NULL && next_thread == NULL) {
                /*
                 * Defensive: called before lwp_start and no
                 * threads are in the scheduler.
                 */
                print_custom_debug_msg(
                        "lwp_yield(): there are no threads, returning\n"
                );
                return;
        } else if (curr_thread != NULL && next_thread == NULL) {
                /* if no next thread, check if calling thread terminated */
                /* if it didn't */
                if (!(LWPTERMINATED(curr_thread->status))) {
                        /* just go back, don't need to load any new context */
                        print_custom_debug_msg(
                                "lwp_yield(): live current thread, "
                                "no next thread, returning\n"
                        );
                        return;
                }
                /* if it did, terminate program with extracted exit code */
                print_custom_debug_msg(
                        "lwp_yield(): current thread terminated, "
                        "no next thread, exiting process\n"
                );
                exit(LWPTERMSTAT(curr_thread->status));
        } else if (curr_thread == NULL && next_thread != NULL) {
                /*
                 * Defensive: called before lwp_start and a
                 * thread was inserted into the scheduler since
                 * admit() is not static.
                 */
                curr_thread = next_thread;
                print_custom_debug_msg(
                        "lwp_yield(): no current thread, "
                        "starting next runnable thread\n"
                );
                len = snprintf(msg, sizeof(msg),
                        "lwp_yield(): no current thread, starting thread %lu\n",
                        (unsigned long)curr_thread->tid);
                if (len >= 0) {
                        print_custom_debug_msg(msg);
                }
                
                swap_rfiles(NULL, &curr_thread->state);
        } else if (curr_thread == next_thread) {
                /*
                 * If only one thread exists, next() returns the
                 * same thread as the current one.
                 */
                print_custom_debug_msg(
                        "lwp_yield(): next thread is current "
                        "thread, continuing\n"
                );
                return;
        } else {
                /*
                 * There is a current thread and a different next
                 * thread to switch to.
                 */
                prev_thread = curr_thread;
                curr_thread = next_thread;
                /*
                 * Save old context, load new context, and return
                 * to where next_thread left off.
                 */
                len = snprintf(msg, sizeof(msg),
                        "lwp_yield(): switching context "
                        "from thread %lu to thread %lu\n",
                        (unsigned long)prev_thread->tid,
                        (unsigned long)next_thread->tid);
                if (len >= 0) {
                        print_custom_debug_msg(msg);
                }
                swap_rfiles(&prev_thread->state, &next_thread->state);
        }
}


/*
 * Terminates the calling thread. Its termination status becomes
 * the low 8 bits of the passed integer. The thread’s resources
 * will be deallocated once it is waited for in lwp wait().
 * Yields control to the next thread using lwp yield()
 */
void lwp_exit(int status)
{
        scheduler scheduler_in_use;
        thread thread_to_wake;
        char msg[DEBUG_MSG_BUF_SIZE];
        int len;

        if (curr_thread == NULL) {
                print_custom_debug_msg(
                        "lwp_exit(): no thread to terminate, returning\n"
                );
                return;
        }
        
        /* set to terminated */
        curr_thread->status = MKTERMSTAT(LWP_TERM, status);
        /* add to zombie queue */
        insert_zombie(curr_thread);

        scheduler_in_use = lwp_get_scheduler();
        if (scheduler_in_use == NULL) {
                scheduler_in_use = RoundRobin;
                current_scheduler = RoundRobin;
        }
        /* remove thread from scheduler */
        scheduler_in_use->remove(curr_thread);
        /* remove thread from live queue*/
        remove_live(curr_thread);

        thread_to_wake = waiting_head;
        /* if there is a thread in the waiting queue */
        if (thread_to_wake != NULL) {
                /*
                 * Associate the oldest head with the current
                 * thread so that it knows who to reap.
                 */
                thread_to_wake->exited = curr_thread;
                /* wake the waiter by readmitting to scheduler */
                scheduler_in_use->admit(thread_to_wake);

                /* remove the awakened from waiting queue */
                remove_waiting(thread_to_wake);
        }

        len = snprintf(msg, sizeof(msg),
                "success in lwp_exit(): thread %lu has "
                "been terminated, yielding\n",
                (unsigned long)curr_thread->tid);
        if (len >= 0) {
                print_custom_debug_msg(msg);
        }
        lwp_yield();
}

/*
 * Reaps a terminated thread, frees its resources, and optionally
 * reports its exit status. If no terminated thread is available,
 * it blocks unless waiting would be pointless, in which case it
 * returns NO_THREAD.
 */
tid_t lwp_wait(int *status)
{
        scheduler scheduler_in_use;
        thread thread_to_reap;
        tid_t terminated_tid;
        char msg[DEBUG_MSG_BUF_SIZE];
        int len;

        scheduler_in_use = lwp_get_scheduler();
        if (scheduler_in_use == NULL) {
                scheduler_in_use = RoundRobin;
                current_scheduler = RoundRobin;
        }
        /* defensive: if lwp_wait is called before lwp_start */
        if (curr_thread == NULL) {
                print_custom_debug_msg(
                        "lwp_wait(): no current thread, "
                        "returning NO_THREAD\n"
                );
                return NO_THREAD;
        }

        thread_to_reap = NULL;
        if (curr_thread->exited != NULL) {
                thread_to_reap = curr_thread->exited;
                curr_thread->exited = NULL;
        } else if (zombies_head != NULL) {
                thread_to_reap = zombies_head->zombie_thread;
        } else if (scheduler_in_use->qlen() <= 1) {
                print_custom_debug_msg(
                        "lwp_wait(): waiting would block forever, "
                        "returning NO_THREAD\n"
                );
                return NO_THREAD;
        } else {
                /*
                 * Block by removing the calling thread from the
                 * scheduler and inserting it into the waiting
                 * queue.
                 */
                scheduler_in_use->remove(curr_thread);
                insert_waiting(curr_thread);

                print_custom_debug_msg(
                        "lwp_wait(): no terminated thread "
                        "available; blocking and yielding\n"
                );

                /* yield to let next thread run while blocked */
                lwp_yield();

                /*
                 * After the waiting thread comes back, reap the
                 * thread it's associated with.
                 */
                if (curr_thread->exited != NULL) {
                        thread_to_reap = curr_thread->exited;
                        curr_thread->exited = NULL;
                } else {
                        len = snprintf(msg, sizeof(msg),
                                "lwp_wait(): thread %lu resumed "
                                "without an exited thread; "
                                "returning NO_THREAD\n",
                                (unsigned long)curr_thread->tid);
                        if (len >= 0) {
                                print_custom_debug_msg(msg);
                        }

                        return NO_THREAD;
                }
        }

        /* reap_and_remove has null checking */
        terminated_tid = reap_and_remove(thread_to_reap, status);
        return terminated_tid;
}

/*
 * Returns the tid of the calling LWP or NO_THREAD if not called by
 * a LWP
 */
tid_t lwp_gettid(void)
{
        if (curr_thread == NULL) {
                print_custom_debug_msg(
                        "lwp_gettid(): no current thread, "
                        "returning NO_THREAD\n"
                );
                return NO_THREAD;
        }
        return curr_thread->tid;
}

/*
 * Returns the thread corresponding to the given thread ID, or NULL
 * if the ID is invalid
 */
thread tid2thread(tid_t tid)
{
        thread live_thread = live_head;
        thread waiting_thread = waiting_head;
        zombie curr_zombie = zombies_head;
        char msg[DEBUG_MSG_BUF_SIZE];
        int len;

        /* search in live queue */
        while (live_thread) {
                if (live_thread->tid == tid) {
                        len = snprintf(msg, sizeof(msg),
                                "tid2thread(): found live thread %lu\n",
                                (unsigned long)tid);
                        if (len >= 0) {
                                print_custom_debug_msg(msg);
                        }

                        return live_thread;
                }
                live_thread = live_thread->live_next;
        }

        /* search in waiting queue */
        while (waiting_thread) {
                if (waiting_thread->tid == tid) {
                        len = snprintf(msg, sizeof(msg),
                                "tid2thread(): found waiting thread %lu\n",
                                (unsigned long)tid);
                        if (len >= 0) {
                                print_custom_debug_msg(msg);
                        }

                        return waiting_thread;
                }
                waiting_thread = waiting_thread->wait_next;
        }

        /* search in zombie queue */
        while (curr_zombie) {
                if (curr_zombie->zombie_thread->tid == tid) {
                        len = snprintf(msg, sizeof(msg),
                                "tid2thread(): found zombie thread %lu\n",
                                (unsigned long)tid);
                        if (len >= 0) {
                                print_custom_debug_msg(msg);
                        }

                        return curr_zombie->zombie_thread;
                }
                curr_zombie = curr_zombie->next;
        }
        
        /* not found */
        len = snprintf(msg, sizeof(msg),
                "tid2thread(): thread %lu not found, returning NULL\n",
                (unsigned long)tid);
        if (len >= 0) {
                print_custom_debug_msg(msg);
        }
        return NULL;
}

/*
 * Causes the LWP package to use the given scheduler to choose the
 * next process to run. Transfers all threads from the old
 * scheduler to the new one in next() order. If scheduler is NULL
 * the library returns to round-robin scheduling
 */
void lwp_set_scheduler(scheduler fun)
{
        scheduler new_scheduler;
        thread scheduler_thread;
        int len_of_queue;
        int i;

        /* if fun is null, default to round robin */
        if (fun == NULL) {
                print_custom_debug_msg(
                        "lwp_set_scheduler(): defaulting "
                        "to Round Robin\n"
                );
                new_scheduler = RoundRobin;
        } else {
                print_custom_debug_msg(
                        "lwp_set_scheduler(): switching schedulers\n"
                );
                new_scheduler = fun;
        }

        /* edge case: if same pointer, don't need to transfer */
        if (new_scheduler == current_scheduler) {
                print_custom_debug_msg(
                        "lwp_set_scheduler(): new scheduler is "
                        "already current; returning\n"
                );
                return;
        }

        if (new_scheduler->init != NULL) {
                new_scheduler->init();
        }

        /* only transfer threads from old if not null */
        if (current_scheduler != NULL) {
                print_custom_debug_msg(
                        "lwp_set_scheduler(): transferring "
                        "runnable threads to new scheduler\n"
                );

                scheduler_thread = current_scheduler->next();
                len_of_queue = current_scheduler->qlen();
                for (i = 0; i < len_of_queue; i++) {
                        current_scheduler->remove(scheduler_thread);
                        new_scheduler->admit(scheduler_thread);

                        scheduler_thread = current_scheduler->next();
                }

                /* shutdown old scheduler if defined */
                if (current_scheduler->shutdown != NULL) {
                        current_scheduler->shutdown();
                }
        }

        print_custom_debug_msg(
                "lwp_set_scheduler(): scheduler switch complete\n"
        );
        current_scheduler = new_scheduler;
        
}

/* Returns the pointer to the current scheduler */
scheduler lwp_get_scheduler(void)
{
        return current_scheduler;
}

/* Call the given lwpfunction with the given argument.
 * Calls lwp exit() with its return value.
 */
static void lwp_wrap(lwpfun fun, void *arg)
{
        int rval;
        rval = fun(arg);
        lwp_exit(rval);
}

tid_t assign_tid(void)
{
        /* post-increment */
        return curr_tid++;
}

/*
 * Uses the soft resource limit.
 * If RLIMIT_STACK does not exist or if its value is RLIM INFINITY,
 * default to 8MB.
 * Rounds up to the nearest multiple of the page size.
 */
static size_t get_stack_size(void)
{
        struct rlimit rlp;
        rlim_t raw_stack_size;
        long raw_page_size;
        size_t page_size;
        size_t unrounded_stack_size;
        size_t stack_size;

        if (getrlimit(RLIMIT_STACK, &rlp) == -1
            || rlp.rlim_cur == RLIM_INFINITY) {
                raw_stack_size = DEFAULT_STACK_SIZE;
        } else {
                raw_stack_size = rlp.rlim_cur;
        }

        raw_page_size = sysconf(_SC_PAGE_SIZE);
        if (raw_page_size < 0) {
                /* not mem safe */
                return 0;
        }
        page_size = (size_t)raw_page_size;
        unrounded_stack_size = (size_t)raw_stack_size;

        stack_size = round_up_page_size(unrounded_stack_size, page_size);

        return stack_size;
}

/*
 * Rounds size up to the next multiple of page size
 */
static size_t round_up_page_size(size_t size, size_t page_size)
{
        return ((size + page_size - 1) / page_size) * page_size;
}

/*
 * Rounds size down to the next multiple of 16 bytes
 */
static size_t round_down_16(size_t size)
{
        return size - (size % STACK_ALIGNMENT);
}

/*
 * Reaps terminated thread, removes from zombie queue,
 * frees resources, optionally reports exit status
 */
static tid_t reap_and_remove(thread dead_thread, int *status)
{
        zombie zombie_node;
        unsigned long *zombie_stack;
        size_t zombie_stack_size;
        tid_t zombie_tid;
        char msg[DEBUG_MSG_BUF_SIZE];
        int len;

        if (dead_thread == NULL) {
                print_custom_debug_msg(
                        "reap_and_remove(): no thread to reap, "
                        "returning NO_THREAD\n"
                );
                return NO_THREAD;
        }

        zombie_node = find_zombie(dead_thread);
        if (zombie_node == NULL) {
                len = snprintf(msg, sizeof(msg),
                        "reap_and_remove(): thread %lu not found "
                        "in zombie queue, returning NO_THREAD\n",
                        (unsigned long)dead_thread->tid);
                if (len >= 0) {
                        print_custom_debug_msg(msg);
                }
                return NO_THREAD;
        }
        remove_zombie(dead_thread);

        len = snprintf(msg, sizeof(msg),
                "reap_and_remove(): reaping thread %lu\n",
                (unsigned long)dead_thread->tid);
        if (len >= 0) {
                print_custom_debug_msg(msg);
        }

        /* free thread's stack */
        zombie_stack = dead_thread->stack;
        zombie_stack_size = dead_thread->stacksize;
        if (zombie_stack != NULL) {
                if (munmap(zombie_stack, zombie_stack_size) == -1) {
                        len = snprintf(msg, sizeof(msg),
                                "failure in reap_and_remove(): "
                                "munmap failed while reaping "
                                "thread %lu, exiting process\n",
                                (unsigned long)dead_thread->tid);
                        if (len >= 0) {
                                print_custom_debug_msg(msg);
                        }

                        /* munmap sets own errno */
                        exit(EXIT_FAILURE);
                }
        }

        /* save termination status into status if status is non-NULL */
        if (status != NULL) {
                *status = LWPTERMSTAT(dead_thread->status);
        }

        zombie_tid = dead_thread->tid;
        /* Note: REMEMBER TO DEQUEUE/REMOVE FIRST */
        /* free thread */
        free(dead_thread);
        /* free zombie wrapper */
        free(zombie_node);

        len = snprintf(msg, sizeof(msg),
                "reap_and_remove(): thread %lu reaped successfully\n",
                (unsigned long)zombie_tid);
        if (len >= 0) {
                print_custom_debug_msg(msg);
        }

        return zombie_tid;
}

static void insert_zombie(thread new)
{
        zombie new_zombie;
        char msg[DEBUG_MSG_BUF_SIZE];
        int len;

        if (new == NULL) {
                return;
        }

        new_zombie = malloc(sizeof(struct zombie));
        if (new_zombie == NULL) {
                /* if malloc fails everything fails */
                len = snprintf(msg, sizeof(msg),
                        "failure in insert_zombie(): malloc for "
                        "zombie wrapper for thread %lu\n",
                        (unsigned long)new->tid);
                if (len >= 0) {
                        print_custom_debug_msg(msg);
                }

                exit(EXIT_FAILURE);
        }
        new_zombie->zombie_thread = new;
        new_zombie->next = NULL;

        if (zombies_head == NULL) {
                zombies_head = new_zombie;
                zombies_tail = new_zombie;
        } else {
                zombies_tail->next = new_zombie;
                new_zombie->next = NULL;
                zombies_tail = new_zombie;
        }
}

static zombie find_zombie(thread curr)
{
        zombie curr_zombie = zombies_head;

        while (curr_zombie) {
                if (curr_zombie->zombie_thread == curr) {
                        return curr_zombie;
                }
                curr_zombie = curr_zombie->next;
        }
        return NULL;
}

static void remove_zombie(thread victim)
{
        zombie prev_zombie = NULL;
        zombie next_zombie = NULL;
        zombie curr_zombie = zombies_head;
        bool found = false;

        if (victim == NULL) {
                return;
        }

        /* check if victim is actually in the queue */
        while (curr_zombie != NULL) {
                if (curr_zombie->zombie_thread == victim) {
                        found = true;
                        break;
                }
                prev_zombie = curr_zombie;
                curr_zombie = curr_zombie->next;
        }
        if (!found) {
                return;
        }

        next_zombie = curr_zombie->next;

        if (curr_zombie == zombies_head && curr_zombie == zombies_tail) {
                zombies_head = NULL;
                zombies_tail = NULL;
        } else if (curr_zombie == zombies_head) {
                zombies_head = next_zombie;
        } else if (curr_zombie == zombies_tail) {
                prev_zombie->next = NULL;
                zombies_tail = prev_zombie;
        } else {
                prev_zombie->next = next_zombie;
        }
        curr_zombie->next = NULL;
}


static void insert_live(thread new)
{
        if (new == NULL) {
                return;
        } else if (live_head == NULL) {
                new->live_next = NULL;
                live_head = new;
                live_tail = new;
        } else {
                live_tail->live_next = new;
                new->live_next = NULL;
                live_tail = new;
        }
}


static void remove_live(thread victim)
{
        thread prev_thread = NULL;
        thread next_thread = NULL;
        thread curr = live_head;
        bool found = false;

        if (victim == NULL) {
                return;
        }
        /* check if victim is actually in the queue */
        while (curr != NULL) {
                if (curr == victim) {
                        found = true;
                        break;
                }
                prev_thread = curr;
                curr = curr->live_next;
        }
        if (!found) {
                return;
        }

        next_thread = curr->live_next;

        if (curr == live_head && curr == live_tail) {
                live_head = NULL;
                live_tail = NULL;
        } else if (curr == live_head) {
                live_head = next_thread;
        } else if (curr == live_tail) {
                prev_thread->live_next = NULL;
                live_tail = prev_thread;
        } else {
                prev_thread->live_next = next_thread;
        }
        curr->live_next = NULL;
}

static void insert_waiting(thread new)
{
        if (new == NULL) {
                return;
        } else if (waiting_head == NULL) {
                new->wait_next = NULL;
                waiting_head = new;
                waiting_tail = new;
        } else {
                waiting_tail->wait_next = new;
                new->wait_next = NULL;
                waiting_tail = new;
        }
}


static void remove_waiting(thread victim)
{
        thread prev_thread = NULL;
        thread next_thread = NULL;
        thread curr = waiting_head;
        bool found = false;

        if (victim == NULL) {
                return;
        }
        /* check if victim is actually in the queue */
        while (curr != NULL) {
                if (curr == victim) {
                        found = true;
                        break;
                }
                prev_thread = curr;
                curr = curr->wait_next;
        }
        if (!found) {
                return;
        }

        next_thread = curr->wait_next;

        if (curr == waiting_head && curr == waiting_tail) {
                waiting_head = NULL;
                waiting_tail = NULL;
        } else if (curr == waiting_head) {
                waiting_head = next_thread;
        } else if (curr == waiting_tail) {
                prev_thread->wait_next = NULL;
                waiting_tail = prev_thread;
        } else {
                prev_thread->wait_next = next_thread;
        }
        curr->wait_next = NULL;
}


/*
 * Prints additional debug messages to stderr
 * if LWP_DEBUG flag enabled
 */
#ifdef LWP_DEBUG
static void print_custom_debug_msg(const char *msg)
{
        char buf[DEBUG_MSG_BUF_SIZE];
        int len;

        len = snprintf(buf, sizeof(buf), "%s", msg);
        if (len < 0) {
                return;
        }

        if (len > (int)sizeof(buf) - 1) {
                len = sizeof(buf) - 1;
        }
        ssize_t n = write(STDERR_FILENO, buf, len);
        if (n == -1) {
                /* failed; errno is set */
                return;
        }
}
#else
static void print_custom_debug_msg(const char *msg)
{
        (void)msg;
}
#endif

