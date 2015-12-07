#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "beacon.h"
#include "c3listener.h"
#include "kalman.h"
#include "time_util.h"

/** Kalman Filter **/

double kalman(void *b, int8_t z, double ts) {
  hashable_filterable_t *v = b;
  kalman_t *f = &v->kalman;
  if (!f->init){
    /* log_stdout("Initialized filter\n"); */
    f->state[0] = z;
    /* Initial covariance calibrated via usb dongle and dev board */
    f->P[0][0] = 1.2;
    f->P[0][1] = 0.45;
    f->P[1][0] = 0.45;
    f->P[1][1] = 0.34;
    f->last_seen = ts;
    f->init = true;
    return (double) z;
  }
  double dt = ts - f->last_seen;
  f->last_seen = ts;
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


