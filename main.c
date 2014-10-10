#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <json-c/json.h>

#define HOSTNAME_MAX_LEN 20

char hostname[HOSTNAME_MAX_LEN+1] = {0};

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
    printf("Could not get socket options\n");
    return -1;
  }
  
  hci_filter_clear(&nf);
  hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
  hci_filter_set_event(EVT_LE_META_EVENT, &nf);
  
  if (setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf)) < 0) {
    printf("Could not set socket options\n");
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
    printf("Num reports: %d\n", num_reports);
    for (int i = 0; i < num_reports; i++) {
      info = (le_advertising_info *) (meta->data + offset + 1);
      
      uint8_t data[info->length+2];
      int rssi;
      data[info->length] = 0;
      memcpy(data, &info->data, info->length);
    
      ba2str(&info->bdaddr, addr);
      ble_adv = json_object_new_object();
      json_object_object_add(ble_adv, "mac", json_object_new_string(addr));
      json_object_object_add(ble_adv, "listener", json_object_new_string(hostname));
      json_object_object_add(ble_adv, "timestamp", json_object_new_int(timestamp));
      json_object_object_add(ble_adv, "data", json_object_new_string(data));
      json_object_object_add(ble_adv, "evt_type", json_object_new_int(info->evt_type));
      offset += info->length + 11;
      rssi = *((int8_t*) (meta->data + offset - 1));
      json_object_object_add(ble_adv, "rssi", json_object_new_int(rssi));
    }
    
    printf("%s\n", json_object_to_json_string(ble_adv));
}

 done:
  setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));
  
  if (len < 0)
    return -1;
  
  return 0;
}

int main() {
  int dev_id = 0;
  int err, opt, dd;
  uint8_t own_type = 0x00;
  uint8_t scan_type = 0x01;
  uint8_t filter_type = 0;
  uint8_t filter_policy = 0x00;
  uint16_t interval = htobs(0x0010);
  uint16_t window = htobs(0x0010);
  uint8_t filter_dup = 1;

  gethostname(hostname, HOSTNAME_MAX_LEN);
      
  interval = htobs(0x0012);
  window = htobs(0x0012);
  
  if (dev_id < 0)
    dev_id = hci_get_route(NULL);
  
  dd = hci_open_dev(dev_id);
  if (dd < 0) {
    perror("Could not open device");
    exit(1);
  }
  
  err = hci_le_set_scan_parameters(dd, scan_type, interval, window,
				   own_type, filter_policy, 1000);
  if (err < 0) {
    perror("Set scan parameters failed");
    exit(1);
  }
  
  err = hci_le_set_scan_enable(dd, 0x01, filter_dup, 1000);
  if (err < 0) {
    perror("Enable scan failed");
    exit(1);
  }
  
  printf("LE Scan ...\n");
  
  err = print_advertising_devices(dd, filter_type);
  if (err < 0) {
    perror("Could not receive advertising events");
    exit(1);
  }
  
  err = hci_le_set_scan_enable(dd, 0x00, filter_dup, 1000);
  if (err < 0) {
    perror("Disable scan failed");
    exit(1);
  }
  
  hci_close_dev(dd);
  return 0;
}
