/* ipc-privileged.c - Shawn Nock; Jaycon Systems, LLC
 *
 *   IPC callbacks and side-effect functions running as root in the
 *   parent process
 */

#include <string.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <json-c/json.h>

#include "ipc.h"
#include "ipc-privileged.h"
#include "log.h"

void ipc_parent_readcb(struct bufferevent *bev, void *ctx) {
    log_notice("Parent notified of bufferevent\n");
    struct evbuffer *in_buf = bufferevent_get_input(bev);
    while (evbuffer_get_length(in_buf) > 0) {
        ipc_cmd_t *c = ipc_cmd_fetch_alloc(bev);
        char *output = ipc_cmd_str(c);
        log_notice("Parent RX: %s", output);
        free(output);
        ipc_resp_t *r = ipc_resp_alloc();
        r->serial = c->serial;
        r->success = true;
        r->resp_l = 5;
        r->resp = calloc(1, strlen("{}") + 1);
        strcpy(r->resp, "{}");
        ipc_resp_send(bev, r);
        ipc_resp_free(r);
        ipc_cmd_free(c);
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
