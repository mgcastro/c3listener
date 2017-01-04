#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <math.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/util.h>
#include <evhttp.h>

#include <json-c/json.h>

#include "beacon.h"
#include "ble.h"
#include "config.h"
#include "hostname.h"
#include "http.h"
#include "ipc.h"
#include "time_util.h"
#include "udp.h"

#ifdef HAVE_UCI
#include "uci.h"
#include <uci.h>
#endif

#ifdef HAVE_LIBMAGIC
#include <magic.h>
magic_t magic = NULL;
#endif /* HAVE_LIBMAGIC */

#include "config.h"
#include "log.h"

struct mime_type {
    const char *ext;
    const char *mime;
} types[] = {
    {"html", "text/html"},
    {"css", "text/css"},
    {"js", "application/javascript"},
    {"json", "application/json"},
    {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/png"},
    {NULL, NULL},
};

typedef void (*url_cb)(struct evhttp_request *, void *);

/* Exported from the report module */
extern char hostname[HOSTNAME_MAX_LEN + 1];

/* Exported from ipc.c; ipc points to the correct end of a
   bufferevent_pair in the parent and child */
extern struct bufferevent *ipc_bev;

/* List to keep track of pending requests to the root
   UID parent */
struct slisthead req_list_head = TAILQ_HEAD_INITIALIZER(req_list_head);

struct slisthead *http_get_request_list_head(void) {
    return &req_list_head;
}

/* int http_req_list_length(void) { */
/*   int c = 0; */
/*   struct http_req* np = NULL; */
/*   TAILQ_FOREACH(np, &req_list_head, entries) { */
/*     c++; */
/*   } */
/*   return c+1; */
/* } */

static bool http_reset_req_p = false;

bool http_get_reset_req(void) {
    return http_reset_req_p ? true : false;
}

void http_set_reset_req(void) {
    http_reset_req_p = true;
}

char const *http_valid_cmd(char *key)
/* Returns a pointer to a whitelisted command key (const char[]) or
   NULL if the command is unrecognized */
{
    static const char *valid_cmds[] = {
        "proto", "ipaddr",    "netmask", "gateway", "dns",
        "ssid",  "key",       "server",  "port",    "report_interval",
        "reset", "path_loss", "haab",    NULL};
    for (int x = 0; valid_cmds[x] != NULL; x++) {
        if (!strncmp(key, valid_cmds[x], strlen(valid_cmds[x]))) {
            return valid_cmds[x];
        }
    }
    return NULL;
}

int http_req_list_length(void) {
    struct http_req *a, *b;
    a = TAILQ_FIRST(&req_list_head);
    b = TAILQ_LAST(&req_list_head, slisthead);
    if (a && b) {
        return (int)b->serial - a->serial + 1;
    } else {
        return 0;
    }
}

void http_req_list_dump(void) {
    struct http_req *np;
    int count = 0;
    log_notice("Request List:\n");
    TAILQ_FOREACH(np, http_get_request_list_head(), entries) {
        count++;
        log_notice("\tSlot %d: serial=%lu, req=%p\n", count, np->serial,
                   np->req);
    }
}

static void server_json(struct evhttp_request *req, void *arg) {
    UNUSED(arg);
    config_refresh();
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "listener_id",
                           json_object_new_string(hostname_get()));
    json_object_object_add(jobj, "last_seen",
                           json_object_new_string("Not implemented"));
    json_object_object_add(
        jobj, "server", json_object_new_string(config_get_remote_hostname()));
    json_object_object_add(jobj, "port",
                           json_object_new_string(config_get_remote_port()));
    json_object_object_add(
        jobj, "report_interval",
        json_object_new_int(tv2ms(config_get_report_interval())));
    json_object_object_add(jobj, "path_loss",
                           json_object_new_double(config_get_path_loss()));
    json_object_object_add(jobj, "haab",
                           json_object_new_double(config_get_haab()));
    json_object_object_add(jobj, "reset_required",
                           json_object_new_boolean(http_get_reset_req()));
    char *last_ack = time_desc_delta(time_now() - udp_get_last_ack());
    json_object_object_add(jobj, "last_ack", json_object_new_string(last_ack));
    free(last_ack);

    struct evbuffer *buf = evhttp_request_get_output_buffer(req);
    const char *json = json_object_to_json_string(jobj);
    evbuffer_add(buf, json, strlen(json));
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type",
                      "application/json");
    evhttp_send_reply(req, 200, "OK", buf);
    json_object_put(jobj);
}

static void network_json(struct evhttp_request *req, void *arg) {
    UNUSED(arg);
    struct evbuffer *buf = evhttp_request_get_output_buffer(req);

#ifdef HAVE_UCI
    json_object *jobj = json_object_new_object();

    json_object_object_add(jobj, "wired", uci_section_jobj("network.lan2"));
    json_object_object_add(jobj, "wireless",
                           uci_section_jobj("wireless.@wifi-iface[0]"));
    const char *json = json_object_to_json_string(jobj);
    evbuffer_add(buf, json, strlen(json));
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type",
                      "application/json");
    evhttp_send_reply(req, 200, "OK", buf);
    json_object_put(jobj);
#else
    evbuffer_add_printf(buf, "{}");
    evhttp_send_reply(req, 200, "OK", buf);
#endif /* HAVE_UCI */
}

static void network_status_json(struct evhttp_request *req, void *arg) {
    UNUSED(arg);
    json_object *jobj = json_object_new_object();
    json_object *wired = json_object_new_object();
    json_object *wireless = json_object_new_object();
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    int n;
    for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
        json_object *json_if = NULL;
        if (!ifa->ifa_addr) {
            continue;
        }
        if (!strncmp(ifa->ifa_name, "eth", 3) ||
            !strncmp(ifa->ifa_name, "enp", 3)) {
            json_if = wired;
        } else if (!strncmp(ifa->ifa_name, "wlan", 4) ||
                   !strncmp(ifa->ifa_name, "wlp", 3)) {
            json_if = wireless;
        } else {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) {
            json_object_object_add(json_if, "interface",
                                   json_object_new_string(ifa->ifa_name));
            char *ipaddr =
                inet_ntoa(((struct sockaddr_in *)ifa->ifa_addr)->sin_addr);
            if (!strcmp(ipaddr, "192.168.7.1")) {
                continue;
            }
            json_object_object_add(json_if, "ipaddr",
                                   json_object_new_string(ipaddr));
            json_object_object_add(
                json_if, "netmask",
                json_object_new_string(inet_ntoa(
                    ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr)));
        }
    }
    json_object_object_add(jobj, "wired", wired);
    json_object_object_add(jobj, "wireless", wireless);
    struct evbuffer *buf = evhttp_request_get_output_buffer(req);
    const char *json = json_object_to_json_string(jobj);
    evbuffer_add(buf, json, strlen(json));
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type",
                      "application/json");
    evhttp_send_reply(req, 200, "OK", buf);
    freeifaddrs(ifaddr);
    json_object_put(jobj);
}

static void *beacon_hash_walker(void *ptr, void *jobj) {
    beacon_t *b = ptr;
    json_object *b_jobj = json_object_new_object();
    json_object_object_add(b_jobj, "type", json_object_new_int(b->type));
    if (b->type == BEACON_IBEACON) {
        struct ibeacon_id *id = b->id;
        json_object_object_add(b_jobj, "major", json_object_new_int(id->major));
        json_object_object_add(b_jobj, "minor", json_object_new_int(id->minor));
    } else if (b->type == BEACON_SECURE) {
        struct sbeacon_id *id = b->id;
        char *mac = hexlify(id->mac, 6);
        json_object_object_add(b_jobj, "mac", json_object_new_string(mac));
        free(mac);
    }
    json_object_object_add(b_jobj, "distance",
                           json_object_new_double(b->distance));
    json_object_object_add(b_jobj, "error",
                           json_object_new_double(sqrt(b->variance)));
    json_object_array_add(jobj, b_jobj);
    return ptr;
}

static void beacon_json(struct evhttp_request *req, void *arg) {
    UNUSED(arg);
    json_object *b_array = json_object_new_array();

    walker_cb func[1] = {beacon_hash_walker};
    void *args[1] = {b_array};

    hash_walk(func, args, 1);
    struct evbuffer *buf = evhttp_request_get_output_buffer(req);
    const char *json = json_object_to_json_string(b_array);
    evbuffer_add_printf(buf, "%s", json);
    json_object_put(b_array);
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type",
                      "application/json");
    evhttp_send_reply(req, 200, "OK", buf);
}

struct url_map_s {
    const char *path;
    url_cb handler;
} url_map[] = {
    {"/json/server.json", server_json},
    {"/json/network.json", network_json},
    {"/json/network_status.json", network_status_json},
    {"/json/beacons.json", beacon_json},
    {NULL, NULL},
};

static const char *mime_guess(const char *fname) {
    struct mime_type *entry;
    char *last_dot = strrchr(fname, '.');
    if (last_dot != NULL) {
        char *extension = last_dot + 1;
        for (entry = types; entry->ext; entry++) {
            if (!evutil_ascii_strcasecmp(entry->ext, extension)) {
                return entry->mime;
            }
        }
    }
#ifdef HAVE_LIBMAGIC
    if (!magic) {
        magic = magic_open(MAGIC_MIME);
        magic_load(magic, NULL);
    }
    return magic_file(magic, fname);
#else
    return "application/octet-stream";
#endif /* HAVE_LIBMAGIC */
}

static void http_post_cb(struct evhttp_request *req, void *arg) {
    UNUSED(arg);
    /* const char *uri = evhttp_request_get_uri(req); */

    /* struct evhttp_uri *decoded = NULL; */
    /* decoded = evhttp_uri_parse(uri); */
    /* if (!decoded) { */
    /*     evhttp_send_error(req, HTTP_BADREQUEST, 0); */
    /*     return; */
    /* } */
    /* const char *path; */
    /* path = evhttp_uri_get_path(decoded); */
    /* if (!path) { */
    /*     path = "/"; */
    /* } */

    /* char *decoded_path = evhttp_uridecode(path, 0, NULL); */
    /* if (decoded_path == NULL) { */
    /*     goto err; */
    /* } */

    /* Bail if we're backlogged = unprocessed IPC queue > 100 entries */
    if (http_req_list_length() > HTTP_MAX_PENDING_REQUESTS) {
        evhttp_send_error(req, 429, "Try again later");
        goto done;
    }

    size_t req_len = evbuffer_get_length(req->input_buffer);
    if (req_len > HTTP_MAX_POST_BYTES) {
        evhttp_send_error(req, 413, "Request too large");
        evbuffer_drain(req->input_buffer, req_len);
        goto done;
    }
    char *cbuf = calloc(1, req_len + 1);
    if (!cbuf) {
        log_error("Failed to allocate memory");
        evhttp_send_error(req, 429, "Try again later");
        goto done;
    }
    if (evbuffer_remove(req->input_buffer, cbuf, req_len) < 0) {
        log_error("Failed to read request");
        goto err;
    }

    struct evkeyvalq params;
    if (evhttp_parse_query_str(cbuf, &params) < 0) {
        evhttp_send_error(req, 400, "Invalid query string");
        free(cbuf);
        goto done;
    }
    free(cbuf);

    /* Build a command list from the request parameters */
    struct evkeyval *kv;
    ipc_cmd_list_t *cmd_list = NULL;
    if (!(cmd_list = calloc(1, sizeof(ipc_cmd_list_t)))) {
        log_error("Failed to allocate memory");
        evhttp_send_error(req, 429, "Try again later");
        goto done;
    }
    char const *cmd = NULL;
    TAILQ_FOREACH(kv, &params, next) {
        if (!(cmd = http_valid_cmd(kv->key))) {
            evhttp_send_error(req, 400, "Bad Parameter");
            ipc_cmd_list_free(cmd_list);
            evhttp_clear_headers(&params);
            goto done;
        }
        cmd_list->num++;
    }
    if (cmd_list->num == 0) {
        evhttp_send_error(req, 400, "No Parameters");
        ipc_cmd_list_free(cmd_list);
        evhttp_clear_headers(&params);
        goto done;
    }
    cmd_list->serial = ipc_get_serial();
    if (!(cmd_list->entries = calloc(cmd_list->num, sizeof(ipc_cmd_t *)))) {
        ipc_cmd_list_free(cmd_list);
        evhttp_clear_headers(&params);
        evhttp_send_error(req, 429, "Try again later");
        goto done;
    }
    size_t count = 0;
    TAILQ_FOREACH(kv, &params, next) {
        /* Every subsequent function uses the const char[] returned
           from the whitelist */
        cmd = http_valid_cmd(kv->key);
        if (!evutil_ascii_strcasecmp(cmd, "reset")) {
            cmd_list->entries[count] = ipc_cmd_restart();
        } else {
            if (!(cmd_list->entries[count] = ipc_cmd_set(cmd, kv->value))) {
                ipc_cmd_list_free(cmd_list);
                evhttp_clear_headers(&params);
                evhttp_send_error(req, 429, "Try again later");
                goto done;
            }
        }
        count++;
    }
    evhttp_clear_headers(&params);
    /* Store the request object until we hear back from parent via
       IPC */
    struct http_req *r = calloc(1, sizeof(struct http_req));
    r->serial = cmd_list->serial;
    r->req = req;
    TAILQ_INSERT_TAIL(&req_list_head, r, entries);
    ipc_cmd_list_send(ipc_bev, cmd_list);
    ipc_cmd_list_free(cmd_list);
    goto done;
err:
    evhttp_send_error(req, 500, "Internal server error");
done:
    return;
}

void http_main_cb(struct evhttp_request *req, void *arg) {
    const char *docroot = arg;
    struct evhttp_uri *decoded = NULL;
    char *decoded_path = NULL;
    char *whole_path = NULL;
    char const *path = NULL;

    switch (evhttp_request_get_command(req)) {
    case EVHTTP_REQ_POST:
        http_post_cb(req, arg);
        return;
    case EVHTTP_REQ_GET:
        break;
    default:
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }

    const char *uri = evhttp_request_get_uri(req);
    decoded = evhttp_uri_parse(uri);
    if (!decoded) {
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }

    if (!(path = evhttp_uri_get_path(decoded))) {
        path = "/";
    }

    /* Check for a custom handler for this path, call the handler on
       match */
    for (struct url_map_s *url_map_entry = url_map; url_map_entry->path;
         url_map_entry++) {
        if (!evutil_ascii_strcasecmp(url_map_entry->path, path)) {
            url_map_entry->handler(req, arg);
            goto done;
        }
    }
    int fd = -1;
    if (!(decoded_path = evhttp_uridecode(path, 0, NULL))) {
        goto err;
    }
    if (!strncmp(decoded_path, "/", 2)) {
        if (asprintf(&decoded_path, "/index.html") < 0) {
            log_error("Failed to allocate memory");
            evhttp_send_error(req, 429, "Try again later");
            goto done;
        }
    }

    if (asprintf(&whole_path, "%s%s", docroot, decoded_path) < 0) {
        log_error("Failed to allocate memory");
        evhttp_send_error(req, 429, "Try again later");
        goto done;
    }
    struct stat st;
    if (stat(whole_path, &st) < 0) {
        log_notice("Cannot find %s\n", decoded_path);
        goto err;
    } else {
        if (S_ISDIR(st.st_mode)) {
            log_notice("%s is a directory\n", docroot);
            goto err;
        }
    }

    /* Provide ETag and Not Modified */
    char etag[12] = {0};
    sprintf(etag, "%ld", st.st_mtime);
    const char *current_etag = evhttp_find_header(
        evhttp_request_get_input_headers(req), "If-None-Match");
    if (current_etag) {
        if (!evutil_ascii_strcasecmp(current_etag, etag)) {
            evhttp_send_reply(req, HTTP_NOTMODIFIED, "Not Modified",
                              evhttp_request_get_output_buffer(req));
            goto done;
        }
    }
    evhttp_add_header(evhttp_request_get_output_headers(req), "ETag", etag);

    /* Get Mime type / set header */
    const char *type;
    type = mime_guess(decoded_path);
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type",
                      type);

    /* Send file */
    if ((fd = open(whole_path, O_RDONLY)) < 0) {
        perror("open");
        goto err;
    }
    struct evbuffer *buf = evhttp_request_get_output_buffer(req);
    evbuffer_add_file(buf, fd, 0, st.st_size);
    evhttp_send_reply(req, 200, "OK", buf);
    goto done;

err:
    evhttp_send_error(req, 404, "Document was not found");
    if (fd >= 0) {
        close(fd);
    }
done:
    if (decoded) {
        evhttp_uri_free(decoded);
    }
    if (decoded_path) {
        free(decoded_path);
    }
    if (whole_path) {
        free(whole_path);
    }
}
