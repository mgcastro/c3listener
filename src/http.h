#pragma once

#include <event2/http.h>

void http_index_cb(struct evhttp_request *, void *);
void http_main_cb(struct evhttp_request *, void *);
