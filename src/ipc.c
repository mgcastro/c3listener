#include <event2/bufferevent.h>

#include "log.h"

void ipc_child_readcb(struct bufferevent *bev, void *ctx) {
  return;
}

void ipc_parent_readcb(struct bufferevent *bev, void *ctx) {
  size_t key_l, val_l;
  while (1) {
    int n = bufferevent_read(bev, &key_l, sizeof(size_t));
    if (n < 1) {
      break;
    }
    char key[key_l+1];
    key[key_l] = 0;
    bufferevent_read(bev, key, key_l);
    bufferevent_read(bev, &val_l, sizeof(size_t));
    char val[val_l+1];
    val[val_l] = 0;
    bufferevent_read(bev, val, val_l);
    log_notice("PARENT <<< %d:%s = %d:%s\n", key_l, key, val_l, val);
  }
}
