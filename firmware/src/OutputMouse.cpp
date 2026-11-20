#include "OutputMouse.h"

namespace {
constexpr uint8_t kMouseReportId = 1;

static const uint8_t kMouseReportDescriptor[] PROGMEM = {

    // Generic Desktop / Mouse
    0x05, 0x01,        // Usage Page: Generic Desktop
    0x09, 0x02,        // Usage: Mouse
    0xA1, 0x01,        // Collection: Application

    // Report ID = 1
    0x85, 0x01,

    // Pointer
    0x09, 0x01,        // Usage: Pointer
    0xA1, 0x00,        // Collection: Physical

    // ---------------- Buttons ----------------

    0x05, 0x09,        // Usage Page: Button
    0x19, 0x01,        // Usage Minimum: Button 1
    0x29, 0x08,        // Usage Maximum: Button 8

    0x15, 0x00,        // Logical Minimum: 0
    0x25, 0x01,        // Logical Maximum: 1

    0x95, 0x08,        // Report Count: 8
    0x75, 0x01,        // Report Size: 1 bit

    0x81, 0x02,        // Input: Data, Variable, Absolute

    // ---------------- Movement ----------------

    0x05, 0x01,        // Usage Page: Generic Desktop

    0x09, 0x30,        // Usage: X
    0x09, 0x31,        // Usage: Y
    0x09, 0x38,        // Usage: Wheel

    0x15, 0x81,        // Logical Minimum: -127
    0x25, 0x7F,        // Logical Maximum: 127

    0x75, 0x08,        // Report Size: 8 bits
    0x95, 0x03,        // Report Count: 3

    0x81, 0x06,        // Input: Data, Variable, Relative

    // End Collections
    0xC0,              // End Physical Collection
    0xC0               // End Application Collection
};

struct MouseReport {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
};

static_assert(sizeof(MouseReport) == 4, "Mouse HID report must be four bytes");

int8_t clampReportDelta(int16_t delta) {
    if (delta < -127) {
        return -127;
    }
    if (delta > 127) {
        return 127;
    }
    return static_cast<int8_t>(delta);
}
}

OutputMouse::OutputMouse()
    : descriptor_(kMouseReportDescriptor, sizeof(kMouseReportDescriptor)) {
    HID().AppendDescriptor(&descriptor_);
}

void OutputMouse::begin() {
    update(0, 0, 0, 0);
}

void OutputMouse::update(uint8_t buttons, int16_t x, int16_t y, int16_t wheel) {
    // Split large deltas across reports while preserving the button state.
    do {
        const MouseReport report{
            buttons, clampReportDelta(x), clampReportDelta(y), clampReportDelta(wheel)
        };
        if (HID().SendReport(kMouseReportId, &report, sizeof(report)) < 0) {
            return;
        }
        x -= report.x;
        y -= report.y;
        wheel -= report.wheel;
    } while (x != 0 || y != 0 || wheel != 0);
}
