#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include <mutex.h>

static int mutex_fd = -1;

mutex_err_t mutex_init(mutex_t *m)
{
    // DONE initialize userspace side mutex state
    // and create kernel space mutex state

    mutex_ioctl_lock_create_arg_t arg;
    int ret = ioctl(mutex_fd, MUTEX_IOCTL_LOCK_CREATE, &arg);
    if (ret) {
        return MUTEX_INTERNAL_ERR;
    }

    m->id = arg.id;
    shared_spinlock_init(&m->spinlock);
    atomic_init(&m->sleep_cnt, 0);
    return MUTEX_OK;
}

mutex_err_t mutex_deinit(mutex_t *m)
{
    // DONE destroy kernel side mutex state
    // and deinitialize userspace side.

    mutex_ioctl_lock_destroy_arg_t arg = { .id = m->id };
    int ret = ioctl(mutex_fd, MUTEX_IOCTL_LOCK_DESTROY, &arg);
    if (ret) {
        return MUTEX_INTERNAL_ERR;
    }

    m->id = -1;
    return MUTEX_OK;
}

mutex_err_t mutex_lock(mutex_t *m)
{
    // DONE lock spinlock here.
    // If not successful then go to sleep
    // in kernel.

    if (shared_spin_trylock(&m->spinlock)) {
        return MUTEX_OK;
    }

    mutex_ioctl_sleep_arg_t arg = {
        .id = m->id,
        .shared_spinlock = &m->spinlock
    };
    atomic_fetch_add(&m->sleep_cnt, 1);
    mutex_err_t ret = ioctl(mutex_fd, MUTEX_IOCTL_SLEEP, &arg) ? MUTEX_INTERNAL_ERR : MUTEX_OK;
    atomic_fetch_sub(&m->sleep_cnt, 1);
    return ret;
}

mutex_err_t mutex_unlock(mutex_t *m)
{
    // DONE unlock spinlock
    // and wakeup one kernel side waiter
    // if it exists.

    shared_spin_unlock(&m->spinlock);

    if (atomic_load(&m->sleep_cnt) > 0) {
        mutex_ioctl_wakeup_arg_t arg = { .id = m->id };
        return ioctl(mutex_fd, MUTEX_IOCTL_WAKEUP, &arg) ? MUTEX_INTERNAL_ERR : MUTEX_OK;
    } else {
        return MUTEX_OK;
    }
}

mutex_err_t mutex_lib_init()
{
    // DONE create current process mutex
    // registry on kernel side

    if (mutex_fd >= 0) {
        return MUTEX_DOUBLE_INIT;
    }

    int fd = open("/dev/mutex", O_RDWR);
    if (fd < 0) {
        return MUTEX_INTERNAL_ERR;
    }
    mutex_fd = fd;
    return MUTEX_OK;
}

mutex_err_t mutex_lib_deinit()
{
    // DONE destroy current process mutex
    // registry on kernel side

    int ret = close(mutex_fd);
    if (ret) {
        return MUTEX_INTERNAL_ERR;
    }
    mutex_fd = -1;
    return MUTEX_OK;
}
