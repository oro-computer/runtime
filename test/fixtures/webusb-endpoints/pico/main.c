// Full-speed vendor-specific endpoint fixture for a Raspberry Pi Pico (RP2040).
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"
#include "device/dcd.h"
#include "device/usbd_pvt.h"

enum { PACKET_SIZE = 64, QUEUE_DEPTH = 4, PAIRS = 3 };
#define ENDPOINT(address, type, interval) \
  7, TUSB_DESC_ENDPOINT, (address), (type), PACKET_SIZE, 0, (interval)

static const tusb_desc_device_t device = {
  .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200, .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor = 0xcafe, .idProduct = 0x4030, .bcdDevice = 0x0100,
  .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1
};
enum { CONFIG_BYTES = 9 + 9 + 9 + 6 * 7 };
static const uint8_t configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_BYTES, 0, 100),
  9, TUSB_DESC_INTERFACE, 0, 0, 0, 255, 0x53, 1, 0,
  9, TUSB_DESC_INTERFACE, 0, 1, 6, 255, 0x53, 1, 0,
  ENDPOINT(0x01, TUSB_XFER_BULK, 0), ENDPOINT(0x81, TUSB_XFER_BULK, 0),
  ENDPOINT(0x02, TUSB_XFER_INTERRUPT, 1), ENDPOINT(0x82, TUSB_XFER_INTERRUPT, 1),
  ENDPOINT(0x03, TUSB_XFER_ISOCHRONOUS, 1), ENDPOINT(0x83, TUSB_XFER_ISOCHRONOUS, 1)
};
_Static_assert(sizeof(configuration) == CONFIG_BYTES, "Configuration descriptor length");

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) { return index == 0 ? configuration : NULL; }

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language) {
  (void)language;
  static const char *const strings[] = {"", "Oro", "WebUSB endpoint fixture", "ORO-ENDPOINT-1"};
  static uint16_t text[32];
  if (index >= sizeof(strings) / sizeof(strings[0])) return NULL;
  size_t length = 1;
  if (index == 0) {
    text[1] = 0x0409;
  } else {
    length = strlen(strings[index]);
    for (size_t i = 0; i < length; ++i) text[i + 1] = (uint8_t)strings[index][i];
  }
  text[0] = (TUSB_DESC_STRING << 8) | (2 * (length + 1));
  return text;
}

typedef struct {
  uint8_t bytes[PACKET_SIZE];
  uint16_t size;
} packet_t;

typedef struct {
  packet_t queue[QUEUE_DEPTH];
  size_t head, count;
  bool receiving, sending;
} endpoint_pair_t;

static endpoint_pair_t pairs[PAIRS];
static uint8_t alternate;

// Each OUT occupies its own queue slot until the matching IN completes. This
// preserves packet boundaries when several isochronous OUT frames precede IN.
static bool schedule(uint8_t port, uint8_t number) {
  endpoint_pair_t *pair = &pairs[number - 1];
  if (pair->count && !pair->sending) {
    packet_t *first = &pair->queue[pair->head];
    if (!usbd_edpt_xfer(port, 0x80 | number, first->bytes, first->size)) return false;
    pair->sending = true;
  }
  if (pair->count < QUEUE_DEPTH && !pair->receiving) {
    size_t tail = (pair->head + pair->count) % QUEUE_DEPTH;
    if (!usbd_edpt_xfer(port, number, pair->queue[tail].bytes, PACKET_SIZE)) return false;
    pair->receiving = true;
  }
  return true;
}

static bool complete(uint8_t port, uint8_t address, xfer_result_t result, uint32_t count) {
  uint8_t number = tu_edpt_number(address);
  if (!alternate || number == 0 || number > PAIRS) return false;
  endpoint_pair_t *pair = &pairs[number - 1];
  if (address & 0x80) {
    if (!pair->sending) return false;
    pair->sending = false;
    if (result != XFER_RESULT_SUCCESS) return false;
    pair->head = (pair->head + 1) % QUEUE_DEPTH;
    --pair->count;
  } else {
    if (!pair->receiving) return false;
    pair->receiving = false;
    if (result != XFER_RESULT_SUCCESS || count > PACKET_SIZE) return false;
    size_t tail = (pair->head + pair->count) % QUEUE_DEPTH;
    pair->queue[tail].size = (uint16_t)count;
    ++pair->count;
  }
  return schedule(port, number);
}

static void reset(uint8_t port) {
  (void)port;
  alternate = 0;
  memset(pairs, 0, sizeof(pairs));
}

static void init(void) { reset(0); }

static uint16_t open_interface(uint8_t port, const tusb_desc_interface_t *iface, uint16_t available) {
  (void)port;
  if (available < CONFIG_BYTES - 9 || iface->bInterfaceNumber != 0 ||
      iface->bAlternateSetting != 0 || iface->bInterfaceClass != 255) return 0;
  return CONFIG_BYTES - 9;
}

static bool select_alternate(uint8_t port, uint8_t value) {
  // This fixture owns every non-control endpoint on the controller.
  dcd_edpt_close_all(port);
  reset(port);
  if (!value) return true;
  for (size_t pos = 27; pos < sizeof(configuration); pos += sizeof(tusb_desc_endpoint_t)) {
    const tusb_desc_endpoint_t *ep = (const tusb_desc_endpoint_t *)(configuration + pos);
    if (ep->bmAttributes.xfer == TUSB_XFER_ISOCHRONOUS) {
      if (!usbd_edpt_iso_alloc(port, ep->bEndpointAddress, PACKET_SIZE) ||
          !usbd_edpt_iso_activate(port, ep)) return false;
    } else {
      if (!usbd_edpt_open(port, ep)) return false;
      usbd_edpt_clear_stall(port, ep->bEndpointAddress);
    }
  }
  alternate = value;
  for (uint8_t number = 1; number <= PAIRS; ++number) {
    if (!schedule(port, number)) return false;
  }
  return true;
}

static bool control(uint8_t port, uint8_t stage, const tusb_control_request_t *request) {
  if (stage != CONTROL_STAGE_SETUP) return true;
  if (request->wIndex != 0) return false;
  if (request->bmRequestType == 0x81 && request->bRequest == TUSB_REQ_GET_INTERFACE) {
    return tud_control_xfer(port, request, &alternate, 1);
  }
  if (request->bmRequestType == 0x01 && request->bRequest == TUSB_REQ_SET_INTERFACE &&
      request->wValue <= 1 && request->wLength == 0) {
    return select_alternate(port, (uint8_t)request->wValue) && tud_control_status(port, request);
  }
  return false;
}

bool tud_vendor_control_xfer_cb(uint8_t port, uint8_t stage, const tusb_control_request_t *request) {
  if (stage != CONTROL_STAGE_SETUP) return true;
  if (!alternate || request->bmRequestType != 0x41 || request->bRequest != 0x40 ||
      request->wIndex || request->wValue || request->wLength || pairs[0].count) return false;
  usbd_edpt_stall(port, 0x81);
  return tud_control_status(port, request);
}

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count) {
  static const usbd_class_driver_t driver = {
    .name = "Oro endpoints", .init = init, .reset = reset, .open = open_interface,
    .control_xfer_cb = control, .xfer_cb = complete
  };
  *count = 1;
  return &driver;
}

int main(void) {
  tusb_init();
  for (;;) tud_task();
}
