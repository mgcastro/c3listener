#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/domain.h>
#include <avahi-common/llist.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include "c3listener.h"

static AvahiSimplePoll *simple_poll = NULL;
static AvahiClient *client = NULL;

static void host_name_resolver_callback(AvahiHostNameResolver *r,
                                        AVAHI_GCC_UNUSED AvahiIfIndex interface,
                                        AVAHI_GCC_UNUSED AvahiProtocol protocol,
                                        AvahiResolverEvent event,
                                        const char *name, const AvahiAddress *a,
                                        AVAHI_GCC_UNUSED AvahiLookupResultFlags
                                            flags,
                                        AVAHI_GCC_UNUSED void *userdata) {

  assert(r);

  switch (event) {
  case AVAHI_RESOLVER_FOUND: {
    char address[AVAHI_ADDRESS_STR_MAX];

    avahi_address_snprint(address, sizeof(address), a);

    printf("%s\t%s\n", name, address);
    m_config.post_url = malloc((size_t)(strlen(m_config.post_url_template)+strlen(address)));
    sprintf(m_config.post_url, m_config.post_url_template, address);
    m_config.configured = true;
    break;
  }

  case AVAHI_RESOLVER_FAILURE:

    fprintf(stderr, _("Failed to resolve host name: '%s': %s\n"), name,
            avahi_strerror(avahi_client_errno(client)));

    break;
  }

  avahi_host_name_resolver_free(r);
  avahi_simple_poll_quit(simple_poll);
}

static void client_callback(AvahiClient *c, AvahiClientState state,
                            AVAHI_GCC_UNUSED void *userdata) {
  switch (state) {
  case AVAHI_CLIENT_FAILURE:
    fprintf(stderr, _("Client failure, exiting: %s\n"),
            avahi_strerror(avahi_client_errno(c)));
    avahi_simple_poll_quit(simple_poll);
    break;

  case AVAHI_CLIENT_S_REGISTERING:
  case AVAHI_CLIENT_S_RUNNING:
  case AVAHI_CLIENT_S_COLLISION:
  case AVAHI_CLIENT_CONNECTING:
    ;
  }
}

AvahiServiceBrowser *sb = NULL;
int configure_via_avahi(config_t *cfg) {
  AvahiClient *client = NULL;
  int error;

  if (config_lookup_string(cfg, "avahi_name", &m_config.avahi_server) &&
      config_lookup_string(cfg, "post_url_template", &m_config.post_url_template)) {
    /* If we have all the config info needed for avahi name
       resolution, then proceed */
    printf(_("Using Avahi/Zeroconf: trying to resolve: %s\n"),
	   m_config.avahi_server);
    if (!(simple_poll = avahi_simple_poll_new())) {
      fprintf(stderr, _("Failed to create simple poll object.\n"));
      errno = ENOMEM
      return -1;
    }

    /* avahi_client_new seems to intermittantly return a null client,
       keep trying until we get a valid one */
    client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0,
				       client_callback, NULL, &error);
    if (!client) {
      fprintf(stderr, _("Failed to create host name resolver: %s\n"),
	      avahi_strerror(avahi_client_errno(client)));
      errno = avahi_client_errno(client);
      return -1;
    }
    if (!(avahi_host_name_resolver_new(client, AVAHI_IF_UNSPEC,
				       AVAHI_PROTO_UNSPEC, m_config.avahi_server,
				       AVAHI_PROTO_UNSPEC, 0, host_name_resolver_callback,
				       NULL))) {
      fprintf(stderr, _("Failed to create host name resolver: %s\n"),
	      avahi_strerror(avahi_client_errno(client)));
      errno = avahi_client_errno(client);
      return -1;
    }
    /* Configuration happens (or not) during this poll loop via
       the host_name_resolver_callback function */
    avahi_simple_poll_loop(simple_poll);
  } else {
    fprintf(stderr, _("Avahi configuration failed, no \"post_url_template\" or \"avahi_name\" in config file.\n"));
    errno = EINVAL;
    return -1;
  }
}
