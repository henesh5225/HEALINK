# HEALINK - QNX SDP / Raspberry Pi AArch64
# Maintained final project build.

NAME := healink
DIAG_NAME := healink_diag
TEST_NAME := healink_fusion_test
LORA_PROBE := sx1278_probe
LORA_REPEAT := sx1278_repeat

CC := qcc
QNX_VARIANT ?= gcc_ntoaarch64le

CFLAGS := -V$(QNX_VARIANT) \
          -Wall \
          -Wextra \
          -Wconversion \
          -Wshadow \
          -Wpedantic \
          -O2 \
          -g \
          -fno-builtin

CPPFLAGS := -Iinclude
LDFLAGS :=
LDLIBS := -lm

SRC := \
    src/main.c \
    src/core/healink_state.c \
    src/core/healink_ipc.c \
    src/core/healink_fusion.c \
    src/core/healink_classifier.c \
    src/core/healink_staleness.c \
    src/core/healink_emergency.c \
    src/core/healink_safety.c \
    src/comms/healink_lora.c \
    src/platform/qnx_time.c \
    src/platform/qnx_i2c.c \
    src/platform/rpi_gpio.c \
    src/sensors/max30102.c \
    src/sensors/ads1115.c \
    src/sensors/ad8232.c \
    src/sensors/mpu6050.c \
    src/sensors/ds18b20.c \
    src/sensors/dht11.c \
    src/ui/ssd1306.c \
    src/ui/healink_ui.c

OBJ := $(SRC:.c=.o)

DIAG_SRC := \
    src/diag/healink_diag.c \
    src/platform/qnx_i2c.c
DIAG_OBJ := $(DIAG_SRC:.c=.o)

TEST_SRC := \
    tests/test_fusion.c \
    src/core/healink_fusion.c \
    src/core/healink_classifier.c \
    src/core/healink_staleness.c \
    src/core/healink_safety.c
TEST_OBJ := $(TEST_SRC:.c=.o)

.PHONY: all clean diag test a10 lora-probe lora-repeat lora-tools print-config

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(DIAG_NAME): $(DIAG_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

diag: $(DIAG_NAME)

test: $(TEST_NAME)

$(TEST_NAME): $(TEST_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(LORA_PROBE): tools/sx1278_probe.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

$(LORA_REPEAT): tools/sx1278_repeat.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

lora-probe: $(LORA_PROBE)
lora-repeat: $(LORA_REPEAT)
lora-tools: $(LORA_PROBE) $(LORA_REPEAT)
	@echo "LoRa bring-up tools built: $(LORA_PROBE) $(LORA_REPEAT)"

a10: $(NAME) $(TEST_NAME)
	@echo "Run target-side: sh tools/qnx_a10_soak.sh 3 60"

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(DIAG_OBJ) $(TEST_OBJ) \
	      $(NAME) $(DIAG_NAME) $(TEST_NAME) \
	      $(LORA_PROBE) $(LORA_REPEAT)

print-config:
	@echo "NAME=$(NAME)"
	@echo "DIAG_NAME=$(DIAG_NAME)"
	@echo "TEST_NAME=$(TEST_NAME)"
	@echo "QNX_VARIANT=$(QNX_VARIANT)"
	@echo "CC=$(CC)"
