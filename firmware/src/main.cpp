#include <Arduino.h>
#include <SPI.h>
#include <usbhub.h>
#include <hidboot.h>
#include <HID.h>

// Arduino Leonardo/AVR USB identity is compile-time only.
// To mimic a mouse VID/PID/name, edit platformio.ini:
//   board_build.vid
//   board_build.pid
//   board_build.usb_manufacturer
//   board_build.usb_product

static const uint8_t kMouseReportDescriptor[] PROGMEM = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x08,        //     Usage Maximum (Button 8)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x08,        //     Report Count (8)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,              //   End Collection
    0xC0               // End Collection
};

class OutputMouseDevice {
public:
    OutputMouseDevice() : descriptor(kMouseReportDescriptor, sizeof(kMouseReportDescriptor)) {
        HID().AppendDescriptor(&descriptor);
    }

    void begin() {
        send(0, 0, 0, 0);
    }

    void update(uint8_t buttons, int8_t x, int8_t y, int8_t wheel) {
        send(buttons, x, y, wheel);
    }

private:
    HIDSubDescriptor descriptor;

    struct __attribute__((packed)) MouseReport {
        uint8_t buttons;
        int8_t x;
        int8_t y;
        int8_t wheel;
    };

    void send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel) {
        MouseReport report = {buttons, x, y, wheel};
        HID().SendReport(1, &report, sizeof(report));
    }
};

OutputMouseDevice OutputMouse;

USB Usb;
USBHub Hub(&Usb);

// false = stable boot mouse mode. This is what most mice need on USB Host Shield.
// true  = ask the mouse for report protocol. Some gaming mice work, some do not.
constexpr bool kUseReportProtocol = false;
HIDBoot<USB_HID_PROTOCOL_MOUSE> HostMouse(&Usb, kUseReportProtocol);

// Boot mouse reports are normally:
//   byte 0: buttons
//   byte 1: X
//   byte 2: Y
//   byte 3: wheel, if present
//
// If your mouse sends an extra Report ID even in report protocol mode, set this to 1.
constexpr uint8_t kInputReportOffset = 0;

class MouseBridgeParser : public HIDReportParser {
public:
    void Parse(USBHID* hid, bool isReportId, uint8_t len, uint8_t* buf) override {
        (void)hid;
        (void)isReportId;

        if (buf == nullptr || len <= kInputReportOffset) {
            return;
        }

        const uint8_t* report = buf + kInputReportOffset;
        const uint8_t reportLen = len - kInputReportOffset;

        if (reportLen < 3) {
            return;
        }

        const uint8_t buttons = report[0];
        const int8_t x = static_cast<int8_t>(report[1]);
        const int8_t y = static_cast<int8_t>(report[2]);
        const int8_t wheel = (reportLen >= 4) ? static_cast<int8_t>(report[3]) : 0;

        OutputMouse.update(buttons, x, y, wheel);
    }
};

MouseBridgeParser MouseParser;

void setup() {
#ifndef CDC_DISABLED
    Serial.begin(115200);
#endif
    OutputMouse.begin();

    if (Usb.Init() == -1) {
        while (true) {
            delay(100);
        }
    }

    delay(200);
    HostMouse.SetReportParser(0, &MouseParser);
#ifndef CDC_DISABLED
    Serial.println(F("USB mouse bridge ready"));
#endif
}

void loop() {
    Usb.Task();
}
