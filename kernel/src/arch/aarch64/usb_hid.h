/* USB HID decoding, independent of the USB transport. Created by Jonas Lee. */
#ifndef _OJJY_USB_HID_H
#define _OJJY_USB_HID_H
#include "types.h"

#define HID_MAX_FIELDS 48
typedef struct {
    uint16_t bit, page, usage;
    uint8_t size, report_id;
    bool relative;
    int32_t min, max;
} HidField;
typedef struct {
    HidField fields[HID_MAX_FIELDS];
    unsigned field_count;
    bool keyboard, has_ids;
    uint8_t previous_keys[8], buttons;
    uint64_t repeat_at;
} UsbHid;

bool usb_hid_parse_pointer(UsbHid *hid, const uint8_t *descriptor, size_t length);
void usb_hid_report(UsbHid *hid, const uint8_t *report, size_t length);
void usb_hid_tick(UsbHid *hid, uint64_t now);
void usb_hid_release(UsbHid *hid);
#endif
