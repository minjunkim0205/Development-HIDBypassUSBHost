#pragma once

#include <stdint.h>

struct MouseConfig {
    static constexpr uint8_t kButtonCount = 8;
    static constexpr uint16_t kMinSensitivityPercent = 25;
    static constexpr uint16_t kDefaultSensitivityPercent = 100;
    static constexpr uint16_t kMaxSensitivityPercent = 400;

    bool invertX = false;
    bool invertY = false;
    bool invertScroll = false;
    uint16_t sensitivityPercent = kDefaultSensitivityPercent;

    // Indexed by input button in HID order. A value of 0 disables the button.
    uint8_t buttonMapping[kButtonCount] = {1, 2, 3, 4, 5, 6, 7, 8};
};
