#include "qnx_i2c.h"

#include <devctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hw/i2c.h>


int healink_i2c_open(healink_i2c_t *i2c_bus,
                     const char *device,
                     uint32_t speed_hz)
{
    uint32_t speed;
    int status;

    if (i2c_bus == NULL || device == NULL || speed_hz == 0U) {
        errno = EINVAL;
        return -1;
    }

    memset(i2c_bus, 0, sizeof(*i2c_bus));
    i2c_bus->file_descriptor = -1;
    i2c_bus->speed_hz = 0U;

    i2c_bus->file_descriptor = open(device, O_RDWR);
    if (i2c_bus->file_descriptor < 0) {
        return -1;
    }

    speed = speed_hz;

    status = devctl(i2c_bus->file_descriptor,
                    DCMD_I2C_SET_BUS_SPEED,
                    &speed,
                    sizeof(speed),
                    NULL);

    if (status != EOK) {
        close(i2c_bus->file_descriptor);
        i2c_bus->file_descriptor = -1;
        errno = status;
        return -1;
    }

    i2c_bus->speed_hz = speed_hz;

    return 0;
}


void healink_i2c_close(healink_i2c_t *i2c_bus)
{
    if (i2c_bus == NULL) {
        return;
    }

    if (i2c_bus->file_descriptor >= 0) {
        close(i2c_bus->file_descriptor);
        i2c_bus->file_descriptor = -1;
    }

    i2c_bus->speed_hz = 0U;
}


int healink_i2c_write(healink_i2c_t *i2c_bus,
                      uint8_t device_address,
                      const uint8_t *data,
                      size_t length)
{
    size_t message_size;
    uint8_t *message_buffer;
    i2c_send_t *message;
    int status;

    if (i2c_bus == NULL ||
        i2c_bus->file_descriptor < 0 ||
        data == NULL ||
        length == 0U ||
        length > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    if (length > SIZE_MAX - sizeof(i2c_send_t)) {
        errno = EOVERFLOW;
        return -1;
    }

    message_size = sizeof(i2c_send_t) + length;

    message_buffer = malloc(message_size);
    if (message_buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    memset(message_buffer, 0, message_size);

    message = (i2c_send_t *)message_buffer;
    message->slave.addr = device_address;
    message->slave.fmt = I2C_ADDRFMT_7BIT;
    message->len = (uint32_t)length;
    message->stop = 1U;

    memcpy(message_buffer + sizeof(i2c_send_t), data, length);

    status = devctl(i2c_bus->file_descriptor,
                    DCMD_I2C_SEND,
                    message_buffer,
                    message_size,
                    NULL);

    free(message_buffer);

    if (status != EOK) {
        errno = status;
        return -1;
    }

    return 0;
}

int healink_i2c_read(healink_i2c_t *i2c_bus,
                     uint8_t device_address,
                     uint8_t *data,
                     size_t length)
{
    size_t message_size;
    uint8_t *message_buffer;
    i2c_recv_t *message;
    int status;

    if (i2c_bus == NULL ||
        i2c_bus->file_descriptor < 0 ||
        data == NULL ||
        length == 0U ||
        length > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    if (length > SIZE_MAX - sizeof(i2c_recv_t)) {
        errno = EOVERFLOW;
        return -1;
    }

    message_size = sizeof(i2c_recv_t) + length;

    message_buffer = malloc(message_size);
    if (message_buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    memset(message_buffer, 0, message_size);

    message = (i2c_recv_t *)message_buffer;
    message->slave.addr = device_address;
    message->slave.fmt = I2C_ADDRFMT_7BIT;
    message->len = (uint32_t)length;
    message->stop = 1U;

    status = devctl(i2c_bus->file_descriptor,
                    DCMD_I2C_RECV,
                    message_buffer,
                    message_size,
                    NULL);

    if (status == EOK) {
        memcpy(data,
               message_buffer + sizeof(i2c_recv_t),
               length);
    }

    free(message_buffer);

    if (status != EOK) {
        errno = status;
        return -1;
    }

    return 0;
}

int healink_i2c_write_read(healink_i2c_t *i2c_bus,
                           uint8_t device_address,
                           const uint8_t *write_buffer,
                           size_t write_length,
                           uint8_t *read_buffer,
                           size_t read_length)
{
    size_t buffer_size;
    size_t io_size;
    uint8_t *buffer;
    i2c_sendrecv_t *msg;
    int status;

    if (i2c_bus == NULL ||
        i2c_bus->file_descriptor < 0 ||
        write_buffer == NULL ||
        write_length == 0U ||
        read_buffer == NULL ||
        read_length == 0U ||
        write_length > UINT32_MAX ||
        read_length > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    /*
     * DCMD_I2C_SENDRECV has one buffer following its header.  It contains
     * the outgoing bytes before devctl(), then is overwritten in place with
     * the incoming bytes.  It must therefore fit the larger transfer, not
     * the sum of both transfers.
     */
    io_size = write_length > read_length ? write_length : read_length;
    if (io_size > SIZE_MAX - sizeof(i2c_sendrecv_t)) {
        errno = EOVERFLOW;
        return -1;
    }

    buffer_size = sizeof(i2c_sendrecv_t) + io_size;

    buffer = malloc(buffer_size);
    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    memset(buffer, 0, buffer_size);

    msg = (i2c_sendrecv_t *)buffer;

    msg->slave.addr = device_address;
    msg->slave.fmt = I2C_ADDRFMT_7BIT;
    msg->send_len = (uint32_t)write_length;
    msg->recv_len = (uint32_t)read_length;
    msg->stop = 1U;

    memcpy(buffer + sizeof(i2c_sendrecv_t),
           write_buffer,
           write_length);

    status = devctl(i2c_bus->file_descriptor,
                    DCMD_I2C_SENDRECV,
                    buffer,
                    buffer_size,
                    NULL);

    if (status == EOK) {
        memcpy(read_buffer,
               buffer + sizeof(i2c_sendrecv_t),
               read_length);
    }

    free(buffer);

    if (status != EOK) {
        errno = status;
        return -1;
    }

    return 0;
}


int healink_i2c_write_reg8(healink_i2c_t *i2c_bus,
                           uint8_t device_address,
                           uint8_t register_address,
                           uint8_t value)
{
    uint8_t buffer[2];

    buffer[0] = register_address;
    buffer[1] = value;

    return healink_i2c_write(i2c_bus,
                             device_address,
                             buffer,
                             sizeof(buffer));
}


int healink_i2c_read_reg8(healink_i2c_t *i2c_bus,
                          uint8_t device_address,
                          uint8_t register_address,
                          uint8_t *value)
{
    return healink_i2c_write_read(i2c_bus,
                                  device_address,
                                  &register_address,
                                  1U,
                                  value,
                                  1U);
}


int healink_i2c_read_regs(healink_i2c_t *i2c_bus,
                          uint8_t device_address,
                          uint8_t register_address,
                          uint8_t *buffer,
                          size_t length)
{
    return healink_i2c_write_read(i2c_bus,
                                  device_address,
                                  &register_address,
                                  1U,
                                  buffer,
                                  length);
}


int healink_i2c_write_reg16_be(healink_i2c_t *i2c_bus,
                               uint8_t device_address,
                               uint8_t register_address,
                               uint16_t value)
{
    uint8_t buffer[3];

    buffer[0] = register_address;
    buffer[1] = (uint8_t)(value >> 8U);
    buffer[2] = (uint8_t)(value & 0xFFU);

    return healink_i2c_write(i2c_bus,
                             device_address,
                             buffer,
                             sizeof(buffer));
}


int healink_i2c_read_reg16_be(healink_i2c_t *i2c_bus,
                              uint8_t device_address,
                              uint8_t register_address,
                              uint16_t *value)
{
    uint8_t buffer[2];
    int result;

    if (value == NULL) {
        errno = EINVAL;
        return -1;
    }

    result = healink_i2c_read_regs(i2c_bus,
                               device_address,
                               register_address,
                               buffer,
                               sizeof(buffer));

    if (result != 0) {
        return result;
    }

    *value = ((uint16_t)buffer[0] << 8U) |
             (uint16_t)buffer[1];

    return 0;
}
