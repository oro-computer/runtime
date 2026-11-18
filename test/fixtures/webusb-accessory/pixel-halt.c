// Prepare or clear a bulk IN halt on one Pixel running the AOA echo fixture.
#include <libusb.h>
#include <stdio.h>
#include <string.h>

static int accessory_in(libusb_device *device) {
  struct libusb_config_descriptor *config = NULL;
  int address = -1;
  if (libusb_get_active_config_descriptor(device, &config) < 0) return -1;
  for (int i = 0; i < config->bNumInterfaces; ++i) {
    const struct libusb_interface *interface = &config->interface[i];
    if (interface->num_altsetting != 1) continue;
    const struct libusb_interface_descriptor *alt = interface->altsetting;
    if (alt->bInterfaceNumber != 0 || alt->bAlternateSetting != 0 ||
        alt->bInterfaceClass != 255 || alt->bInterfaceSubClass != 255 ||
        alt->bInterfaceProtocol != 0 || alt->bNumEndpoints != 2) continue;
    int input = -1;
    int output = -1;
    for (int j = 0; j < alt->bNumEndpoints; ++j) {
      const struct libusb_endpoint_descriptor *ep = &alt->endpoint[j];
      if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) continue;
      if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) input = ep->bEndpointAddress;
      else output = ep->bEndpointAddress;
    }
    if (output >= 0) address = input;
  }
  libusb_free_config_descriptor(config);
  return address;
}

static int set_halt(libusb_device_handle *handle, int endpoint, int arm) {
  int rc = libusb_claim_interface(handle, 0);
  if (rc < 0) return rc;
  unsigned char status[2] = {0};
  if (arm) {
    rc = libusb_control_transfer(handle, LIBUSB_RECIPIENT_ENDPOINT, LIBUSB_REQUEST_SET_FEATURE,
        0, endpoint, NULL, 0, 1000);
  } else {
    rc = libusb_clear_halt(handle, endpoint);
  }
  if (rc == 0) {
    rc = libusb_control_transfer(handle, LIBUSB_ENDPOINT_IN | LIBUSB_RECIPIENT_ENDPOINT,
        LIBUSB_REQUEST_GET_STATUS, 0, endpoint, status, sizeof(status), 1000);
    if (rc >= 0) rc = rc == 2 && (status[0] & 1) == arm ? 0 : LIBUSB_ERROR_IO;
  }
  if (rc < 0 && arm) libusb_clear_halt(handle, endpoint);
  const int released = libusb_release_interface(handle, 0);
  if (rc == 0 && released < 0 && arm) libusb_clear_halt(handle, endpoint);
  return rc < 0 ? rc : released;
}

int main(int argc, char **argv) {
  if (argc != 3 || (strcmp(argv[1], "arm") && strcmp(argv[1], "clear")) || !argv[2][0]) {
    fprintf(stderr, "Usage: pixel-halt arm|clear PIXEL_USB_SERIAL\n");
    return 2;
  }
  libusb_context *context = NULL;
  if (libusb_init(&context) < 0) return 1;
  libusb_device **devices = NULL;
  const ssize_t count = libusb_get_device_list(context, &devices);
  int rc = count < 0 ? (int)count : LIBUSB_ERROR_NO_DEVICE;
  for (ssize_t i = 0; i < count; ++i) {
    struct libusb_device_descriptor d;
    if (libusb_get_device_descriptor(devices[i], &d) || d.idVendor != 0x18d1 ||
        (d.idProduct != 0x2d00 && d.idProduct != 0x2d01) || !d.iSerialNumber) continue;
    const int endpoint = accessory_in(devices[i]);
    if (endpoint < 0) continue;
    libusb_device_handle *handle = NULL;
    rc = libusb_open(devices[i], &handle);
    if (rc < 0) continue;
    unsigned char serial[256];
    const int length = libusb_get_string_descriptor_ascii(handle, d.iSerialNumber, serial, sizeof(serial));
    if (length > 0 && (size_t)length == strlen(argv[2]) && !memcmp(serial, argv[2], length)) {
      rc = set_halt(handle, endpoint, !strcmp(argv[1], "arm"));
      libusb_close(handle);
      break;
    }
    rc = LIBUSB_ERROR_NO_DEVICE;
    libusb_close(handle);
  }
  if (devices) libusb_free_device_list(devices, 1);
  libusb_exit(context);
  if (rc < 0) fprintf(stderr, "Pixel bulk IN: %s\n", libusb_error_name(rc));
  else printf("Pixel bulk IN halt %s and verified.\n", !strcmp(argv[1], "arm") ? "set" : "cleared");
  return rc < 0;
}
