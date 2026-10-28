// The GEEK as a composite USB device with the firmware's own identity.
//
// Build without ARDUINO_USB_CDC_ON_BOOT: with it the core starts USB before setup(), too
// early for a product id and name of our own. Instead Serial is this CDC port, and
// geek_usb_identity() sets the identity and starts it from setup(). After that, begin()
// every other interface (keyboard, mouse, the card as a drive...) and call USB.begin().
#pragma once
#include <USB.h>
#include <USBCDC.h>

#if !ARDUINO_USB_CDC_ON_BOOT
extern USBCDC GeekSerial;
#define Serial GeekSerial
#endif

// Product id under Espressif's vendor id 0x303a, product and manufacturer strings. Pick a
// pid of your own: macOS caches the name by vendor:product, and the default 0x1001 is on
// file as a DevKit from the factory firmware.
void geek_usb_identity(uint16_t pid, const char *product, const char *manufacturer, uint32_t baud = 115200);
