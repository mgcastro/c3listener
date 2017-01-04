#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

static uint8_t *hostname_bytes = NULL;
static char *hostname_ascii = NULL;

static bool is_hex(char c) {
    static char _map[] = "0123456789abcdef";
    for (uint_fast8_t i = 0; i < strlen(_map); i++) {
        if (_map[i] == c) {
            return true;
        }
    }
    return false;
}

static uint8_t u8_from_hex(char *hex) {
    /* Converts char[2] to a uint8_t */
    static uint8_t hex_map[] = {['0'] = 0,  1,  2,          3,  4,  5,  6,  7,
                                8,          9,  ['a'] = 10, 11, 12, 13, 14, 15,
                                ['A'] = 10, 11, 12,         13, 14, 15};
    return hex_map[(uint8_t)hex[0]] << 4 | hex_map[(uint8_t)hex[1]];
}

static uint8_t *hostname_to_bytes(char *hostname, int length) {
    /* If hostname looks like ma address, then try to encode in fewer
       bits to appease Steve */
    if (length != 12) {
        return NULL;
    }
    for (int i = 0; i < length; i++) {
        if (!is_hex(hostname[i])) {
            return NULL;
        }
    }
    uint8_t *buf = calloc(1, 7); // Six mac bytes plus a null
    for (int i = 0, j = 0; i < length; i += 2) {
        buf[j++] = u8_from_hex(&hostname[i]);
    }
    return buf;
}

void hostname_init(void) {
    /* Hostname length limited to 255 by protocol */
    char *tmp = calloc(1, 256);
    if (gethostname(tmp, 255) < 0) {
        log_error("Unable to retrieve hostname information");
        exit(1);
    }
    size_t hostlen = strlen(tmp);
    if (hostname_ascii != NULL) {
        free(hostname_ascii);
    }
    hostname_ascii = calloc(1, hostlen + 1);
    memcpy(hostname_ascii, tmp, hostlen);
    free(tmp);

    /* Try to convert a ascii-hex mac representation into a compact
       one; other wise use printable ascii from gethostname */
    uint8_t *mac = hostname_to_bytes(hostname_ascii, hostlen);
    if (mac) {
        if (hostname_bytes != NULL) {
            free(hostname_bytes);
        }
        hostname_bytes = mac;
    }
}

static inline void ensure_init(void) {
    if (!hostname_ascii) {
        hostname_init();
    }
}

const uint8_t *hostname_get_bytes(void) {
    /* Called by report_gen_header to get the compact representation
       of host_id where hostname == mac addr. If no compact
       representation exists, use plain ascii */
    ensure_init();
    if (hostname_bytes) {
        return hostname_bytes;
    }
    return (uint8_t *)hostname_ascii;
}

const char *hostname_get(void) {
    /* Always returns a printable hostname for web ui / logs */
    ensure_init();
    return hostname_ascii;
}

size_t hostname_get_bytes_length(void) {
    return strlen((const char *)hostname_get_bytes());
}
