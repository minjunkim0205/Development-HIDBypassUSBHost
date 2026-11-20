#pragma once

#include <HID.h>

class OutputMouse {
public:
    OutputMouse();

    void begin();
    void update(uint8_t buttons, int16_t x, int16_t y, int16_t wheel);

private:
    HIDSubDescriptor descriptor_;
};
