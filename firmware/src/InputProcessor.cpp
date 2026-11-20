#include "InputProcessor.h"

#include "ConfigManager.h"
#include "OutputMouse.h"

namespace {
constexpr int16_t kPercentScale = 100;

int16_t applySensitivity(int16_t delta, uint16_t percent, int16_t& remainder) {
    if (percent == kPercentScale) {
        remainder = 0;
        return delta;
    }

    // Keep fractional counts so small movements contribute to later reports.
    const int32_t scaled = static_cast<int32_t>(delta) * percent + remainder;
    const int16_t whole = scaled / kPercentScale;
    remainder = scaled % kPercentScale;
    return whole;
}

uint8_t remapButtons(uint8_t buttons, const MouseConfig& config) {
    uint8_t mapped = 0;
    for (uint8_t index = 0; index < MouseConfig::kButtonCount; ++index) {
        const uint8_t target = config.buttonMapping[index];
        if ((buttons & (1U << index)) && target != 0) {
            mapped |= 1U << (target - 1);
        }
    }
    return mapped;
}
}

InputProcessor::InputProcessor(OutputMouse& output, const ConfigManager& configManager)
    : output_(output), configManager_(configManager) {}

void InputProcessor::process(uint8_t buttons, int8_t x, int8_t y, int8_t wheel) {
    const uint16_t revision = configManager_.revision();
    if (configRevision_ != revision) {
        configRevision_ = revision;
        remainderX_ = 0;
        remainderY_ = 0;
    }

    const MouseConfig& config = configManager_.current();
    const int16_t deltaX = config.invertX ? -static_cast<int16_t>(x) : x;
    const int16_t deltaY = config.invertY ? -static_cast<int16_t>(y) : y;
    const int16_t deltaWheel = config.invertScroll ? -static_cast<int16_t>(wheel) : wheel;

    output_.update(remapButtons(buttons, config),
                   applySensitivity(deltaX, config.sensitivityPercent, remainderX_),
                   applySensitivity(deltaY, config.sensitivityPercent, remainderY_),
                   deltaWheel);
}
