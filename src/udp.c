#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

#include "c3listener.h"
#include "config.h"
#include "log.h"
#include "time_util.h"
#include "udp.h"

static double udp_last_ack = 0;
static bool connection_valid = false;
static int udp_fd = -1;
static struct bufferevent *udp_bev = NULL;

double udp_get_last_ack(void) {
    return udp_last_ack;
}

/* int udp_get_fd(void) { */
/*     return udp_fd; */
/* } */

bool udp_connected(void) {
    return (udp_fd > 0);
}

struct bufferevent *udp_get_bev(void) {
    return udp_bev;
}

void udp_readcb(struct bufferevent *bev, void *c) {
    UNUSED(c);
    char buf[3] = {0};
    struct evbuffer *input = bufferevent_get_input(bev);
    while (evbuffer_get_length(input) >= 3) {
        bufferevent_read(bev, buf, 3);
        if (!strncmp(buf, "ACK", 3)) {
            udp_last_ack = time_now();
            if (!connection_valid) {
                connection_valid = true;
                log_notice("Connected to %s", config_get_remote_hostname());
            }
        }
    }
    return;
}

static void udp_retry_later(struct event_base *base) {
    static struct event *retry_timer = NULL;
    struct timeval delay_tv = {SERVER_RECONNECT_INTERVAL_SEC, 0};
    if (!retry_timer) {
        retry_timer = evtimer_new(base, udp_init, base);
    }
    if (!evtimer_pending(retry_timer, &delay_tv)) {
        evtimer_add(retry_timer, &delay_tv);
    }
}

void udp_eventcb(struct bufferevent *bev, short events, void *ptr) {
    UNUSED(bev);
    struct event_base *base = ptr;
    if (events & BEV_EVENT_ERROR) {
        log_error("%s: %s. Retrying in %ds", config_get_remote_hostname(),
                  strerror(errno), SERVER_RECONNECT_INTERVAL_SEC);
        /* Don't try to write to errored out bev */
        bufferevent_disable(bev, EV_WRITE);
        udp_retry_later(base);
    } else if (events & BEV_EVENT_EOF) {
        log_error("Bufferevent reports EOF.");
    } else if (events & BEV_EVENT_TIMEOUT) {
        log_warn("Bufferevent reports timeout.");
    }
}

static void udp_retry_later(struct event_base *);

void udp_init(int unused1, short unused2, void *arg) {
    UNUSED(unused1);
    UNUSED(unused2);
    struct event_base *base = arg;
    const char *server_hostname = config_get_remote_hostname();
    const char *port = config_get_remote_port();

    /* Cleanup any existing sockets / bufferevents */
    if (udp_bev) {
        bufferevent_free(udp_bev);
        udp_fd = 1;
    }

    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
    hints.ai_flags = 0;
    hints.ai_protocol = 0; /* Any protocol */

    int s = getaddrinfo(server_hostname, port, &hints, &result);
    if (s != 0) {
        log_warn("Failed to resolve %s, retry in %ds", server_hostname,
                 SERVER_RECONNECT_INTERVAL_SEC);
        udp_retry_later(base);
        return;
    }

    /* getaddrinfo() returns a list of address structures.
       Try each address until we successfully connect(2).
       If socket(2) (or connect(2)) fails, we (close the socket
       and) try the next address. */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        void *p;
        if (rp->ai_addr->sa_family == AF_INET) {
            p = &(((struct sockaddr_in *)rp->ai_addr)->sin_addr);
        } else {
            p = &(((struct sockaddr_in6 *)rp->ai_addr)->sin6_addr);
        }
        char s[INET6_ADDRSTRLEN];
        inet_ntop(rp->ai_family, p, s, sizeof s);
        udp_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (udp_fd == -1) {
            continue;
        }
        if (connect(udp_fd, rp->ai_addr, rp->ai_addrlen) != -1) {
            log_notice("Trying connection to %s", s);
            evutil_make_socket_nonblocking(udp_fd);
            udp_bev =
                bufferevent_socket_new(base, udp_fd, BEV_OPT_CLOSE_ON_FREE);
            bufferevent_setcb(udp_bev, udp_readcb, NULL, udp_eventcb, base);
            bufferevent_enable(udp_bev, EV_READ | EV_WRITE);
            break; /* Success */
        } else {
            /* This happens if we can resolve the hostname (because
               it's an IP address) but don't have networking (yet) */
            log_warn("Failed to connect: %s\n", strerror(errno));
            close(udp_fd);
            udp_fd = -1;
            udp_retry_later(base);
        }
    }
    free(result);
    return;
}
