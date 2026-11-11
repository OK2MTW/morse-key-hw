// SPDX-License-Identifier: MIT
// USB descriptors: interface 0 = HID boot keyboard (unchanged behaviour, H-30/H-33),
// interfaces 1+2 = USB MIDI (settings channel, docs/midi-settings-spec.md).
#include <string.h>
#include "tusb.h"

// VID:PID – open decision D-2 (pid.codes). Kept as in 0.3.0 for now; bcdDevice marks the new descriptor set
// so Windows does not reuse cached descriptors of the keyboard-only firmware.
#define USB_VID 0x0483
#define USB_PID 0x5740
#define USB_BCD_DEVICE 0x0400

enum { ITF_HID = 0, ITF_MIDI, ITF_MIDI_STREAMING, ITF_TOTAL };
enum { EP_HID_IN = 0x81, EP_MIDI_OUT = 0x02, EP_MIDI_IN = 0x82 };
enum { STR_LANG = 0, STR_MANUFACTURER, STR_PRODUCT, STR_HID, STR_MIDI };

static const tusb_desc_device_t desc_device = {
  .bLength = sizeof(tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,
  .bDeviceClass = 0x00,          // class per interface
  .bDeviceSubClass = 0x00,
  .bDeviceProtocol = 0x00,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor = USB_VID,
  .idProduct = USB_PID,
  .bcdDevice = USB_BCD_DEVICE,
  .iManufacturer = STR_MANUFACTURER,
  .iProduct = STR_PRODUCT,
  .iSerialNumber = 0,            // no serial number (H-30)
  .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&desc_device; }

static const uint8_t desc_hid_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return desc_hid_report;
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_MIDI_DESC_LEN)

static const uint8_t desc_configuration[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_TOTAL_LEN, 0x80, 100),
  // boot keyboard, 8-byte reports, polled every 1 ms
  TUD_HID_DESCRIPTOR(ITF_HID, STR_HID, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report), EP_HID_IN, 8, 1),
  TUD_MIDI_DESCRIPTOR(ITF_MIDI, STR_MIDI, EP_MIDI_OUT, EP_MIDI_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return desc_configuration;
}

static const char *const strings[] = {
  [STR_MANUFACTURER] = "morse-key-hw",
  [STR_PRODUCT] = "CW Key",
  [STR_HID] = "CW Key",
  [STR_MIDI] = "CW Key",
};

static uint16_t desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  uint8_t count;
  if (index == STR_LANG) {
    desc_str[1] = 0x0409;
    count = 1;
  } else {
    if (index >= sizeof strings / sizeof strings[0] || !strings[index]) return NULL;
    const char *s = strings[index];
    count = (uint8_t)strlen(s);
    if (count > 32) count = 32;
    for (uint8_t i = 0; i < count; i++) desc_str[1 + i] = (uint8_t)s[i];
  }
  desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
  return desc_str;
}

// HID class callbacks: no feature/output reports are used (keyboard LED reports are ignored).
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t bufsize) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}
