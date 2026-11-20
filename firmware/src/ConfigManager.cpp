#include "ConfigManager.h"

#include <EEPROM.h>
#include <stddef.h>
#include <string.h>
#include <util/crc16.h>

namespace {
constexpr uint8_t kLegacyVersion = 1;
constexpr uint8_t kProfileVersion = 2;
constexpr uint8_t kCommitted = 0xA5;
constexpr uint8_t kInvertX = 1U << 0;
constexpr uint8_t kInvertY = 1U << 1;
constexpr uint8_t kInvertScroll = 1U << 2;
constexpr uint8_t kKnownFlags = kInvertX | kInvertY | kInvertScroll;
constexpr uint8_t kProfileCount = ConfigManager::kProfileCount;
constexpr uint8_t kKnownProfiles = (1U << kProfileCount) - 1;

struct StoredConfig {
    uint8_t flags;
    uint8_t sensitivityLow;
    uint8_t sensitivityHigh;
    uint8_t buttons[MouseConfig::kButtonCount];
};

struct LegacyRecord {
    uint8_t version;
    uint8_t sequence;
    StoredConfig config;
    uint8_t crcLow;
    uint8_t crcHigh;
};

struct ProfileRecord {
    uint8_t version;
    uint8_t sequence;
    uint8_t mask;
    uint8_t active;
    StoredConfig configs[kProfileCount];
    uint8_t crcLow;
    uint8_t crcHigh;
};

static_assert(sizeof(LegacyRecord) == 15, "Unexpected legacy EEPROM layout");
static_assert(sizeof(ProfileRecord) == 50, "Unexpected profile EEPROM layout");

// Keep the original slots readable. Profile records live beyond both of them.
constexpr int kProfileAddress = 2 * (1 + sizeof(LegacyRecord));
constexpr int kProfileSlotSize = 1 + sizeof(ProfileRecord);
static_assert(kProfileAddress + 2 * kProfileSlotSize <= E2END + 1, "Profiles exceed EEPROM");

struct StoredProfiles {
    ProfileRecord record{};
    int address = -1;
    bool found = false;
};

constexpr uint8_t profileBit(uint8_t index) {
    return 1U << index;
}

uint16_t readWord(uint8_t low, uint8_t high) {
    return static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8);
}

template <class Record>
uint16_t checksum(const Record& record) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint16_t crc = 0xFFFF;
    for (size_t index = 0; index < offsetof(Record, crcLow); ++index) {
        crc = _crc16_update(crc, bytes[index]);
    }
    return crc;
}

bool isValid(const MouseConfig& config) {
    if (config.sensitivityPercent < MouseConfig::kMinSensitivityPercent ||
        config.sensitivityPercent > MouseConfig::kMaxSensitivityPercent) {
        return false;
    }
    for (uint8_t target : config.buttonMapping) {
        if (target > MouseConfig::kButtonCount) {
            return false;
        }
    }
    return true;
}

StoredConfig encode(const MouseConfig& config) {
    StoredConfig stored{};
    stored.flags = (config.invertX ? kInvertX : 0) |
                   (config.invertY ? kInvertY : 0) |
                   (config.invertScroll ? kInvertScroll : 0);
    stored.sensitivityLow = static_cast<uint8_t>(config.sensitivityPercent);
    stored.sensitivityHigh = static_cast<uint8_t>(config.sensitivityPercent >> 8);
    memcpy(stored.buttons, config.buttonMapping, sizeof(stored.buttons));
    return stored;
}

MouseConfig decode(const StoredConfig& stored) {
    MouseConfig config;
    config.invertX = (stored.flags & kInvertX) != 0;
    config.invertY = (stored.flags & kInvertY) != 0;
    config.invertScroll = (stored.flags & kInvertScroll) != 0;
    config.sensitivityPercent = readWord(stored.sensitivityLow, stored.sensitivityHigh);
    memcpy(config.buttonMapping, stored.buttons, sizeof(stored.buttons));
    return config;
}

bool isValid(const StoredConfig& stored) {
    return (stored.flags & ~kKnownFlags) == 0 && isValid(decode(stored));
}

bool isValid(const LegacyRecord& record) {
    return record.version == kLegacyVersion && isValid(record.config);
}

bool isValid(const ProfileRecord& record) {
    if (record.version != kProfileVersion || (record.mask & ~kKnownProfiles) != 0 ||
        record.active >= kProfileCount || (record.mask & profileBit(record.active)) == 0) {
        return false;
    }
    for (uint8_t index = 0; index < kProfileCount; ++index) {
        if ((record.mask & profileBit(index)) && !isValid(record.configs[index])) {
            return false;
        }
    }
    return true;
}

template <class Record>
bool readSlot(int address, Record& record) {
    if (EEPROM.read(address) != kCommitted) {
        return false;
    }
    EEPROM.get(address + 1, record);
    return checksum(record) == readWord(record.crcLow, record.crcHigh) && isValid(record);
}

template <class Record>
int latestSlot(int base, Record& record) {
    const int secondAddress = base + 1 + sizeof(Record);
    Record second{};
    const bool firstValid = readSlot(base, record);
    const bool secondValid = readSlot(secondAddress, second);

    // The two committed records have consecutive sequence numbers, including wrap to zero.
    if (secondValid && (!firstValid || static_cast<uint8_t>(second.sequence - record.sequence) == 1)) {
        record = second;
        return secondAddress;
    }
    return firstValid ? base : -1;
}

StoredProfiles readProfiles() {
    StoredProfiles stored;
    stored.address = latestSlot(kProfileAddress, stored.record);
    if (stored.address >= 0) {
        stored.found = true;
        return stored;
    }

    LegacyRecord legacy{};
    stored.found = latestSlot(0, legacy) >= 0;
    stored.record = ProfileRecord{};
    stored.record.version = kProfileVersion;
    stored.record.mask = profileBit(0);
    stored.record.configs[0] = stored.found ? legacy.config : encode(MouseConfig{});
    return stored;
}

bool commitProfiles(const StoredProfiles& previous, ProfileRecord record) {
    if (previous.address >= 0 && memcmp(&record, &previous.record, sizeof(record)) == 0) {
        return true;
    }
    record.sequence = previous.address < 0 ? 0 : static_cast<uint8_t>(record.sequence + 1);
    const uint16_t crc = checksum(record);
    record.crcLow = static_cast<uint8_t>(crc);
    record.crcHigh = static_cast<uint8_t>(crc >> 8);

    // Keep the previous record valid until the alternate slot is fully written.
    const int address = previous.address == kProfileAddress ?
                        kProfileAddress + kProfileSlotSize : kProfileAddress;
    EEPROM.update(address, 0);
    if (EEPROM.read(address) != 0) {
        return false;
    }
    EEPROM.put(address + 1, record);
    ProfileRecord written{};
    EEPROM.get(address + 1, written);
    if (memcmp(&record, &written, sizeof(record)) != 0) {
        return false;
    }
    EEPROM.update(address, kCommitted);
    return EEPROM.read(address) == kCommitted;
}
}

const MouseConfig& ConfigManager::current() const {
    return profiles_[activeProfile_];
}

uint16_t ConfigManager::revision() const {
    return revision_;
}

bool ConfigManager::set(const MouseConfig& config) {
    if (!isValid(config)) {
        return false;
    }
    profiles_[activeProfile_] = config;
    ++revision_;
    return true;
}

void ConfigManager::resetToDefaults() {
    set(MouseConfig{});
}

bool ConfigManager::load() {
    const StoredProfiles stored = readProfiles();
    profileMask_ = stored.record.mask;
    activeProfile_ = stored.record.active;
    for (uint8_t index = 0; index < kProfileCount; ++index) {
        profiles_[index] = (profileMask_ & profileBit(index)) ?
                           decode(stored.record.configs[index]) : MouseConfig{};
    }
    ++revision_;
    return stored.found;
}

bool ConfigManager::save() {
    const StoredProfiles stored = readProfiles();
    if (stored.record.mask != profileMask_) {
        return false;
    }
    ProfileRecord record = stored.record;
    record.active = activeProfile_;
    record.configs[activeProfile_] = encode(current());
    return commitProfiles(stored, record);
}

uint8_t ConfigManager::activeProfile() const {
    return activeProfile_;
}

uint8_t ConfigManager::profileMask() const {
    return profileMask_;
}

bool ConfigManager::selectProfile(uint8_t index) {
    if (index >= kProfileCount || (profileMask_ & profileBit(index)) == 0) {
        return false;
    }
    activeProfile_ = index;
    ++revision_;
    return true;
}

ConfigManager::ProfileResult ConfigManager::createProfile(uint8_t index) {
    if (index >= kProfileCount || (profileMask_ & profileBit(index)) != 0) {
        return ProfileResult::Invalid;
    }
    const StoredProfiles stored = readProfiles();
    if (stored.record.mask != profileMask_) {
        return ProfileResult::SaveFailed;
    }

    ProfileRecord record = stored.record;
    record.mask |= profileBit(index);
    record.active = index;
    record.configs[index] = encode(current());
    if (!commitProfiles(stored, record)) {
        return ProfileResult::SaveFailed;
    }

    profiles_[index] = current();
    profileMask_ = record.mask;
    selectProfile(index);
    return ProfileResult::Ok;
}

ConfigManager::ProfileResult ConfigManager::deleteProfile(uint8_t index) {
    if (index != activeProfile_) {
        return ProfileResult::Invalid;
    }
    const uint8_t remaining = profileMask_ & ~profileBit(index);
    if (remaining == 0) {
        return ProfileResult::Invalid;
    }
    const StoredProfiles stored = readProfiles();
    if (stored.record.mask != profileMask_) {
        return ProfileResult::SaveFailed;
    }

    uint8_t next = 0;
    while ((remaining & profileBit(next)) == 0) {
        ++next;
    }
    ProfileRecord record = stored.record;
    record.mask = remaining;
    record.active = next;
    record.configs[index] = StoredConfig{};
    if (!commitProfiles(stored, record)) {
        return ProfileResult::SaveFailed;
    }

    profiles_[index] = MouseConfig{};
    profileMask_ = remaining;
    selectProfile(next);
    return ProfileResult::Ok;
}
