#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kalman.h"
#include "c3listener.h"

/** Hash Table Implementation **/
#define HASH_TABLE_LEN_PRIME 251

static beacon_t* beacon_hash[HASH_TABLE_LEN_PRIME];

beacon_t* hash(uint8_t *uuid, uint16_t major, uint16_t minor) {
  uint32_t index = 0;
  beacon_t *b = NULL, *b_last_good = NULL;
  
  for (int i = 0; i < 16; i++) {
    index += uuid[i];
  }
  index += major + minor;
  //log_stdout("\tIndex: %d\n", index % HASH_TABLE_LEN_PRIME);
  if ((b = beacon_hash[index % HASH_TABLE_LEN_PRIME])) {
    /* Beacon or hash collision exists */
    do {
      b_last_good = b;
      if (b->major == major &&
	  b->minor == minor && !memcmp(b->uuid, uuid, 16)) {
	/* Beacon is a match */
	//log_stdout("\tBeacon found\n");
	return b;
      } else {
	log_stdout("\tCollision\n");
      }
    } while ((b = b->next));
  }
  /* The beacon isn't in the table, create it */
  log_stdout("\tBeacon Created\n");
  b = malloc(sizeof(beacon_t));
  memset(b, 0, sizeof(beacon_t));
  memcpy(b->uuid, uuid, 16);
  b->major = major;
  b->minor = minor;
  /* Hang the beacon off latest collision, or on the hash table
     directly */
  if (b_last_good) {
    b_last_good->next = b;
  } else {
    beacon_hash[index % HASH_TABLE_LEN_PRIME] = b;
  }
  return b;
}

/** Time functions **/
double nsec_to_sec(long nsec) {
  return (double) nsec / 1E9;
}

double timespec_to_seconds(const struct timespec tv) {
  return (double) tv.tv_sec + nsec_to_sec(tv.tv_nsec);
}

double deltaT(const beacon_t beacon, struct timespec tv) {
  return beacon.last_seen - timespec_to_seconds(tv);
}

/** Kalman Filter **/

double kalman(beacon_t* b, int8_t z, double ts) {
  kalman_t *f = &b->kalman;
  if (!b->init){
    log_stdout("Initialized filter\n");
    f->state[0] = z;
    /* Initial covariance calibrated via usb dongle and dev board */
    f->P[0][0] = 1.2;
    f->P[0][1] = 0.45;
    f->P[1][0] = 0.45;
    f->P[1][1] = 0.34;
    b->last_seen = ts;
    b->init = true;
    return (double) z;
  }
  double dt = ts - b->last_seen;
  b->last_seen = ts;
  //log_stdout("delta-T: %f; b->last_seen: %f; now: %f\n", dt, b->last_seen, ts);
  /* Process noise matrix Q is generated per packet to account for
     variable dt */
  p_noise_t Q;
  Q[0][0] = Q_SPECTRAL_DENSITY*dt*dt*dt/3;
  Q[0][1] = Q_SPECTRAL_DENSITY*dt*dt/2;
  Q[1][0] = Q_SPECTRAL_DENSITY*dt*dt/2;
  Q[1][1] = Q_SPECTRAL_DENSITY*dt;
  //log_stdout("Q11=%f, Q12=%f, Q21=%f, Q22=%f\n", Q[0][0], Q[0][1], Q[1][0], Q[1][1]);
  /* Predict */
  /** Project state **/
  state_t state_est;
  state_est[0] = f->state[0] + f->state[1]*dt;
  state_est[1] = f->state[1];
  /** Project covariance **/
  covariance_t P_est;
  P_est[0][0] = f->P[0][0] + (f->P[1][0] + f->P[0][1])*dt + f->P[1][1]*dt*dt + Q[0][0];
  P_est[0][1] = f->P[0][1] + f->P[1][1]*dt + Q[0][1];
  P_est[1][0] = f->P[1][0] + f->P[1][1]*dt + Q[1][0];
  P_est[1][1] = f->P[1][1] + Q[1][1];
  //log_stdout("P_est11=%f, P_est12=%f, P_est21=%f, P_est22=%f\n", P_est[0][0], P_est[0][1], P_est[1][0], P_est[1][1]);
  /* Update */
  /** Compute Kalman gain **/
  kgain_t K;
  K[0]=P_est[0][0]/(P_est[0][0] + MEASUREMENT_VARIANCE);
  K[1]=P_est[1][0]/(P_est[0][0] + MEASUREMENT_VARIANCE);
  /** Update state estimate **/
  f->state[0] = K[0]*(z - state_est[0]) + state_est[0];
  f->state[1] = K[1]*(z - state_est[0]) + state_est[1];
  /** Update covariance **/
  f->P[0][0] =  P_est[0][0]*(-K[0] + 1);
  f->P[0][1] =  P_est[0][1]*(-K[0] + 1);
  f->P[1][0] =  -P_est[0][0]*K[1] + P_est[1][0];
  f->P[1][1] =  -P_est[0][1]*K[1] + P_est[1][1];
  //log_stdout("P11=%f, P12=%f, P21=%f, P22=%f\n", f->P[0][0], f->P[0][1], f->P[1][0], f->P[1][1]);
  return f->state[0];
}

/** Abstraction **/

