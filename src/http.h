#pragma once

#include <event2/http.h>
#include <sys/queue.h>

#define HTTP_MAX_POST_BYTES 128

/* Request list structure definition */
TAILQ_HEAD(slisthead, http_req);
struct http_req {
    uint32_t serial;
    struct evhttp_request *req;
    TAILQ_ENTRY(http_req) entries;
};

void http_index_cb(struct evhttp_request *, void *);
void http_main_cb(struct evhttp_request *, void *);
struct slisthead *http_get_request_list_head(void);
int http_req_list_length(void);
void http_req_list_dump(void);
void http_set_reset_req(void);
bool http_get_reset_req(void);
