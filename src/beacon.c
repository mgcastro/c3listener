#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "beacon.h"
#include "ble.h"
#include "config.h"
#include "hash.h"
#include "log.h"
#include "time_util.h"

uint32_t beacon_index(void *a) {
    beacon_t *b = a;
    uint32_t index = 0;
    if (b->type == BEACON_IBEACON) {
        struct ibeacon_id *id = b->id;
        for (int i = 0; i < 16; i++) {
            index += id->uuid[i];
        }
        index += id->major + id->minor;
    } else if (b->type == BEACON_SECURE) {
        struct sbeacon_id *id = b->id;
        for (int i = 0; i < 6; i++) {
            index += id->mac[i];
        }
    } else {
        log_error("Unknown beacon type in hash: %d", b->type);
        assert(false);
    }
    return index;
}

bool beacon_eq(void *a, void *b) {
    beacon_t *aa = a, *bb = b;
    if (!(aa->type == bb->type)) {
        return false;
    }
    if (aa->type == BEACON_IBEACON) {
        struct ibeacon_id *id_a = aa->id, *id_b = bb->id;
        /* log_debug("%d == %d, %d == %d, UUID Result: %d\n", */
        /* 	     aa->major, bb->major, aa->minor, bb->minor,
         * !memcmp(aa->uuid,
         * bb->uuid, 16)); */
        return (id_a->major == id_b->major && id_a->minor == id_b->minor &&
                !memcmp(id_a->uuid, id_b->uuid, 16));
    } else if (aa->type == BEACON_SECURE) {
        struct sbeacon_id *id_a = aa->id, *id_b = bb->id;
        return !memcmp(id_a->mac, id_b->mac, 6);
    }
    log_error("Unknown beacon type in hash: %d", bb->type);
    assert(false);
    return false;
}

beacon_t *ibeacon_find_or_add(uint8_t const *const uuid, uint16_t major,
                              uint16_t minor) {
    beacon_t *b = malloc(sizeof(beacon_t)), *ret;
    memset(b, 0, sizeof(beacon_t));
    b->type = BEACON_IBEACON;
    struct ibeacon_id *id = malloc(sizeof(struct ibeacon_id));
    memcpy(id->uuid, uuid, 16);
    id->major = major;
    id->minor = minor;
    b->id = id;
    b->kalman.init = false;
    /* log_debug("%d == %d, %d == %d\n", major, b->major, minor, b->minor); */
    ret = hash_add(b, beacon_index, beacon_eq);
    if (ret != b) {
        /* If the beacon already exists, hash_add will return its existing
           address; we then need to free our temporary copy. If they are
           the same, our temporary copy has been linked into the
           hashtable */
        beacon_delete(b);
    } else {
        /* Finish initialization on node, if new */
        b->last_report = NAN;
        log_notice("Acquired ibeacon maj=%d min=%d", id->major, id->minor);
    }
    return ret;
}

beacon_t *sbeacon_find_or_add(uint8_t *mac) {
    beacon_t *b = malloc(sizeof(beacon_t)), *ret;
    memset(b, 0, sizeof(beacon_t));
    b->type = BEACON_SECURE;
    struct sbeacon_id *id = malloc(sizeof(struct sbeacon_id));
    memcpy(id->mac, mac, 6);
    b->id = id;
    b->kalman.init = false;
    /* log_debug("%d == %d, %d == %d\n", major, b->major, minor, b->minor); */
    ret = hash_add(b, beacon_index, beacon_eq);
    if (ret != b) {
        /* If the beacon already exists, hash_add will return its existing
           address; we then need to free our temporary copy. If they are
           the same, our temporary copy has been linked into the
           hashtable */
        beacon_delete(b);
    } else {
        /* Finish initialization on node, if new */
        b->last_report = NAN;
        char *mac = hexlify(id->mac, 6);
        log_notice("Acquired secure beacon id=%s", mac);
        free(mac);
    }
    return ret;
}

void beacon_delete(void *v) {
    beacon_t *b = v;
    free(b->id);
    free(b);
}

void *beacon_expire(void *a, void *c) {
    beacon_t *b = a;
    double now_ts;
    if (c != NULL) {
        now_ts = *(double *)c;
    } else {
        now_ts = time_now();
    }
    /* log_debug("%f - %f = %f\n", b->kalman.last_seen, now_ts,
     * b->kalman.last_seen - now_ts); */
    if (now_ts - b->kalman.last_seen > MAX_BEACON_INACTIVE_SEC) {
        log_debug("Beacon pruned\n");
        a = NULL; /* Alert the parent that we cannot dereference */
        hash_delete(b, beacon_index, beacon_eq, beacon_delete);
        return NULL;
    }
    return a;
}
