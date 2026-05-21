/*
 * secret.h
 *
 * Shared constants and state for the /dev/Secret driver.
 */

#ifndef __SECRET_H
#define __SECRET_H

#include <sys/types.h>

#ifndef SECRET_SIZE
#define SECRET_SIZE 8192
#endif

#ifndef DEBUG_ENABLED
#define DEBUG_ENABLED 0
#endif

/*
 * State kept by /dev/Secret.
 */
struct secret_state {
        /* Number of open file descriptors. */
        int open_counter;

        uid_t secret_owner;
        int has_owner;

        /* Secret bytes. This may not be a string. */
        char secret_buf[SECRET_SIZE];

        int secret_len;
        int read_offset;
        int read_fd_opened;
};

#endif /* __SECRET_H */
