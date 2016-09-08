#pragma once

#include <stdint.h>
#include <sys/time.h>
#include <time.h>

double timespec_to_seconds(const struct timespec);
double time_now(void);
uint_fast32_t tv2ms(struct timeval);
