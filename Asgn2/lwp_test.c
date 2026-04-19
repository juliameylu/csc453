#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lwp.h"
#include "scheduler.h"

static int exit_immediately(void *arg);
static int staggered_worker(void *arg);
static void expect_true(int condition, const char *message);
static int run_in_child(
        const char *name,
        int (*scenario)(void),
        int expected_exit
);

static int scenario_prestart_calls(void);
static int scenario_prestart_create_then_yield(void);
static int scenario_start_only_current(void);
static int scenario_wait_and_reap(void);
static int scenario_scheduler_switch(void);

static int worker_yields = 0;

static int exit_immediately(void *arg)
{
        long status = (long)arg;

        lwp_exit((int)status);
        return (int)status;
}

static int staggered_worker(void *arg)
{
        long worker_id = (long)arg;
        int i;

        for (i = 0; i < worker_yields; i++) {
                lwp_yield();
        }

        return (int)(worker_id + 10);
}

static void expect_true(int condition, const char *message)
{
        if (!condition) {
                fprintf(stderr, "TEST FAILURE: %s\n", message);
                exit(EXIT_FAILURE);
        }
}

static int run_in_child(
        const char *name,
        int (*scenario)(void),
        int expected_exit
)
{
        pid_t pid;
        int wait_status;

        fflush(NULL);
        pid = fork();
        if (pid < 0) {
                perror("fork");
                exit(EXIT_FAILURE);
        }
        if (pid == 0) {
                int child_status;

                child_status = scenario();
                _exit(child_status);
        }

        if (waitpid(pid, &wait_status, 0) < 0) {
                perror("waitpid");
                exit(EXIT_FAILURE);
        }

        if (WIFEXITED(wait_status)) {
                if (WEXITSTATUS(wait_status) != expected_exit) {
                        fprintf(stderr,
                                "[FAIL] %s (expected exit %d, got %d)\n",
                                name,
                                expected_exit,
                                WEXITSTATUS(wait_status));
                        return 0;
                }
                printf("[PASS] %s (exit %d)\n",
                        name,
                        WEXITSTATUS(wait_status));
                return 1;
        }

        if (WIFSIGNALED(wait_status)) {
            fprintf(stderr,
                    "[FAIL] %s (signal %d)\n",
                    name,
                    WTERMSIG(wait_status));
        } else {
                fprintf(stderr, "[FAIL] %s (abnormal exit)\n", name);
        }

        return 0;
}

static int scenario_prestart_calls(void)
{
        expect_true(lwp_gettid() == NO_THREAD,
                "lwp_gettid() before lwp_start should return NO_THREAD");
        expect_true(lwp_wait(NULL) == NO_THREAD,
                "lwp_wait() before lwp_start should return NO_THREAD");

        lwp_yield();
        lwp_set_scheduler(RoundRobin);
        lwp_set_scheduler(RoundRobin);

        return 0;
}

static int scenario_prestart_create_then_yield(void)
{
        tid_t tid;

        tid = lwp_create(exit_immediately, (void *)7L);
        expect_true(tid != NO_THREAD,
                "lwp_create() before lwp_start should succeed");
        expect_true(tid2thread(tid) != NULL,
                "tid2thread() should find a newly created thread");

        /*
         * This exercises the branch where there is no current thread
         * yet but there is a runnable created thread in the scheduler.
         * The child process should exit with the worker's status.
         */
        lwp_yield();

        return 99;
}

static int scenario_start_only_current(void)
{
        lwp_start();

        expect_true(lwp_gettid() != NO_THREAD,
                "lwp_start() should install the calling thread as an LWP");
        expect_true(lwp_wait(NULL) == NO_THREAD,
                "lwp_wait() with no zombies and no other runnable threads "
                "should return NO_THREAD");

        return 0;
}

static int scenario_wait_and_reap(void)
{
        tid_t tids[3];
        int seen[3];
        int i;

        memset(seen, 0, sizeof(seen));

        worker_yields = 0;
        tids[0] = lwp_create(staggered_worker, (void *)1L);
        worker_yields = 1;
        tids[1] = lwp_create(staggered_worker, (void *)2L);
        worker_yields = 2;
        tids[2] = lwp_create(staggered_worker, (void *)3L);

        expect_true(tids[0] != NO_THREAD && tids[1] != NO_THREAD
                && tids[2] != NO_THREAD,
                "all workers should be created");
        expect_true(tid2thread(tids[1]) != NULL,
                "tid2thread() should find a live worker");

        lwp_start();

        for (i = 0; i < 3; i++) {
                int status;
                tid_t reaped_tid;

                reaped_tid = lwp_wait(&status);
                expect_true(reaped_tid != NO_THREAD,
                        "lwp_wait() should reap a terminated worker");

                if (reaped_tid == tids[0]) {
                        expect_true(LWPTERMSTAT(status) == 11,
                                "worker 1 should exit with status 11");
                        seen[0] = 1;
                } else if (reaped_tid == tids[1]) {
                        expect_true(LWPTERMSTAT(status) == 12,
                                "worker 2 should exit with status 12");
                        seen[1] = 1;
                } else if (reaped_tid == tids[2]) {
                        expect_true(LWPTERMSTAT(status) == 13,
                                "worker 3 should exit with status 13");
                        seen[2] = 1;
                } else {
                        expect_true(FALSE,
                                "lwp_wait() returned an unexpected tid");
                }
        }

        expect_true(seen[0] && seen[1] && seen[2],
                "all created workers should be reaped");
        expect_true(tid2thread(tids[0]) == NULL,
                "reaped thread should no longer be found by tid2thread()");
        expect_true(lwp_wait(NULL) == NO_THREAD,
                "after all workers are reaped, lwp_wait() should "
                "return NO_THREAD");

        return 0;
}

static int scenario_scheduler_switch(void)
{
        lwp_set_scheduler(NULL);
        lwp_set_scheduler(RoundRobin);
        lwp_set_scheduler(RoundRobin);

        expect_true(lwp_get_scheduler() == RoundRobin,
                "current scheduler should be RoundRobin");

        return 0;
}

int main(void)
{
        int passed;
        int total;

        passed = 0;
        total = 0;

        total++;
        passed += run_in_child(
                "prestart public calls",
                scenario_prestart_calls,
                0
        );

        total++;
        passed += run_in_child(
                "prestart create then yield",
                scenario_prestart_create_then_yield,
                7
        );

        total++;
        passed += run_in_child(
                "start with only current thread",
                scenario_start_only_current,
                0
        );

        total++;
        passed += run_in_child(
                "wait and reap workers",
                scenario_wait_and_reap,
                0
        );

        total++;
        passed += run_in_child(
                "scheduler switching",
                scenario_scheduler_switch,
                0
        );

        printf("Passed %d/%d scenarios\n", passed, total);

        return (passed == total) ? EXIT_SUCCESS : EXIT_FAILURE;
}
