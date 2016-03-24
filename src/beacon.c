#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "beacon.h"
#include "hash.h"
#include "log.h"

int beacon_index(void *a) {
    beacon_t *b = a;
    int index = 0;
    for (int i = 0; i < 16; i++) {
        index += b->uuid[i];
    }
    index += b->major + b->minor;
    return index;
}

bool beacon_eq(void *a, void *b) {
    beacon_t *aa = a, *bb = b;
    /* log_debug("%d == %d, %d == %d, UUID Result: %d\n", */
    /* 	     aa->major, bb->major, aa->minor, bb->minor, !memcmp(aa->uuid,
     * bb->uuid, 16)); */
    return (aa->major == bb->major && aa->minor == bb->minor &&
            !memcmp(aa->uuid, bb->uuid, 16));
}

beacon_t *beacon_find_or_add(uint8_t *uuid, uint16_t major, uint16_t minor) {
    beacon_t *b = malloc(sizeof(beacon_t)), *ret;
    memset(b, 0, sizeof(beacon_t));
    memcpy(b->uuid, uuid, 16);
    b->major = major;
    b->minor = minor;
    b->kalman.init = false;
    /* log_debug("%d == %d, %d == %d\n", major, b->major, minor, b->minor); */
    ret = hash_add(b, beacon_index, beacon_eq);
    if (ret != b) {
        /* If the beacon already exists, hash_add will return its existing
           address; we then need to free our temporary copy. If they are
           the same, our temporary copy has been linked into the
           hashtable */
        free(b);
    } else {
        /* Finish initialization on node, if new */
        b->last_report = NAN;
    }
    return ret;
}

void *beacon_expire(void *a, void *c) {
    beacon_t *b = a;
    double now_ts = *(double *)c;
    /* log_debug("%f - %f = %f\n", b->kalman.last_seen, now_ts,
     * b->kalman.last_seen - now_ts); */
    if (now_ts - b->kalman.last_seen > MAX_BEACON_INACTIVE_SEC) {
        log_debug("Beacon pruned\n");
        a = NULL; /* Alert the parent that we cannot dereference */
        hash_delete(b, beacon_index, beacon_eq);
        return NULL;
    }
    return a;
}
