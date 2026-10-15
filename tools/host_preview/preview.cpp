// Host harness: renders HUD scenarios through the real HudRenderer into a framebuffer and dumps PPMs.
#include "display/display_driver.h"
#include "display/hud_renderer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

static int64_t g_nowUs = 1000000;
int64_t esp_timer_get_time() { return g_nowUs; }
void *heap_caps_malloc(size_t n, unsigned) { return std::malloc(n); }
void heap_caps_free(void *p) { std::free(p); }
const char *esp_err_to_name(esp_err_t) { return "err"; }
extern "C" struct tm *gmtime_r(const time_t *t, struct tm *r) { gmtime_s(r, t); return r; }

namespace waze_hud {
static uint16_t g_fb[320 * 170];
DisplayDriver &DisplayDriver::instance() { static DisplayDriver d; return d; }
esp_err_t DisplayDriver::drawRegion(const Rect &r, const uint16_t *px) {
    if (r.x < 0 || r.y < 0 || r.x + r.width > 320 || r.y + r.height > 170) { std::printf("BAD REGION\n"); return -1; }
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x) g_fb[(r.y + y) * 320 + r.x + x] = px[y * r.width + x];
    return 0;
}
esp_err_t DisplayDriver::setBrightness(uint8_t) { return 0; }
esp_err_t DisplayDriver::setOrientation(bool, bool) { return 0; }
esp_err_t DisplayDriver::setInvertColor(bool) { return 0; }

template <std::size_t N> void setText(std::array<char, N> &t, const char *s) { std::snprintf(t.data(), t.size(), "%s", s); }

HudState base() {
    HudState s{};
    s.connected = true; s.hasProducerState = true; s.navigationActive = true;
    s.speedKmh = 70; s.speedLimitKmh = 50; s.maneuver = Maneuver::UTurn; s.maneuverDistanceM = 350;
    s.laneCount = 3;
    s.lanes[0] = {0x05, 0x04}; s.lanes[1] = {0x01, 0x01}; s.lanes[2] = {0x30, 0x10};
    setText(s.currentStreet, "Đường Phạm Văn Đồng");
    setText(s.nextStreet, "Võ Văn Kiệt");
    setText(s.eta, "20:01");
    s.clockUnixSeconds = 1787478627; s.timezoneOffsetMinutes = 420; s.clockSyncMonotonicMs = 0;
    return s;
}

static void dump(const char *name) {
    std::string path = std::string(getenv("OUTDIR")) + "/" + name + ".ppm";
    FILE *f = std::fopen(path.c_str(), "wb");
    std::fprintf(f, "P6\n320 170\n255\n");
    for (int i = 0; i < 320 * 170; ++i) {
        uint16_t p = g_fb[i];
        unsigned char rgb[3] = {(unsigned char)(((p >> 11) & 31) * 255 / 31), (unsigned char)(((p >> 5) & 63) * 255 / 63), (unsigned char)((p & 31) * 255 / 31)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

static void shot(const char *name, const HudState &st, DeviceSettings set = {}, SystemStatusSnapshot sys = {}) {
    std::memset(g_fb, 0x55, sizeof(g_fb));
    HudRenderer r;
    r.init();
    r.render(st, set, sys);
    dump(name);
}
}  // namespace waze_hud

int main() {
    using namespace waze_hud;
    DeviceSettings limitOnly{}; limitOnly.displayMode = DisplayMode::SpeedLimitOnly;
    DeviceSettings noStreet{}; noStreet.showStreet = false;
    shot("s00_base", base());
    { auto s = base(); s.nearestAlert = {AlertKind::SpeedCamera, 300, 0}; s.upcomingAlerts[0] = {AlertKind::Police, 850, 0}; s.upcomingAlertCount = 1; shot("s01_cam_police", s); }
    { auto s = base(); s.nearestAlert = {AlertKind::Police, 120, 0}; s.upcomingAlerts[0] = {AlertKind::Hazard, 600, 0}; s.upcomingAlerts[1] = {AlertKind::SpeedDrop, 900, 40}; s.upcomingAlertCount = 2; shot("s02_three_alerts", s); }
    { auto s = base(); s.noPassingZone = true; s.noPassingRemainingM = 1250; s.nearestAlert = {AlertKind::NoPassing, 0, 0}; shot("s03_nopassing", s); }
    { auto s = base(); s.maneuver = Maneuver::RoundaboutRight; s.roundaboutExit = 3; s.maneuverDistanceM = 1200; setText(s.currentStreet, "Võ Nguyên Giáp"); shot("s04_roundabout", s); }
    g_nowUs = 1000000;  // (1000 ms / 250) % 2 == 0 -> overspeed phase on
    { auto s = base(); s.speedKmh = 86; s.speedLimitKmh = 60; s.overSpeed = true; s.maneuver = Maneuver::SharpLeft; s.maneuverDistanceM = 35; shot("s05_overspeed", s); }
    { HudState s{}; shot("s06_disconnected", s); }
    { HudState s{}; s.connected = true; shot("s06b_connected_wait", s); }
    { auto s = base(); setText(s.currentStreet, "Đường Cách Mạng Tháng Tám, Phường Bến Thành"); setText(s.nextStreet, "Đại lộ Võ Văn Kiệt, Phường Cầu Ông Lãnh"); s.maneuver = Maneuver::ExitRight; s.maneuverDistanceM = 980; shot("s07_long_names", s); }
    { auto s = base(); s.maneuver = Maneuver::RoundaboutStraight; s.roundaboutExit = 2; s.maneuverDistanceM = 420; s.nearestAlert = {AlertKind::PhoneCamera, 650, 0}; shot("s08_rb_straight", s); }
    { auto s = base(); s.nearestAlert = {AlertKind::Railway, 180, 0}; s.upcomingAlerts[0] = {AlertKind::Flood, 900, 0}; s.upcomingAlerts[1] = {AlertKind::SchoolZone, 1400, 0}; s.upcomingAlertCount = 2; shot("s09_rail_flood_school", s); }
    { auto s = base(); s.nearestAlert = {AlertKind::TrafficJam, 420, 0, 4, 18}; s.upcomingAlerts[0] = {AlertKind::TrafficJam, 1350, 0, 2, 5}; s.upcomingAlertCount = 1; shot("s10_traffic", s); }
    { auto s = base(); s.laneCount = 10; for (uint8_t i = 0; i < 10; ++i) { s.lanes[i].directionMask = i < 3 ? 0x04 : i > 5 ? 0x20 : 0x01; s.lanes[i].selectedMask = i >= 3 && i <= 5 ? 0x01 : 0; } s.upcomingAlerts[0] = {AlertKind::Police, 650, 0}; s.upcomingAlerts[1] = {AlertKind::Hazard, 1200, 0}; s.upcomingAlertCount = 2; shot("s11_ten_lanes", s); }
    { auto s = base(); s.laneCount = 16; for (uint8_t i = 0; i < 16; ++i) { s.lanes[i].directionMask = 0x01; s.lanes[i].selectedMask = i == 12 ? 0x01 : 0; } shot("s12_sixteen_lanes", s); }
    { auto s = base(); s.laneCount = 0; shot("s13_no_lanes", s); }
    { auto s = base(); s.navigationActive = false; s.maneuver = Maneuver::None; s.laneCount = 0; setText(s.nextStreet, ""); setText(s.eta, ""); shot("s14_freedrive", s); }
    { auto s = base(); s.navigationActive = false; s.maneuver = Maneuver::None; s.laneCount = 0; setText(s.nextStreet, ""); setText(s.eta, ""); s.nearestAlert = {AlertKind::Police, 400, 0}; shot("s15_freedrive_alert", s); }
    shot("m00_limitonly", base(), limitOnly);
    { auto s = base(); s.laneCount = 0; shot("m01_limitonly_nolanes", s, limitOnly); }
    { auto s = base(); s.navigationActive = false; s.maneuver = Maneuver::None; s.laneCount = 0; setText(s.nextStreet, ""); setText(s.eta, ""); shot("m02_limitonly_free", s, limitOnly); }
    shot("m03_nostreet", base(), noStreet);
    { SystemStatusSnapshot sys{}; sys.batteryPresent = true; sys.batteryPercent = 72; auto s = base(); shot("m04_battery", s, {}, sys); }
    { SystemStatusSnapshot sys{}; sys.visible = true; sys.batteryPresent = true; sys.batteryPercent = 72; sys.bleConnected = true; sys.bleRssiDbm = -62; shot("m05_status", base(), {}, sys); }
    { DeviceSettings d{}; d.theme = UiTheme::Night; auto s = base(); shot("m06_night", s, d); }
    { auto s = base(); s.speedKmh = 130; s.speedLimitKmh = 120; shot("m07_3digit", s); }
    { auto s = base(); s.speedLimitKmh = 0; shot("m08_nolimit", s); }
    std::printf("done\n");
    return 0;
}
