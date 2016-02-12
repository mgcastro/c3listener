/* Report generation */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "beacon.h"
#include "config.h"
#include "kalman.h"
#include "log.h"
#include "report.h"
#include "time_util.h"
#include "udp.h"

#define BEACON_REPORT_SIZE (16 + sizeof(uint16_t) * 3 + sizeof(int16_t) * 2)

static uint8_t *p_buf = NULL;
static int p_buf_pos = 0, b_count = 0, p_buf_size = 0, hostlen = 0;
static char hostname[HOSTNAME_MAX_LEN + 1] = {0};
static struct bufferevent *udp_bev;

void report_cb(int a, short b, void *self) {
  int cb_idx = 0;
  walker_cb func[MAX_HASH_CB] = {NULL};
  void *args[MAX_HASH_CB] = {NULL};
  
  report_clear();
  report_header(REPORT_VERSION_0, REPORT_PACKET_TYPE_DATA);
  func[cb_idx] = report_beacon;
  args[cb_idx++] = NULL;
  
  hash_walk(func, args, cb_idx);
  
  /* If we generated a report this walk, send it */
  if (report_length() > report_header_length()) {
    report_send();
  } else {
    report_header(REPORT_VERSION_0, REPORT_PACKET_TYPE_KEEPALIVE);
    report_send();
    log_debug("Keep alive sent\n");
  }
}

void report_clear(void) {
  memset(p_buf, 0, p_buf_size);
  p_buf_pos = 0;
  b_count = 0;
}

void report_init(struct bufferevent *bev) {
  udp_bev = bev;
  gethostname(hostname, HOSTNAME_MAX_LEN);
  hostlen = strnlen(hostname, HOSTNAME_MAX_LEN);
  if (p_buf == NULL) {
    p_buf_size = hostlen + BEACON_REPORT_SIZE * (b_count + 1);
    p_buf = malloc(p_buf_size);
  }
  memset(p_buf, 0, p_buf_size);
  p_buf_pos = 0;
  b_count = 0;
}

void report_header(int version, int packet_type) {
  p_buf[0] = version << 4 | packet_type;
  p_buf[1] = BEACON_REPORT_SIZE;
  p_buf[2] = hostlen;
  memcpy(p_buf + 3, hostname, hostlen);
  p_buf_pos = 3 + hostlen;
  return;
}

int report_length(void) {
  return p_buf_pos;
}

int report_header_length(void) {
  return hostlen + 3;
}

int report_free_bytes(void) {
  return p_buf_size - p_buf_pos;
}

void report_send(void) {
  bufferevent_write(udp_bev, p_buf, report_length());
  udp_send(p_buf, report_length());
}

void *report_beacon(void *a, void *unused) {
  /* Appends a beacon report to udp_packet buffer returning size,
     funny args and return are to comply with walker_cb ABI */
  beacon_t *b = a;
  if (!b->count) {
    /* If there are no new adverts, skip report */
    return a;
  }
  if (report_free_bytes() < BEACON_REPORT_SIZE) {
    p_buf = realloc(p_buf, p_buf_size + BEACON_REPORT_SIZE);
    memset(p_buf + p_buf_size, 0, BEACON_REPORT_SIZE);
    p_buf_size += BEACON_REPORT_SIZE;
  }
  uint8_t *p, *q;
  q = p = p_buf + p_buf_pos;
  memcpy(p, b->uuid, 16);
  p += 16;
  *(p++) = (uint8_t)(b->major >> 8);
  *(p++) = (uint8_t)(b->major);
  *(p++) = (uint8_t)(b->minor >> 8);
  *(p++) = (uint8_t)(b->minor);
  *(p++) = (uint8_t)(b->count >> 8);
  *(p++) = (uint8_t)(b->count);
  uint16_t dist = round(b->distance * 100);
  *(p++) = (uint8_t)(dist >> 8);
  *(p++) = (uint8_t)(dist);
  uint16_t variance = round(b->kalman.P[0][0] * 100);
  *(p++) = (uint8_t)(variance >> 8);
  *(p++) = (uint8_t)(variance);
  p_buf_pos += p - q;
  b->count = 0;
  return a;
}
