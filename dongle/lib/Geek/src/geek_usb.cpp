#include "geek_usb.h"

#if !ARDUINO_USB_CDC_ON_BOOT
USBCDC GeekSerial;
#endif

void geek_usb_identity(uint16_t pid, const char *product, const char *manufacturer, uint32_t baud)
{
    USB.PID(pid);
    USB.productName(product);
    USB.manufacturerName(manufacturer);
    Serial.begin(baud);
}
