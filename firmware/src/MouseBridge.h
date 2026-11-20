#pragma once

#include <SPI.h>
#include <usbhub.h>
#include <hidboot.h>

class InputProcessor;

class MouseBridge : private HIDReportParser {
public:
    explicit MouseBridge(InputProcessor& processor);

    bool begin();
    void task();

private:
    void Parse(USBHID*, bool, uint8_t length, uint8_t* buffer) override;

    InputProcessor& processor_;
    USB usb_;
    USBHub hub_;
    HIDBoot<USB_HID_PROTOCOL_MOUSE> hostMouse_;
};
