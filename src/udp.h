#pragma once

#include <event2/bufferevent.h>

int udp_send(uint8_t *, uint8_t);
void udp_init(int, short, void *);
void udp_readcb(struct bufferevent *, void *);
void udp_cleanup(void);
double udp_get_last_ack(void);
int udp_get_fd(void);
struct bufferevent *udp_get_bev(void);
bool udp_connected(void);
