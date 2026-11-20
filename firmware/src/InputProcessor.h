#pragma once

#include <stdint.h>

class OutputMouse;
class ConfigManager;

class InputProcessor {
public:
    InputProcessor(OutputMouse& output, const ConfigManager& configManager);

    void process(uint8_t buttons, int8_t x, int8_t y, int8_t wheel);

private:
    OutputMouse& output_;
    const ConfigManager& configManager_;
    uint16_t configRevision_ = 0;
    int16_t remainderX_ = 0;
    int16_t remainderY_ = 0;
};
