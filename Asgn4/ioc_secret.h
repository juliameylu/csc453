/*
 * ioc_secret.h
 *
 * Ioctl request number for the /dev/Secret driver.
 */

#ifndef _SYS_IOC_SECRET_H
#define _SYS_IOC_SECRET_H

#include <minix/ioctl.h>

#define SSGRANT _IOW('K', 1, uid_t)

#endif /* _SYS_IOC_SECRET_H */
