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

#define DEFAULT_SPI_DEV "/dev/io-spi/spi0/dev0"
#define SX1278_REG_VERSION 0x42U
#define SX1278_EXPECTED_VERSION 0x12U

static int spi_xfer(int fd, const uint8_t *tx, uint8_t *rx, uint32_t nbytes)
{
    const size_t bytes = sizeof(spi_xchng_t) + nbytes;
    spi_xchng_t *msg = calloc(1U, bytes);

    if (msg == NULL) {
        perror("calloc");
        return -1;
    }

    msg->nbytes = nbytes;
    memcpy(msg->data, tx, nbytes);

    const int status = devctl(fd, DCMD_SPI_DATA_XCHNG, msg, bytes, NULL);
    if (status != EOK) {
        fprintf(stderr,
                "DCMD_SPI_DATA_XCHNG failed: %s (%d)\n",
                strerror(status), status);
        free(msg);
        return -1;
    }

    if (rx != NULL) {
        memcpy(rx, msg->data, nbytes);
    }

    free(msg);
    return 0;
}

int main(int argc, char **argv)
{
    const char *device = DEFAULT_SPI_DEV;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [spi_device]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 2) {
        device = argv[1];
    }

    printf("HEALINK SX1278 SPI probe\n");
    printf("Device: %s\n", device);

    const int fd = open(device, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "open(%s) failed: %s (%d)\n",
                device, strerror(errno), errno);
        return EXIT_FAILURE;
    }

    spi_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.mode = 0U;
    cfg.clock_rate = 1000000U;

    int status = devctl(fd, DCMD_SPI_SET_CONFIG, &cfg, sizeof(cfg), NULL);
    if (status != EOK) {
        fprintf(stderr,
                "DCMD_SPI_SET_CONFIG failed: %s (%d)\n",
                strerror(status), status);
        close(fd);
        return EXIT_FAILURE;
    }

    spi_devinfo_t info;
    memset(&info, 0, sizeof(info));
    status = devctl(fd, DCMD_SPI_GET_DEVINFO, &info, sizeof(info), NULL);
    if (status == EOK) {
        printf("SPI devno=%d name=%s clock=%u mode=%u\n",
               info.devno, info.name,
               info.current_clkrate, info.cfg.mode);
    } else {
        printf("SPI devinfo unavailable: %s (%d)\n",
               strerror(status), status);
    }

    uint8_t tx[2] = { SX1278_REG_VERSION, 0x00U };
    uint8_t rx[2] = { 0U, 0U };

    printf("Reading SX1278 RegVersion (0x42)...\n");
    if (spi_xfer(fd, tx, rx, 2U) != 0) {
        close(fd);
        return EXIT_FAILURE;
    }

    printf("TX: %02X %02X\n", tx[0], tx[1]);
    printf("RX: %02X %02X\n", rx[0], rx[1]);
    printf("RegVersion: 0x%02X\n", rx[1]);

    if (rx[1] == SX1278_EXPECTED_VERSION) {
        printf("SX1278 PROBE: PASS (expected 0x12)\n");
    } else {
        printf("SX1278 PROBE: UNEXPECTED VERSION (expected 0x12)\n");
    }

    close(fd);
    return (rx[1] == SX1278_EXPECTED_VERSION) ? EXIT_SUCCESS : EXIT_FAILURE;
}
