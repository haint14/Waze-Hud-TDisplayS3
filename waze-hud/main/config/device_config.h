#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <cstdint>

namespace waze_hud {

enum class UiTheme : uint8_t { Auto, Day, Night };

// Show the vehicle speed next to the speed-limit sign, or only a large speed-limit sign.
enum class DisplayMode : uint8_t { CurrentSpeed, SpeedLimitOnly };

// The T-Display-S3 glass needs INVON, which is the controller state when this setting is off.
// The 180-degree rotation default is off (USB on the left).
struct DeviceSettings {
    bool fixedBrightness{false};
    uint8_t brightness{100};
    UiTheme theme{UiTheme::Auto};
    bool invertColor{false};
    DisplayMode displayMode{DisplayMode::CurrentSpeed};
    bool showStreet{true};
    bool mirrorHud{false};
    bool rotateDisplay{false};
    int8_t overspeedOffsetKmh{0};
    int8_t offsetX{0};
    int8_t offsetY{0};
    uint32_t revision{8};
};

using HlpSendLine = void (*)(const char *line, void *context);

class DeviceConfig {
public:
    static DeviceConfig &instance();

    esp_err_t init();
    DeviceSettings snapshot() const;
    esp_err_t toggleRotation();
    esp_err_t toggleMirror();
    bool handleMessage(const cJSON *root, HlpSendLine send, void *context);
    void publishSchema(HlpSendLine send, void *context);

private:
    DeviceConfig() = default;
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    DeviceSettings active_{};
};

}  // namespace waze_hud
