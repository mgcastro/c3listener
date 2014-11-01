#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <json-c/json.h>

#include <curl/curl.h>
extern CURL* curl;

#include "c3listener.h"
extern c3_config_t m_config;

#ifdef HAVE_GETTEXT
#include "gettext.h"
#define _(string) gettext(string)
#else
#define _(string) string
#endif /* HAVE_GETTEXT */

static void hexlify(uint8_t* dest, const uint8_t* src, size_t n) {
  memset(dest, 0, n*2+1); 
  uint8_t buf[3] = {0};
  for(int i = 0; i < n; i++) {
    sprintf((char *)&buf, "%.2x", src[i]);
    strncat((char *)dest, (char *)&buf, 2);
  }
  return;
}

int ble_scan_loop(int dd, uint8_t filter_type) {
  int ret = ERR_SUCCESS;
  time_t timestamp;
  /* Initialize Curl */
  if (ret = m_curl_init() < 0) {
    perror(_("Couldn't initialize libcurl"));
    return -1;
  }
  
  json_object *ble_adv;
  ble_adv = json_object_new_object();

  unsigned char buf[HCI_MAX_EVENT_SIZE], *ptr;
  struct hci_filter nf, of;
  socklen_t olen;
  int len;

  olen = sizeof(of);
  if (getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen) < 0) {
    perror(_("Could not get socket options"));
    return -1;
  }

  hci_filter_clear(&nf);
  hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
  hci_filter_set_event(EVT_LE_META_EVENT, &nf);

  if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
    perror(_("Could not set socket options"));
    return -1;
  }

  while (1) {
    evt_le_meta_event *meta;
    le_advertising_info *info;

    char addr[18];

    while ((len = read(dd, buf, sizeof(buf))) < 0) {
      if (errno == EINTR) {
	perror(_("HCI Socket read interrupted"));
        len = 0;
        goto done;
      }

      if (errno == EAGAIN)
        continue;
      perror(_("Unknown HCI socket error"));
      goto done;
    }
    timestamp = time(NULL);
    ptr = buf + (1 + HCI_EVENT_HDR_SIZE);
    len -= (1 + HCI_EVENT_HDR_SIZE);

    meta = (void *)ptr;

    if (meta->subevent != 0x02) {
      printf(_("Failed to set HCI Socket Filter"));
      goto done;
    }

    int num_reports, offset = 0;
    num_reports = meta->data[0];

    for (int i = 0; i < num_reports; i++) {
      info = (le_advertising_info *)(meta->data + offset + 1);

      uint8_t data[info->length + 2], hdata[info->length*2+1];
      
      int rssi;
      data[info->length+1] = 0;
      memcpy(data, &info->data, info->length);
      hexlify(hdata, data, info->length);

      ba2str(&info->bdaddr, addr);
      ble_adv = json_object_new_object();
      json_object_object_add(ble_adv, _("mac"), json_object_new_string(addr));
      json_object_object_add(ble_adv, _("listener"),
                             json_object_new_string(m_config.hostname));
      json_object_object_add(ble_adv, _("timestamp"),
                             json_object_new_int(timestamp));
      json_object_object_add(ble_adv, _("data"),
                             json_object_new_string((const char *)hdata));
      offset += info->length + 11;
      rssi = *((int8_t *)(meta->data + offset - 1));
      json_object_object_add(ble_adv, _("rssi"), json_object_new_int(rssi));
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS,
                       json_object_to_json_string(ble_adv));
      curl_easy_perform(curl);
      log_stdout("%s\n", json_object_to_json_string(ble_adv));
    }
  }

 done:
  setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));
  
  if (len < 0)
    perror("WTF");
    return -1;
  
  return 0;
}
