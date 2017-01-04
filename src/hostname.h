#pragma once

#include <stdint.h>

void hostname_init(void);
const uint8_t *hostname_get_bytes(void);
const char *hostname_get(void);
size_t hostname_get_bytes_length(void);
