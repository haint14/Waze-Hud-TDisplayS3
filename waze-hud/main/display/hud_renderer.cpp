#include "display/hud_renderer.h"

#include "display/colors.h"
#include "display/display_driver.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace waze_hud {
namespace {
constexpr char kTag[] = "DISPLAY";

constexpr int screenY(int value) {
    return value * layout::UiYScaleNumerator / layout::UiYScaleDenominator;
}

uint16_t foreground(const DeviceSettings &settings) {
    return settings.theme == UiTheme::Night ? colors::rgb565(220, 105, 80) : colors::Foreground;
}

template <typename Left, typename Right>
bool sameText(const Left &left, const Right &right) { return std::strcmp(left.data(), right.data()) == 0; }

bool maneuverChanged(const HudState &a, const HudState &b) {
    if (a.maneuver != b.maneuver || a.secondManeuver != b.secondManeuver ||
        a.maneuverDistanceM != b.maneuverDistanceM || a.roundaboutExit != b.roundaboutExit ||
        a.laneCount != b.laneCount || !sameText(a.nextStreet, b.nextStreet)) return true;
    for (uint8_t index = 0; index < a.laneCount; ++index)
        if (!(a.lanes[index] == b.lanes[index])) return true;
    return false;
}


bool isRoundaboutManeuver(Maneuver maneuver) {
    return maneuver == Maneuver::Roundabout || maneuver == Maneuver::RoundaboutLeft ||
           maneuver == Maneuver::RoundaboutRight ||
           maneuver == Maneuver::RoundaboutStraight ||
           maneuver == Maneuver::RoundaboutUTurn;
}

bool alertsChanged(const HudState &a, const HudState &b) {
    if (!(a.nearestAlert == b.nearestAlert) || a.upcomingAlertCount != b.upcomingAlertCount ||
        a.noPassingZone != b.noPassingZone || a.noPassingRemainingM != b.noPassingRemainingM) return true;
    for (uint8_t i = 0; i < a.upcomingAlertCount; ++i)
        if (!(a.upcomingAlerts[i] == b.upcomingAlerts[i])) return true;
    return false;
}

bool hasSettingsChanged(const DeviceSettings &a, const DeviceSettings &b) {
    return a.fixedBrightness != b.fixedBrightness || a.brightness != b.brightness ||
           a.theme != b.theme || a.invertColor != b.invertColor ||
           a.displayMode != b.displayMode || a.showStreet != b.showStreet ||
           a.mirrorHud != b.mirrorHud || a.rotateDisplay != b.rotateDisplay ||
           a.overspeedOffsetKmh != b.overspeedOffsetKmh ||
           a.offsetX != b.offsetX || a.offsetY != b.offsetY || a.revision != b.revision;
}


bool firmwareOverspeed(const HudState &state, const DeviceSettings &settings) {
    if (state.speedLimitKmh <= 0) return false;
    const int threshold = std::max(0, state.speedLimitKmh +
                                     static_cast<int>(settings.overspeedOffsetKmh));
    return state.speedKmh > threshold;
}

uint16_t alertDistanceColor(int distanceM, uint16_t normalColor) {
    if (distanceM < 0) return normalColor;
    if (distanceM < 100) return colors::Red;
    if (distanceM <= 300) return colors::Amber;
    if (distanceM <= 600) return colors::White;
    return colors::Green;
}

uint16_t bleSignalColor(const SystemStatusSnapshot &status) {
    if (!status.bleConnected) return colors::Muted;
    if (status.bleRssiDbm >= -60) return colors::Green;
    if (status.bleRssiDbm >= -75) return colors::Blue;
    if (status.bleRssiDbm >= -85) return colors::Amber;
    return colors::Red;
}

// ASCII text drawn one glyph at a time with 1 px tighter advance, used for the
// date/clock row where the column is only 95 px wide.
constexpr int kTightTracking = 1;

int tightTextWidth(const Canvas &canvas, const char *text, const assets::BitmapFont &font) {
    int width = 0;
    for (const char *c = text; *c; ++c) {
        const char one[2] = {*c, 0};
        width += canvas.fontTextWidth(one, font) - kTightTracking;
    }
    return width;
}

void drawTightText(Canvas &canvas, int x, int y, const char *text,
                   const assets::BitmapFont &font, uint16_t color) {
    for (const char *c = text; *c; ++c) {
        const char one[2] = {*c, 0};
        canvas.fontText(x, y, one, font, color, -1, false);
        x += canvas.fontTextWidth(one, font) - kTightTracking;
    }
}

// Days since 1970-01-01 -> civil date (Howard Hinnant's algorithm).
void civilFromDays(int64_t days, int &year, int &month, int &day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t dayOfEra = days - era * 146097;
    const int64_t yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t monthIndex = (5 * dayOfYear + 2) / 153;
    day = static_cast<int>(dayOfYear - (153 * monthIndex + 2) / 5 + 1);
    month = static_cast<int>(monthIndex < 10 ? monthIndex + 3 : monthIndex - 9);
    year = static_cast<int>(yearOfEra + era * 400 + (month <= 2 ? 1 : 0));
}

int64_t localClockMillis(const HudState &state) {
    if (state.clockUnixSeconds <= 0) return INT64_MIN;
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    const uint64_t elapsedMs = nowMs >= state.clockSyncMonotonicMs
        ? nowMs - state.clockSyncMonotonicMs : 0U;
    return state.clockUnixSeconds * 1000LL + static_cast<int64_t>(elapsedMs) +
           static_cast<int64_t>(state.timezoneOffsetMinutes) * 60000LL;
}

uint8_t calculateTimeOfDayBrightness(int normalizedMinute, uint8_t maxBrightness) {
    constexpr int kDayStartMinute = 6 * 60;          // 06:00 (360)
    constexpr int kSunsetStartMinute = 17 * 60 + 30; // 17:30 (1050)
    constexpr int kNightStartMinute = 19 * 60;        // 19:00 (1140)
    constexpr int kSunriseStartMinute = 5 * 60;       // 05:00 (300)
    constexpr uint8_t kNightBrightness = 20;

    if (maxBrightness < kNightBrightness) maxBrightness = 100;

    if (normalizedMinute >= kDayStartMinute && normalizedMinute < kSunsetStartMinute) {
        return maxBrightness;
    } else if (normalizedMinute >= kSunsetStartMinute && normalizedMinute < kNightStartMinute) {
        // Sunset transition: 17:30 -> 19:00 (Max -> 20%)
        const float progress = static_cast<float>(normalizedMinute - kSunsetStartMinute) /
                               static_cast<float>(kNightStartMinute - kSunsetStartMinute);
        return static_cast<uint8_t>(maxBrightness - progress * (maxBrightness - kNightBrightness) + 0.5f);
    } else if (normalizedMinute >= kNightStartMinute || normalizedMinute < kSunriseStartMinute) {
        // Night: 19:00 -> 05:00 (20%)
        return kNightBrightness;
    } else {
        // Sunrise transition: 05:00 -> 06:00 (20% -> Max)
        const float progress = static_cast<float>(normalizedMinute - kSunriseStartMinute) /
                               static_cast<float>(kDayStartMinute - kSunriseStartMinute);
        return static_cast<uint8_t>(kNightBrightness + progress * (maxBrightness - kNightBrightness) + 0.5f);
    }
}

const char *displayStreet(const HudState &state) {
    // Hiển thị tên đường
    if (state.currentStreet[0] != 0) return state.currentStreet.data();
    return "Cầu đường chưa đặt tên";
}

bool sameRegion(const Rect &left, const Rect &right) {
    return left.x == right.x && left.y == right.y && left.width == right.width &&
           left.height == right.height;
}

void arrowHead(Canvas &canvas, int x, int y, int dx, int dy, uint16_t color, int thickness) {
    if (std::abs(dx) >= std::abs(dy)) {
        const int sign = dx >= 0 ? 1 : -1;
        canvas.line(x, y, x - sign * 9, y - 7, color, thickness);
        canvas.line(x, y, x - sign * 9, y + 7, color, thickness);
    } else {
        const int sign = dy >= 0 ? 1 : -1;
        canvas.line(x, y, x - 7, y - sign * 9, color, thickness);
        canvas.line(x, y, x + 7, y - sign * 9, color, thickness);
    }
}

const assets::AlphaMask *maneuverAsset(Maneuver maneuver) {
    switch (maneuver) {
        case Maneuver::Continue: return &assets::kManeuverContinue;
        case Maneuver::Left: return &assets::kManeuverLeft;
        case Maneuver::Right: return &assets::kManeuverRight;
        case Maneuver::UTurn: return &assets::kManeuverUTurn;
        case Maneuver::Roundabout: return &assets::kManeuverRoundabout;
        case Maneuver::RoundaboutLeft: return &assets::kManeuverRoundaboutLeft;
        case Maneuver::RoundaboutRight: return &assets::kManeuverRoundaboutRight;
        case Maneuver::RoundaboutStraight: return &assets::kManeuverRoundaboutStraight;
        case Maneuver::RoundaboutUTurn: return &assets::kManeuverRoundaboutUTurn;
        case Maneuver::KeepLeft: return &assets::kManeuverExitLeft;
        case Maneuver::KeepRight: return &assets::kManeuverExitRight;
        case Maneuver::ExitLeft: return &assets::kManeuverExitLeft;
        case Maneuver::ExitRight: return &assets::kManeuverExitRight;
        case Maneuver::Arrive: return &assets::kManeuverArrive;
        default: return nullptr;
    }
}

void drawRoundaboutExit(Canvas &canvas, int exit, uint16_t color) {
    if (exit <= 0) return;
    char number[12];
    std::snprintf(number,sizeof(number),"%d",exit);
    constexpr int centerX = 42;
    constexpr int width = 36;
    constexpr int centerY = 54;
    canvas.fontText(centerX-width/2, centerY - assets::kNumberMedium.lineHeight/2,
                    number, assets::kNumberMedium, color, width, true);
}

enum class SpeedSignContext { Current, AlertLarge, AlertSmall };

const assets::ColorBitmap *speedLimitAsset(int value, SpeedSignContext context) {
    for (std::size_t index = 0; index < assets::kSpeedLimitAssetCount; ++index) {
        const assets::SpeedLimitAssetSet &entry = assets::kSpeedLimitAssets[index];
        if (entry.value != value) continue;
        switch (context) {
            case SpeedSignContext::Current: return entry.current;
            case SpeedSignContext::AlertLarge: return entry.alertLarge;
            case SpeedSignContext::AlertSmall: return entry.alertSmall;
        }
    }
    return nullptr;
}

void drawManeuverIcon(Canvas &canvas, Maneuver maneuver, int exit, uint16_t color) {
    constexpr int cx = 42;
    constexpr int maneuverAssetY = 34;
    constexpr int top = 38;
    constexpr int bottom = 90;
    constexpr int roundaboutCenterY = 62;
    constexpr int junctionY = 67;
    const int thick = 5;
    if (maneuver == Maneuver::None) return;
    if (const assets::AlphaMask *asset = maneuverAsset(maneuver)) {
        canvas.alphaMask(12, maneuverAssetY, *asset, color);
        if (isRoundaboutManeuver(maneuver) && exit > 0) {
            drawRoundaboutExit(canvas,exit,color);
        }
        return;
    }
    if (maneuver == Maneuver::Arrive) {
        canvas.line(cx - 18, top + 7, cx - 18, bottom - 3, color, 3);
        canvas.fillRect(cx - 15, top + 8, 28, 18, color);
        canvas.fillRect(cx - 10, top + 13, 5, 5, colors::Background);
        canvas.fillRect(cx, top + 13, 5, 5, colors::Background);
        return;
    }
    if (isRoundaboutManeuver(maneuver)) {
        canvas.circle(cx, roundaboutCenterY, 20, color, 4);
        canvas.line(cx, bottom, cx, roundaboutCenterY + 18, color, thick);
        if (maneuver == Maneuver::RoundaboutStraight) {
            canvas.line(cx, roundaboutCenterY - 20, cx, top, color, thick);
            arrowHead(canvas, cx, top, 0, -1, color, 3);
            drawRoundaboutExit(canvas,exit,color);
            return;
        }
        if (maneuver == Maneuver::RoundaboutUTurn) {
            const int endX = cx - 27;
            canvas.line(cx - 19, roundaboutCenterY, endX, roundaboutCenterY, color, thick);
            canvas.line(endX, roundaboutCenterY, endX, roundaboutCenterY + 13, color, thick);
            arrowHead(canvas, endX, roundaboutCenterY + 13, 0, 1, color, 3);
            drawRoundaboutExit(canvas,exit,color);
            return;
        }
        const bool left = maneuver == Maneuver::RoundaboutLeft;
        const int endX = left ? cx - 27 : cx + 27;
        canvas.line(left ? cx - 19 : cx + 19, roundaboutCenterY, endX, roundaboutCenterY, color, thick);
        arrowHead(canvas, endX, roundaboutCenterY, left ? -1 : 1, 0, color, 3);
        drawRoundaboutExit(canvas,exit,color);
        return;
    }
    if (maneuver == Maneuver::UTurn || maneuver == Maneuver::UTurnRightReserved) {
        const bool right = maneuver == Maneuver::UTurnRightReserved;
        const int side = right ? 1 : -1;
        canvas.line(cx, bottom, cx, top + 17, color, thick);
        canvas.line(cx, top + 17, cx + side * 16, top + 5, color, thick);
        canvas.line(cx + side * 16, top + 5, cx + side * 27, top + 17, color, thick);
        canvas.line(cx + side * 27, top + 17, cx + side * 27, top + 30, color, thick);
        arrowHead(canvas, cx + side * 27, top + 30, 0, 1, color, 3);
        return;
    }

    int endX = cx, endY = top;
    switch (maneuver) {
        case Maneuver::Left: endX = 15; endY = top + 15; break;
        case Maneuver::Right: endX = 69; endY = top + 15; break;
        case Maneuver::SlightLeft: case Maneuver::KeepLeft: endX = 21; endY = top + 4; break;
        case Maneuver::SlightRight: case Maneuver::KeepRight: endX = 63; endY = top + 4; break;
        case Maneuver::SharpLeft: case Maneuver::ExitLeft: endX = 14; endY = top + 35; break;
        case Maneuver::SharpRight: case Maneuver::ExitRight: endX = 70; endY = top + 35; break;
        default: break;
    }
    canvas.line(cx, bottom, cx, junctionY, color, thick);
    canvas.line(cx, junctionY, endX, endY, color, thick);
    arrowHead(canvas, endX, endY, endX - cx, endY - junctionY, color, 3);
}

const assets::ColorBitmap *alertAsset(AlertKind kind, bool dominant) {
    const uint8_t code = static_cast<uint8_t>(kind);
    for (std::size_t index = 0; index < assets::kAlertAssetCount; ++index) {
        const assets::AlertAssetSet &entry = assets::kAlertAssets[index];
        if (entry.code == code) return dominant ? entry.large : entry.small;
    }
    return nullptr;
}

uint16_t trafficSeverityColor(uint8_t severity) {
    switch (severity) {
        case 1: return colors::Green;
        case 2: return colors::Amber;
        case 3: return colors::rgb565(255, 112, 24);
        case 4: case 5: return colors::Red;
        default: return colors::Muted;
    }
}

const assets::ColorBitmap *trafficJamAsset(uint8_t severity, bool dominant) {
    switch (severity) {
        case 1: return dominant ? &assets::kAlertTrafficJam1Large
                                : &assets::kAlertTrafficJam1Small;
        case 2: return dominant ? &assets::kAlertTrafficJam2Large
                                : &assets::kAlertTrafficJam2Small;
        case 4: case 5: return dominant ? &assets::kAlertTrafficJam4Large
                                        : &assets::kAlertTrafficJam4Small;
        default: return alertAsset(AlertKind::TrafficJam, dominant);
    }
}

const char *trafficSeverityLabel(uint8_t severity) {
    switch (severity) {
        case 1: return "NHẸ";
        case 2: return "VỪA";
        case 3: return "NẶNG";
        case 4: return "ĐỨNG IM";
        case 5: return "ĐƯỜNG ĐÓNG";
        default: return "KẸT XE";
    }
}

void drawTrafficSeverityTicks(Canvas &canvas, int centerX, int y,
                              uint8_t severity, uint16_t color) {
    if (severity == 0) return;
    constexpr int tickWidth = 4;
    constexpr int gap = 2;
    constexpr int totalWidth = 5 * tickWidth + 4 * gap;
    const int startX = centerX - totalWidth / 2;
    for (int tick = 0; tick < 5; ++tick)
        canvas.fillRect(startX + tick * (tickWidth + gap), y, tickWidth, 3,
                        tick < severity ? color : colors::Muted);
}

void drawCard(Canvas &canvas, int x, int y, int w, int h, uint16_t borderCol) {
    canvas.line(x + 2, y, x + w - 3, y, borderCol);
    canvas.line(x + 2, y + h - 1, x + w - 3, y + h - 1, borderCol);
    canvas.line(x, y + 2, x, y + h - 3, borderCol);
    canvas.line(x + w - 1, y + 2, x + w - 1, y + h - 3, borderCol);
    canvas.pixel(x + 1, y + 1, borderCol);
    canvas.pixel(x + w - 2, y + 1, borderCol);
    canvas.pixel(x + 1, y + h - 2, borderCol);
    canvas.pixel(x + w - 2, y + h - 2, borderCol);
}

void drawAlertIcon(Canvas &canvas, int cx, int cy, int radius, const AlertState &alert, bool dominant) {
    if (alert.kind == AlertKind::None) return;
    const int iconSize = radius * 2;
    if (alert.kind == AlertKind::SpeedDrop) {
        const assets::ColorBitmap *sign = speedLimitAsset(
            alert.valueKmh, dominant ? SpeedSignContext::AlertLarge : SpeedSignContext::AlertSmall);
        if (sign && sign->pixels && sign->alpha) {
            if (dominant) {
                canvas.colorBitmapScaled(cx - iconSize / 2, cy - iconSize / 2, *sign, iconSize);
            } else {
                canvas.colorBitmap(cx - sign->width / 2, cy - sign->height / 2, *sign);
            }
            return;
        }
        const int thick = dominant ? 4 : 2;
        char value[5]{};
        canvas.fillCircle(cx,cy,radius,colors::White);
        canvas.circle(cx,cy,radius,colors::Red,thick);
        std::snprintf(value,sizeof(value),"%d",alert.valueKmh);
        if (dominant)
            canvas.fontText(cx-radius,cy-assets::kNumberMedium.lineHeight/2,value,
                            assets::kNumberMedium,colors::Black,2*radius,true);
        else
            canvas.fontText(cx-radius,cy-assets::kNumberSmall.lineHeight/2,value,
                            assets::kNumberSmall,colors::Black,2*radius,true);
        return;
    }

    const bool trafficJam = alert.kind == AlertKind::TrafficJam;
    const uint16_t severityColor = trafficSeverityColor(alert.trafficSeverity);
    if (trafficJam && alert.trafficSeverity > 0)
        canvas.circle(cx, cy, radius + (dominant ? 3 : 2), severityColor,
                      dominant ? 2 : 1);
    const assets::ColorBitmap *bitmap = trafficJam
        ? trafficJamAsset(alert.trafficSeverity, dominant) : alertAsset(alert.kind, dominant);
    if (!bitmap) bitmap = alertAsset(AlertKind::Hazard, dominant);
    if (bitmap && bitmap->pixels && bitmap->alpha) {
        if (dominant) {
            canvas.colorBitmapScaled(cx - iconSize / 2, cy - iconSize / 2, *bitmap, iconSize);
        } else {
            canvas.colorBitmap(cx - bitmap->width / 2, cy - bitmap->height / 2, *bitmap);
        }
        if (trafficJam && !dominant)
            drawTrafficSeverityTicks(canvas, cx, cy + radius + 3,
                                     alert.trafficSeverity, severityColor);
        return;
    }

    // Last-resort primitive only when the generated hazard asset is absent.
    canvas.triangle(cx,cy-radius,cx-radius,cy+radius,cx+radius,cy+radius,colors::Amber);
    canvas.fontText(cx-radius,cy-assets::kTextMedium.lineHeight/2,"!",assets::kTextMedium,
                    colors::Amber,2*radius,true);
}

void formatDistance(int meters, char *output, size_t capacity) {
    if (meters < 0) { output[0] = 0; return; }
    if (meters < 1000) std::snprintf(output, capacity, "%d M", meters);
    else if (meters < 10000) std::snprintf(output, capacity, "%.1f KM", meters / 1000.0);
    else std::snprintf(output, capacity, "%d KM", (meters + 500) / 1000);
}

inline int alertPriorityScore(AlertKind kind) {
    switch (kind) {
        // Hạng 1: Các loại biển cấm
        case AlertKind::NoPassing:
        case AlertKind::SpeedDrop:
        case AlertKind::NoCar:
        case AlertKind::NoMotorcycle:
        case AlertKind::NoLeftTurn:
        case AlertKind::NoRightTurn:
        case AlertKind::NoUTurn:
        case AlertKind::NoStraight:
        case AlertKind::ProhibitedRoad:
        case AlertKind::CombinedTurnRestriction:
        case AlertKind::NoStraightAndRight:
        case AlertKind::NoLeftAndUTurn:
        case AlertKind::NoStraightAndLeft:
        case AlertKind::NoLeftAndRight:
        case AlertKind::CarNoLeftAndUTurn:
        case AlertKind::CarNoRightAndUTurn:
        case AlertKind::NoRightAndUTurn:
        case AlertKind::CarNoLeftTurn:
        case AlertKind::CarNoRightTurn:
        case AlertKind::CarNoUTurn:
            return 1;

        // Hạng 2: Camera, Đèn đỏ, Cảnh sát
        case AlertKind::SpeedCamera:
        case AlertKind::RedLightCamera:
        case AlertKind::TrafficLight:
        case AlertKind::Police:
        case AlertKind::PhoneCamera:
        case AlertKind::SeatbeltCamera:
        case AlertKind::DistanceCamera:
        case AlertKind::BusLaneCamera:
        case AlertKind::NoiseCamera:
        case AlertKind::StopSignCamera:
        case AlertKind::DummyCamera:
            return 2;

        // Hạng 3: Các cảnh báo chướng ngại, nguy hiểm, giao thông khác
        default:
            return (kind != AlertKind::None) ? 3 : 99;
    }
}

inline bool compareAlertPriority(const AlertState &a, const AlertState &b) {
    if (a.kind == AlertKind::None) return false;
    if (b.kind == AlertKind::None) return true;
    const int scoreA = alertPriorityScore(a.kind);
    const int scoreB = alertPriorityScore(b.kind);
    if (scoreA != scoreB) return scoreA < scoreB;
    return a.distanceM < b.distanceM;
}

// Alert column: the primary alert is shown only within 800 m, the follow-up within 1.5 km.
constexpr int kPrimaryAlertMaxM = 800;
constexpr int kSecondaryAlertMaxM = 1500;

bool alertWithinRange(const AlertState &alert, int maxDistanceM) {
    // An active no-passing zone reports the length left inside it, not a distance ahead.
    return alert.kind == AlertKind::NoPassing || alert.distanceM < 0 || alert.distanceM <= maxDistanceM;
}

// Returns the alerts to draw: [0] primary, [1] optional follow-up (at most 2).
uint8_t collectSortedAlerts(const HudState &state, AlertState *outAlerts, uint8_t maxCount) {
    uint8_t count = 0;
    if (state.noPassingZone && count < maxCount) {
        AlertState zone{};
        zone.kind = AlertKind::NoPassing;
        zone.distanceM = state.noPassingRemainingM;
        zone.valueKmh = 0;
        outAlerts[count++] = zone;
    }
    if (state.nearestAlert.kind != AlertKind::None) {
        bool dup = false;
        for (uint8_t i = 0; i < count; ++i) {
            if (outAlerts[i] == state.nearestAlert) { dup = true; break; }
        }
        if (!dup && count < maxCount) {
            outAlerts[count++] = state.nearestAlert;
        }
    }
    for (uint8_t i = 0; i < state.upcomingAlertCount && count < maxCount; ++i) {
        const auto &up = state.upcomingAlerts[i];
        if (up.kind == AlertKind::None) continue;
        bool dup = false;
        for (uint8_t j = 0; j < count; ++j) {
            if (outAlerts[j] == up) { dup = true; break; }
        }
        if (!dup && count < maxCount) {
            outAlerts[count++] = up;
        }
    }
    // Alerts far beyond the follow-up range are not actionable yet.
    uint8_t kept = 0;
    for (uint8_t i = 0; i < count; ++i)
        if (alertWithinRange(outAlerts[i], kSecondaryAlertMaxM)) outAlerts[kept++] = outAlerts[i];
    count = kept;
    std::sort(outAlerts, outAlerts + count, compareAlertPriority);

    // The primary alert is the highest-priority one that is close enough; it is moved to the front
    // and only one follow-up alert (the next by priority) is kept. No close alert: column is idle.
    uint8_t primary = 0;
    while (primary < count && !alertWithinRange(outAlerts[primary], kPrimaryAlertMaxM)) ++primary;
    if (primary == count) return 0;
    std::rotate(outAlerts, outAlerts + primary, outAlerts + primary + 1);
    return std::min<uint8_t>(count, 2);
}

// Bottom of the digit glyphs inside a font's line box, i.e. where the text baseline sits.
// Line-box heights differ between fonts (diacritics), so units must be aligned on this.
int digitBaseline(const assets::BitmapFont &font) {
    for (std::size_t index = 0; index < font.glyphCount; ++index) {
        const assets::FontGlyph &glyph = font.glyphs[index];
        if (glyph.codepoint == static_cast<uint32_t>('0')) return glyph.yOffset + glyph.height;
    }
    return font.lineHeight;
}

// No alert ahead: show how far the vehicle is below/above the speed limit in the alert column,
// e.g. "DƯỚI / GIỚI HẠN / 4 km/h". Green below, yellow at the limit, red above.
bool alertColumnIdle(const HudState &state) {
    AlertState alerts[8];
    return collectSortedAlerts(state, alerts, 8) == 0;
}

void drawSpeedMargin(Canvas &canvas, const HudState &state) {
    if (state.speedLimitKmh <= 0) return;  // limit unknown: nothing meaningful to compare
    const int difference = state.speedKmh - state.speedLimitKmh;
    const char *word = difference < 0 ? "DƯỚI" : difference > 0 ? "VƯỢT" : "BẰNG";
    const uint16_t color = difference < 0 ? colors::Green
                         : difference > 0 ? colors::Red : colors::Yellow;
    char number[16];
    std::snprintf(number, sizeof(number), "%d", std::abs(difference));

    constexpr int kUnitGap = 3;
    const int width = canvas.width();
    drawCard(canvas, 2, 30, width - 4, 84, colors::Muted);
    canvas.fontText(0, 38, word, assets::kTextMedium, color, width, true);
    canvas.fontText(0, 62, "GIỚI HẠN", assets::kTextSmall, colors::Muted, width, true);
    const int numberWidth = canvas.fontTextWidth(number, assets::kTextLarge);
    const int unitWidth = canvas.fontTextWidth("km/h", assets::kTextSmall) + kUnitGap;
    const int startX = (width - (numberWidth + unitWidth)) / 2;
    constexpr int kNumberY = 80;
    canvas.fontText(startX, kNumberY, number, assets::kTextLarge, color, -1, false);
    canvas.fontText(startX + numberWidth + kUnitGap,
                    kNumberY + digitBaseline(assets::kTextLarge) - digitBaseline(assets::kTextSmall),
                    "km/h", assets::kTextSmall, colors::Muted, -1, false);
}

// The cluster and alert columns are laid out for a 130 px band; centre it in the 140 px band.
constexpr int kClusterShiftY = 5;
// Lanes occupy the top-left corner of the cluster (above the manoeuvre icon, left of the speed
// sign). While lanes are shown the next-street banner starts to the right of that block.
constexpr int kLaneBlockWidth = 88;
constexpr int kLaneBlockHeight = 31;
constexpr int kLaneColumnWidth = 85;
constexpr int kMaxVisibleLanes = 12;

bool lanesShown(const HudState &state) {
    return state.navigationActive && state.maneuver != Maneuver::None && state.laneCount > 0;
}

int nextStreetTextX(const HudState &state) { return lanesShown(state) ? 90 : 5; }
int nextStreetAvailableWidth(const HudState &state) { return lanesShown(state) ? 130 : 215; }

void laneArrowHead(Canvas &canvas, int x, int y, int dx, int dy, uint16_t color, int thickness) {
    constexpr int size = 2;
    if (std::abs(dx) >= std::abs(dy)) {
        const int sign = dx >= 0 ? 1 : -1;
        canvas.line(x,y,x-sign*size,y-size,color,thickness);
        canvas.line(x,y,x-sign*size,y+size,color,thickness);
    } else {
        const int sign = dy >= 0 ? 1 : -1;
        canvas.line(x,y,x-size,y-sign*size,color,thickness);
        canvas.line(x,y,x+size,y-sign*size,color,thickness);
    }
}

void drawLane(Canvas &canvas, int x, int branchWidth, const LaneState &lane, uint16_t fg) {
    const bool recommendedLane = lane.selectedMask != 0;
    canvas.line(x,29,x,16,recommendedLane ? fg : colors::Muted,recommendedLane ? 2 : 1);
    for (int bit = 0; bit < 8; ++bit) {
        const uint8_t flag = static_cast<uint8_t>(1U << bit);
        if ((lane.directionMask & flag) == 0) continue;
        const bool selected = (lane.selectedMask & flag) != 0;
        const uint16_t color = selected ? fg : colors::Muted;
        const int thickness = selected ? 2 : 1;
        int endX = x;
        int endY = 5;
        switch (bit) {
            case 1: endX = x - std::max(1, branchWidth / 2); endY = 6; break;
            case 2: endX = x - branchWidth; endY = 9; break;
            case 3: endX = x - branchWidth; endY = 13; break;
            case 4: endX = x + std::max(1, branchWidth / 2); endY = 6; break;
            case 5: endX = x + branchWidth; endY = 9; break;
            case 6: endX = x + branchWidth; endY = 13; break;
            case 7:
                endX = x - branchWidth;
                endY = 15;
                canvas.line(x,16,endX,10,color,thickness);
                canvas.line(endX,10,endX,15,color,thickness);
                laneArrowHead(canvas,endX,15,0,1,color,thickness);
                continue;
            default: break;
        }
        canvas.line(x,16,endX,endY,color,thickness);
        laneArrowHead(canvas,endX,endY,endX-x,endY-16,color,thickness);
    }
}

void drawMiniLanes(Canvas &canvas, const HudState &state, uint16_t fg) {
    const int total = std::min<int>(state.laneCount, static_cast<int>(kMaxLanes));
    const int visible = std::min(total, kMaxVisibleLanes);
    if (visible <= 0) return;
    int start = 0;
    if (total > visible) {
        int first = -1;
        int last = -1;
        for (int index = 0; index < total; ++index) {
            if (state.lanes[index].selectedMask == 0) continue;
            if (first < 0) first = index;
            last = index;
        }
        if (first >= 0) start = std::clamp((first + last) / 2 - visible / 2, 0, total - visible);
    }
    const int spacing = std::max(6, 80 / visible);
    const int totalWidth = spacing * visible;
    const int firstX = (kLaneColumnWidth - totalWidth) / 2 + spacing / 2;
    const int branchWidth = std::max(2, spacing / 2 - 1);
    for (int index = 0; index < visible; ++index)
        drawLane(canvas, firstX + index * spacing, branchWidth, state.lanes[start + index], fg);
}

// Bottom strip: street name on the left, ETA on the right while navigating.
struct StreetLayout {
    bool showEta{false};
    char eta[16]{};
    int etaWidth{0};
    int availableWidth{304};
};

StreetLayout streetLayout(const Canvas &canvas, const HudState &state) {
    StreetLayout result;
    if (state.navigationActive && state.eta[0] != 0) {
        std::snprintf(result.eta, sizeof(result.eta), "ETA %s", state.eta.data());
        result.etaWidth = canvas.fontTextWidth(result.eta, assets::kTextSmall) + 6;
        result.showEta = true;
        result.availableWidth = layout::Width - result.etaWidth - 16;
    }
    return result;
}

void formatStreetLine(const HudState &state, char *output, std::size_t capacity) {
    if (state.navigationActive) {
        std::snprintf(output, capacity, "HT: %s", displayStreet(state));
    } else {
        std::snprintf(output, capacity, "%s", displayStreet(state));
    }
}
}  // namespace

esp_err_t HudRenderer::init() {
    buffer_ = static_cast<uint16_t *>(heap_caps_malloc(layout::MaxRegionPixels * sizeof(uint16_t),
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!buffer_) {
        ESP_LOGE(kTag, "Unable to allocate %d-byte dirty-region buffer",
                 layout::MaxRegionPixels * static_cast<int>(sizeof(uint16_t)));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void HudRenderer::render(const HudState &state, const DeviceSettings &settings,
                         const SystemStatusSnapshot &systemStatus) {
    const int64_t currentClockMillis = localClockMillis(state);
    const int64_t currentClockSecond = currentClockMillis == INT64_MIN
        ? INT64_MIN : currentClockMillis / 1000LL;
    const int64_t currentClockMinute = currentClockSecond == INT64_MIN
        ? INT64_MIN : currentClockSecond / 60;
    clockActive_ = state.connected && state.hasProducerState && currentClockSecond != INT64_MIN;
    const bool limitOnly = settings.displayMode == DisplayMode::SpeedLimitOnly;

    Canvas metrics(buffer_, layout::Street.width, layout::Street.height);
    const StreetLayout streetInfo = streetLayout(metrics, state);
    const bool streetChanged = firstFrame_ || !sameText(state.currentStreet, previous_.currentStreet) ||
                               state.navigationActive != previous_.navigationActive ||
                               !sameText(state.eta, previous_.eta);
    const int availableStreetWidth = streetInfo.availableWidth;
    char streetDisplayBuf[180]{};
    formatStreetLine(state, streetDisplayBuf, sizeof(streetDisplayBuf));
    const int streetWidth = settings.showStreet
        ? metrics.fontTextWidth(streetDisplayBuf, assets::kTextMedium) : 0;
    const bool shouldMarquee = state.connected && state.hasProducerState && settings.showStreet &&
                               streetWidth > availableStreetWidth;
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    if (!shouldMarquee) {
        marqueeActive_ = false;
        marqueeOffset_ = 0;
    } else {
        if (!marqueeActive_ || streetChanged || streetWidth != marqueeTextWidth_ ||
            availableStreetWidth != marqueeAvailableWidth_) {
            marqueeEpochMs_ = nowMs;
            marqueeOffset_ = 0;
            marqueeRenderedOffset_ = -1;
        }
        marqueeActive_ = true;
        marqueeTextWidth_ = streetWidth;
        marqueeAvailableWidth_ = availableStreetWidth;
        constexpr uint64_t kStartHoldMs = 1200;
        constexpr uint64_t kEndHoldMs = 900;
        constexpr uint64_t kMsPerPixel = 45;
        const int overflow = streetWidth - availableStreetWidth;
        const uint64_t scrollMs = static_cast<uint64_t>(overflow) * kMsPerPixel;
        const uint64_t cycleMs = kStartHoldMs + scrollMs + kEndHoldMs;
        const uint64_t elapsed = cycleMs > 0 ? (nowMs - marqueeEpochMs_) % cycleMs : 0;
        if (elapsed < kStartHoldMs) marqueeOffset_ = 0;
        else if (elapsed < kStartHoldMs + scrollMs)
            marqueeOffset_ = std::min(overflow, static_cast<int>((elapsed - kStartHoldMs) / kMsPerPixel));
        else marqueeOffset_ = overflow;
    }
    const bool marqueeFrameChanged = marqueeActive_ && marqueeOffset_ != marqueeRenderedOffset_;

    const bool nextStreetChanged = firstFrame_ || !sameText(state.nextStreet, previous_.nextStreet);
    const int availableNextStreetWidth = nextStreetAvailableWidth(state);
    const int nextStreetWidth = state.nextStreet[0] != 0
        ? metrics.fontTextWidth(state.nextStreet.data(), assets::kTextMedium) : 0;
    const bool shouldNextStreetMarquee = state.connected && state.hasProducerState &&
                                         state.nextStreet[0] != 0 &&
                                         nextStreetWidth > availableNextStreetWidth;
    if (!shouldNextStreetMarquee) {
        nextStreetMarqueeActive_ = false;
        nextStreetMarqueeOffset_ = 0;
    } else {
        if (!nextStreetMarqueeActive_ || nextStreetChanged ||
            nextStreetWidth != nextStreetMarqueeTextWidth_ ||
            availableNextStreetWidth != nextStreetMarqueeAvailableWidth_) {
            nextStreetMarqueeEpochMs_ = nowMs;
            nextStreetMarqueeOffset_ = 0;
            nextStreetMarqueeRenderedOffset_ = -1;
        }
        nextStreetMarqueeActive_ = true;
        nextStreetMarqueeTextWidth_ = nextStreetWidth;
        nextStreetMarqueeAvailableWidth_ = availableNextStreetWidth;
        constexpr uint64_t kNsStartHoldMs = 1000;
        constexpr uint64_t kNsEndHoldMs = 500;
        constexpr uint64_t kNsMsPerPixel = 28;
        const int overflow = nextStreetWidth - availableNextStreetWidth + 6;
        const uint64_t scrollMs = static_cast<uint64_t>(overflow) * kNsMsPerPixel;
        const uint64_t cycleMs = kNsStartHoldMs + scrollMs + kNsEndHoldMs;
        const uint64_t elapsed = cycleMs > 0 ? (nowMs - nextStreetMarqueeEpochMs_) % cycleMs : 0;
        if (elapsed < kNsStartHoldMs) nextStreetMarqueeOffset_ = 0;
        else if (elapsed < kNsStartHoldMs + scrollMs)
            nextStreetMarqueeOffset_ = std::min(overflow, static_cast<int>((elapsed - kNsStartHoldMs) / kNsMsPerPixel));
        else nextStreetMarqueeOffset_ = overflow;
    }
    const bool nextStreetMarqueeFrameChanged = nextStreetMarqueeActive_ &&
                                               nextStreetMarqueeOffset_ != nextStreetMarqueeRenderedOffset_;

    const bool isOverspeed = state.connected && state.hasProducerState &&
                             !state.signalStale &&
                             firmwareOverspeed(state, settings);
    overspeedActive_ = isOverspeed;
    const bool currentOverspeedPhase = isOverspeed && ((nowMs / 250ULL) % 2ULL == 0ULL);
    const bool overspeedPhaseChanged = currentOverspeedPhase != renderedOverspeedPhase_;
    if (overspeedPhaseChanged) {
        renderedOverspeedPhase_ = currentOverspeedPhase;
    }
    const bool statusChanged = firstFrame_ || state.connected != previous_.connected ||
                               state.hasProducerState != previous_.hasProducerState ||
                               state.navigationActive != previous_.navigationActive;
    const bool configChanged = firstFrame_ || hasSettingsChanged(settings, previousSettings_);
    uint8_t targetBrightness = 100;
    if (settings.fixedBrightness) {
        targetBrightness = settings.brightness;
    } else {
        if (currentClockMinute != INT64_MIN) {
            const int normalizedMinute = static_cast<int>((currentClockMinute % 1440 + 1440) % 1440);
            targetBrightness = calculateTimeOfDayBrightness(normalizedMinute, 100);
        } else if (settings.theme == UiTheme::Night) {
            targetBrightness = 20;
        }
    }

    if (firstFrame_ || targetBrightness != currentAppliedBrightness_) {
        const esp_err_t brightnessResult = DisplayDriver::instance().setBrightness(targetBrightness);
        if (brightnessResult != ESP_OK)
            ESP_LOGE(kTag, "Brightness update failed: %s", esp_err_to_name(brightnessResult));
        else
            currentAppliedBrightness_ = targetBrightness;
    }

    if (firstFrame_ || settings.invertColor != currentAppliedInvert_) {
        const esp_err_t invertResult = DisplayDriver::instance().setInvertColor(settings.invertColor);
        if (invertResult != ESP_OK)
            ESP_LOGE(kTag, "HUD color inversion update failed: %s", esp_err_to_name(invertResult));
        else
            currentAppliedInvert_ = settings.invertColor;
    }

    if (configChanged) {
        const esp_err_t orientationResult = DisplayDriver::instance().setOrientation(
            settings.mirrorHud, settings.rotateDisplay);
        if (orientationResult != ESP_OK)
            ESP_LOGE(kTag, "HUD orientation update failed: %s", esp_err_to_name(orientationResult));
    }
    const bool systemStatusChanged = firstFrame_ || systemStatus != previousSystemStatus_;
    if (systemStatus.visible) {
        if (systemStatusChanged || configChanged) {
            renderRegion(layout::MainCluster, state, settings, systemStatus);
            renderRegion(layout::Alerts, state, settings, systemStatus);
            renderRegion(layout::Street, state, settings, systemStatus);
        }
        previous_ = state;
        previousSettings_ = settings;
        previousSystemStatus_ = systemStatus;
        firstFrame_ = false;
        return;
    }
    const bool systemStatusClosed = previousSystemStatus_.visible;
    bool streetRendered = false;
    bool mainClusterRendered = false;

    const bool triggerFullRedraw = systemStatusClosed || statusChanged || configChanged ||
                                   !state.connected || !state.hasProducerState ||
                                   overspeedPhaseChanged;
    if (triggerFullRedraw) {
        renderRegion(layout::MainCluster, state, settings, systemStatus);
        mainClusterRendered = true;
        renderRegion(layout::Alerts, state, settings, systemStatus);
        renderRegion(layout::Street, state, settings, systemStatus);
        streetRendered = true;
    } else {
        // The vehicle speed is only drawn in the cluster in "current speed" mode.
        const bool mainClusterChanged = maneuverChanged(state, previous_) ||
                                       nextStreetMarqueeFrameChanged ||
                                       state.speedLimitKmh != previous_.speedLimitKmh ||
                                       (!limitOnly && (state.speedKmh != previous_.speedKmh ||
                                                       state.overSpeed != previous_.overSpeed)) ||
                                       currentClockMinute != renderedClockMinute_ ||
                                       systemStatusChanged;
        if (mainClusterChanged) {
            renderRegion(layout::MainCluster, state, settings, systemStatus);
            mainClusterRendered = true;
        }
        const bool speedMarginChanged =
            !limitOnly &&
            (state.speedKmh != previous_.speedKmh ||
             state.speedLimitKmh != previous_.speedLimitKmh) && alertColumnIdle(state);
        const bool changedAlerts = alertsChanged(state, previous_) ||
                                   currentClockMinute != renderedClockMinute_ ||
                                   speedMarginChanged;
        if (changedAlerts) renderRegion(layout::Alerts, state, settings, systemStatus);
        if (streetChanged || marqueeFrameChanged) {
            renderRegion(layout::Street, state, settings, systemStatus);
            streetRendered = true;
        }
    }
    previous_ = state;
    previousSettings_ = settings;
    previousSystemStatus_ = systemStatus;
    renderedClockMinute_ = currentClockMinute;
    if (streetRendered) marqueeRenderedOffset_ = marqueeOffset_;
    if (mainClusterRendered) nextStreetMarqueeRenderedOffset_ = nextStreetMarqueeOffset_;
    firstFrame_ = false;
}

void renderOverspeedBorder(Canvas &canvas, const Rect &region) {
    constexpr int kBorder = 4;
    if (region.y == 0) {
        canvas.fillRect(0, 0, canvas.width(), kBorder, colors::Red);
    }
    if (region.y + region.height == layout::Height) {
        canvas.fillRect(0, canvas.height() - kBorder, canvas.width(), kBorder, colors::Red);
    }
    if (region.x == 0) {
        canvas.fillRect(0, 0, kBorder, canvas.height(), colors::Red);
    }
    if (region.x + region.width == layout::Width) {
        canvas.fillRect(canvas.width() - kBorder, 0, kBorder, canvas.height(), colors::Red);
    }
}

void HudRenderer::renderRegion(const Rect &region, const HudState &state,
                               const DeviceSettings &settings,
                               const SystemStatusSnapshot &systemStatus) {
    const Rect physicalRegion = layout::physicalRect(region);
    Canvas canvas(buffer_, physicalRegion.width, physicalRegion.height,
                  region.width, region.height);
    canvas.setTranslation(settings.offsetX, settings.offsetY);

    const bool live = state.connected && state.hasProducerState;
    if (systemStatus.visible) {
        renderSystemStatus(canvas, region, systemStatus, settings);
    } else if (!live) {
        renderStatus(canvas, region, state, settings);
    } else if (sameRegion(region, layout::MainCluster)) {
        canvas.setTranslation(settings.offsetX, settings.offsetY + kClusterShiftY);
        renderMainCluster(canvas, state, settings);
    } else if (sameRegion(region, layout::Alerts)) {
        canvas.setTranslation(settings.offsetX, settings.offsetY + kClusterShiftY);
        renderAlerts(canvas, state, settings);
    } else {
        renderStreet(canvas, state, settings);
    }

    if (!systemStatus.visible && live) {
        canvas.setTranslation(settings.offsetX, settings.offsetY);
        renderMainIndicators(canvas, region, systemStatus);
        if (renderedOverspeedPhase_) {
            canvas.setTranslation(0, 0);
            renderOverspeedBorder(canvas, region);
        }
    }
    const esp_err_t result = DisplayDriver::instance().drawRegion(physicalRegion, buffer_);
    if (result != ESP_OK) ESP_LOGE(kTag, "Dirty region (%d,%d %dx%d) failed: %s",
                                   region.x,region.y,region.width,region.height,esp_err_to_name(result));
}

void HudRenderer::renderMainIndicators(Canvas &canvas, const Rect &region,
                                       const SystemStatusSnapshot &systemStatus) {
    // The battery sits in the bottom-left corner of the cluster, below the manoeuvre distance. It
    // is omitted entirely when GPIO4 does not carry a plausible single-cell LiPo voltage.
    if (!systemStatus.batteryPresent || !sameRegion(region, layout::MainCluster)) return;
    constexpr int batteryX = 8;
    constexpr int batteryY = 128;
    constexpr int batteryWidth = 20;
    constexpr int batteryHeight = 11;
    const uint16_t batteryColor = systemStatus.batteryPercent <= 15 ? colors::Red
        : systemStatus.batteryPercent <= 35 ? colors::Amber : colors::Green;
    canvas.fillRect(batteryX - region.x, batteryY - region.y,
                    batteryWidth, batteryHeight, batteryColor);
    canvas.fillRect(batteryX + 2 - region.x, batteryY + 2 - region.y,
                    batteryWidth - 4, batteryHeight - 4, colors::Background);
    canvas.fillRect(batteryX + batteryWidth - region.x, batteryY + 3 - region.y,
                    3, batteryHeight - 6, batteryColor);
    const int fillWidth = (batteryWidth - 6) * systemStatus.batteryPercent / 100;
    canvas.fillRect(batteryX + 3 - region.x, batteryY + 3 - region.y,
                    fillWidth, batteryHeight - 6, batteryColor);
    char percent[8];
    std::snprintf(percent, sizeof(percent), "%u%%",
                  static_cast<unsigned>(systemStatus.batteryPercent));
    canvas.fontText(batteryX + batteryWidth + 8 - region.x, batteryY - 2 - region.y, percent,
                    assets::kTextSmall, batteryColor, 38, false);
}

void HudRenderer::renderSystemStatus(Canvas &canvas, const Rect &region,
                                     const SystemStatusSnapshot &systemStatus,
                                     const DeviceSettings &settings) {
    canvas.clear(colors::Background);
    const uint16_t fg = foreground(settings);
    canvas.fontText(0 - region.x, screenY(7) - region.y, "TRẠNG THÁI",
                    assets::kTextMedium, fg, layout::Width, true);

    // Battery body and terminal. The fill is proportional to the estimated
    // single-cell LiPo charge; an X marks an unavailable/non-battery reading.
    constexpr int batteryX = 25;
    const int batteryY = screenY(47);
    constexpr int batteryWidth = 52;
    constexpr int batteryHeight = 27;
    const uint16_t batteryColor = systemStatus.batteryPresent
        ? (systemStatus.batteryPercent <= 15 ? colors::Red
           : systemStatus.batteryPercent <= 35 ? colors::Amber : colors::Green)
        : colors::Muted;
    canvas.fillRect(batteryX - region.x, batteryY - region.y,
                    batteryWidth, batteryHeight, batteryColor);
    canvas.fillRect(batteryX + 3 - region.x, batteryY + 3 - region.y,
                    batteryWidth - 6, batteryHeight - 6, colors::Background);
    canvas.fillRect(batteryX + batteryWidth - region.x, batteryY + 8 - region.y,
                    5, batteryHeight - 16, batteryColor);
    if (systemStatus.batteryPresent) {
        const int fillWidth = (batteryWidth - 10) * systemStatus.batteryPercent / 100;
        canvas.fillRect(batteryX + 5 - region.x, batteryY + 5 - region.y,
                        fillWidth, batteryHeight - 10, batteryColor);
    } else {
        canvas.line(batteryX + 8 - region.x, batteryY + 6 - region.y,
                    batteryX + batteryWidth - 8 - region.x,
                    batteryY + batteryHeight - 6 - region.y, colors::Red, 3);
        canvas.line(batteryX + batteryWidth - 8 - region.x, batteryY + 6 - region.y,
                    batteryX + 8 - region.x,
                    batteryY + batteryHeight - 6 - region.y, colors::Red, 3);
    }
    char batteryText[24];
    if (systemStatus.batteryPresent)
        std::snprintf(batteryText, sizeof(batteryText), "PIN %u%%",
                      static_cast<unsigned>(systemStatus.batteryPercent));
    else
        std::snprintf(batteryText, sizeof(batteryText), "KHÔNG CÓ PIN");
    canvas.fontText(95 - region.x, screenY(49) - region.y, batteryText,
                    assets::kTextMedium,
                    batteryColor, 215, false);

    // Bluetooth rune plus four qualitative signal bars.
    constexpr int bluetoothX = 49;
    const int bluetoothTop = screenY(92);
    const int bluetoothBottom = screenY(132);
    const uint16_t bluetoothColor = bleSignalColor(systemStatus);
    canvas.line(bluetoothX - region.x, bluetoothTop - region.y,
                bluetoothX - region.x, bluetoothBottom - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX - region.x, bluetoothTop - region.y,
                bluetoothX + 13 - region.x, screenY(103) - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX + 13 - region.x, screenY(103) - region.y,
                bluetoothX - 10 - region.x, screenY(122) - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX - 10 - region.x, screenY(101) - region.y,
                bluetoothX + 13 - region.x, screenY(122) - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX + 13 - region.x, screenY(122) - region.y,
                bluetoothX - region.x, bluetoothBottom - region.y, bluetoothColor, 3);

    int signalBars = 0;
    if (systemStatus.bleConnected) {
        signalBars = systemStatus.bleRssiDbm >= -55 ? 4 :
                     systemStatus.bleRssiDbm >= -67 ? 3 :
                     systemStatus.bleRssiDbm >= -78 ? 2 :
                     systemStatus.bleRssiDbm >= -90 ? 1 : 0;
    }
    for (int bar = 0; bar < 4; ++bar) {
        const int height = 5 + bar * 5;
        canvas.fillRect(69 + bar * 6 - region.x, screenY(132) - height - region.y,
                        4, height, bar < signalBars ? bluetoothColor : colors::Muted);
    }
    char bleText[28];
    if (systemStatus.bleConnected)
        std::snprintf(bleText, sizeof(bleText), "BLE %d dBm",
                      static_cast<int>(systemStatus.bleRssiDbm));
    else
        std::snprintf(bleText, sizeof(bleText), "BLE CHƯA KẾT NỐI");
    canvas.fontText(100 - region.x, screenY(102) - region.y, bleText,
                    assets::kTextMedium, bluetoothColor, 210, false);
}

void HudRenderer::renderStatus(Canvas &canvas, const Rect &region, const HudState &state, const DeviceSettings &) {
    canvas.clear(colors::Background);

    // Header bar (y = 0..33)
    canvas.fillRect(0 - region.x, 33 - region.y, layout::Width, 1, colors::Muted);
    canvas.fontText(12 - region.x, 4 - region.y, "WAZE HUD", assets::kTextLarge, colors::White, 140, false);

    // Status badge on the right
    const char *statusText = state.signalStale ? "MẤT TÍN HIỆU" : state.connected ? "ĐÃ KẾT NỐI" : "CHỜ KẾT NỐI";
    const uint16_t statusColor = state.signalStale ? colors::Amber : state.connected ? colors::Green : colors::Cyan;
    constexpr int badgeW = 120;
    const int badgeX = layout::Width - badgeW - 8;
    canvas.line(badgeX - region.x, 6 - region.y, badgeX + badgeW - region.x, 6 - region.y, statusColor);
    canvas.line(badgeX - region.x, 28 - region.y, badgeX + badgeW - region.x, 28 - region.y, statusColor);
    canvas.line(badgeX - region.x, 6 - region.y, badgeX - region.x, 28 - region.y, statusColor);
    canvas.line(badgeX + badgeW - region.x, 6 - region.y, badgeX + badgeW - region.x, 28 - region.y, statusColor);
    canvas.fillCircle(badgeX + 9 - region.x, 17 - region.y, 3, statusColor);
    canvas.fontText(badgeX + 16 - region.x, 9 - region.y, statusText, assets::kTextSmall, colors::White, badgeW - 20, false);

    // Instruction card (y = 38..131)
    constexpr int cardX = 10;
    constexpr int cardY = 38;
    constexpr int cardW = 300;
    constexpr int cardH = 94;
    canvas.line(cardX - region.x, cardY - region.y, cardX + cardW - region.x, cardY - region.y, colors::Muted);
    canvas.line(cardX - region.x, cardY + cardH - region.y, cardX + cardW - region.x, cardY + cardH - region.y, colors::Muted);
    canvas.line(cardX - region.x, cardY - region.y, cardX - region.x, cardY + cardH - region.y, colors::Muted);
    canvas.line(cardX + cardW - region.x, cardY - region.y, cardX + cardW - region.x, cardY + cardH - region.y, colors::Muted);

    if (state.signalStale) {
        canvas.fontText(cardX + 15 - region.x, cardY + 12 - region.y, "Mất kết nối với điện thoại", assets::kTextMedium, colors::Amber, cardW - 30, false);
        canvas.fontText(cardX + 15 - region.x, cardY + 46 - region.y, "Đang tự động kết nối lại...", assets::kTextSmall, colors::White, cardW - 30, false);
        canvas.fontText(cardX + 15 - region.x, cardY + 68 - region.y, "Kiểm tra lại Bluetooth trên máy", assets::kTextSmall, colors::Muted, cardW - 30, false);
    } else if (state.connected) {
        canvas.fontText(cardX + 15 - region.x, cardY + 12 - region.y, "Đã kết nối thành công!", assets::kTextMedium, colors::Green, cardW - 30, false);
        canvas.fontText(cardX + 15 - region.x, cardY + 46 - region.y, "Đang chờ WazeMod gửi dữ liệu...", assets::kTextSmall, colors::White, cardW - 30, false);
        canvas.fontText(cardX + 15 - region.x, cardY + 68 - region.y, "Bắt đầu chuyến đi trên điện thoại", assets::kTextSmall, colors::Muted, cardW - 30, false);
    } else {
        canvas.fontText(cardX + 10 - region.x, cardY + 4 - region.y, "CÁCH KẾT NỐI VỚI ĐIỆN THOẠI:", assets::kTextSmall, colors::Amber, cardW - 20, false);
        const char *steps[] = {"Bật Bluetooth trên điện thoại", "Mở ứng dụng WazeMod",
                               "Cài đặt MOD -> Kết nối với thiết bị"};
        const char *numbers[] = {"1", "2", "3"};
        for (int step = 0; step < 3; ++step) {
            const int boxY = cardY + 24 + step * 23;
            canvas.fillRect(cardX + 10 - region.x, boxY - region.y, 20, 20, colors::Cyan);
            canvas.fontText(cardX + 16 - region.x, boxY + 2 - region.y, numbers[step], assets::kTextSmall, colors::Background, 10, false);
            canvas.fontText(cardX + 38 - region.x, boxY + 2 - region.y, steps[step], assets::kTextSmall, colors::White, cardW - 46, false);
        }
    }

    // Footer hints
    canvas.fontText(0 - region.x, 138 - region.y, "Tên thiết bị BLE: WazeHUD", assets::kTextSmall, colors::Cyan, layout::Width, true);
    canvas.fontText(0 - region.x, 154 - region.y, "wazemod.io.vn - f38 VOZ", assets::kTextSmall, colors::Muted, layout::Width, true);
}

void HudRenderer::renderMainCluster(Canvas &canvas, const HudState &state,
                                    const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const bool isNavigating = state.navigationActive && state.maneuver != Maneuver::None;
    const bool hasNextStreet = isNavigating && state.nextStreet[0] != 0;
    const bool limitOnly = settings.displayMode == DisplayMode::SpeedLimitOnly;

    // 1. Left column (x = 0..84): manoeuvre, or the clock while free driving.
    if (!isNavigating) {
        const int64_t clockMs = localClockMillis(state);
        if (clockMs != INT64_MIN) {
            const time_t localSec = static_cast<time_t>(clockMs / 1000LL);
            struct tm tmVal{};
            gmtime_r(&localSec, &tmVal);

            static const char *const kDaysOfWeek[] = {
                "CHỦ NHẬT", "THỨ HAI", "THỨ BA", "THỨ TƯ",
                "THỨ NĂM", "THỨ SÁU", "THỨ BẢY"
            };
            const char *dayStr = (tmVal.tm_wday >= 0 && tmVal.tm_wday <= 6)
                ? kDaysOfWeek[tmVal.tm_wday] : "";

            char timeStr[16];
            std::snprintf(timeStr, sizeof(timeStr), "%02d:%02d", tmVal.tm_hour, tmVal.tm_min);

            const int year = std::clamp(tmVal.tm_year + 1900, 2000, 2099);
            const int month = std::clamp(tmVal.tm_mon + 1, 1, 12);
            const int mday = std::clamp(tmVal.tm_mday, 1, 31);
            char dayMonthStr[8];
            char yearStr[8];
            std::snprintf(dayMonthStr, sizeof(dayMonthStr), "%02d/%02d", mday, month);
            std::snprintf(yearStr, sizeof(yearStr), "%04d", year);

            canvas.fontText(0, 12, dayStr, assets::kTextSmall, colors::Cyan, 84, true);
            canvas.fontText(0, 36, timeStr, assets::kTextLarge, colors::White, 84, true);
            canvas.line(18, 70, 66, 70, colors::Muted, 1);
            // Date on two centred lines: dd/mm in metallic gold, the year in green, each a bright face over
            // a darker 1 px shadow. Two lines fit the 84 px column without cramping or clipping.
            canvas.fontText(1, 75, dayMonthStr, assets::kTextLarge, colors::GoldShadow, 84, true);
            canvas.fontText(0, 74, dayMonthStr, assets::kTextLarge, colors::Gold, 84, true);
            canvas.fontText(1, 103, yearStr, assets::kTextMedium, colors::GreenShadow, 84, true);
            canvas.fontText(0, 102, yearStr, assets::kTextMedium, colors::Green, 84, true);
        } else {
            canvas.fontText(4, 16, "CHẠY", assets::kTextSmall, colors::Muted, 77, true);
            canvas.fontText(4, 34, "TỰ DO", assets::kTextMedium, colors::Green, 77, true);
            canvas.circle(42, 80, 18, colors::Muted, 1);
            canvas.fillCircle(42, 80, 3, colors::Green);
            canvas.line(42, 66, 42, 75, colors::Red, 2);
            canvas.fontText(4, 106, "CRUISE", assets::kTextSmall, colors::Muted, 77, true);
        }
    } else {
        drawManeuverIcon(canvas, state.maneuver, state.roundaboutExit, colors::White);
        char distance[16]; formatDistance(state.maneuverDistanceM, distance, sizeof(distance));
        canvas.fontText(2, 98, distance, assets::kTextMedium, colors::White, 81, true);
    }

    // 2. Speed sign. Free driving has no next-street banner, so the sign is as large as possible;
    // while navigating it shrinks so the banner can sit above it. "Limit only" mode drops the
    // vehicle-speed badge and makes the sign as large as the layout allows.
    constexpr int signX = 155;
    int signY = 62;
    int outerRadius = 60;
    int innerRadius = 50;
    if (hasNextStreet) {
        signY = limitOnly ? 78 : 72;
        outerRadius = limitOnly ? 50 : 48;
        innerRadius = limitOnly ? 42 : 40;
    }

    // Night mode follows the theme setting, or the clock when the theme is automatic.
    bool isNight = false;
    if (settings.theme == UiTheme::Night) {
        isNight = true;
    } else if (settings.theme == UiTheme::Day) {
        isNight = false;
    } else {
        const int64_t clockMs = localClockMillis(state);
        if (clockMs != INT64_MIN) {
            const int64_t minute = (clockMs / 1000LL) / 60;
            const int normMin = static_cast<int>((minute % 1440 + 1440) % 1440);
            isNight = (normMin >= 17 * 60 + 30 || normMin < 6 * 60);
        }
    }

    const uint16_t signBgColor = isNight ? colors::Panel : colors::White;
    const uint16_t signTextColor = isNight ? colors::White : colors::Black;

    canvas.fillCircle(signX, signY, outerRadius, colors::Red);
    canvas.fillCircle(signX, signY, innerRadius, signBgColor);

    if (state.speedLimitKmh > 0) {
        char limit[16];
        std::snprintf(limit, sizeof(limit), "%d", state.speedLimitKmh);
        const auto &speedFont = (state.speedLimitKmh >= 100) ? assets::kNumberSpeedLimit3 : assets::kNumberSpeedLimit;
        canvas.fontText(signX - innerRadius, signY - speedFont.lineHeight / 2 - 1,
                        limit, speedFont, signTextColor, innerRadius * 2, true);
    } else {
        canvas.fontText(signX - innerRadius, signY - assets::kNumberSpeedLimit.lineHeight / 2 - 1,
                        "?", assets::kNumberSpeedLimit, signTextColor, innerRadius * 2, true);
    }

    if (!limitOnly) {
        char speed[16];
        std::snprintf(speed, sizeof(speed), "%d", std::clamp(state.speedKmh, 0, 999));
        const uint16_t speedColor = firmwareOverspeed(state, settings)
            ? (renderedOverspeedPhase_ ? colors::Red : colors::White)
            : foreground(settings);

        // The badge is sized to the number: the 36 px speed-limit digits drawn at 85% (about 31 px).
        const auto &speedFont = assets::kNumberSpeedLimit3;
        constexpr float kBadgeFontScale = 0.85F;
        constexpr int kBadgePadX = 7;
        constexpr int kBadgePadY = 3;
        const int numberWidth = static_cast<int>(std::lround(canvas.fontTextWidth(speed, speedFont) * kBadgeFontScale));
        const int numberHeight = static_cast<int>(std::lround(speedFont.lineHeight * kBadgeFontScale));
        const int badgeW = std::max(44, numberWidth + 2 * kBadgePadX);
        const int badgeH = numberHeight + 2 * kBadgePadY;
        const int badgeX = 225 - badgeW - 2;
        const int badgeY = 130 - badgeH - 2;
        canvas.fillRect(badgeX, badgeY, badgeW, badgeH, colors::Panel);
        drawCard(canvas, badgeX, badgeY, badgeW, badgeH, colors::Muted);
        canvas.fontTextScaled(badgeX, badgeY + kBadgePadY, speed, speedFont, speedColor, kBadgeFontScale,
                              badgeW, true);
    }

    // 3. Next-street banner over the top of the sign. With lanes it starts right of the lane
    // block, so the scrolling text is cut off at the block edge.
    if (hasNextStreet) {
        constexpr int bannerH = 26;
        canvas.fillRect(0, 0, canvas.width(), bannerH, colors::Panel);
        const int offset = nextStreetMarqueeActive_ ? nextStreetMarqueeOffset_ : 0;
        canvas.fontText(nextStreetTextX(state) - offset, 2, state.nextStreet.data(),
                        assets::kTextMedium, colors::White, -1, false);
    }

    // 4. Lane guidance (miniature, top-left corner).
    if (lanesShown(state)) {
        canvas.fillRect(0, 0, kLaneBlockWidth, kLaneBlockHeight, colors::Panel);
        drawMiniLanes(canvas, state, colors::White);
    }
}

void HudRenderer::renderAlerts(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Background);

    const int64_t millis = localClockMillis(state);
    const int64_t second = millis == INT64_MIN ? INT64_MIN : millis / 1000LL;
    if (state.navigationActive && second != INT64_MIN) {
        const int64_t minute = second / 60;
        const int normalizedMinute = static_cast<int>((minute % 1440 + 1440) % 1440);
        char clock[8];
        std::snprintf(clock, sizeof(clock), "%02d:%02d",
                      normalizedMinute / 60, normalizedMinute % 60);
        // Date and clock share the medium font. Two 50 px strings do not fit
        // the 95 px column, so both are drawn with 1 px tighter letter spacing.
        int year = 0, month = 0, day = 0;
        civilFromDays(second / 86400, year, month, day);
        char date[8];
        std::snprintf(date, sizeof(date), "%02d/%02d", day, month);
        // A 1 px vertical bar separates them: date | clock.
        constexpr int kGap = 1;
        constexpr int kSeparatorWidth = 1;
        const int clockWidth = tightTextWidth(canvas, clock, assets::kTextMedium);
        const int dateWidth = tightTextWidth(canvas, date, assets::kTextMedium);
        const int clockX = canvas.width() - clockWidth;
        const int separatorX = clockX - kGap - kSeparatorWidth;
        const int dateX = std::max(0, separatorX - kGap - dateWidth);
        drawTightText(canvas, dateX, 4, date, assets::kTextMedium, colors::Yellow);
        canvas.fillRect(separatorX, 5, kSeparatorWidth, 15, colors::Muted);
        drawTightText(canvas, clockX, 4, clock, assets::kTextMedium, colors::White);
    }

    AlertState allAlerts[8];
    const uint8_t alertCount = collectSortedAlerts(state, allAlerts, 8);
    if (alertCount == 0) {
        // Idle column: how far below/above the limit. Hidden in "limit only" mode, which keeps
        // the vehicle speed off the screen.
        if (settings.displayMode == DisplayMode::CurrentSpeed) drawSpeedMargin(canvas, state);
        return;
    }

    // Highest-priority alert large; one follow-up alert as a single icon + distance row below.
    const AlertState &primary = allAlerts[0];
    const bool hasSecondary = alertCount > 1;
    const int iconRadius = hasSecondary ? 16 : 20;
    const int iconY = hasSecondary ? 42 : 48;
    const int textY = hasSecondary ? 64 : 74;
    drawAlertIcon(canvas, 47, iconY, iconRadius, primary, true);
    char distance[16]; formatDistance(primary.distanceM, distance, sizeof(distance));
    const auto &distFont = hasSecondary ? assets::kTextSmall : assets::kTextMedium;
    canvas.fontText(2, textY, distance, distFont,
                    alertDistanceColor(primary.distanceM, colors::White), 91, true);

    const bool trafficJam = primary.kind == AlertKind::TrafficJam;
    if (trafficJam) {
        char trafficDetail[48];
        std::snprintf(trafficDetail, sizeof(trafficDetail), "%.20s",
                          trafficSeverityLabel(primary.trafficSeverity));
        const int labelY = hasSecondary ? 77 : 96;
        canvas.fontText(1, labelY, trafficDetail, assets::kTextSmall,
                        trafficSeverityColor(primary.trafficSeverity), 93, true);
    }

    if (!hasSecondary) return;
    // Follow-up row: icon on the left, distance on the right, under a thin divider. The divider
    // sits below the traffic-jam label of the primary alert.
    constexpr int kDividerY = 96;
    constexpr int kRowIconX = 22;
    constexpr int kRowIconY = 114;
    constexpr int kRowTextX = 40;
    constexpr int kRowTextWidth = 53;
    canvas.fillRect(8, kDividerY, canvas.width() - 16, 1, colors::Muted);
    const AlertState &upcoming = allAlerts[1];
    drawAlertIcon(canvas, kRowIconX, kRowIconY, 13, upcoming, false);
    char upcomingDistance[12]; formatDistance(upcoming.distanceM, upcomingDistance, sizeof(upcomingDistance));
    const auto &rowFont = canvas.fontTextWidth(upcomingDistance, assets::kTextMedium) <= kRowTextWidth
        ? assets::kTextMedium : assets::kTextSmall;
    canvas.fontText(kRowTextX, kRowIconY - rowFont.lineHeight / 2, upcomingDistance, rowFont,
                    alertDistanceColor(upcoming.distanceM, colors::White), kRowTextWidth, false);
}

void HudRenderer::renderStreet(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const int textY = std::max(0, (layout::StreetHeight - assets::kTextMedium.lineHeight) / 2);
    const StreetLayout street = streetLayout(canvas, state);

    if (settings.showStreet) {
        char streetBuf[180]{};
        formatStreetLine(state, streetBuf, sizeof(streetBuf));
        if (marqueeActive_) {
            canvas.fontText(8 - marqueeOffset_, textY, streetBuf, assets::kTextMedium,
                            colors::White, -1, false);
        } else if (state.navigationActive) {
            canvas.fontText(8, textY, streetBuf, assets::kTextMedium, colors::White,
                            street.availableWidth, false);
        } else {
            canvas.fontText(5, textY, streetBuf, assets::kTextMedium, colors::White,
                            310, true);
        }
    }

    if (street.showEta) {
        // Clip marquee pixels before painting the ETA column.
        const int etaX = layout::Width - street.etaWidth - 4;
        canvas.fillRect(etaX - 4, 0, street.etaWidth + 8, layout::StreetHeight, colors::Panel);
        const int etaY = textY + (assets::kTextMedium.lineHeight - assets::kTextSmall.lineHeight) / 2;
        canvas.fontText(etaX, etaY, street.eta, assets::kTextSmall, colors::Cyan,
                        street.etaWidth, false);
    }
}

}  // namespace waze_hud
