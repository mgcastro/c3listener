#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <event2/buffer.h>
#include <event2/http.h>
#include <event2/util.h>

#ifdef HAVE_LIBMAGIC
#include <magic.h>
magic_t magic = NULL;
#endif /* HAVE_LIBMAGIC */

#include "log.h"

#ifndef HAVE_LIBMAGIC
struct mime_type {
  const char *ext;
  const char *mime;
} types[] = {
  {"html", "text/html"},
  {"css", "text/css"},
  {"js", "application/javascript"},
  {"jpg", "image/jpeg" },
  {"jpeg", "image/jpeg" },
  {"png", "image/png" },
  {NULL, NULL},
};

static const char *mime_guess(const char *fname) {
  struct mime_type *entry;
  char *last_dot = strrchr(fname, '.');
  if (last_dot != NULL) {
    char *extension = last_dot + 1;
    for(entry = types; entry->ext; entry++) {
      if (!evutil_ascii_strcasecmp(entry->ext, extension))
	return entry->mime;
    }
  }
  return "application/octet-stream";
}
#endif /* HAVE_LIBMAGIC */

static void http_post_cb(struct evhttp_request *req, void *arg) {
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
  log_notice("Got a GET request for <%s>\n",  uri);

  /* Check if file exists || send 404 */
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

  char *whole_path = NULL;
  char *decoded_path = evhttp_uridecode(path, 0, NULL);
  if (decoded_path == NULL)
    goto err;
  
  size_t len = strlen(decoded_path)+strlen(docroot)+2;
  if (!(whole_path = malloc(len))) {
    perror("malloc");
    goto err;
  }
  evutil_snprintf(whole_path, len, "%s/%s", docroot, decoded_path);

  struct stat st;
  if (stat(whole_path, &st)<0) {
    goto err;
  }
  if (S_ISDIR(st.st_mode)) {
    goto err;
  }

  /* Get Mime type / set header */
  const char *type;
#ifdef HAVE_LIBMAGIC
  if (!magic) {
    magic = magic_open(MAGIC_MIME);
    magic_load(magic, NULL);
  }
  type = magic_file(magic, whole_path);
#else
  type = mime_guess(decoded_path);
#endif /* HAVE_LIBMAGIC */
  evhttp_add_header(evhttp_request_get_output_headers(req),
		    "Content-Type", type);
  
  /* Send file */
  int fd = -1;
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
  if (fd>=0)
    close(fd);
 done:
  if (decoded)
    evhttp_uri_free(decoded);
  if (decoded_path)
    free(decoded_path);
  if (whole_path)
    free(whole_path);
}
