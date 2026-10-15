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
    setText(s.currentStreet, "Đường Lê Lợi");
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


struct Step { HudState st; DeviceSettings set; SystemStatusSnapshot sys; const char *name; };
static HudState W(HudState s, const char *street, const char *next) { setText(s.currentStreet, street); setText(s.nextStreet, next); return s; }
}  // namespace waze_hud
int main() {
    using namespace waze_hud;
    std::vector<Step> steps;
    auto add = [&](const char *n, HudState s, DeviceSettings d = {}, SystemStatusSnapshot y = {}) { steps.push_back({s, d, y, n}); };
    DeviceSettings lim{}; lim.displayMode = DisplayMode::SpeedLimitOnly;
    DeviceSettings nos{}; nos.showStreet = false;
    DeviceSettings night{}; night.theme = UiTheme::Night;
    HudState b = base();
    add("base", b);
    { auto s = b; s.speedKmh = 40; add("slow", s); }
    { auto s = b; s.nearestAlert = {AlertKind::SpeedCamera, 300, 0}; s.upcomingAlerts[0] = {AlertKind::Police, 850, 0}; s.upcomingAlertCount = 1; add("cam", s); }
    { auto s = b; s.nearestAlert = {AlertKind::Police, 120, 0}; s.upcomingAlerts[0] = {AlertKind::Hazard, 600, 0}; s.upcomingAlerts[1] = {AlertKind::SpeedDrop, 900, 40}; s.upcomingAlertCount = 2; add("three", s); }
    { auto s = b; s.laneCount = 0; add("nolanes", s); }
    { auto s = b; s.laneCount = 0; setText(s.nextStreet, ""); add("nonext", s); }
    { auto s = b; s.navigationActive = false; s.maneuver = Maneuver::None; s.laneCount = 0; setText(s.nextStreet, ""); setText(s.eta, ""); add("free", s); }
    { auto s = b; s.navigationActive = false; s.maneuver = Maneuver::None; s.laneCount = 0; setText(s.nextStreet, ""); setText(s.eta, ""); add("free_lim", s, lim); }
    add("lim", b, lim);
    add("nostreet", b, nos);
    add("night", b, night);
    { auto s = b; s.eta[0] = 0; add("noeta", s); }
    { auto s = b; s.speedKmh = 120; s.speedLimitKmh = 100; add("fast", s); }
    { auto s = b; s.speedLimitKmh = 0; add("nolimit", s); }
    { SystemStatusSnapshot y{}; y.batteryPresent = true; y.batteryPercent = 50; y.generation = 1; add("battery", b, {}, y); }
    { SystemStatusSnapshot y{}; y.visible = true; y.generation = 2; add("status", b, {}, y); }
    add("back", b);
    { HudState s{}; add("disc", s); }
    add("base2", b);
    int bad = 0;
    HudRenderer inc; inc.init();
    for (size_t i = 0; i < steps.size(); ++i) {
        g_nowUs = 1000000 + (int64_t)i * 1000000;
        inc.render(steps[i].st, steps[i].set, steps[i].sys);
        static uint16_t incFb[320 * 170];
        std::memcpy(incFb, g_fb, sizeof(incFb));
        std::memset(g_fb, 0x55, sizeof(g_fb));
        HudRenderer fresh; fresh.init();
        fresh.render(steps[i].st, steps[i].set, steps[i].sys);
        int diff = 0, minx = 999, miny = 999, maxx = -1, maxy = -1;
        for (int p = 0; p < 320 * 170; ++p) if (incFb[p] != g_fb[p]) { ++diff; int x = p % 320, y = p / 320; minx = std::min(minx, x); maxx = std::max(maxx, x); miny = std::min(miny, y); maxy = std::max(maxy, y); }
        std::printf("%-10s diff=%d", steps[i].name, diff);
        if (diff) { std::printf("  bbox x%d-%d y%d-%d", minx, maxx, miny, maxy); ++bad; }
        std::puts("");
        // restore incremental framebuffer so the next incremental step builds on it
        std::memcpy(g_fb, incFb, sizeof(incFb));
    }
    std::printf("steps with diffs: %d", bad); std::puts("");
    return 0;
}
