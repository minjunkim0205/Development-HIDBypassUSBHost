#include "MouseBridge.h"

#include "InputProcessor.h"

namespace {
constexpr bool kUseReportProtocol = false;
constexpr uint8_t kInputReportOffset = 0;
}

MouseBridge::MouseBridge(InputProcessor& processor)
    : processor_(processor), usb_(), hub_(&usb_), hostMouse_(&usb_, kUseReportProtocol) {}

bool MouseBridge::begin() {
    if (usb_.Init() == -1) {
        return false;
    }

    delay(200);
    hostMouse_.SetReportParser(0, this);
    return true;
}

void MouseBridge::task() {
    usb_.Task();
}

void MouseBridge::Parse(USBHID*, bool, uint8_t length, uint8_t* buffer) {
    if (buffer == nullptr || length < kInputReportOffset + 3) {
        return;
    }

    const uint8_t* report = buffer + kInputReportOffset;
    const uint8_t reportLength = length - kInputReportOffset;
    const uint8_t buttons = report[0];
    const int8_t x = static_cast<int8_t>(report[1]);
    const int8_t y = static_cast<int8_t>(report[2]);
    const int8_t wheel = reportLength >= 4 ? static_cast<int8_t>(report[3]) : 0;

    processor_.process(buttons, x, y, wheel);
}
