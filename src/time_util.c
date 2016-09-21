#include <stdio.h>
#include <stdlib.h>

#include "time_util.h"

/** Time functions **/
static double nsec_to_sec(long nsec) {
    return (double)nsec / 1E9;
}

double timespec_to_seconds(const struct timespec tv) {
    return (double)tv.tv_sec + nsec_to_sec(tv.tv_nsec);
}

double time_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return timespec_to_seconds(t);
}

uint_fast32_t tv2ms(struct timeval time) {
    return (time.tv_sec * 1000 + time.tv_usec / 1000);
}

char *time_desc_delta(double time) {
    char *desc = calloc(1, 256);
    if (time < 1) {
        sprintf(desc, "%dms ago", (int)(time * 1000));
    } else if (time < 60) {
        sprintf(desc, "%ds ago", (int)time);
    } else if (time < 3600) {
        sprintf(desc, "%dm ago", (int)(time / 60));
    } else if (time < 3600 * 24) {
        sprintf(desc, "%dh ago", (int)(time / 3600));
    } else {
        sprintf(desc, "%d days ago", (int)(time / (3600 * 24)));
    }
    return desc;
}
