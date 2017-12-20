#ifndef _MUTEX_UAPI_H
#define _MUTEX_UAPI_H

#ifdef __KERNEL__
#include <asm/ioctl.h>
#include "shared_spinlock.h"
#else
#include <sys/ioctl.h>
#include <stddef.h>
#include <shared_spinlock.h>
#endif //__KERNEL__

#define MUTEX_IOCTL_MAGIC 'M'

// DONE define mutex dev IOCTL interface here

typedef long mutex_id_t;

typedef struct mutex_ioctl_lock_create_arg {
    mutex_id_t id; // out param
} mutex_ioctl_lock_create_arg_t;

// FIXME?: use _IOWR?
#define MUTEX_IOCTL_LOCK_CREATE \
    _IOW(MUTEX_IOCTL_MAGIC, 1, mutex_ioctl_lock_create_arg_t)


typedef struct mutex_ioctl_lock_destroy_arg {
    mutex_id_t id;
} mutex_ioctl_lock_destroy_arg_t;

#define MUTEX_IOCTL_LOCK_DESTROY \
    _IOW(MUTEX_IOCTL_MAGIC, 2, mutex_ioctl_lock_destroy_arg_t)


typedef struct mutex_ioctl_sleep_arg {
    mutex_id_t id;
    shared_spinlock_t *shared_spinlock;
} mutex_ioctl_sleep_arg_t;

#define MUTEX_IOCTL_SLEEP \
    _IOW(MUTEX_IOCTL_MAGIC, 3, mutex_ioctl_sleep_arg_t)


typedef struct mutex_ioctl_wakeup_arg {
    mutex_id_t id;
} mutex_ioctl_wakeup_arg_t;

#define MUTEX_IOCTL_WAKEUP \
    _IOW(MUTEX_IOCTL_MAGIC, 4, mutex_ioctl_wakeup_arg_t)

#endif //_VSD_UAPI_H
