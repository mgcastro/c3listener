#pragma once

#define MSG_MAX_NAME 10
#define MSG_MAX_PAYLOAD 117

void ipc_parent_readcb(struct bufferevent *, void *);
void ipc_child_readcb(struct bufferevent *, void *);
