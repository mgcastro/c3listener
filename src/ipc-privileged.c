/* ipc-privileged.c - Shawn Nock; Jaycon Systems, LLC
 *
 *   IPC callbacks and side-effect functions running as root in the
 *   parent process
 */

#include <stdio.h>
#include <string.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <json-c/json.h>

#include "config.h"
#include "ipc-privileged.h"
#include "ipc.h"
#include "log.h"
#include "uci.h"

enum {
    /* Other CONFIG_ enums defined in uci.h and config.h near the
       modules that set them */
    CONFIG_NOT_FOUND = 1,
    IPC_UNKNOWN_CMD,
    IPC_REBOOT_FAILED,
};

const char *uci_settings[] = {"proto", "ipaddr", "netmask", "gateway",
                              "dns",   "ssid",   "key",     NULL};

static ipc_resp_t ipc_resp_buf;
static ipc_cmd_list_t ipc_cmd_list_buf;

static int ipc_priv_set(char *key, char *val) {
    int rv = CONFIG_NOT_FOUND;
    for (size_t j = 0; uci_settings[j] != NULL; j++) {
        if (!strncmp(key, uci_settings[j], strlen(uci_settings[j]))) {
            rv = uci_simple_set(key, val);
        }
    }
    if (rv == CONFIG_NOT_FOUND) {
        /* If it's not a recognised UCI setting, try to update a
           libconfig setting */
        rv = config_set(key, val);
    }
    return rv;
}

void ipc_parent_readcb(struct bufferevent *bev, void *ctx) {
    UNUSED(ctx);
    struct evbuffer *input = bufferevent_get_input(bev);
    while (evbuffer_get_length(input) >= sizeof(ipc_cmd_list_t)) {
        ipc_cmd_list_t *l = &ipc_cmd_list_buf;
        if (evbuffer_copyout(input, l, sizeof(ipc_cmd_list_t)) < 0) {
            log_error("Failed to read evbuffer");
            return;
        }
        ipc_resp_t *r = &ipc_resp_buf;
        memset(r, 0, sizeof(ipc_resp_buf));
        r->serial = l->serial;
        r->status = IPC_ERROR; /* This default saves typing below */

        if (!(l = ipc_cmd_list_recover(input))) {
            log_error("Failed to allocate memory");
            r->status = IPC_ABORT;
            ipc_resp_send(bev, r);
            return;
        }

        for (size_t i = 0; i < l->num; i++) {
            int cmd = l->entries[i]->cmd;
            char *key = l->entries[i]->key, *val = l->entries[i]->val;
            int rv;
            /* Dispatch the commands */
            switch (cmd) {
            case IPC_CMD_SET:
                rv = ipc_priv_set(key, val);
                break;
            case IPC_CMD_RESTART:
                config_reboot();
                /* Function should not return */
                rv = IPC_REBOOT_FAILED;
                break;
            default:
                rv = IPC_UNKNOWN_CMD;
                break;
            }
            /* Log errors and generate responses for child process /
               user */
            switch (rv) {
            case IPC_REBOOT_FAILED:
                r->code = 503;
                if (asprintf(&r->resp, "Failed to restart listener\n") < 0) {
                    r->status = IPC_ABORT;
                }
                break;
            case IPC_UNKNOWN_CMD:
                r->code = 503;
                if (asprintf(&r->resp, "Unknown IPC Command %d\n", cmd)) {
                    r->status = IPC_ABORT;
                }
                break;
            case CONFIG_OK:
                r->code = 200;
                r->status = IPC_SUCCESS;
                asprintf(&r->resp, "Ok");
                config_local_write();
                break;
            case CONFIG_NOT_FOUND:
            case CONFIG_UCI_NOT_FOUND:
            case CONFIG_CONF_NOT_FOUND:
            case CONFIG_UCI_NO_SECTION:
            case CONFIG_UCI_LOOKUP_FAIL:
                r->code = 400;
                if (asprintf(&r->resp, "Unable to set unknown parameter: %s.",
                             key) < 0) {
                    r->status = IPC_ABORT;
                    log_error("Unable to allocate memory");
                }
                break;
            case CONFIG_UCI_LOOKUP_INCOMPLETE:
                r->code = 503;
                if (asprintf(&r->resp, "Error looking up %s via uci.", key) <
                    0) {
                    log_error("Unable to allocate memory");
                    r->status = IPC_ABORT;
                }
                break;
            case CONFIG_UCI_SET_FAIL:
            case CONFIG_UCI_SAVE_FAIL:
                r->code = 503;
                if (sprintf(r->resp, "Error saving %s via uci.", key) < 0) {
                    log_error("Unable to allocate memory");
                    r->status = IPC_ABORT;
                }
                break;
            case CONFIG_CONF_TYPE_MISMATCH:
            case CONFIG_CONF_UNSUPPORTED_TYPE:
                r->code = 503;
                if (asprintf(&r->resp, "Type error in config for setting %s.",
                             key) < 0) {
                    log_error("Unable to allocate memory");
                    r->status = IPC_ABORT;
                }
                break;
            case CONFIG_CONF_EINVAL:
                r->code = 400;
                if (asprintf(
                        &r->resp,
                        "Cannot coerce %s into proper type for persistence.",
                        key) < 0) {
                    log_error("Unable to allocate memory");
                    r->status = IPC_ABORT;
                }
                break;
            default:
                r->code = 503;
                if (asprintf(&r->resp, "Unknown error saving %s.", key) < 0) {
                    log_error("Unable to allocate memory");
                    r->status = IPC_ABORT;
                }
                break;
            }
            r->resp_l = strlen(r->resp) + 1;
            if (r->status != IPC_SUCCESS) {
                /* Stop processing on error so the first error makes
                   it back */
                break;
            }
        }
        ipc_resp_send(bev, r);
        ipc_cmd_list_free(l);
        if (r->resp) {
            free(r->resp);
        }
    }
}

/* static void ipc_cmd_get_uci(struct evhttp_request *req, void *arg) { */
/*   json_object *jobj = json_object_new_object(); */

/*   json_object_object_add(jobj, "wired", */
/* 			 uci_section_jobj("network.lan2")); */
/*   json_object_object_add(jobj, "wireless", */
/* 			 uci_section_jobj("wireless.@wifi-iface[0]")); */

/*   struct evbuffer *buf = evhttp_request_get_output_buffer(req); */
/*   const char *json = json_object_to_json_string(jobj); */
/*   return  */
/* } */
