#ifndef __BEACON_H
#define __BEACON_H

#include <stdint.h>
#include "kalman.h"

typedef struct ibeacon {
  hashable_filterable_t *next, *prev;
  kalman_t kalman;
  uint8_t uuid[16];
  uint16_t major, minor;
  uint16_t count;
  double last_seen, last_report, distance;
  int8_t tx_power;
  bool init;
} beacon_t;

int beacon_index(void *);
bool beacon_eq(void *, void *);
beacon_t *beacon_find_or_add(uint8_t *, uint16_t, uint16_t);
void *beacon_expire(void *, void *);

#endif /* __BEACON_H */
