#pragma once

#include <event2/bufferevent.h>

void ble_readcb(struct bufferevent *bev, void *ptr);
void ble_scan_loop(int, uint8_t);
int ble_init(int);
char *hexlify(const uint8_t *, size_t);
