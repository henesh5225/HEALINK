#define _POSIX_C_SOURCE 200809L

#include <hw/io-spi.h>
#include <devctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SPI_DEV "/dev/io-spi/spi0/dev0"

#define SX1278_REG_VERSION     0x42U
#define SX1278_EXPECTED_VERSION 0x12U

#define XFER_BYTES 4U
#define READ_COUNT 10

static int spi_read_version(int fd, uint8_t *version)
{
    const size_t bytes = sizeof(spi_xchng_t) + XFER_BYTES;

    spi_xchng_t *msg = calloc(1U, bytes);

    if (msg == NULL) {
        perror("calloc");
        return -1;
    }

    msg->nbytes = XFER_BYTES;

    /*
     * QNX spi0/dev0 is configured with word_width=32.
     *
     * SX1278 RegVersion read:
     *   byte 0 = register address
     *   byte 1 = dummy/read byte
     *   byte 2/3 = additional clock cycles
     */
    msg->data[0] = SX1278_REG_VERSION;
    msg->data[1] = 0x00U;
    msg->data[2] = 0x00U;
    msg->data[3] = 0x00U;

    const int status = devctl(fd,
                              DCMD_SPI_DATA_XCHNG,
                              msg,
                              bytes,
                              NULL);

    if (status != EOK) {
        fprintf(stderr,
                "DCMD_SPI_DATA_XCHNG failed: %s (%d)\n",
                strerror(status),
                status);

        free(msg);
        return -1;
    }

    *version = msg->data[1];

    printf("TX: %02X %02X %02X %02X  "
           "RX: %02X %02X %02X %02X\n",
           SX1278_REG_VERSION,
           0x00U,
           0x00U,
           0x00U,
           msg->data[0],
           msg->data[1],
           msg->data[2],
           msg->data[3]);

    free(msg);
    return 0;
}

int main(void)
{
    printf("HEALINK SX1278 32-bit repeated SPI test\n");
    printf("Device: %s\n", SPI_DEV);
    printf("Using existing QNX spi.conf; no DCMD_SPI_SET_CONFIG issued.\n");
    printf("Exchange length: %u bytes\n\n", XFER_BYTES);

    const int fd = open(SPI_DEV, O_RDWR);

    if (fd == -1) {
        fprintf(stderr,
                "open(%s) failed: %s (%d)\n",
                SPI_DEV,
                strerror(errno),
                errno);
        return EXIT_FAILURE;
    }

    int passed = 0;

    for (int i = 0; i < READ_COUNT; ++i) {

        uint8_t version = 0U;

        printf("READ %d: ", i + 1);
        fflush(stdout);

        if (spi_read_version(fd, &version) != 0) {
            printf("FAILED\n");
            close(fd);
            return EXIT_FAILURE;
        }

        if (version == SX1278_EXPECTED_VERSION) {
            printf("RegVersion=0x%02X PASS\n", version);
            ++passed;
        } else {
            printf("RegVersion=0x%02X UNEXPECTED\n", version);
        }
    }

    close(fd);

    printf("\nResult: %d/%d valid SX1278 reads.\n",
           passed,
           READ_COUNT);

    return (passed == READ_COUNT) ? EXIT_SUCCESS : EXIT_FAILURE;
}
