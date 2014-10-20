#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "gettext.h"
#define _(string) gettext (string)
#include <locale.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <json-c/json.h>

#include <curl/curl.h>

#include <libconfig.h>

#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/domain.h>
#include <avahi-common/llist.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#include <config.h>

static AvahiSimplePoll *simple_poll = NULL;
static AvahiClient *client = NULL;

static void host_name_resolver_callback(
    AvahiHostNameResolver *r,
    AVAHI_GCC_UNUSED AvahiIfIndex interface,
    AVAHI_GCC_UNUSED AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const AvahiAddress *a,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
    AVAHI_GCC_UNUSED void *userdata) {

    assert(r);

    switch (event) {
        case AVAHI_RESOLVER_FOUND: {
            char address[AVAHI_ADDRESS_STR_MAX];

            avahi_address_snprint(address, sizeof(address), a);

            printf("%s\t%s\n", name, address);

            break;
        }

        case AVAHI_RESOLVER_FAILURE:

	  fprintf(stderr, _("Failed to resolve host name: '%s': %s\n"), name, avahi_strerror(avahi_client_errno(client)));
            break;
    }


    avahi_host_name_resolver_free(r);
    avahi_simple_poll_quit(simple_poll);
}

static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
    switch (state) {
        case AVAHI_CLIENT_FAILURE:
	  fprintf(stderr, _("Client failure, exiting: %s\n"), avahi_strerror(avahi_client_errno(c)));
            avahi_simple_poll_quit(simple_poll);
            break;

        case AVAHI_CLIENT_S_REGISTERING:
        case AVAHI_CLIENT_S_RUNNING:
        case AVAHI_CLIENT_S_COLLISION:
        case AVAHI_CLIENT_CONNECTING:
            ;
    }
}

//#define DEBUG
#define HOSTNAME_MAX_LEN 20
#define CONFIG_FILE "/etc/c3listener.conf"

char hostname[HOSTNAME_MAX_LEN+1] = {0};
CURL *curl;
struct curl_slist *headers=NULL;

static volatile int signal_received = 0;

static void sigint_handler(int sig)
{
	signal_received = sig;
}

static int print_advertising_devices(int dd, uint8_t filter_type) {
  json_object *ble_adv;
  ble_adv = json_object_new_object();

  time_t timestamp;

  unsigned char buf[HCI_MAX_EVENT_SIZE], *ptr;
  struct hci_filter nf, of;
  struct sigaction sa;
  socklen_t olen;
  int len;
  
  olen = sizeof(of);
  if (getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen) < 0) {
    printf(_("Could not get socket options\n"));
    return -1;
  }
  
  hci_filter_clear(&nf);
  hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
  hci_filter_set_event(EVT_LE_META_EVENT, &nf);
  
  if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
    printf(_("Could not set socket options\n"));
    return -1;
  }
  
  memset(&sa, 0, sizeof(sa));
  sa.sa_flags = SA_NOCLDSTOP;
  sa.sa_handler = sigint_handler;
  sigaction(SIGINT, &sa, NULL);
  
  while (1) {
    evt_le_meta_event *meta;
    le_advertising_info *info;
    
    char addr[18];
    
    while ((len = read(dd, buf, sizeof(buf))) < 0) {
      if (errno == EINTR && signal_received == SIGINT) {
	len = 0;
	goto done;
      }
      
      if (errno == EAGAIN || errno == EINTR)
	continue;
      goto done;
    }
    timestamp = time(NULL);
    ptr = buf + (1 + HCI_EVENT_HDR_SIZE);
    len -= (1 + HCI_EVENT_HDR_SIZE);
    
    meta = (void *) ptr;
    
    if (meta->subevent != 0x02)
      goto done;
    
    int num_reports, offset = 0;
    num_reports = meta->data[0];
#ifdef DEBUG
    printf("_(Num reports:") " %d\n", num_reports);
#endif /* DEBUG */
    for (int i = 0; i < num_reports; i++) {
      info = (le_advertising_info *) (meta->data + offset + 1);
      
      uint8_t data[info->length+2];
      int rssi;
      data[info->length] = 0;
      memcpy(data, &info->data, info->length);
    
      ba2str(&info->bdaddr, addr);
      ble_adv = json_object_new_object();
      json_object_object_add(ble_adv, _("mac"), json_object_new_string(addr));
      json_object_object_add(ble_adv, _("listener"), json_object_new_string(hostname));
      json_object_object_add(ble_adv, _("timestamp"), json_object_new_int(timestamp));
      json_object_object_add(ble_adv, _("data"), json_object_new_string((const char *)data));
      offset += info->length + 11;
      rssi = *((int8_t*) (meta->data + offset - 1));
      json_object_object_add(ble_adv, _("rssi"), json_object_new_int(rssi));
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_object_to_json_string(ble_adv));
      curl_easy_perform(curl);
#ifdef DEBUG
      printf("%s\n", json_object_to_json_string(ble_adv));
#endif /* DEBUG */
    }
    
}

 done:
  setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));
  
  if (len < 0)
    return -1;
  
  return 0;
}

int main() {
  setlocale (LC_ALL, "");
  bindtextdomain(PACKAGE,
		  LOCALEDIR);
  textdomain(PACKAGE);
  config_t cfg;
  int dev_id = 0;
  int err, dd;
  uint8_t own_type = 0x00;
  uint8_t scan_type = 0x01;
  uint8_t filter_type = 0;
  uint8_t filter_policy = 0x00;
  uint16_t interval = htobs(0x0010);
  uint16_t window = htobs(0x0010);
  uint8_t filter_dup = 1;
  const char *post_url, *avahi_server;
  int use_avahi = 0, ret = 1, error;
  AvahiClient *client = NULL;
  AvahiServiceBrowser *sb = NULL;

  config_init(&cfg);
  if(! config_read_file(&cfg, CONFIG_FILE))
  {
    fprintf(stderr, _("Problem with config file: %s: %s:%d - %s\n"), CONFIG_FILE, config_error_file(&cfg), config_error_line(&cfg), config_error_text(&cfg));
    config_destroy(&cfg);
    return(1);
  }
  if(config_lookup_bool(&cfg, "use_avahi", &use_avahi)) {
    if(use_avahi) {
      if(config_lookup_string(&cfg, "avahi_name", &avahi_server)) {
	printf(_("Using Avahi/Zeroconf: trying to resolve: %s\n"), avahi_server);
	if (!(simple_poll = avahi_simple_poll_new())) {
	  fprintf(stderr, _("Failed to create simple poll object.\n"));
	  goto fail;
	}

	if (!(client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, NULL, &error))) {
	  fprintf(stderr, _("Failed to create client object: %s\n"), avahi_strerror(error));
	  goto fail;
	}
	if (!(avahi_host_name_resolver_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, avahi_server, AVAHI_PROTO_UNSPEC, 0, host_name_resolver_callback, NULL))) {
	  fprintf(stderr, _("Failed to create host name resolver: %s\n"), avahi_strerror(avahi_client_errno(client)));
	  goto fail;
	}
	avahi_simple_poll_loop(simple_poll);
      }
      else
	use_avahi = 0;
    }
  }
  if(!use_avahi) {
    if(config_lookup_string(&cfg, "post_url", &post_url))
      printf(_("Using static post from config file: %s\n\n"), post_url);
    else {
      fprintf(stderr, _("No 'post_url' setting in configuration file.\n"));
    config_destroy(&cfg);
    return(1);
    }
  }

  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if (!curl) {
    perror(_("Couldn't initialize libcurl handle"));
    exit(1);
  }
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, post_url);
  curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  gethostname(hostname, HOSTNAME_MAX_LEN);
      
  interval = htobs(0x0012);
  window = htobs(0x0012);
  
  if (dev_id < 0)
    dev_id = hci_get_route(NULL);
  
  dd = hci_open_dev(dev_id);
  if (dd < 0) {
    perror(_("Could not open bluetooth device"));
    exit(1);
  }
  
  err = hci_le_set_scan_parameters(dd, scan_type, interval, window,
				   own_type, filter_policy, 1000);
  if (err < 0) {
    perror(_("Set scan parameters failed"));
    exit(1);
  }
  
  err = hci_le_set_scan_enable(dd, 0x01, filter_dup, 1000);
  if (err < 0) {
    perror(_("Enable scan failed"));
    exit(1);
  }

#ifdef DEBUG
  printf(_("Scanning ...")"\n");
#endif /* DEBUG */

  err = print_advertising_devices(dd, filter_type);
  if (err < 0) {
    perror(_("Could not receive advertising events"));
    exit(1);
  }
  
  err = hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000);
  if (err < 0) {
    perror(_("Disable scan failed"));
    exit(1);
  }
 fail:
  if (sb)
    avahi_service_browser_free(sb);
  
  if (client)
    avahi_client_free(client);
  
  if (simple_poll)
    avahi_simple_poll_free(simple_poll);
  
  curl_slist_free_all(headers);
  curl_global_cleanup();
  hci_close_dev(dd);
  return ret;
}
