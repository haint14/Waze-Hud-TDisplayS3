#pragma once

#include <cstdint>

namespace waze_hud {

struct Rect {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
};

// LILYGO T-Display-S3: 320x170 landscape. The logical canvas equals the panel, so there is no
// scaling; fonts and bitmaps are drawn 1:1. Three dirty regions:
//   MainCluster  manoeuvre / lanes / clock (left) and speed sign (right)
//   Alerts       alert column, date and clock while navigating, speed margin while idle
//   Street       street name and ETA
namespace layout {
constexpr const char *DeviceName = "LILYGO T-Display-S3";
constexpr int UiYScaleNumerator = 1;
constexpr int UiYScaleDenominator = 1;
constexpr int Width = 320;
constexpr int Height = 170;
constexpr int PhysicalWidth = 320;
constexpr int PhysicalHeight = 170;
constexpr int MainHeight = 140;
constexpr int StreetHeight = Height - MainHeight;

constexpr Rect MainCluster{0, 0, 225, MainHeight};
constexpr Rect Alerts{225, 0, 95, MainHeight};
constexpr Rect Street{0, MainHeight, Width, StreetHeight};
constexpr Rect Full{0, 0, Width, Height};

constexpr Rect physicalRect(const Rect &logical) { return logical; }

// Largest dirty region; sizes the DMA buffer and the i80 maximum transfer.
constexpr int MaxRegionPixels = MainCluster.width * MainCluster.height;

static_assert(MainCluster.width + Alerts.width == PhysicalWidth,
              "HUD columns must cover the framebuffer without gaps");
static_assert(MainCluster.height + Street.height == PhysicalHeight,
              "HUD rows must cover the framebuffer without gaps");
}  // namespace layout

}  // namespace waze_hud
