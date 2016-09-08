#ifndef __BEACON_H
#define __BEACON_H

#include "kalman.h"
#include <stdint.h>

enum beacon_types { BEACON_IBEACON = 0, BEACON_SECURE };

struct ibeacon_id {
    uint8_t uuid[16];
    uint16_t major, minor;
    uint16_t count;
};

struct sbeacon_id {
    uint8_t mac[6];
};

typedef struct ibeacon {
    hashable_filterable_t *next, *prev;
    kalman_t kalman;
    uint8_t type;
    void *id;
    uint16_t count;
    double last_seen, last_report, distance, variance;
    int8_t tx_power;
    bool init;
} beacon_t;

uint32_t beacon_index(void *);
bool beacon_eq(void *, void *);
beacon_t *ibeacon_find_or_add(uint8_t *, uint16_t, uint16_t);
beacon_t *sbeacon_find_or_add(uint8_t *);
void *beacon_expire(void *, void *);
void beacon_delete(void *);

#endif /* __BEACON_H */
