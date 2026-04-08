/*
* pipeit.c
* Julia Lu (jlu97)
*
* Creates the pipeline "ls | sort -r > outfile" using fork, exec,
* pipe, dup2, and wait. The parent waits for both children and
* exits with nonzero status if an error occurs.
*/


#include <unistd.h>     /* pipe, fork, dup2, execlp, close */
#include <stdlib.h>     /* exit */
#include <stdio.h>      /* perror */
#include <fcntl.h>      /* open, O_WRONLY, O_CREAT, O_TRUNC */
#include <sys/wait.h>   /* wait */
#include <sys/types.h>  /* pid_t */
#include <sys/stat.h>   /* symbolic perm names */

/* ls | sort -r > outfile */
int main(void) {
        /* locals */
        int pipefd[2]; /* pipefd[0] = read end; pipefd[1] = write end */
        pid_t pid_ls, pid_sort_r;
        int child_status;
        pid_t waited_pid;

        /* make the pipe */
        if (pipe(pipefd) < 0) {
                perror("pipe has failed");
                exit(1);
        }

        /* child 1 will be used for ls */
        pid_ls = fork();
        if (pid_ls < 0) {
                perror("fork child ls has failed");
                exit(1);
        }
        if (pid_ls == 0) {
                /* ls child only needs write end */
                close(pipefd[0]); /* close read end */
                /* ls outputs to STDOUT_FILENO, use dup2 to redirect to pipe */
                if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                        perror("child ls dup2 has failed");
                        exit(1);
                }
                close(pipefd[1]); /* redundant now */
                execlp("ls", "ls", NULL);
                perror("execlp has failed");
                exit(1);
        }

        /* child 2 will be used for reverse sort */
        pid_sort_r = fork();
        if (pid_sort_r < 0) {
                /* parent exits if sort r child (second child) fails */
                /* edge case: child 1 may still be running,
                        - close pipefds
                        - wait on ls child (first child) */
                perror("fork child sort -r has failed");
                close(pipefd[0]);
                close(pipefd[1]);
                if (wait(&child_status) < 0) {
                        perror("wait");
                        exit(1);
                }
                exit(1);
        }
        if (pid_sort_r == 0) {
                int outfile_fd;

                /* reverse sort child only needs read end */
                close(pipefd[1]); /* close write end */
                /* sort -r reads from STDIN_FILENO, 
                        use dup2 to redirect to pipe */
                if (dup2(pipefd[0], STDIN_FILENO) < 0) {
                        perror("child sort r dup2 pipe read end has failed");
                        exit(1);
                }

                close(pipefd[0]); /* redundant now */

                outfile_fd = open("outfile", 
                        O_CREAT | O_WRONLY | O_TRUNC, 
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
                );
                /* create if needed, open write-only, truncate existing file,
                mode rw-r--r-- */
                if (outfile_fd < 0) {
                        perror("open outfile has failed");
                        exit(1);
                }

                /* sort -r writes to STDOUT_FILENO, 
                        use dup2 to redirect to outfile */
                if (dup2(outfile_fd, STDOUT_FILENO) < 0) {
                        perror("child sort dup2 stdout to outfile has failed");
                        exit(1);
                }

                close(outfile_fd); /* redundant now */
                execlp("sort", "sort", "-r", NULL);
                /* note: sort will keep reading until EOF */
                
                perror("sort -r has failed");
                exit(1);
        }

        /* from parent, close pipefds
                - prevents blocking once ls child is done writing */
        close(pipefd[0]);
        close(pipefd[1]);

        /* wait on children and catch if they failed */
        /* use wait() instead of waitpid()
                - order doesn't matter */
        waited_pid = wait(&child_status);
        if (waited_pid < 0) {
                perror("wait failed");
                exit(1);
        }
        if (!WIFEXITED(child_status)) {
                fprintf(stderr, "child did not exit normally\n");
                exit(1);
        }
        if (WEXITSTATUS(child_status) != 0) {
                fprintf(stderr,
                        "child exited with status %d\n", 
                        WEXITSTATUS(child_status));
                exit(1);
        }

        waited_pid = wait(&child_status);
        if (waited_pid < 0) {
                perror("wait failed");
                exit(1);
        }
        if (!WIFEXITED(child_status)) {
                fprintf(stderr, "child did not exit normally\n");
                exit(1);
        }
        if (WEXITSTATUS(child_status) != 0) {
                fprintf(stderr,
                        "child exited with status %d\n", 
                        WEXITSTATUS(child_status));
                exit(1);
        }

        return 0; /* success */

}
