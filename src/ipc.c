#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>

#include <sys/queue.h>

#include "config.h"
#include "http.h"
#include "ipc.h"
#include "log.h"

static uint32_t ipc_serial = 0;
struct bufferevent *ipc_bev = {0};

static ipc_resp_t ipc_resp_buf;

extern struct slisthead req_list_head;

uint32_t ipc_get_serial(void) {
    return ipc_serial++;
}

ipc_cmd_t *ipc_cmd_restart(void) {
    ipc_cmd_t *cmd = calloc(1, sizeof(ipc_cmd_t));
    cmd->cmd = IPC_CMD_RESTART;
    return cmd;
}

ipc_resp_t *ipc_resp_alloc(void) {
    ipc_resp_t *r = calloc(1, sizeof(ipc_resp_t));
    return r;
}

struct evbuffer *ipc_cmd_list_flatten(ipc_cmd_list_t *list) {
    struct evbuffer *buf = evbuffer_new();
    evbuffer_add(buf, list, sizeof(ipc_cmd_list_t));
    for (size_t i = 0; i < list->num; i++) {
        struct evbuffer *j = ipc_cmd_flatten(list->entries[i]);
	if (j) {
	    evbuffer_add_buffer(buf, j);
	    free(j);
	}
    }
    return buf;
}

ipc_cmd_list_t *ipc_cmd_list_recover(struct evbuffer *buf) {
    ipc_cmd_list_t *list = NULL;
    if (!(list = calloc(1, sizeof(ipc_cmd_list_t)))) {
	log_error("Failed to allocate memory");
	return NULL;
    }
    evbuffer_remove(buf, list, sizeof(ipc_cmd_list_t));
    if (!(list->entries = calloc(list->num, sizeof(ipc_cmd_t *)))) {
	log_error("Failed to allocate memory");
	ipc_cmd_list_free(list);
	return NULL;
    }
    for (size_t i = 0; i < list->num; i++) {
        list->entries[i] = ipc_cmd_recover(buf);
    }
    return list;
}

ipc_cmd_t *ipc_cmd_recover(struct evbuffer *buf) {
    ipc_cmd_t *cmd = calloc(1, sizeof(ipc_cmd_t));
    if (!cmd) {
	log_error("Failed to allocate memory");
	return NULL;
    }
    if (evbuffer_remove(buf, cmd, sizeof(ipc_cmd_t)) < 0) {
	log_error("Failed to read ipc_cmd_t");
    }
    if (cmd->key_l) {
        if (!(cmd->key = calloc(1, cmd->key_l))) {
	    log_error("Failed to allocate memory");
	    return NULL;
	}
        if (evbuffer_remove(buf, cmd->key, cmd->key_l) < 0) {
	    log_error("Failed to read ipc_cmd_t key");
	}
    }
    if (cmd->val_l) {
        if (!(cmd->val = calloc(1, cmd->val_l))) {
	    log_error("Failed to allocate memory");
	    return NULL;
	}
        if (evbuffer_remove(buf, cmd->val, cmd->val_l) < 0) {
	    log_error("Failed to read ipc_cmd_t value");
	}
    }
    return cmd;
}

size_t ipc_cmd_size(ipc_cmd_t *cmd) {
    return sizeof(ipc_cmd_t) + cmd->key_l + cmd->val_l;
}

struct evbuffer *ipc_cmd_flatten(ipc_cmd_t *cmd) {
    struct evbuffer *buf = evbuffer_new();
    if (!buf) {
	return NULL;
    }
    if (evbuffer_add(buf, cmd, sizeof(ipc_cmd_t)) < 0 ||
	evbuffer_add(buf, cmd->key, cmd->key_l) < 0 ||
	evbuffer_add(buf, cmd->val, cmd->val_l) < 0) {
	evbuffer_free(buf);
	return NULL;
    }
    return buf;
}

void ipc_cmd_list_free(ipc_cmd_list_t *list) {
    if (list) {
	if (list->entries) {
	    for (size_t i = 0; i < list->num; i++) {
		if (list->entries[i]) {
		    ipc_cmd_free(list->entries[i]);
		}
	    }
	    free(list->entries);
	}
	free(list);
    }
}

ipc_resp_t *ipc_resp_fetch_alloc(struct bufferevent *bev) {
    /* Allocate and retreive response from IPC bev */
    ipc_resp_t *r = ipc_resp_alloc();
    if (bufferevent_read(bev, r,
			 sizeof(ipc_resp_t)) < sizeof(ipc_resp_t)) {
	log_error("Failed to read complete ipc_resp_t");
    }
    if (r->resp_l > 0) {
        r->resp = calloc(1, r->resp_l);
        if (bufferevent_read(bev, r->resp, r->resp_l) < r->resp_l) {
	    log_error("Failed to read complete ipc response");
	}
    }
    return r;
}

char *ipc_resp_str(ipc_resp_t *r) {
    char *output = NULL;
    if (asprintf(&output, "%d, %s; %s\n", (int)r->serial,
		 r->status == IPC_ABORT ? "Aborted" :
		 r->status == IPC_ERROR ? "Error" : "Success",
                 r->resp_l ? r->resp : "NULL") < 0) {
        log_error("Failed to allocate memory");
	return NULL;
    }
    return output;
}

void ipc_child_readcb(struct bufferevent *bev, void *ctx) {
    UNUSED(ctx);
    UNUSED(bev);
    struct evbuffer *input = bufferevent_get_input(bev);
    while (evbuffer_get_length(input) >= sizeof(ipc_resp_t)) {
	/* We initially copy the dehydrated structure to a static
	   buffer incase were out of memory. If the response looks
	   good, we'll pull the rest of the data and rehydrate */
        ipc_resp_t *r = NULL;
	if(evbuffer_copyout(input, &ipc_resp_buf, sizeof(ipc_resp_t)) < 0) {
	    log_error("Failed to copyout from evbuffer");
	    return;
	}

        /* Find the pending web request */
        struct http_req *hreq = NULL;
	struct evhttp_request *req = NULL;
        int count = 0;
        TAILQ_FOREACH(hreq, http_get_request_list_head(), entries) {
            count++;
            if (hreq->serial == ipc_resp_buf.serial) {
                TAILQ_REMOVE(http_get_request_list_head(), hreq, entries);
		req = hreq->req;
		free(hreq);
		break;
            }
        }
        if (req == NULL) {
            log_error("Matching request not found for command serial %d\n",
                      ipc_resp_buf.serial);
	    evbuffer_drain(input, sizeof(ipc_resp_t)+ipc_resp_buf.resp_l);
	    return;
        } else {
	    if (ipc_resp_buf.status == IPC_ABORT) {
		evhttp_send_error(req, 429, "Try again later");
		evbuffer_drain(input, sizeof(ipc_resp_t));
		return;
	    }
	    if (!(r = ipc_resp_fetch_alloc(bev))) {
		log_error("Failed to allocate memory");
		evhttp_send_error(req, 429, "Try again later");
		ipc_resp_free(r);
	    } else {
		if (r->status == IPC_ERROR) {
		    evhttp_send_error(req, r->code, r->resp);
		} else if (r->status == IPC_SUCCESS) {
		    http_set_reset_req();
		    evhttp_send_reply(req, r->code, r->resp, NULL);
		} else {
		    log_error("Unknown IPC response status");
		}
		ipc_resp_free(r);
	    }
	}
    }
}

void ipc_resp_free(ipc_resp_t *r) {
    if (r) {
	if (r->resp) {
	    free(r->resp);
	}
	free(r);
    }
    return;
}

ipc_cmd_t *ipc_cmd_alloc(void) {
    ipc_cmd_t *c = calloc(1, sizeof(ipc_cmd_t));
    return c;
}

ipc_cmd_t *ipc_cmd_fetch_alloc(struct bufferevent *bev) {
    ipc_cmd_t *c = ipc_cmd_alloc();
    if (bufferevent_read(bev, c,
			 sizeof(ipc_cmd_t)) < sizeof(ipc_cmd_t)) {
	log_error("Failed to read complete ipc_cmd_t from bufferevent");
    }

    if (c->key_l > 0) {
        c->key = calloc(1, c->key_l + 1);
        if (bufferevent_read(bev, c->key, c->key_l) < c->key_l) {
	    log_error("Failed to read complete ipc_cmd key");
	}
    }
    if (c->val_l > 0) {
        c->val = calloc(1, c->val_l + 1);
        if (bufferevent_read(bev, c->val, c->val_l) < c->val_l) {
	    log_error("Failed to read complete ipc_cmd value");
	}
    }
    return c;
}

void ipc_cmd_free(ipc_cmd_t *c) {
    if (c) {
	if (c->key) {
	    free(c->key);
	}
	if (c->val) {
	    free(c->val);
	}
	free(c);
    }
    return;
}

char *ipc_cmd_str(ipc_cmd_t *c) {
    char *output;
    if (asprintf(&output, "%d %s=%s", c->cmd, c->key_l ? c->key : "NULL",
                 c->val_l ? c->val : "NULL") < 0) {
        log_error("Failed to allocate memory");
	return NULL;
    }
    return output;
}

int ipc_cmd_list_send(struct bufferevent *bev, ipc_cmd_list_t *list) {
    /* Sends an IPC cmd as a single write to bufferevent, returns 0 on
       success, number of bytes sent on failure */
    struct evbuffer *buf = ipc_cmd_list_flatten(list);
    int n = bufferevent_write_buffer(bev, buf);
    evbuffer_free(buf);
    return n;
}

int ipc_resp_send(struct bufferevent *bev, ipc_resp_t *r) {
    /* Sends an IPC resp as a single write to bufferevent, returns 0 on
       success, number of bytes sent on failure */
    struct evbuffer *buf = evbuffer_new();
    evbuffer_add(buf, r, sizeof(ipc_resp_t));
    if (r->resp_l) {
        evbuffer_add(buf, r->resp, r->resp_l);
    }
    int n = bufferevent_write_buffer(bev, buf);
    evbuffer_free(buf);
    return n;
}

ipc_cmd_t *ipc_cmd_set(const char *key, const char *val) {
    ipc_cmd_t *c = ipc_cmd_alloc();

    c->cmd = IPC_CMD_SET;
    c->key_l = strlen(key) + 1;
    c->key = calloc(1, c->key_l);
    memcpy(c->key, key, c->key_l);

    c->val_l = strlen(val) + 1;
    c->val = calloc(1, c->val_l);
    memcpy(c->val, val, c->val_l);
    return c;
}
