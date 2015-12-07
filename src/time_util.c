#include <time.h>

/** Time functions **/
static double nsec_to_sec(long nsec) {
  return (double) nsec / 1E9;
}

double timespec_to_seconds(const struct timespec tv) {
  return (double) tv.tv_sec + nsec_to_sec(tv.tv_nsec);
}

double time_now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return timespec_to_seconds(t);
}
