/* Report generation */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

#include "beacon.h"
#include "config.h"
#include "kalman.h"
#include "log.h"
#include "report.h"
#include "time_util.h"
#include "udp.h"

#define BEACON_REPORT_SIZE (16 + sizeof(uint16_t) * 3 + sizeof(int16_t) * 2)

static void report_add_header(struct evbuffer *buf, enum report_version version,
                              enum report_packet_type packet_type) {
    char *hostname = config_get_local_hostname();
    uint_fast8_t hostname_len = strlen(hostname);
    uint8_t tmp[] = {(version << 4 | packet_type), (BEACON_REPORT_SIZE),
                     (hostname_len)};
    evbuffer_add(buf, tmp, sizeof(tmp));
    evbuffer_add(buf, hostname, hostname_len);
    return;
}

static void report_send(struct evbuffer *buf) {
    struct bufferevent *udp_bev = NULL;
    if ((udp_bev = udp_get_bev())) {
	bufferevent_write_buffer(udp_bev, buf);
    }
}

void report_cb(int a, short b, void *self) {
    UNUSED(a);
    UNUSED(b);
    UNUSED(self);
    int cb_idx = 0;
    walker_cb func[MAX_HASH_CB] = {NULL};
    void *args[MAX_HASH_CB] = {NULL};

    struct evbuffer *buf = evbuffer_new();

    report_add_header(buf, REPORT_VERSION_0, REPORT_PACKET_TYPE_DATA);
    size_t header_len = evbuffer_get_length(buf);
    func[cb_idx] = report_ibeacon;
    args[cb_idx] = &buf;

    hash_walk(func, args, cb_idx);

    /* If we generated a report this walk, send it */
    if (evbuffer_get_length(buf) > header_len) {
        report_send(buf);
    } else {
        evbuffer_drain(buf, header_len);
        report_add_header(buf, REPORT_VERSION_0, REPORT_PACKET_TYPE_KEEPALIVE);
        report_send(buf);
    }
    evbuffer_free(buf);
}

void report_secure(beacon_t const *const b, uint8_t const *const data,
                   size_t payload_len) {
    struct evbuffer *buf = evbuffer_new();

    report_add_header(buf, REPORT_VERSION_0, REPORT_PACKET_TYPE_SECURE);

    struct sbeacon_id *id = b->id;
    evbuffer_add(buf, id->mac, 6);
    /* Strip TX_POWER (last byte), it's not needed at the server */
    evbuffer_add(buf, data, payload_len - 1);
    uint16_t dist = round(b->distance * 100);
    uint16_t variance = round(b->variance * 100);
    const uint8_t rearr_buf[] = {(dist & 0xff), (dist >> 8), (variance & 0xff),
                                 (variance >> 8)};
    evbuffer_add(buf, rearr_buf, sizeof(rearr_buf));
    report_send(buf);
    evbuffer_free(buf);
}

void *report_ibeacon(void *a, void *v) {
    beacon_t *b = a;
    struct evbuffer *buf = v;

    /* Appends a beacon report to report buffer, funny return value
       are to comply with walker_cb ABI */
    if (!b->count || !(b->type == BEACON_IBEACON)) {
        /* If there are no new adverts or this isn't an ibeacon,
           skip */
        a = beacon_expire(a, NULL);
        return a;
    }

    struct ibeacon_id *id = b->id;

    evbuffer_add(buf, id->uuid, 16);
    uint16_t dist = round(b->distance * 100);
    uint16_t variance = round(b->variance * 100);
    /* Little endian, for reasons? */
    uint8_t tmp[] = {(id->major & 0xff), (id->major >> 8),  (id->minor & 0xff),
                     (id->minor >> 8),   (b->count & 0xff), (b->count >> 8),
                     (dist & 0xff),      (dist >> 8),       (variance >> 8),
                     (variance & 0xff)};
    evbuffer_add(buf, tmp, sizeof(tmp));
    /* Reset beacon packet counter as it counts *unreported*
       packets */
    b->count = 0;
    /* Returns pointer to current object since there is no barrier to
       continuing the walk */
    return a;
}
