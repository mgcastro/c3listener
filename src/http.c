#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/util.h>
#include <evhttp.h>

#include <json-c/json.h>

#include <uci.h>

#include "beacon.h"
#include "config.h"
#include "ipc.h"
#include "time_util.h"
#include "uci_json.h"

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

/* Exported from the config module */
extern char hostname[HOSTNAME_MAX_LEN + 1];

/* Exported from ipc.c; ipc points to the correct end of a
   bufferevent_pair in the parent and child */
extern struct bufferevent *ipc_bev;

static void server_json(struct evhttp_request *req, void *arg) {
    UNUSED(arg);
    json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "listener_id",
                           json_object_new_string(hostname));
    json_object_object_add(jobj, "last_seen",
                           json_object_new_string("Not implemented"));
    json_object_object_add(
        jobj, "host", json_object_new_string(config_get_remote_hostname()));
    json_object_object_add(jobj, "port",
                           json_object_new_string(config_get_remote_port()));
    json_object_object_add(
        jobj, "interval",
        json_object_new_int(tv2ms(config_get_report_interval())));
    json_object_object_add(jobj, "path_loss",
                           json_object_new_double(config_get_path_loss()));
    json_object_object_add(jobj, "haab",
                           json_object_new_double(config_get_haab()));

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
    json_object *jobj = json_object_new_object();

    json_object_object_add(jobj, "wired", uci_section_jobj("network.lan2"));
    json_object_object_add(jobj, "wireless",
                           uci_section_jobj("wireless.@wifi-iface[0]"));

    struct evbuffer *buf = evhttp_request_get_output_buffer(req);
    const char *json = json_object_to_json_string(jobj);
    evbuffer_add(buf, json, strlen(json));
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type",
                      "application/json");
    evhttp_send_reply(req, 200, "OK", buf);
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
        char mac[7] = {0};
        memcpy(mac, id->mac, 6);
        json_object_object_add(b_jobj, "mac", json_object_new_string(mac));
    }
    json_object_object_add(b_jobj, "distance",
                           json_object_new_double(b->distance));
    json_object_object_add(b_jobj, "error",
                           json_object_new_double(b->kalman.P[0][0]));
    json_object_array_add(jobj, b_jobj);
    return ptr;
}

static void beacon_json(struct evhttp_request *req, void *arg) {
    UNUSED(arg);
    log_notice("In beacon callback");
    json_object *b_array = json_object_new_array();

    walker_cb func[1] = {beacon_hash_walker};
    void *args[1] = {b_array};

    hash_walk(func, args, 1);
    log_notice("Finished json beacon walk.");
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
    {"/json/net_status.json", network_json},
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
    const char *uri = evhttp_request_get_uri(req);
    log_notice("Got a POST request for <%s>\n", uri);

    struct evhttp_uri *decoded = NULL;
    decoded = evhttp_uri_parse(uri);
    if (!decoded) {
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }

    const char *path;
    path = evhttp_uri_get_path(decoded);
    if (!path) {
        path = "/";
    }

    char *decoded_path = evhttp_uridecode(path, 0, NULL);
    if (decoded_path == NULL) {
        goto err;
    }

    /* Set header */
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type",
                      "application/json");

    struct evbuffer *buf = evhttp_request_get_output_buffer(req);
    // evbuffer_add_file(buf, fd, 0, st.st_size);

    char cbuf[128] = {0};
    size_t n = 0;
    while (evbuffer_get_length(req->input_buffer)) {
        n += evbuffer_remove(req->input_buffer, cbuf, sizeof(cbuf) - (n));
        if (n >= sizeof(cbuf)) {
            evhttp_send_error(req, HTTP_BADREQUEST, "Request too large");
            goto done;
        }
    }
    struct evkeyvalq params;
    evhttp_parse_query_str(cbuf, &params);

    /* Build a command list from the request parameters */
    struct evkeyval *kv;
    ipc_cmd_list_t *cmd_list = calloc(1, sizeof(cmd_list));
    TAILQ_FOREACH(kv, &params, next) {
        cmd_list->num++;
    }
    cmd_list->serial = ipc_get_serial();
    cmd_list->entries = calloc(cmd_list->num, sizeof(ipc_cmd_t *));
    size_t count = 0;
    TAILQ_FOREACH(kv, &params, next) {
        cmd_list->entries[count] = ipc_cmd_set(kv->key, kv->value);
        count++;
    }

    ipc_cmd_list_send(ipc_bev, cmd_list);
    ipc_cmd_list_free(cmd_list);

    evhttp_send_reply(req, 200, "OK", buf);
err:
    evhttp_send_error(req, 500, "Boo");
done:
    evhttp_clear_headers(&params);
    if (decoded) {
        evhttp_uri_free(decoded);
    }
    if (decoded_path) {
        free(decoded_path);
    }
}

void http_main_cb(struct evhttp_request *req, void *arg) {
    const char *docroot = arg;

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
    log_notice("Got a GET request for <%s>\n", uri);

    struct evhttp_uri *decoded = NULL;
    decoded = evhttp_uri_parse(uri);
    if (!decoded) {
        evhttp_send_error(req, HTTP_BADREQUEST, 0);
        return;
    }

    const char *path = evhttp_uri_get_path(decoded);
    if (!path) {
        path = "/";
    }

    /* Check for a custom handler for this path, call the handler on
       match */
    for (struct url_map_s *url_map_entry = url_map; url_map_entry->path;
         url_map_entry++) {
        if (!evutil_ascii_strcasecmp(url_map_entry->path, path)) {
            url_map_entry->handler(req, arg);
            return;
        }
    }
    int fd = -1;
    char *whole_path = NULL;
    char *decoded_path = evhttp_uridecode(path, 0, NULL);
    if (decoded_path == NULL) {
        goto err;
    }
    if (!strncmp(decoded_path, "/", 2)) {
        log_notice("Trying to rewrite root path\n");
        decoded_path = realloc(decoded_path, strlen("/index.html"));
        strcpy(decoded_path, "/index.html");
    }

    size_t len = strlen(decoded_path) + strlen(docroot) + 2;
    if (!(whole_path = malloc(len))) {
        perror("malloc");
        goto err;
    }
    evutil_snprintf(whole_path, len, "%s%s", docroot, decoded_path);
    log_notice("decoded: %s; whole: %s\n", decoded_path, whole_path);
    struct stat st;
    if (stat(whole_path, &st) < 0) {
        log_notice("Cannot find %s\n");
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
        if (!strcmp(current_etag, etag)) {
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
