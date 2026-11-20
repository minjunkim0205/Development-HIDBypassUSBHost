#include <Arduino.h>

#include "ConfigHID.h"
#include "ConfigManager.h"
#include "InputProcessor.h"
#include "MouseBridge.h"
#include "OutputMouse.h"

namespace {
ConfigManager configManager;
OutputMouse outputMouse;
ConfigHID configHid(configManager);
InputProcessor inputProcessor(outputMouse, configManager);
MouseBridge mouseBridge(inputProcessor);
}

void setup() {
#ifndef CDC_DISABLED
    Serial.begin(115200);
#endif

    configManager.load();
    outputMouse.begin();

    if (!mouseBridge.begin()) {
        while (true) {
            configHid.task();
            delay(100);
        }
    }

#ifndef CDC_DISABLED
    Serial.println(F("USB mouse bridge ready"));
#endif
}

void loop() {
    configHid.task();
    mouseBridge.task();
    // Test
    //if (!configManager.current().invertX && millis() >= 5000UL) {
    //    MouseConfig config = configManager.current();
    //    config.invertX = true;
    //    if (configManager.set(config)) {
    //        configManager.save();
    //    }
    //}
}
