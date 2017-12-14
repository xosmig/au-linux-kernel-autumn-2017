#include <fcntl.h>

#include <pthread.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include "vsd_device.h"
#include "../vsd_driver/vsd_ioctl.h"

static const char *dev_file = "/dev/vsd";
static int dev_fd = -1;
static bool initialized = false;
static pthread_rwlock_t init_lock = PTHREAD_RWLOCK_INITIALIZER;

int vsd_init()
{
    int ret = 0;

    pthread_rwlock_wrlock(&init_lock);

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
    pthread_rwlock_unlock(&init_lock);
    return ret;
}

int vsd_deinit()
{
    int ret = 0;

    pthread_rwlock_wrlock(&init_lock);

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
    pthread_rwlock_unlock(&init_lock);
    return ret;
}

#define RUN_INIT_LOCKED(ret_type, func, ...) \
    do { \
        pthread_rwlock_rdlock(&init_lock); \
        ret_type ret = func(__VA_ARGS__); \
        pthread_rwlock_unlock(&init_lock); \
        return ret; \
    } while (0)

static int vsd_get_size_impl(size_t *out_size)
{
    if (!initialized) {
        return -VSD_DEV_NOT_INIT;
    }

    vsd_ioctl_get_size_arg_t arg;
    int ret = ioctl(dev_fd, VSD_IOCTL_GET_SIZE, &arg);
    if (ret) {
        return ret;
    }

    *out_size = arg.size;
    return 0;
}

int vsd_get_size(size_t *out_size)
{
    RUN_INIT_LOCKED(int, vsd_get_size_impl, out_size);
}

static int vsd_set_size_impl(size_t size)
{
    if (!initialized) {
        return -VSD_DEV_NOT_INIT;
    }

    vsd_ioctl_set_size_arg_t arg = {.size = size};
    return ioctl(dev_fd, VSD_IOCTL_SET_SIZE, &arg);
}

int vsd_set_size(size_t size)
{
    RUN_INIT_LOCKED(int, vsd_set_size_impl, size);
}

static ssize_t vsd_read_impl(char* dst, off_t offset, size_t size)
{
    if (!initialized) {
        return -VSD_DEV_NOT_INIT;
    }

    // We need a separate file descriptor to avoid sharing the file position
    int fd = open(dev_file, O_RDWR);
    if (fd < 0) {
        return fd;
    }
    int ret = 0;

    int lseek_res = lseek(fd, offset, SEEK_SET);
    if (lseek_res < 0) {
        ret = lseek_res;
        goto end_opened;
    }

    ret = read(fd, dst, size);
end_opened:
    close(fd);
    return ret;
}

ssize_t vsd_read(char* dst, off_t offset, size_t size)
{
    RUN_INIT_LOCKED(ssize_t, vsd_read_impl, dst, offset, size);
}

static ssize_t vsd_write_impl(const char* src, off_t offset, size_t size)
{
    if (!initialized) {
        return -VSD_DEV_NOT_INIT;
    }

    // We need a separate file descriptor to avoid sharing the file position
    int fd = open(dev_file, O_RDWR);
    if (fd < 0) {
        return fd;
    }
    int ret = 0;

    int lseek_res = lseek(fd, offset, SEEK_SET);
    if (lseek_res < 0) {
        ret = lseek_res;
        goto end_opened;
    }

    ret = write(fd, src, size);
end_opened:
    close(fd);
    return ret;
}

ssize_t vsd_write(const char* src, off_t offset, size_t size)
{
    RUN_INIT_LOCKED(ssize_t, vsd_write_impl, src, offset, size);
}

static void* vsd_mmap_impl(size_t offset)
{
    if (!initialized) {
        errno = VSD_DEV_NOT_INIT;
        return NULL;
    }

    // The only way I know to reliably lock the size
    // and to guarantee that the "lock" will be released if, for example,
    // the userspace process dies
    void *lock = mmap(NULL, 1, PROT_NONE, MAP_SHARED, dev_fd, offset);
    if (!lock) {
        return NULL;
    }
    void *ret = NULL;

    size_t size;
    int get_size_res = vsd_get_size(&size);
    if (get_size_res != 0) {
        ret = NULL;
        errno = get_size_res;
        goto end_locked;
    }

    ret = mmap(NULL, size - offset, PROT_READ | PROT_WRITE, MAP_SHARED, dev_fd, offset);
    int unlock_res = 0;
end_locked:
    unlock_res = munmap(lock, 1);
    assert(unlock_res == 0);
    return ret;
}

void* vsd_mmap(size_t offset) {
    RUN_INIT_LOCKED(void*, vsd_mmap_impl, offset);
}

static int vsd_munmap_impl(void* addr, size_t offset)
{
    if (!initialized) {
        errno = VSD_DEV_NOT_INIT;
        return -1;
    }

    // The size is already locked by this mapping
    size_t size;
    int get_size_res = vsd_get_size(&size);
    if (get_size_res != 0) {
        errno = get_size_res;
        return -1;
    }

    return munmap(addr, size - offset);
}

int vsd_munmap(void* addr, size_t offset)
{
    RUN_INIT_LOCKED(int, vsd_munmap_impl, addr, offset);
}
