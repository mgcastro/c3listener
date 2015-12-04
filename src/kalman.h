#ifndef __KALMAN_H
#define __KALMAN_H

//#define Q_SPECTRAL_DENSITY 0.1225 /* variance of process noise */
#define Q_SPECTRAL_DENSITY 0.01 /* variance of process noise */
#define MEASUREMENT_VARIANCE 9

typedef double p_noise_t[2][2];
typedef double covariance_t[2][2];
typedef double state_t[2];
typedef double kgain_t[2];

typedef struct kalman_parameters {
  state_t state;
  covariance_t P;
} kalman_t;

typedef struct ibeacon {
  uint16_t major, minor;
  uint8_t uuid[16];
  kalman_t kalman;
  double last_seen;
  struct ibeacon* next;
  bool init;
} beacon_t;

double kalman(beacon_t*, int8_t, double);
double timespec_to_seconds(const struct timespec);
beacon_t* hash(uint8_t *, uint16_t, uint16_t);
  
#endif
