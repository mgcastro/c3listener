#pragma once

#include <stdint.h>
#include <time.h>
#include <sys/time.h>

double timespec_to_seconds(const struct timespec);
double time_now(void);
uint_fast32_t tv2ms(struct timeval);
