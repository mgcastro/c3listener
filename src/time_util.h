#ifndef __TIME_UTIL_H
#define __TIME_UTIL_H

#include <time.h>

double timespec_to_seconds(const struct timespec);
double time_now(void);

#endif /* __TIME_UTIL_H */
