# HEALINK code style

The code uses simple names that describe their purpose. Functions use snake_case and keep the module prefix, such as `healink_fusion_update()` and `mpu6050_read()`. Local variables prefer names like `current_time_ns`, `sample_timestamp_ns`, `event_queue`, and `spi_fd` rather than one-letter names. Process-wide objects in `main.c` use the `g_` prefix.

Comments are short and explain only the parts that need context: hardware wiring, QNX timing, safety decisions, or protocol behavior. They are not meant to repeat the code line by line.

The project is built with `-Wall -Wextra -Wconversion -Wshadow -Wpedantic`.
