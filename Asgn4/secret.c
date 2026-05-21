#include <minix/ds.h>
#include <minix/driver.h>
#include <minix/drivers.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>

#include "secret.h"

/*
 * secret.c
 *
 * Driver for /dev/Secret. It keeps one secret, lets only the owner read it,
 * lets the owner give it to another user, and saves its state for live update.
 */

/* Functions the driver library can call. */
FORWARD _PROTOTYPE( char * secret_name, (void) );
FORWARD _PROTOTYPE( int secret_open, (struct driver *d, message *m) );
FORWARD _PROTOTYPE( int secret_close, (struct driver *d, message *m) );
FORWARD _PROTOTYPE( int secret_ioctl, (struct driver *d, message *m) );
FORWARD _PROTOTYPE( struct device * secret_prepare, (int device) );
FORWARD _PROTOTYPE( int secret_transfer, (int procnr, int opcode,
        u64_t position, iovec_t *iov, unsigned nr_req) );
FORWARD _PROTOTYPE( void secret_geometry, (struct partition *entry) );

/* Functions SEF can call. */
FORWARD _PROTOTYPE( void sef_local_startup, (void) );
FORWARD _PROTOTYPE( int sef_cb_init, (int type, sef_init_info_t *info) );
FORWARD _PROTOTYPE( int sef_cb_lu_state_save, (int) );
FORWARD _PROTOTYPE( int lu_state_restore, (void) );

/* Driver function table. */
PRIVATE struct driver secret_tab = {
        secret_name,
        secret_open,
        secret_close,
        secret_ioctl,
        secret_prepare,
        secret_transfer,
        nop_cleanup,
        secret_geometry,
        nop_alarm,
        nop_cancel,
        nop_select,
        nop_ioctl,
        do_nop,
};

/* The device and its saved state. */
PRIVATE struct device secret_device;
PRIVATE struct secret_state secret;
PRIVATE int debug_enabled = DEBUG_ENABLED;

/*
 * Clear the secret and mark the device as empty.
 */
PRIVATE void reset_secret_state(void)
{
        secret.open_counter = 0;
        secret.secret_owner = 0;
        secret.has_owner = FALSE;
        memset(secret.secret_buf, 0, sizeof(secret.secret_buf));
        secret.secret_len = 0;
        secret.read_offset = 0;
        secret.read_fd_opened = FALSE;

        if (debug_enabled) {
                printf("secret: reset\n");
        }
}

/*
 * Return this driver's name.
 */
PRIVATE char * secret_name(void)
{
        return "secret";
}

/*
 * Open /dev/Secret. If it is empty, the caller becomes the owner. If it
 * already has a secret, only the owner can open it for reading.
 */
PRIVATE int secret_open(d, m)
        struct driver *d;
        message *m;
{
        int mode;
        endpoint_t endpoint;
        struct ucred caller_cred;
        int ret;

        mode = m->COUNT;

        if ((mode & R_BIT) && (mode & W_BIT)) {
                if (debug_enabled) {
                        printf("secret_open: read-write open denied\n");
                }
                return EACCES;
        }

        if (!((mode & R_BIT) || (mode & W_BIT))) {
                if (debug_enabled) {
                        printf("secret_open: open without read/write denied\n");
                }
                return EACCES;
        }

        endpoint = m->IO_ENDPT;
        ret = getnucred(endpoint, &caller_cred);
        if (ret < 0) {
                return ret;
        }

        if (debug_enabled) {
                printf("secret_open: uid=%d mode=%x owner=%d "
                        "has_owner=%d open=%d\n",
                        (int)caller_cred.uid, mode, (int)secret.secret_owner,
                        secret.has_owner, secret.open_counter);
        }

        if (!secret.has_owner) {
                secret.has_owner = TRUE;
                secret.secret_owner = caller_cred.uid;
        } else {
                if (mode & W_BIT) {
                        if (debug_enabled) {
                                printf("secret_open: write open denied, "
                                        "device full\n");
                        }
                        return ENOSPC;
                }

                if ((mode & R_BIT) &&
                                (caller_cred.uid != secret.secret_owner)) {
                        if (debug_enabled) {
                                printf("secret_open: read denied for "
                                        "uid=%d\n",
                                        (int)caller_cred.uid);
                        }
                        return EACCES;
                }
        }

        secret.open_counter++;

        if (mode & R_BIT) {
                secret.read_fd_opened = TRUE;
        }

        if (debug_enabled) {
                printf("secret_open: ok owner=%d has_owner=%d open=%d "
                        "read_opened=%d\n",
                        (int)secret.secret_owner, secret.has_owner,
                        secret.open_counter, secret.read_fd_opened);
        }

        return OK;
}

/*
 * Close /dev/Secret. Once someone has opened it for reading, the last close
 * clears the secret.
 */
PRIVATE int secret_close(d, m)
        struct driver *d;
        message *m;
{
        if (debug_enabled) {
                printf("secret_close: open=%d read_opened=%d\n",
                        secret.open_counter, secret.read_fd_opened);
        }

        if (secret.open_counter > 0) {
                secret.open_counter--;
        }

        if ((secret.open_counter == 0) && secret.read_fd_opened) {
                if (debug_enabled) {
                        printf("secret_close: last close after read, "
                                "resetting\n");
                }
                reset_secret_state();
        }

        return OK;
}

/*
 * Handle SSGRANT.
 */
PRIVATE int secret_ioctl(d, m)
        struct driver *d;
        message *m;
{
        int request;
        int ret;
        uid_t grantee;

        request = m->REQUEST;

        if (debug_enabled) {
                printf("secret_ioctl: request=%d owner=%d\n",
                        request, (int)secret.secret_owner);
        }

        /* SSGRANT is the only ioctl this driver supports. */
        if (request != SSGRANT) {
                if (debug_enabled) {
                        printf("secret_ioctl: unknown request\n");
                }
                return ENOTTY;
        }

        ret = sys_safecopyfrom(m->IO_ENDPT, (vir_bytes)m->IO_GRANT, 0,
                (vir_bytes)&grantee, sizeof(grantee), D);
        if (ret == OK) {
                secret.secret_owner = grantee;
                if (debug_enabled) {
                        printf("secret_ioctl: new owner=%d\n",
                                (int)secret.secret_owner);
                }
        }

        return ret;
}

/*
 * Give the file system the size of this device.
 */
PRIVATE struct device * secret_prepare(dev)
        int dev;
{
        secret_device.dv_base.lo = 0;
        secret_device.dv_base.hi = 0;
        secret_device.dv_size.lo = SECRET_SIZE;
        secret_device.dv_size.hi = 0;

        return &secret_device;
}

/*
 * Read from or write to the secret buffer. This device ignores seek position.
 */
PRIVATE int secret_transfer(proc_nr, opcode, position, iov, nr_req)
        int proc_nr;
        int opcode;
        u64_t position;
        iovec_t *iov;
        unsigned nr_req;
{
        int bytes;
        int ret;
        int space_remaining;

        ret = OK;

        if (debug_enabled) {
                printf("secret_transfer: opcode=%d iov_size=%d len=%d "
                        "read=%d\n",
                        opcode, iov->iov_size, secret.secret_len,
                        secret.read_offset);
        }

        switch (opcode) {
        case DEV_GATHER_S:
                bytes = secret.secret_len - secret.read_offset < iov->iov_size ?
                        secret.secret_len - secret.read_offset : iov->iov_size;

                if (bytes <= 0) {
                        return OK;
                }

                ret = sys_safecopyto(proc_nr, iov->iov_addr, 0,
                        (vir_bytes)(secret.secret_buf + secret.read_offset),
                        bytes, D);
                if (ret == OK) {
                        iov->iov_size -= bytes;
                        secret.read_offset += bytes;
                        if (debug_enabled) {
                                printf("secret_transfer: read %d bytes "
                                        "read=%d\n",
                                        bytes, secret.read_offset);
                        }
                }
                break;

        case DEV_SCATTER_S:
                if (iov->iov_size == 0) {
                        return OK;
                }

                space_remaining = SECRET_SIZE - secret.secret_len;
                if (space_remaining <= 0) {
                        return ENOSPC;
                }

                bytes = space_remaining < iov->iov_size ?
                        space_remaining : iov->iov_size;

                ret = sys_safecopyfrom(proc_nr, iov->iov_addr, 0,
                        (vir_bytes)(secret.secret_buf + secret.secret_len),
                        bytes, D);
                if (ret == OK) {
                        iov->iov_size -= bytes;
                        secret.secret_len += bytes;
                        if (debug_enabled) {
                                printf("secret_transfer: wrote %d bytes "
                                        "len=%d\n",
                                        bytes, secret.secret_len);
                        }
                }
                break;

        default:
                return EINVAL;
        }

        return ret;
}

/*
 * Fill in empty geometry values.
 */
PRIVATE void secret_geometry(entry)
        struct partition *entry;
{
        entry->cylinders = 0;
        entry->heads = 0;
        entry->sectors = 0;
}

/*
 * Save the device state before live update.
 */
PRIVATE int sef_cb_lu_state_save(state)
        int state;
{
        int ret;

        ret = ds_publish_u32("open_counter", secret.open_counter,
                DSF_OVERWRITE);
        if (ret != OK) {
                return ret;
        }

        ret = ds_publish_mem("secret_buf", (void *) secret.secret_buf,
                SECRET_SIZE, DSF_OVERWRITE);
        if (ret != OK) {
                return ret;
        }

        ret = ds_publish_u32("secret_len", secret.secret_len, DSF_OVERWRITE);
        if (ret != OK) {
                return ret;
        }

        ret = ds_publish_u32("read_offset", secret.read_offset,
                DSF_OVERWRITE);
        if (ret != OK) {
                return ret;
        }

        ret = ds_publish_u32("secret_owner", secret.secret_owner,
                DSF_OVERWRITE);
        if (ret != OK) {
                return ret;
        }

        ret = ds_publish_u32("has_owner", secret.has_owner, DSF_OVERWRITE);
        if (ret != OK) {
                return ret;
        }

        ret = ds_publish_u32("read_fd_opened", secret.read_fd_opened,
                DSF_OVERWRITE);
        if (ret != OK) {
                return ret;
        }

        if (debug_enabled) {
                printf("secret: saved state for live update\n");
        }

        return OK;
}

/*
 * Load the saved state after live update.
 */
PRIVATE int lu_state_restore(void)
{
        int ret;
        u32_t value;
        size_t buf_size;

        ret = ds_retrieve_u32("open_counter", &value);
        if (ret != OK) {
                return ret;
        }
        secret.open_counter = (int)value;

        ret = ds_delete_u32("open_counter");
        if (ret != OK) {
                return ret;
        }

        buf_size = sizeof(secret.secret_buf);
        ret = ds_retrieve_mem("secret_buf", secret.secret_buf, &buf_size);
        if (ret != OK) {
                return ret;
        }

        ret = ds_delete_mem("secret_buf");
        if (ret != OK) {
                return ret;
        }

        ret = ds_retrieve_u32("secret_len", &value);
        if (ret != OK) {
                return ret;
        }
        secret.secret_len = (int)value;

        ret = ds_delete_u32("secret_len");
        if (ret != OK) {
                return ret;
        }

        ret = ds_retrieve_u32("read_offset", &value);
        if (ret != OK) {
                return ret;
        }
        secret.read_offset = (int)value;

        ret = ds_delete_u32("read_offset");
        if (ret != OK) {
                return ret;
        }

        ret = ds_retrieve_u32("secret_owner", &value);
        if (ret != OK) {
                return ret;
        }
        secret.secret_owner = (uid_t)value;

        ret = ds_delete_u32("secret_owner");
        if (ret != OK) {
                return ret;
        }

        ret = ds_retrieve_u32("has_owner", &value);
        if (ret != OK) {
                return ret;
        }
        secret.has_owner = (int)value;

        ret = ds_delete_u32("has_owner");
        if (ret != OK) {
                return ret;
        }

        ret = ds_retrieve_u32("read_fd_opened", &value);
        if (ret != OK) {
                return ret;
        }
        secret.read_fd_opened = (int)value;

        ret = ds_delete_u32("read_fd_opened");
        if (ret != OK) {
                return ret;
        }

        if (debug_enabled) {
                printf("secret: restored state after live update\n");
        }

        return OK;
}

/*
 * Register this driver's SEF callbacks and start SEF.
 */
PRIVATE void sef_local_startup(void)
{
        sef_setcb_init_fresh(sef_cb_init);
        sef_setcb_init_lu(sef_cb_init);
        sef_setcb_init_restart(sef_cb_init);

        sef_setcb_lu_prepare(sef_cb_lu_prepare_always_ready);
        sef_setcb_lu_state_isvalid(sef_cb_lu_state_isvalid_standard);
        sef_setcb_lu_state_save(sef_cb_lu_state_save);

        sef_startup();
}

/*
 * Start fresh, restart, or load saved state after live update.
 */
PRIVATE int sef_cb_init(int type, sef_init_info_t *info)
{
        int do_announce_driver;
        int ret;

        do_announce_driver = TRUE;

        switch (type) {
        case SEF_INIT_FRESH:
                if (debug_enabled) {
                        printf("secret: fresh start\n");
                }
                reset_secret_state();
                break;

        case SEF_INIT_LU:
                if (debug_enabled) {
                        printf("secret: live update start\n");
                }
                ret = lu_state_restore();
                if (ret != OK) {
                        return ret;
                }
                do_announce_driver = FALSE;
                break;

        case SEF_INIT_RESTART:
                if (debug_enabled) {
                        printf("secret: restart\n");
                }
                reset_secret_state();
                break;
        }

        if (do_announce_driver) {
                driver_announce();
        }

        return OK;
}

/*
 * Start the driver.
 */
PUBLIC int main(int argc, char **argv)
{
        sef_local_startup();

        driver_task(&secret_tab, DRIVER_STD);
        return OK;
}
