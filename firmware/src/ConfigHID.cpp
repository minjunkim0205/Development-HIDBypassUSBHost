#include "ConfigHID.h"

#include "ConfigManager.h"

#include <string.h>
#include <util/atomic.h>

namespace {
enum Command : uint8_t {
    ReadConfig = 1, WriteConfig, SaveConfig, RestoreDefaults,
    ListProfiles, SelectProfile, CreateProfile, DeleteProfile
};
enum Status : uint8_t { Success = 0, InvalidRequest, InvalidConfig, SaveFailed, UnknownCommand, InvalidProfile };

// All offsets are within the payload. The numbered HID report adds a leading ID.
constexpr uint8_t kVersion = 0;
constexpr uint8_t kCommand = 1;
constexpr uint8_t kTokenLow = 2;
constexpr uint8_t kTokenHigh = 3;
constexpr uint8_t kStatus = 4;
constexpr uint8_t kFlags = 5;
constexpr uint8_t kSensitivityLow = 6;
constexpr uint8_t kSensitivityHigh = 7;
constexpr uint8_t kButtons = 8;
// Profile commands repurpose the first three data bytes. Indexes start at zero.
constexpr uint8_t kProfileIndex = 5;
constexpr uint8_t kProfileMask = 6;
constexpr uint8_t kProfileCount = 7;
constexpr uint8_t kInvertX = 1U << 0;
constexpr uint8_t kInvertY = 1U << 1;
constexpr uint8_t kInvertScroll = 1U << 2;
constexpr uint8_t kKnownFlags = kInvertX | kInvertY | kInvertScroll;

Status profileStatus(ConfigManager::ProfileResult result) {
    if (result == ConfigManager::ProfileResult::Ok) {
        return Success;
    }
    return result == ConfigManager::ProfileResult::Invalid ? InvalidProfile : SaveFailed;
}
}

const uint8_t ConfigHID::kDescriptor[] PROGMEM = {
    0x06, 0x52, 0xFF,       // Vendor-defined usage page 0xFF52
    0x09, 0x01,             // Configuration collection
    0xA1, 0x01,             // Application collection
    0x85, kReportId,
    0x09, 0x02,             // Configuration data
    0x15, 0x00,             // Logical minimum 0
    0x26, 0xFF, 0x00,       // Logical maximum 255
    0x75, 0x08,             // Eight bits per field
    0x95, kPayloadSize,
    0xB1, 0x02,             // Feature (Data, Variable, Absolute)
    0xC0
};

ConfigHID::ConfigHID(ConfigManager& configManager)
    : PluggableUSBModule(1, 1, endpointType_), configManager_(configManager) {
    PluggableUSB().plug(this);
}

int ConfigHID::getInterface(uint8_t* interfaceCount) {
    ++*interfaceCount;
    const HIDDescriptor descriptor = {
        D_INTERFACE(pluggedInterface, 1, USB_DEVICE_CLASS_HUMAN_INTERFACE,
                    HID_SUBCLASS_NONE, HID_PROTOCOL_NONE),
        D_HIDREPORT(sizeof(kDescriptor)),
        D_ENDPOINT(USB_ENDPOINT_IN(pluggedEndpoint), USB_ENDPOINT_TYPE_INTERRUPT, USB_EP_SIZE, 10)
    };
    return USB_SendControl(0, &descriptor, sizeof(descriptor));
}

int ConfigHID::getDescriptor(USBSetup& request) {
    if (request.bmRequestType != REQUEST_DEVICETOHOST_STANDARD_INTERFACE ||
        request.wIndex != pluggedInterface || request.wValueL != 0) {
        return 0;
    }

    if (request.wValueH == HID_HID_DESCRIPTOR_TYPE) {
        const HIDDescDescriptor descriptor = D_HIDREPORT(sizeof(kDescriptor));
        return USB_SendControl(0, &descriptor, sizeof(descriptor));
    }
    if (request.wValueH == HID_REPORT_DESCRIPTOR_TYPE) {
        return USB_SendControl(TRANSFER_PGM, kDescriptor, sizeof(kDescriptor));
    }
    return 0;
}

bool ConfigHID::setup(USBSetup& request) {
    if (request.wIndex != pluggedInterface) {
        return false;
    }

    if (request.bmRequestType == REQUEST_HOSTTODEVICE_CLASS_INTERFACE &&
        request.bRequest == HID_SET_IDLE && request.wLength == 0) {
        return true;
    }

    uint8_t report[kPayloadSize + 1];
    if (request.wValueH != HID_REPORT_TYPE_FEATURE || request.wValueL != kReportId) {
        return false;
    }

    if (request.bmRequestType == REQUEST_HOSTTODEVICE_CLASS_INTERFACE &&
        request.bRequest == HID_SET_REPORT && request.wLength == sizeof(report)) {
        if (USB_RecvControl(report, sizeof(report)) != sizeof(report) ||
            report[0] != kReportId || pending_) {
            return false;
        }
        memcpy(command_, report + 1, sizeof(command_));
        pending_ = true;
        return true;
    }

    if (request.bmRequestType == REQUEST_DEVICETOHOST_CLASS_INTERFACE &&
        request.bRequest == HID_GET_REPORT && request.wLength >= sizeof(report)) {
        report[0] = kReportId;
        memcpy(report + 1, response_, sizeof(response_));
        return USB_SendControl(0, report, sizeof(report)) == sizeof(report);
    }

    return false;
}

void ConfigHID::task() {
    if (!pending_) {
        return;
    }

    Payload command;
    Payload response{};
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        memcpy(command, command_, sizeof(command));
    }

    // USB control requests execute in an ISR. Handle writes in the main loop.
    process(command, response);

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        memcpy(response_, response, sizeof(response_));
        pending_ = false;
    }
}

void ConfigHID::process(const Payload& command, Payload& response) {
    static_assert(kButtons + MouseConfig::kButtonCount == kPayloadSize, "Unexpected report layout");

    Status status = Success;
    if (command[kVersion] != kProtocolVersion || command[kStatus] != 0) {
        status = InvalidRequest;
    } else {
        switch (command[kCommand]) {
        case ReadConfig:
        case ListProfiles:
            break;
        case WriteConfig: {
            const uint8_t flags = command[kFlags];
            if ((flags & ~kKnownFlags) != 0) {
                status = InvalidConfig;
                break;
            }
            MouseConfig config;
            config.invertX = (flags & kInvertX) != 0;
            config.invertY = (flags & kInvertY) != 0;
            config.invertScroll = (flags & kInvertScroll) != 0;
            config.sensitivityPercent = static_cast<uint16_t>(command[kSensitivityLow]) |
                                        (static_cast<uint16_t>(command[kSensitivityHigh]) << 8);
            memcpy(config.buttonMapping, command + kButtons, sizeof(config.buttonMapping));
            if (!configManager_.set(config)) {
                status = InvalidConfig;
            }
            break;
        }
        case SaveConfig:
            if (!configManager_.save()) {
                status = SaveFailed;
            }
            break;
        case RestoreDefaults:
            configManager_.resetToDefaults();
            break;
        case SelectProfile:
            if (!configManager_.selectProfile(command[kProfileIndex])) {
                status = InvalidProfile;
            }
            break;
        case CreateProfile:
            status = profileStatus(configManager_.createProfile(command[kProfileIndex]));
            break;
        case DeleteProfile:
            status = profileStatus(configManager_.deleteProfile(command[kProfileIndex]));
            break;
        default:
            status = UnknownCommand;
            break;
        }
    }

    response[kVersion] = kProtocolVersion;
    response[kCommand] = command[kCommand];
    response[kTokenLow] = command[kTokenLow];
    response[kTokenHigh] = command[kTokenHigh];
    response[kStatus] = status;
    if (command[kCommand] >= ListProfiles && command[kCommand] <= DeleteProfile) {
        response[kProfileIndex] = configManager_.activeProfile();
        response[kProfileMask] = configManager_.profileMask();
        response[kProfileCount] = ConfigManager::kProfileCount;
        return;
    }

    const MouseConfig& config = configManager_.current();
    response[kFlags] = (config.invertX ? kInvertX : 0) |
                       (config.invertY ? kInvertY : 0) |
                       (config.invertScroll ? kInvertScroll : 0);
    response[kSensitivityLow] = static_cast<uint8_t>(config.sensitivityPercent);
    response[kSensitivityHigh] = static_cast<uint8_t>(config.sensitivityPercent >> 8);
    memcpy(response + kButtons, config.buttonMapping, sizeof(config.buttonMapping));
}
