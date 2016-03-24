#ifndef C3LISTENER_H
#define C3LISTENER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "kalman.h"

#define HOSTNAME_MAX_LEN 255
#define MAX_NET_PACKET 64

#define REPORT_INTERVAL_MSEC 500 /* How often to send reports */

char *hexlify(const uint8_t *src, size_t n);

typedef struct advdata {
    char mac[6];
    char data[3];
    int data_len;
    int rssi;
} adv_data_t;

#endif /* C3LISTENER_H */
