#ifndef __KALMAN_H
#define __KALMAN_H

#include <stdbool.h>

#include "hash.h"

//#define Q_SPECTRAL_DENSITY 0.1225 /* variance of process noise */
#define Q_SPECTRAL_DENSITY 0.01 /* variance of process noise */
#define MEASUREMENT_VARIANCE 9

#define HASH_TABLE_LEN_PRIME 251

typedef double p_noise_t[2][2];
typedef double covariance_t[2][2];
typedef double state_t[2];
typedef double kgain_t[2];

typedef struct kalman_parameters {
  state_t state;
  covariance_t P;
  bool init;
  double last_seen;
} kalman_t;

typedef struct generic_hashable_filterable {
  hashable_t *next, *prev;
  kalman_t kalman;
  double last_seen;
} hashable_filterable_t;

double kalman(void *, int8_t, double);
  
#endif
