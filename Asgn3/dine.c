/*
 * dine.c
 * Implements a Dining Philosophers solution using pthreads and
 * POSIX semaphores.
 */

#include "dine.h"

/* printing stuff */
const char *think_str = " Think ";
const char *eat_str = " Eat   ";
const char *change_str = "       ";
const char *column_prefix_str = "| ";
const char *column_border_str = "|";

/*
 * Thread body for one philosopher, cycles through eat/think statuses
 * gets and releases forks, prints status changes under the printing
 * lock
 */
void *philosopher(void *arg)
{
        philinfo *philosopher_info = (philinfo *) arg;

        while (philosopher_info->cycles_left > 0) {

                /* don't change state/print if alr CHANGING*/
                if (philosopher_info->state != CHANGING) {
                        if (sem_wait(philosopher_info->printing_lock) == -1) {
                                fprintf(stderr,
                                        "failed to wait on printing "
                                        "semaphore: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                        philosopher_info->state = CHANGING;
                        print_status_change(philosopher_info->all_philosophers);
                        if (sem_post(philosopher_info->printing_lock) == -1) {
                                fprintf(stderr,
                                        "failed to post printing "
                                        "semaphore: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                }

                /* if even */
                if (philosopher_info->id % 2 == 0) {
                        /*
                         * grabbing and printing can't be one atomic
                         * step or else going to block others from
                         * printing
                         */
                        /* grab right fork first */
                        if (sem_wait(philosopher_info->right_fork) == -1) {
                                fprintf(stderr,
                                        "failed right fork wait: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                        /* print */
                        if (sem_wait(philosopher_info->printing_lock) == -1) {
                                fprintf(stderr,
                                        "failed printing wait: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                        philosopher_info->holding_right = true;
                        print_status_change(philosopher_info->all_philosophers);
                        if (sem_post(philosopher_info->printing_lock) == -1) {
                                fprintf(stderr,
                                        "failed printing post: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }

                        /* grab left fork */
                        if (sem_wait(philosopher_info->left_fork) == -1) {
                                fprintf(stderr,
                                        "failed left fork wait: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                        /* print */
                        if (sem_wait(philosopher_info->printing_lock) == -1) {
                                fprintf(stderr,
                                        "failed printing wait: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                        philosopher_info->holding_left = true;
                        print_status_change(philosopher_info->all_philosophers);
                        if (sem_post(philosopher_info->printing_lock) == -1) {
                                fprintf(stderr,
                                        "failed printing post: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                } else { /* if odd */
                        /* grab left fork first */
                        if (sem_wait(philosopher_info->left_fork) == -1) {
                                fprintf(stderr,
                                        "failed left fork wait: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                        /* print */
                        if (sem_wait(philosopher_info->printing_lock) == -1) {
                                fprintf(stderr,
                                        "failed printing wait: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                        philosopher_info->holding_left = true;
                        print_status_change(philosopher_info->all_philosophers);
                        if (sem_post(philosopher_info->printing_lock) == -1) {
                                fprintf(stderr,
                                        "failed printing post: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }

                        /* grab right fork */
                        if (sem_wait(philosopher_info->right_fork) == -1) {
                                fprintf(stderr,
                                        "failed right fork wait: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                        /* print */
                        if (sem_wait(philosopher_info->printing_lock) == -1) {
                                fprintf(stderr,
                                        "failed printing wait: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                        philosopher_info->holding_right = true;
                        print_status_change(philosopher_info->all_philosophers);
                        if (sem_post(philosopher_info->printing_lock) == -1) {
                                fprintf(stderr,
                                        "failed printing post: %s\n",
                                        strerror(errno));
                                pthread_exit(THREAD_FAILURE);
                        }
                }

                /* picked up both forks, eat */
                /* print */
                if (sem_wait(philosopher_info->printing_lock) == -1) {
                        fprintf(stderr,
                                "failed to wait on printing "
                                "semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }
                philosopher_info->state = EATING;
                print_status_change(philosopher_info->all_philosophers);
                if (sem_post(philosopher_info->printing_lock) == -1) {
                        fprintf(stderr,
                                "failed to post printing "
                                "semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }

                dawdle();

                /* finished eating */
                /*
                 * changing: phil who has decided to think, but who has
                 * not yet set down both forks
                 */
                if (sem_wait(philosopher_info->printing_lock) == -1) {
                        fprintf(stderr,
                                "failed to wait on printing semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }
                philosopher_info->state = CHANGING;
                print_status_change(philosopher_info->all_philosophers);
                if (sem_post(philosopher_info->printing_lock) == -1) {
                        fprintf(stderr,
                                "failed to post printing semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }

                dawdle();

                /* doesn't matter what order release forks */
                /*
                 * update print status and release must be one atomic
                 * step
                 */
                /* print */
                if (sem_wait(philosopher_info->printing_lock) == -1) {
                        fprintf(stderr,
                                "failed to wait on printing semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }
                philosopher_info->holding_right = false;
                /* set down right fork first */
                if (sem_post(philosopher_info->right_fork) == -1) {
                        fprintf(stderr,
                                "failed to post right fork semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }

                print_status_change(philosopher_info->all_philosophers);
                if (sem_post(philosopher_info->printing_lock) == -1) {
                        fprintf(stderr,
                                "failed to post printing semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }

                /* print */
                if (sem_wait(philosopher_info->printing_lock) == -1) {
                        fprintf(stderr,
                                "failed to wait on printing semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }
                philosopher_info->holding_left = false;
                /* set down left fork */
                if (sem_post(philosopher_info->left_fork) == -1) {
                        fprintf(stderr,
                                "failed to post left fork semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }
                print_status_change(philosopher_info->all_philosophers);
                if (sem_post(philosopher_info->printing_lock) == -1) {
                        fprintf(stderr,
                                "failed to post printing semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }

                /* finished releasing */
                /* transition from changing to thinking */
                /* print */
                if (sem_wait(philosopher_info->printing_lock) == -1) {
                        fprintf(stderr,
                                "failed to wait on printing semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }
                philosopher_info->state = THINKING;
                print_status_change(philosopher_info->all_philosophers);
                if (sem_post(philosopher_info->printing_lock) == -1) {
                        fprintf(stderr,
                                "failed to post printing semaphore: %s\n",
                                strerror(errno));
                        pthread_exit(THREAD_FAILURE);
                }

                dawdle();
                philosopher_info->cycles_left -= 1;
        }

        /* changing: a process transistioning from thinking to terminated */
        if (sem_wait(philosopher_info->printing_lock) == -1) {
                fprintf(stderr,
                        "failed to wait on printing semaphore: %s\n",
                        strerror(errno));
                pthread_exit(THREAD_FAILURE);
        }
        philosopher_info->state = CHANGING;
        print_status_change(philosopher_info->all_philosophers);
        if (sem_post(philosopher_info->printing_lock) == -1) {
                fprintf(stderr,
                        "failed to post printing semaphore: %s\n",
                        strerror(errno));
                pthread_exit(THREAD_FAILURE);
        }

        return THREAD_SUCCESS;
}

/*
 * Prints one full status row showing the current state of
 * every philosopher and which forks each philosopher is holding
 */
void print_status_change(philinfo *philosophers)
{
        int i;
        int j;
        /* one slot per fork (= num of phils)
         * + one slot for null terminating char
         */
        int forks_held_len = NUM_PHILOSOPHERS + NULL_CHAR_LEN;
        char forks_held[forks_held_len];

        /* outer loop: iterate thru phils */
        for (i = 0; i < NUM_PHILOSOPHERS; i++) {
                /* reset fork string */
                for (j = 0; j < forks_held_len - NULL_CHAR_LEN; j++) {
                        forks_held[j] = '-';
                }
                forks_held[forks_held_len - NULL_CHAR_LEN] = '\0';

                if (philosophers[i].holding_left) {
                        int left_fork_index = philosophers[i].left_fork_index;
                        char left_fork_label = wrapped_label(
                                FIRST_FORK_LABEL,
                                left_fork_index
                        );
                        forks_held[left_fork_index] = left_fork_label;
                }
                if (philosophers[i].holding_right) {
                        int right_fork_index = philosophers[i].right_fork_index;
                        char right_fork_label = wrapped_label(
                                FIRST_FORK_LABEL,
                                right_fork_index
                        );
                        forks_held[right_fork_index] = right_fork_label;
                }
                printf("%s", column_prefix_str);
                printf("%s", forks_held);

                switch (philosophers[i].state) {
                        case EATING:
                                printf("%s", eat_str);
                                break;
                        case THINKING:
                                printf("%s", think_str);
                                break;
                        default:
                                printf("%s", change_str);
                                break;
                }
        }
        printf("%s", column_border_str);
        printf("\n");
}

/* given function
 * sleep for a random amount of time between 0 and
 * DAWDLEFACTOR milliseconds. This routine is somewhat
 * unreliable, since it doesn’t take into account the
 * possiblity that the nanosleep could be interrupted for some
 * legitimate reason.
 */
void dawdle(void)
{
        struct timespec tv;
        int msec = (int)((((double)random()) / RAND_MAX) * DAWDLEFACTOR);

        tv.tv_sec = 0;
        tv.tv_nsec = 1000000 * msec;
        if (-1 == nanosleep(&tv, NULL)) {
                perror("nanosleep");
        }
}

/*
 * Prints the table header
 */
void print_table_header(char *header_border)
{
        print_table_border(header_border);
        print_phil_labels();
        print_table_border(header_border);
}

/*
 * Prints the row of philosopher labels in header
 */
void print_phil_labels(void)
{
        int i;
        int j;
        int label_len = LEADING_SPACE_LEN + NUM_PHILOSOPHERS
                + strlen(change_str) + NULL_CHAR_LEN;
        char label[label_len];
        /*
         * index where the philosopher label should be
         * centered in the column
         */
        int middle = label_len / 2 - 1;

        for (j = 0; j < label_len - NULL_CHAR_LEN; j++) {
                label[j] = ' ';
        }
        label[label_len - NULL_CHAR_LEN] = '\0';

        for (i = 0; i < NUM_PHILOSOPHERS; i++) {
                label[middle] = wrapped_label(FIRST_PHIL_LABEL, i);
                printf("%s%s", column_border_str, label);
        }
        printf("%s\n", column_border_str);
}

/*
 * Fills char array with '=' and a
 * terminating null byte so it can be used as one status table
 * border segment
 */
void build_table_border(char *header_border, int header_border_len)
{
        int i;
        for (i = 0; i < header_border_len - NULL_CHAR_LEN; i++) {
                header_border[i] = '=';
        }
        header_border[header_border_len - NULL_CHAR_LEN] = '\0';
}

/*
 * Prints one full horizontal border row
 */
void print_table_border(char *header_border)
{
        int i;
        for (i = 0; i < NUM_PHILOSOPHERS; i++) {
                printf("%s", column_border_str);
                printf("%s", header_border);
        }
        printf("%s\n", column_border_str);
}

/*
 * Outputs ascii label based off starting char
 * and index of phil/fork
 * wraps around ascii table safely
 */
char wrapped_label(char start, int index)
{
        /* start will be 'A' or '0' */
        /* find where start is in the printable range */
        int start_offset = start - FIRST_PRINTABLE;
        /* move forward by index */
        int unwrapped_offset = start_offset + index;
        /* wrap around */
        int wrapped_offset = unwrapped_offset % PRINTABLE_COUNT;

        return FIRST_PRINTABLE + wrapped_offset;
}

/*
 * Parses command-line arguments,
 * seeds random number generator,
 * inits philosopher data and semaphores,
 * creates philosopher threads, waits for threads,
 * destroys semaphores, and prints the table
 */
int main(int argc, char *argv[])
{
        int cycle = DEFAULT_CYCLES;
        int i = 0;
        int res;
        void *phil_result_val;
        struct timeval tv;
        philinfo philosophers[NUM_PHILOSOPHERS];
        sem_t forks[NUM_PHILOSOPHERS];
        sem_t printing_lock;

        /*
         * one table column:
         * leading space + fork display width
         * + state width + '\0'
         */
        int header_border_len = LEADING_SPACE_LEN + NUM_PHILOSOPHERS
                + strlen(change_str) + NULL_CHAR_LEN;
        char header_border[header_border_len];

        if (gettimeofday(&tv, NULL) != 0) {
                fprintf(stderr,
                        "failed to get time for random seed: %s\n",
                        strerror(errno));
                return EXIT_FAILURE;
        }
        srandom((unsigned int)(tv.tv_sec + tv.tv_usec));

        if (argc > 2) {
                /* input validation */
                fprintf(stderr, "usage: %s [cycles]\n", argv[0]);
                return EXIT_FAILURE;
        }

        if (argc == 2) {
                /* input validation */
                char *end;
                long value;

                errno = 0;
                value = strtol(argv[1], &end, 10);
                if (errno != 0 || *end != '\0'
                    || value <= 0 || value > INT_MAX) {
                        fprintf(stderr,
                                "Usage: %s [positive integer]\n",
                                argv[0]);
                        return EXIT_FAILURE;
                }
                cycle = (int)value;
        }

        /* initialize philosopher infos */
        for (i = 0; i < NUM_PHILOSOPHERS; i++) {
                int left_fork_index = i;
                int right_fork_index = (i + 1) % NUM_PHILOSOPHERS;

                philosophers[i].id = i;
                philosophers[i].left_fork_index = left_fork_index;
                philosophers[i].right_fork_index = right_fork_index;
                philosophers[i].cycles_left = cycle;
                philosophers[i].state = CHANGING;
                philosophers[i].left_fork = &forks[left_fork_index];
                philosophers[i].right_fork = &forks[right_fork_index];

                /* fields for printing */
                philosophers[i].printing_lock = &printing_lock;
                philosophers[i].holding_left = false;
                philosophers[i].holding_right = false;
                philosophers[i].all_philosophers = philosophers;
        }

        /* initialize semaphore forks */
        for (i = 0; i < NUM_PHILOSOPHERS; i++) {
                res = sem_init(&forks[i],
                        SHARED_BETWEEN_THREADS_FLAG,
                        FORK_INIT_VAL);
                if (res != 0) {
                        fprintf(stderr,
                                "failed to initialize fork semaphore "
                                "%d: %s\n",
                                i, strerror(errno));
                        exit(EXIT_FAILURE);
                }
        }

        /* initialize printing semaphore */
        res = sem_init(&printing_lock,
                       SHARED_BETWEEN_THREADS_FLAG,
                       PRINTING_INIT_VAL);
        if (res != 0) {
                fprintf(stderr,
                        "failed to initialize printing semaphore: %s\n",
                        strerror(errno));
                exit(EXIT_FAILURE);
        }

        /* print header */
        build_table_border(header_border, header_border_len);
        print_table_header(header_border);

        /* print initial CHANGING row */
        print_status_change(philosophers);

        /* create and run philosopher threads */
        for (i = 0; i < NUM_PHILOSOPHERS; i++) {
                res = pthread_create(&philosophers[i].tid, NULL,
                        philosopher, (void *)&philosophers[i]);
                if (res != 0) {
                        fprintf(stderr,
                                "failed to create philosopher thread "
                                "%d: %s\n",
                                i, strerror(res));
                        exit(EXIT_FAILURE);
                }
        }

        /* wait on philosophers */
        for (i = 0; i < NUM_PHILOSOPHERS; i++) {
                res = pthread_join(philosophers[i].tid, &phil_result_val);
                if (res != 0) {
                        fprintf(stderr,
                                "failed to join philosopher thread "
                                "%d: %s\n",
                                i, strerror(res));
                        exit(EXIT_FAILURE);
                }
                if (phil_result_val == THREAD_FAILURE) {
                        fprintf(stderr,
                                "philosopher thread %d exited with failure\n",
                                i);
                        exit(EXIT_FAILURE);
                }
        }

        /* destroy semaphore forks */
        for (i = 0; i < NUM_PHILOSOPHERS; i++) {
                res = sem_destroy(&forks[i]);
                if (res != 0) {
                        fprintf(stderr,
                                "failed to destroy fork semaphore "
                                "%d: %s\n",
                                i, strerror(errno));
                        exit(EXIT_FAILURE);
                }
        }

        /* destroy printing semaphore */
        res = sem_destroy(&printing_lock);
        if (res != 0) {
                fprintf(stderr,
                        "failed to destroy printing semaphore: %s\n",
                        strerror(errno));
                exit(EXIT_FAILURE);
        }

        /* print end border */
        print_table_border(header_border);

        return 0;
}
