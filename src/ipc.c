#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>

#include "config.h"
#include "ipc.h"
#include "log.h"

static uint_fast16_t ipc_serial = 0;
struct bufferevent *ipc_bev = {0};

void ipc_child_readcb(struct bufferevent *bev, void *ctx) {
    UNUSED(ctx);
    UNUSED(bev);
    /* Will process resp from parent? */
    return;
}

uint_fast16_t ipc_get_serial(void) {
    return ipc_serial++;
}

void ipc_cmd_restart(void) {
    ipc_cmd_t cmd = {0};
    cmd.cmd = IPC_CMD_RESTART;
    bufferevent_write(ipc_bev, &cmd, sizeof(ipc_cmd_t));
    return;
}

ipc_resp_t *ipc_resp_alloc(void) {
    ipc_resp_t *r = calloc(1, sizeof(ipc_resp_t));
    return r;
}

ipc_resp_t *ipc_resp_fetch_alloc(struct bufferevent *bev) {
    /* Allocate and retreive response from IPC bev */
    ipc_resp_t *r = ipc_resp_alloc();
    bufferevent_read(bev, &r, sizeof(ipc_resp_t));
    if (r->resp_l > 0) {
        r->resp = calloc(1, r->resp_l + 1);
        bufferevent_read(bev, r->resp, r->resp_l);
    }
    return r;
}

void ipc_resp_free(ipc_resp_t *r) {
    if (r->resp) {
        free(r->resp);
    }
    free(r);
    return;
}

ipc_cmd_t *ipc_cmd_alloc(void) {
    ipc_cmd_t *c = calloc(1, sizeof(ipc_cmd_t));
    if (c) {
        c->serial = ipc_get_serial();
    }
    return c;
}

void ipc_process_resp(struct evbuffer *buffer,
                      const struct evbuffer_cb_info *info, void *arg) {
    UNUSED(info);
    struct evhttp_request *req = arg;
    struct evbuffer *buf = evhttp_request_get_output_buffer(req);
    ipc_resp_t *r;
    int n = evbuffer_remove(buffer, &r, sizeof(ipc_resp_t));
    char *outstr = ipc_resp_str(r);
    if (outstr) {
        log_notice("Read %d bytes of %d: %s\n", n, sizeof(ipc_resp_t), outstr);
        free(outstr);
    } else {
        log_notice("Problem in resp_str: %s", strerror(errno));
        exit(errno);
    }
    if (r->resp_l > 0) {
        r->resp = calloc(1, r->resp_l + 1);
        n = evbuffer_remove(buffer, r->resp, r->resp_l);
        log_notice("Read %d bytes of %d\n", n, r->resp_l);
    }
    evhttp_send_reply(req, 200, "Poopsock", buf);
}

ipc_cmd_t *ipc_cmd_fetch_alloc(struct bufferevent *bev) {
    ipc_cmd_t *c = ipc_cmd_alloc();
    int n = bufferevent_read(bev, &c, sizeof(ipc_cmd_t));
    char *outstr = ipc_cmd_str(c);
    if (outstr) {
        log_notice("Read %d bytes of %d: %s\n", n, sizeof(ipc_cmd_t), outstr);
        free(outstr);
    } else {
        log_notice("Problem in cmd_str: %s", strerror(errno));
        exit(errno);
    }
    if (c->key_l > 0) {
        c->key = calloc(1, c->key_l + 1);
        n = bufferevent_read(bev, c->key, c->key_l);
        log_notice("Read %d bytes of %d\n", n, c->key_l);
    }
    if (c->val_l > 0) {
        c->val = calloc(1, c->val_l + 1);
        bufferevent_read(bev, c->val, c->val_l);
        log_notice("Read %d bytes of %d\n", n, c->val_l);
    }
    return c;
}

void ipc_cmd_free(ipc_cmd_t *c) {
    if (c->key) {
        free(c->key);
    }
    if (c->val) {
        free(c->val);
    }
    free(c);
    return;
}

char *ipc_cmd_str(ipc_cmd_t *c) {
    char *output = calloc(1, 256);
    if (output) {
        sprintf(output, "%d:%d ", (int)c->serial, c->cmd);
        if (c->key) {
            strcat(output, c->key);
            strcat(output, "=");
            if (c->val) {
                strcat(output, c->val);
            } else {
                strcat(output, "NULL");
            }
        }
        strcat(output, "\n");
    }
    return output;
}

char *ipc_resp_str(ipc_resp_t *r) {
    char *output = calloc(1, 1024);
    if (r->resp) {
        sprintf(output, "%d:%d; %s", (int)r->serial, (int)r->success, r->resp);
    } else {
        sprintf(output, "%d:%d; ?", (int)r->serial, (int)r->success);
    }
    return output;
}

int ipc_cmd_send(struct bufferevent *bev, ipc_cmd_t *c) {
    /* Sends an IPC cmd as a single write to bufferevent, returns 0 on
       success, number of bytes sent on failure */
    struct evbuffer *buf = evbuffer_new();
    evbuffer_add(buf, c, sizeof(ipc_cmd_t));
    if (c->key_l > 0) {
        evbuffer_add(buf, c->key, c->key_l);
    }
    if (c->val_l > 0) {
        evbuffer_add(buf, c->val, c->val_l);
    }
    int n = bufferevent_write_buffer(bev, buf);
    char *output = ipc_cmd_str(c);
    log_notice("Sent: %s", output);
    free(output);
    evbuffer_free(buf);
    return n;
}

int ipc_resp_send(struct bufferevent *bev, ipc_resp_t *r) {
    /* Sends an IPC resp as a single write to bufferevent, returns 0 on
       success, number of bytes sent on failure */
    struct evbuffer *buf = evbuffer_new();
    evbuffer_add(buf, r, sizeof(ipc_resp_t));
    if (r->resp_l > 0) {
        evbuffer_add(buf, r->resp, r->resp_l);
    }
    int n = bufferevent_write_buffer(bev, buf);
    evbuffer_free(buf);
    return n;
}

ipc_resp_t *ipc_cmd_get(const char *key) {
    ipc_cmd_t *c = ipc_cmd_alloc();
    c->cmd = IPC_CMD_GET;
    c->key_l = strlen(key);
    c->key = calloc(1, c->key_l + 1);
    memcpy(c->key, key, c->key_l);
    ipc_cmd_send(ipc_bev, c);
    ipc_cmd_free(c);

    ipc_resp_t *r = ipc_resp_fetch_alloc(ipc_bev);
    return r;
}

ipc_resp_t *ipc_cmd_set(const char *key, const char *val) {
    ipc_cmd_t *c = ipc_cmd_alloc();

    c->cmd = IPC_CMD_SET;
    c->key_l = strlen(key);
    c->key = calloc(1, c->key_l);
    memcpy(c->key, key, c->key_l);

    c->val_l = strlen(key);
    c->val = calloc(1, c->val_l);
    memcpy(c->val, val, c->val_l);
    ipc_cmd_send(ipc_bev, c);
    ipc_cmd_free(c);

    ipc_resp_t *r = ipc_resp_fetch_alloc(ipc_bev);
    return r;
}
