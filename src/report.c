/* Report generation */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "beacon.h"
#include "c3listener.h"
#include "kalman.h"
#include "time_util.h"

extern c3_config_t m_config;

#define BEACON_REPORT_SIZE (6+16+sizeof(uint16_t)*3+sizeof(double)+1)

static uint8_t *p_buf = NULL;
static int p_buf_pos = 0,
  b_count = 0,
  p_buf_size = 0, hostlen = 0;

void report_clear(void) {
  hostlen = strnlen(m_config.hostname, HOSTNAME_MAX_LEN);
  if (p_buf == NULL) {
    p_buf_size = hostlen + BEACON_REPORT_SIZE*(b_count+1);
    p_buf = malloc(p_buf_size);
    memset(p_buf, 0, p_buf_size);
  } else {
    p_buf_pos = 0;
    b_count = 0;
    memcpy(p_buf, m_config.hostname, hostlen);
    p_buf_pos += hostlen;
  }
};

int report_length(void) {
  return p_buf_pos;
}

int report_header_length(void) {
  return hostlen;
}

int report_free_bytes(void) {
  return p_buf_size - p_buf_pos;
}

void report_send(void) {
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
    p_buf = realloc(p_buf, p_buf_size+BEACON_REPORT_SIZE);
    memset(p_buf+p_buf_size, 0, BEACON_REPORT_SIZE);
    p_buf_size+=BEACON_REPORT_SIZE;
  }
  uint8_t *p, *q;
  q = p = p_buf+p_buf_pos;
  memcpy(p, b->uuid, 16);
  p+=16;
  *(p++) = (uint8_t)(b->major >> 8);
  *(p++) = (uint8_t)(b->major);
  *(p++) = (uint8_t)(b->minor >> 8);
  *(p++) = (uint8_t)(b->minor);
  *(p++) = (uint8_t)(b->count >> 8);
  *(p++) = (uint8_t)(b->count);
  /* log_stdout("\t\t%d: %d\n", b->minor, b->count); */
  memcpy(p, &b->rssi, sizeof(double));
  memcpy(p+=sizeof(double), &b->tx_power, 1);
  p_buf_pos += p-q;
  b->count = 0;
  return a;
}
