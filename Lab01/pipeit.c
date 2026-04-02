/*
 * pipeit.c
 * Julia Lu (jlu97)
 *
 * Creates the pipeline "ls | sort -r > outfile" using fork, exec,
 * pipe, dup2, and waitpid. The parent waits for both children and
 * exits with nonzero status if an error occurs.
 */


#include <unistd.h>     /* pipe, fork, dup2, execlp, close */
#include <stdlib.h>     /* exit */
#include <stdio.h>      /* perror */
#include <fcntl.h>      /* open, O_WRONLY, O_CREAT, O_TRUNC */
#include <sys/wait.h>   /* waitpid */
#include <sys/types.h>  /* pid_t */

/* ls | sort -r > outfile */
int main(void) {
    /* locals */
    int pipefd[2]; /* pipefd[0] = read end; pipefd[1] = write end */
    pid_t pid_ls, pid_sort_r;
    int ls_child_status, sort_r_child_status;
    pid_t waited_pid_ls, waited_pid_sort_r;
    
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
        if (waitpid(pid_ls, &ls_child_status, 0) < 0) {
            perror("waitpid");
            exit(1);
        }
        exit(1);
    }
    if (pid_sort_r == 0) {
        int outfile_fd;

        /* reverse sort child only needs read end */
        close(pipefd[1]); /* close write end */
        /* sort -r reads from STDIN_FILENO, use dup2 to redirect to pipe */
        if (dup2(pipefd[0], STDIN_FILENO) < 0) {
            perror("child sort r dup2 pipe read end has failed");
            exit(1);
        }

        close(pipefd[0]); /* redundant now */

        outfile_fd = open("outfile", O_CREAT|O_WRONLY|O_TRUNC, 0666);
        /* create if needed, open write-only, truncate existing file,
            mode rw-rw-rw- */
        if (outfile_fd < 0) {
            perror("open outfile has failed");
            exit(1);
        }

        /* sort -r writes to STDOUT_FILENO, use dup2 to redirect to outfile */
        if (dup2(outfile_fd, STDOUT_FILENO) < 0) {
            perror("child sort r dup2 stdout to outfile has failed");
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
    waited_pid_ls = waitpid(pid_ls, &ls_child_status, 0);
    if (waited_pid_ls < 0) {
        perror("waitpid ls failed");
        exit(1);
    }
    if (!WIFEXITED(ls_child_status)) {
        fprintf(stderr, "ls child did not exit normally\n");
        exit(1);
    }
    if (WEXITSTATUS(ls_child_status) != 0) {
        fprintf(stderr,
            "ls child exited with status %d\n", 
            WEXITSTATUS(ls_child_status));
        exit(1);
    }

    waited_pid_sort_r = waitpid(pid_sort_r, &sort_r_child_status, 0);
    if (waited_pid_sort_r < 0) {
        perror("waitpid sort -r failed");
        exit(1);
    }
    if (!WIFEXITED(sort_r_child_status)) {
        fprintf(stderr, "sort -r child did not exit normally\n");
        exit(1);
    }
    if (WEXITSTATUS(sort_r_child_status) != 0) {
        fprintf(stderr,
            "sort -r child exited with status %d\n", 
            WEXITSTATUS(sort_r_child_status));
        exit(1);
    }

    return 0; /* success */

}
