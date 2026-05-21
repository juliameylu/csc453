/*
 * grant_secret.c
 *
 * Small test program for /dev/Secret. It writes one secret and can grant it
 * to another user with SSGRANT.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "ioc_secret.h"

#define FILENAME "/dev/Secret"

/*
 * Write a test secret. If a UID is given, grant the secret to that user.
 */
int main(argc, argv)
        int argc;
        char *argv[];
{
        int fd;
        int res;
        char *msg;
        uid_t uid;

        msg = "Hello, world\n";

        fd = open(FILENAME, O_WRONLY);
        printf("Opening... fd=%d\n", fd);
        if (fd < 0) {
                perror("open");
                return 1;
        }

        res = write(fd, msg, strlen(msg));
        printf("Writing... res=%d\n", res);
        if (res < 0) {
                perror("write");
                close(fd);
                return 1;
        }

        /* Grant the secret if a UID was given. */
        if (argc > 1 && 0 != (uid = atoi(argv[1]))) {
                res = ioctl(fd, SSGRANT, &uid);
                if (res < 0) {
                        perror("ioctl");
                }
                printf("Trying to change owner to %d...res=%d\n",
                        uid, res);
        }

        res = close(fd);
        printf("Closing... res=%d\n", res);
        if (res < 0) {
                perror("close");
                return 1;
        }

        return 0;
}
