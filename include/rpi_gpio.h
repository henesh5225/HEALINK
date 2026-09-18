#ifndef RPI_GPIO_H
#define RPI_GPIO_H

#include <stddef.h>

int rpi_gpio_read_level(int gpio_number, int *gpio_level);
int rpi_gpio_write_level(int gpio_number, int gpio_level);
int rpi_gpio_set_input(int gpio_number);
int rpi_gpio_set_output(int gpio_number, int initial_level);

#endif
