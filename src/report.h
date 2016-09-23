#pragma once

#include <event2/bufferevent.h>

enum report_version { REPORT_VERSION_0 = 0 };

enum report_packet_type {
    REPORT_PACKET_TYPE_KEEPALIVE = 0,
    REPORT_PACKET_TYPE_DATA = 1,
    REPORT_PACKET_TYPE_SECURE = 2,
};

void report_cb(int, short int, void *);
void *report_ibeacon(void *a, void *b);
void report_secure(beacon_t const *const, uint8_t const *const, size_t);
