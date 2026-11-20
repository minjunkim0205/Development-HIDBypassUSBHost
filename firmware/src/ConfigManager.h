#pragma once

#include "MouseConfig.h"

class ConfigManager {
public:
    static constexpr uint8_t kProfileCount = 4;
    enum class ProfileResult : uint8_t { Ok, Invalid, SaveFailed };

    const MouseConfig& current() const;
    uint16_t revision() const;
    bool set(const MouseConfig& config);
    void resetToDefaults();

    // A missing or invalid EEPROM record restores the defaults in RAM.
    bool load();
    bool save();

    uint8_t activeProfile() const;
    uint8_t profileMask() const;
    bool selectProfile(uint8_t index);
    ProfileResult createProfile(uint8_t index);
    ProfileResult deleteProfile(uint8_t index);

private:
    MouseConfig profiles_[kProfileCount]{};
    uint8_t profileMask_ = 1;
    uint8_t activeProfile_ = 0;
    uint16_t revision_ = 0;
};
