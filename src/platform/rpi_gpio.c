#define _POSIX_C_SOURCE 200809L
#include "rpi_gpio.h"

#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>

#include "healink_config.h"

static int gpio_bcm2711_configure(int gpio_number, const char *direction, const char *gpio_level)
{
    char command[96];
    int result;
    int n;

    if (direction == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (gpio_level == NULL) {
        n = snprintf(
            command,
            sizeof(command),
            "gpio_number-bcm2711 set %d %s",
            gpio_number,
            direction);
    } else {
        n = snprintf(
            command,
            sizeof(command),
            "gpio_number-bcm2711 set %d %s %s",
            gpio_number,
            direction,
            gpio_level);
    }

    if (n < 0 || (size_t)n >= sizeof(command)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    /*
     * QNX's Raspberry Pi 4 BSP exposes the GPIO controller through the
     * gpio_number-bcm2711 utility while /dev/gpio_number provides the pin resource-manager
     * files used for ordinary gpio_level I/O. Direction is configured here only
     * during initialization, never from a hard real-time acquisition loop.
     */
    result = system(command);
    if (result == -1) {
        return -1;
    }

    if (!WIFEXITED(result) || WEXITSTATUS(result) != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int gpio_fd_open(int gpio_number, int flags)
{
    char path[64];
    int n = snprintf(path, sizeof(path), "%s/%d", HEALINK_GPIO_DEV, gpio_number);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return open(path, flags);
}

int rpi_gpio_read_level(int gpio_number, int *gpio_level)
{
    int gpio_fd;
    char buf[16];
    ssize_t n;

    if (gpio_level == NULL) {
        errno = EINVAL;
        return -1;
    }

    gpio_fd = gpio_fd_open(gpio_number, O_RDONLY);
    if (gpio_fd < 0) {
        return -1;
    }
    n = pread(gpio_fd, buf, sizeof(buf) - 1U, 0);
    close(gpio_fd);
    if (n <= 0) {
        return -1;
    }
    buf[n] = '\0';
    if (strcmp(buf, "0") == 0 || strcmp(buf, "0\n") == 0) {
        *gpio_level = 0;
        return 0;
    }

    if (strcmp(buf, "1") == 0 || strcmp(buf, "1\n") == 0) {
        *gpio_level = 1;
        return 0;
    }

    errno = EPROTO;
    return -1;
}

int rpi_gpio_write_level(int gpio_number, int gpio_level)
{
    int gpio_fd;
    char buf[4];
    int n;

    gpio_fd = gpio_fd_open(gpio_number, O_WRONLY);
    if (gpio_fd < 0) {
        return -1;
    }
    n = snprintf(buf, sizeof(buf), "%d\n", gpio_level ? 1 : 0);
    if (write(gpio_fd, buf, (size_t)n) != (ssize_t)n) {
        close(gpio_fd);
        return -1;
    }
    close(gpio_fd);
    return 0;
}

int rpi_gpio_set_input(int gpio_number)
{
    if (gpio_number < 0) {
        errno = EINVAL;
        return -1;
    }

    return gpio_bcm2711_configure(
        gpio_number,
        "ip",
        NULL);
}

int rpi_gpio_set_output(int gpio_number, int initial_level)
{
    if (gpio_number < 0 || (initial_level != 0 && initial_level != 1)) {
        errno = EINVAL;
        return -1;
    }

    return gpio_bcm2711_configure(
        gpio_number,
        "op",
        initial_level != 0 ? "dh" : "dl");
}
