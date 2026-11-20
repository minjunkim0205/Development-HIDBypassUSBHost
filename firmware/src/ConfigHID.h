#pragma once

#include <HID.h>

class ConfigManager;

class ConfigHID : private PluggableUSBModule {
public:
    explicit ConfigHID(ConfigManager& configManager);
    void task();

private:
    static constexpr uint8_t kReportId = 0x10;
    static constexpr uint8_t kPayloadSize = 16;
    static constexpr uint8_t kProtocolVersion = 1;
    static const uint8_t kDescriptor[];
    using Payload = uint8_t[kPayloadSize];

    int getInterface(uint8_t* interfaceCount) override;
    int getDescriptor(USBSetup& request) override;
    bool setup(USBSetup& request) override;
    void process(const Payload& command, Payload& response);

    uint8_t endpointType_[1] = {EP_TYPE_INTERRUPT_IN};
    ConfigManager& configManager_;
    volatile bool pending_ = false;
    Payload command_{};
    Payload response_{kProtocolVersion};
};
