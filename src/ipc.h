#pragma once

#include <stdbool.h>

#include <event2/bufferevent.h>

/* Protocol for child / parent communication:

   Child:
     Send ipc_cmd
     Send char[key_l]
     Send char[val_l]
     Read ipc_resp
     Read char[ipc_resp.error_l]

   Parent:
    Rx ipc_cmd
    Read char[key_l]
    Read char[val_l]
    Execute commands with args
    Send ipc_cmd

*/

typedef struct ipc_resp {
    uint_fast16_t serial;
    bool success;
    size_t resp_l;
    char *resp;
} ipc_resp_t;

typedef struct ipc_cmd {
    uint_fast16_t serial;
    uint8_t cmd;
    size_t key_l;
    size_t val_l;
    char *key;
    char *val;
} ipc_cmd_t;

enum ipc_commands {
    IPC_CMD_RESTART = 0, /* Reset router, no args */
    IPC_CMD_GET,
    IPC_CMD_SET,
};

void ipc_child_readcb(struct bufferevent *, void *);
ipc_resp_t *ipc_cmd_get(const char *);
ipc_cmd_t *ipc_cmd_fetch_alloc(struct bufferevent *);
void ipc_cmd_free(ipc_cmd_t *);
ipc_resp_t *ipc_cmd_set(const char *, const char *);
char *ipc_cmd_str(ipc_cmd_t *);
ipc_resp_t *ipc_resp_alloc(void);
void ipc_resp_free(ipc_resp_t *);
int ipc_resp_send(struct bufferevent *, ipc_resp_t *);
char *ipc_resp_str(ipc_resp_t *);
