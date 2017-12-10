#include <fcntl.h>

#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include "vsd_device.h"
#include "../vsd_driver/vsd_ioctl.h"

static const char *dev_file = "/dev/vsd";
static int dev_fd = -1;
static bool initialized = false;
static pthread_mutex_t init_mutex;

int vsd_init()
{
    int ret = 0;

    pthread_mutex_lock(&init_mutex);

    if (initialized) {
        ret = VSD_DEV_DOUBLE_INIT;
        goto end;
    }

    int fd = open(dev_file, O_RDWR);
    if (fd < 0) {
        ret = fd;
        goto end;
    }
    dev_fd = fd;
    initialized = true;
end:
    pthread_mutex_unlock(&init_mutex);
    return ret;
}

int vsd_deinit()
{
    int ret = 0;

    pthread_mutex_lock(&init_mutex);

    if (!initialized) {
        ret = VSD_DEV_NOT_INIT;
    }

    int err = close(dev_fd);
    if (err < 0) {
        ret = err;
        goto end;
    }
    initialized = false;
end:
    pthread_mutex_unlock(&init_mutex);
    return ret;
}

int vsd_get_size(size_t *out_size)
{
    int ret = 0;

    vsd_ioctl_get_size_arg_t arg;
    if ((ret = ioctl(dev_fd, VSD_IOCTL_GET_SIZE, &arg))) {
        return ret;
    }

    *out_size = arg.size;
    return 0;
}

int vsd_set_size(size_t size)
{
    int ret = 0;

    vsd_ioctl_set_size_arg_t arg = {.size = size};
    if ((ret = ioctl(dev_fd, VSD_IOCTL_SET_SIZE, &arg))) {
        return ret;
    }

    return 0;
}

ssize_t vsd_read(char* dst, off_t offset, size_t size)
{
    if (lseek(dev_fd, offset, SEEK_SET) != offset) {
        return -2;
    }

    return read(dev_fd, dst, size);
}

ssize_t vsd_write(const char* src, off_t offset, size_t size)
{
    if (lseek(dev_fd, offset, SEEK_SET) != offset) {
        return -2;
    }
    return write(dev_fd, src, size);
}

void* vsd_mmap(size_t offset)
{
    size_t size = 0;
    void *ret = NULL;
    int unlock_res = 0;

    // The only way I know to reliably lock the size
    // and to guarantee that the "lock" will be released if, for example,
    // the userspace process dies
    void *lock = mmap(NULL, 1, PROT_NONE, MAP_SHARED, dev_fd, offset);

    if (vsd_get_size(&size) != 0) {
        return NULL;
    }

    ret = mmap(NULL, size - offset, PROT_READ | PROT_WRITE, MAP_SHARED, dev_fd, offset);
end:
    unlock_res = munmap(lock, 1);
    assert(unlock_res == 0);
    return ret;
}

int vsd_munmap(void* addr, size_t offset)
{
    size_t size = 0;

    // size is already locked
    if (vsd_get_size(&size) != 0) {
        return -2;
    }

    return munmap(addr, size - offset);
}
