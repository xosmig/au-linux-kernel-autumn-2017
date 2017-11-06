#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "../vsd_driver/vsd_ioctl.h"

/*
 * TODO parse command line arguments and call proper
 * VSD_IOCTL_* using C function 'ioctl' (see man ioctl).
 */
static const char *dev_file = "/dev/vsd";

static const char *usage =
"Usage:\n"
"\tvsd_userspace size_get\n"
"\tvsd_userspace size_set SIZE_IN_BYTES\n";

static const char *get_size_command = "size_get";
static const char *set_size_command = "size_set";

static const int err_inval = 1;
static const int err_open_file = 2;
static const int err_close_file = 3;

void print_usage() {
    printf("%s\n", usage);
}

int cli_open_device_file() {
    int fd = open(dev_file, 0);
    if (fd < 0) {
        printf("Can't open device file: %s\n", dev_file);
    }
    return fd;
}

int cli_close_device_file(int fd) {
    int err = close(fd);
    if (err) {
        printf("Can't close the device file: %s\n", dev_file);
        return err_close_file;
    } else {
        return 0;
    }
}

int cli_get_size(int argc, char **argv)
{
    int ret = 0;

    int fd = cli_open_device_file();
    if (fd < 0) {
        ret = err_open_file;
        goto out;
    }

    vsd_ioctl_get_size_arg_t arg;
    ret = ioctl(fd, VSD_IOCTL_GET_SIZE, &arg);
    if (ret) {
        printf("Can't get the size of the device: %s\n", dev_file);
        goto error;
    }

    printf("%lu\n", arg.size);

    return cli_close_device_file(fd);

error:
    cli_close_device_file(fd);
out:
    return ret;
}

int cli_set_size(int argc, char **argv)
{
    int ret = 0;

    int fd = cli_open_device_file();
    if (fd < 0) {
        ret = err_open_file;
        goto out;
    }

    if (argc < 1) {
        goto cli_error;
    }

    vsd_ioctl_set_size_arg_t arg;
    if (sscanf(argv[0], "%lu", &arg.size) != 1) {
        goto cli_error;
    }

    ret = ioctl(fd, VSD_IOCTL_SET_SIZE, &arg);
    if (ret) {
        printf("Can't set the size of the device: %s\n", dev_file);
        goto error;
    }

    return cli_close_device_file(fd);

cli_error:
    print_usage();
    ret = err_inval;
error:
    cli_close_device_file(fd);
out:
    return ret;
}

int cli(int argc, char **argv)
{
    if (argc < 1) {
        print_usage();
        return err_inval;
    }

    if (!strcmp(argv[0], get_size_command)) {
        cli_get_size(argc - 1, argv + 1);
    } else if (!strcmp(argv[0], set_size_command)) {
        cli_set_size(argc - 1, argv + 1);
    }
}

int main(int argc, char **argv)
{
    cli(argc - 1, argv + 1);
    return 0;
}
