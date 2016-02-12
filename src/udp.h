#pragma once

#include <event2/bufferevent.h>

int udp_send(uint8_t *, uint8_t);
int udp_init(const char *, const char *);
void udp_readcb(struct bufferevent *, void *);
void udp_cleanup(void);
