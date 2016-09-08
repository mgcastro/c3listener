/* ipc-privileged.c - Shawn Nock; Jaycon Systems, LLC
 *
 *   IPC callbacks and side-effect functions running as root in the
 *   parent process
 */

#include <string.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <json-c/json.h>

#include "config.h"
#include "ipc-privileged.h"
#include "ipc.h"
#include "log.h"

void ipc_parent_readcb(struct bufferevent *bev, void *ctx) {
    UNUSED(ctx);
    log_notice("Parent notified of bufferevent\n");
    struct evbuffer *input = bufferevent_get_input(bev);
    while (evbuffer_get_length(input) >= sizeof(ipc_cmd_list_t)) {
        ipc_cmd_list_t *l = ipc_cmd_list_recover(input);
        for (size_t i = 0; i < l->num; l++) {
            char *output = ipc_cmd_str(l->entries[i]);
            log_notice("Parent RX: %s", output);
            free(output);
        }
        ipc_resp_t *r = ipc_resp_alloc();
        r->serial = l->serial;
        r->success = true;
        r->resp_l = strlen("{}");
        r->resp = calloc(1, r->resp_l);
        memcpy(r->resp, "{}", r->resp_l);
        ipc_resp_send(bev, r);
        ipc_resp_free(r);
        ipc_cmd_list_free(l);
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
