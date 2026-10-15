#include "display/bitmap_font.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace waze_hud {
namespace {

struct GlyphPattern { char character; std::array<uint8_t, 5> columns; };

// Compact, deterministic 5x7 font. Application text deliberately uses a
// restrained all-caps automotive style at this small physical size.
constexpr GlyphPattern kPatterns[] = {
    {' ',{0,0,0,0,0}}, {'!',{0,0,0x5F,0,0}}, {'-',{0x08,0x08,0x08,0x08,0x08}},
    {'.',{0,0x60,0x60,0,0}}, {'/',{0x20,0x10,0x08,0x04,0x02}}, {':',{0,0x36,0x36,0,0}},
    {'0',{0x3E,0x51,0x49,0x45,0x3E}}, {'1',{0,0x42,0x7F,0x40,0}},
    {'2',{0x42,0x61,0x51,0x49,0x46}}, {'3',{0x21,0x41,0x45,0x4B,0x31}},
    {'4',{0x18,0x14,0x12,0x7F,0x10}}, {'5',{0x27,0x45,0x45,0x45,0x39}},
    {'6',{0x3C,0x4A,0x49,0x49,0x30}}, {'7',{0x01,0x71,0x09,0x05,0x03}},
    {'8',{0x36,0x49,0x49,0x49,0x36}}, {'9',{0x06,0x49,0x49,0x29,0x1E}},
    {'A',{0x7E,0x11,0x11,0x11,0x7E}}, {'B',{0x7F,0x49,0x49,0x49,0x36}},
    {'C',{0x3E,0x41,0x41,0x41,0x22}}, {'D',{0x7F,0x41,0x41,0x22,0x1C}},
    {'E',{0x7F,0x49,0x49,0x49,0x41}}, {'F',{0x7F,0x09,0x09,0x09,0x01}},
    {'G',{0x3E,0x41,0x49,0x49,0x7A}}, {'H',{0x7F,0x08,0x08,0x08,0x7F}},
    {'I',{0,0x41,0x7F,0x41,0}}, {'J',{0x20,0x40,0x41,0x3F,0x01}},
    {'K',{0x7F,0x08,0x14,0x22,0x41}}, {'L',{0x7F,0x40,0x40,0x40,0x40}},
    {'M',{0x7F,0x02,0x0C,0x02,0x7F}}, {'N',{0x7F,0x04,0x08,0x10,0x7F}},
    {'O',{0x3E,0x41,0x41,0x41,0x3E}}, {'P',{0x7F,0x09,0x09,0x09,0x06}},
    {'Q',{0x3E,0x41,0x51,0x21,0x5E}}, {'R',{0x7F,0x09,0x19,0x29,0x46}},
    {'S',{0x46,0x49,0x49,0x49,0x31}}, {'T',{0x01,0x01,0x7F,0x01,0x01}},
    {'U',{0x3F,0x40,0x40,0x40,0x3F}}, {'V',{0x1F,0x20,0x40,0x20,0x1F}},
    {'W',{0x3F,0x40,0x38,0x40,0x3F}}, {'X',{0x63,0x14,0x08,0x14,0x63}},
    {'Y',{0x07,0x08,0x70,0x08,0x07}}, {'Z',{0x61,0x51,0x49,0x45,0x43}},
    {'?',{0x02,0x01,0x51,0x09,0x06}},
};

enum class Modifier : uint8_t { None, Circumflex, Breve, Horn, Stroke };
enum class Tone : uint8_t { None, Acute, Grave, Hook, Tilde, Dot };
struct DecodedGlyph { char base{'?'}; Modifier modifier{Modifier::None}; Tone tone{Tone::None}; };

const std::array<uint8_t, 5> &patternFor(char input) {
    const char c = input >= 'a' && input <= 'z' ? static_cast<char>(input - 32) : input;
    for (const auto &entry : kPatterns) if (entry.character == c) return entry.columns;
    for (const auto &entry : kPatterns) if (entry.character == '?') return entry.columns;
    return kPatterns[0].columns;
}

uint32_t rawNextCodepoint(const char *&text) {
    if (!text || !*text) return 0;
    const auto first = static_cast<uint8_t>(*text++);
    if (first < 0x80) return first;
    if ((first & 0xE0) == 0xC0) {
        if (!*text) return first;
        const uint8_t b = static_cast<uint8_t>(*text++);
        return ((first & 0x1F) << 6) | (b & 0x3F);
    }
    if ((first & 0xF0) == 0xE0) {
        if (!*text || !*(text + 1)) return first;
        const uint8_t b = static_cast<uint8_t>(*text++);
        const uint8_t c = static_cast<uint8_t>(*text++);
        return ((first & 0x0F) << 12) | ((b & 0x3F) << 6) | (c & 0x3F);
    }
    while ((*text & 0xC0) == 0x80) ++text;
    return '?';
}

constexpr uint32_t composeVietnamese(uint32_t base, uint32_t combining) {
    switch (base) {
        case 0x0041: // A
            switch (combining) {
                case 0x0300: return 0x00C0; // A + U+0300 -> À
                case 0x0301: return 0x00C1; // A + U+0301 -> Á
                case 0x0302: return 0x00C2; // A + U+0302 -> Â
                case 0x0303: return 0x00C3; // A + U+0303 -> Ã
                case 0x0306: return 0x0102; // A + U+0306 -> Ă
                case 0x0309: return 0x1EA2; // A + U+0309 -> Ả
                case 0x0323: return 0x1EA0; // A + U+0323 -> Ạ
                default: break;
            }
            break;
        case 0x0044: // D
            switch (combining) {
                case 0x0323: return 0x1E0C; // D + U+0323 -> Ḍ
                default: break;
            }
            break;
        case 0x0045: // E
            switch (combining) {
                case 0x0300: return 0x00C8; // E + U+0300 -> È
                case 0x0301: return 0x00C9; // E + U+0301 -> É
                case 0x0302: return 0x00CA; // E + U+0302 -> Ê
                case 0x0303: return 0x1EBC; // E + U+0303 -> Ẽ
                case 0x0306: return 0x0114; // E + U+0306 -> Ĕ
                case 0x0309: return 0x1EBA; // E + U+0309 -> Ẻ
                case 0x0323: return 0x1EB8; // E + U+0323 -> Ẹ
                default: break;
            }
            break;
        case 0x0049: // I
            switch (combining) {
                case 0x0300: return 0x00CC; // I + U+0300 -> Ì
                case 0x0301: return 0x00CD; // I + U+0301 -> Í
                case 0x0302: return 0x00CE; // I + U+0302 -> Î
                case 0x0303: return 0x0128; // I + U+0303 -> Ĩ
                case 0x0306: return 0x012C; // I + U+0306 -> Ĭ
                case 0x0309: return 0x1EC8; // I + U+0309 -> Ỉ
                case 0x0323: return 0x1ECA; // I + U+0323 -> Ị
                default: break;
            }
            break;
        case 0x004F: // O
            switch (combining) {
                case 0x0300: return 0x00D2; // O + U+0300 -> Ò
                case 0x0301: return 0x00D3; // O + U+0301 -> Ó
                case 0x0302: return 0x00D4; // O + U+0302 -> Ô
                case 0x0303: return 0x00D5; // O + U+0303 -> Õ
                case 0x0306: return 0x014E; // O + U+0306 -> Ŏ
                case 0x0309: return 0x1ECE; // O + U+0309 -> Ỏ
                case 0x031B: return 0x01A0; // O + U+031B -> Ơ
                case 0x0323: return 0x1ECC; // O + U+0323 -> Ọ
                default: break;
            }
            break;
        case 0x0055: // U
            switch (combining) {
                case 0x0300: return 0x00D9; // U + U+0300 -> Ù
                case 0x0301: return 0x00DA; // U + U+0301 -> Ú
                case 0x0302: return 0x00DB; // U + U+0302 -> Û
                case 0x0303: return 0x0168; // U + U+0303 -> Ũ
                case 0x0306: return 0x016C; // U + U+0306 -> Ŭ
                case 0x0309: return 0x1EE6; // U + U+0309 -> Ủ
                case 0x031B: return 0x01AF; // U + U+031B -> Ư
                case 0x0323: return 0x1EE4; // U + U+0323 -> Ụ
                default: break;
            }
            break;
        case 0x0059: // Y
            switch (combining) {
                case 0x0300: return 0x1EF2; // Y + U+0300 -> Ỳ
                case 0x0301: return 0x00DD; // Y + U+0301 -> Ý
                case 0x0302: return 0x0176; // Y + U+0302 -> Ŷ
                case 0x0303: return 0x1EF8; // Y + U+0303 -> Ỹ
                case 0x0309: return 0x1EF6; // Y + U+0309 -> Ỷ
                case 0x0323: return 0x1EF4; // Y + U+0323 -> Ỵ
                default: break;
            }
            break;
        case 0x0061: // a
            switch (combining) {
                case 0x0300: return 0x00E0; // a + U+0300 -> à
                case 0x0301: return 0x00E1; // a + U+0301 -> á
                case 0x0302: return 0x00E2; // a + U+0302 -> â
                case 0x0303: return 0x00E3; // a + U+0303 -> ã
                case 0x0306: return 0x0103; // a + U+0306 -> ă
                case 0x0309: return 0x1EA3; // a + U+0309 -> ả
                case 0x0323: return 0x1EA1; // a + U+0323 -> ạ
                default: break;
            }
            break;
        case 0x0064: // d
            switch (combining) {
                case 0x0323: return 0x1E0D; // d + U+0323 -> ḍ
                default: break;
            }
            break;
        case 0x0065: // e
            switch (combining) {
                case 0x0300: return 0x00E8; // e + U+0300 -> è
                case 0x0301: return 0x00E9; // e + U+0301 -> é
                case 0x0302: return 0x00EA; // e + U+0302 -> ê
                case 0x0303: return 0x1EBD; // e + U+0303 -> ẽ
                case 0x0306: return 0x0115; // e + U+0306 -> ĕ
                case 0x0309: return 0x1EBB; // e + U+0309 -> ẻ
                case 0x0323: return 0x1EB9; // e + U+0323 -> ẹ
                default: break;
            }
            break;
        case 0x0069: // i
            switch (combining) {
                case 0x0300: return 0x00EC; // i + U+0300 -> ì
                case 0x0301: return 0x00ED; // i + U+0301 -> í
                case 0x0302: return 0x00EE; // i + U+0302 -> î
                case 0x0303: return 0x0129; // i + U+0303 -> ĩ
                case 0x0306: return 0x012D; // i + U+0306 -> ĭ
                case 0x0309: return 0x1EC9; // i + U+0309 -> ỉ
                case 0x0323: return 0x1ECB; // i + U+0323 -> ị
                default: break;
            }
            break;
        case 0x006F: // o
            switch (combining) {
                case 0x0300: return 0x00F2; // o + U+0300 -> ò
                case 0x0301: return 0x00F3; // o + U+0301 -> ó
                case 0x0302: return 0x00F4; // o + U+0302 -> ô
                case 0x0303: return 0x00F5; // o + U+0303 -> õ
                case 0x0306: return 0x014F; // o + U+0306 -> ŏ
                case 0x0309: return 0x1ECF; // o + U+0309 -> ỏ
                case 0x031B: return 0x01A1; // o + U+031B -> ơ
                case 0x0323: return 0x1ECD; // o + U+0323 -> ọ
                default: break;
            }
            break;
        case 0x0075: // u
            switch (combining) {
                case 0x0300: return 0x00F9; // u + U+0300 -> ù
                case 0x0301: return 0x00FA; // u + U+0301 -> ú
                case 0x0302: return 0x00FB; // u + U+0302 -> û
                case 0x0303: return 0x0169; // u + U+0303 -> ũ
                case 0x0306: return 0x016D; // u + U+0306 -> ŭ
                case 0x0309: return 0x1EE7; // u + U+0309 -> ủ
                case 0x031B: return 0x01B0; // u + U+031B -> ư
                case 0x0323: return 0x1EE5; // u + U+0323 -> ụ
                default: break;
            }
            break;
        case 0x0079: // y
            switch (combining) {
                case 0x0300: return 0x1EF3; // y + U+0300 -> ỳ
                case 0x0301: return 0x00FD; // y + U+0301 -> ý
                case 0x0302: return 0x0177; // y + U+0302 -> ŷ
                case 0x0303: return 0x1EF9; // y + U+0303 -> ỹ
                case 0x0309: return 0x1EF7; // y + U+0309 -> ỷ
                case 0x0323: return 0x1EF5; // y + U+0323 -> ỵ
                default: break;
            }
            break;
        case 0x00C2: // Â
            switch (combining) {
                case 0x0300: return 0x1EA6; // Â + U+0300 -> Ầ
                case 0x0301: return 0x1EA4; // Â + U+0301 -> Ấ
                case 0x0303: return 0x1EAA; // Â + U+0303 -> Ẫ
                case 0x0309: return 0x1EA8; // Â + U+0309 -> Ẩ
                case 0x0323: return 0x1EAC; // Â + U+0323 -> Ậ
                default: break;
            }
            break;
        case 0x00CA: // Ê
            switch (combining) {
                case 0x0300: return 0x1EC0; // Ê + U+0300 -> Ề
                case 0x0301: return 0x1EBE; // Ê + U+0301 -> Ế
                case 0x0303: return 0x1EC4; // Ê + U+0303 -> Ễ
                case 0x0309: return 0x1EC2; // Ê + U+0309 -> Ể
                case 0x0323: return 0x1EC6; // Ê + U+0323 -> Ệ
                default: break;
            }
            break;
        case 0x00D2: // Ò
            switch (combining) {
                case 0x031B: return 0x1EDC; // Ò + U+031B -> Ờ
                default: break;
            }
            break;
        case 0x00D3: // Ó
            switch (combining) {
                case 0x031B: return 0x1EDA; // Ó + U+031B -> Ớ
                default: break;
            }
            break;
        case 0x00D4: // Ô
            switch (combining) {
                case 0x0300: return 0x1ED2; // Ô + U+0300 -> Ồ
                case 0x0301: return 0x1ED0; // Ô + U+0301 -> Ố
                case 0x0303: return 0x1ED6; // Ô + U+0303 -> Ỗ
                case 0x0309: return 0x1ED4; // Ô + U+0309 -> Ổ
                case 0x0323: return 0x1ED8; // Ô + U+0323 -> Ộ
                default: break;
            }
            break;
        case 0x00D5: // Õ
            switch (combining) {
                case 0x0301: return 0x1E4C; // Õ + U+0301 -> Ṍ
                case 0x031B: return 0x1EE0; // Õ + U+031B -> Ỡ
                default: break;
            }
            break;
        case 0x00D9: // Ù
            switch (combining) {
                case 0x031B: return 0x1EEA; // Ù + U+031B -> Ừ
                default: break;
            }
            break;
        case 0x00DA: // Ú
            switch (combining) {
                case 0x031B: return 0x1EE8; // Ú + U+031B -> Ứ
                default: break;
            }
            break;
        case 0x00E2: // â
            switch (combining) {
                case 0x0300: return 0x1EA7; // â + U+0300 -> ầ
                case 0x0301: return 0x1EA5; // â + U+0301 -> ấ
                case 0x0303: return 0x1EAB; // â + U+0303 -> ẫ
                case 0x0309: return 0x1EA9; // â + U+0309 -> ẩ
                case 0x0323: return 0x1EAD; // â + U+0323 -> ậ
                default: break;
            }
            break;
        case 0x00EA: // ê
            switch (combining) {
                case 0x0300: return 0x1EC1; // ê + U+0300 -> ề
                case 0x0301: return 0x1EBF; // ê + U+0301 -> ế
                case 0x0303: return 0x1EC5; // ê + U+0303 -> ễ
                case 0x0309: return 0x1EC3; // ê + U+0309 -> ể
                case 0x0323: return 0x1EC7; // ê + U+0323 -> ệ
                default: break;
            }
            break;
        case 0x00F2: // ò
            switch (combining) {
                case 0x031B: return 0x1EDD; // ò + U+031B -> ờ
                default: break;
            }
            break;
        case 0x00F3: // ó
            switch (combining) {
                case 0x031B: return 0x1EDB; // ó + U+031B -> ớ
                default: break;
            }
            break;
        case 0x00F4: // ô
            switch (combining) {
                case 0x0300: return 0x1ED3; // ô + U+0300 -> ồ
                case 0x0301: return 0x1ED1; // ô + U+0301 -> ố
                case 0x0303: return 0x1ED7; // ô + U+0303 -> ỗ
                case 0x0309: return 0x1ED5; // ô + U+0309 -> ổ
                case 0x0323: return 0x1ED9; // ô + U+0323 -> ộ
                default: break;
            }
            break;
        case 0x00F5: // õ
            switch (combining) {
                case 0x0301: return 0x1E4D; // õ + U+0301 -> ṍ
                case 0x031B: return 0x1EE1; // õ + U+031B -> ỡ
                default: break;
            }
            break;
        case 0x00F9: // ù
            switch (combining) {
                case 0x031B: return 0x1EEB; // ù + U+031B -> ừ
                default: break;
            }
            break;
        case 0x00FA: // ú
            switch (combining) {
                case 0x031B: return 0x1EE9; // ú + U+031B -> ứ
                default: break;
            }
            break;
        case 0x0102: // Ă
            switch (combining) {
                case 0x0300: return 0x1EB0; // Ă + U+0300 -> Ằ
                case 0x0301: return 0x1EAE; // Ă + U+0301 -> Ắ
                case 0x0303: return 0x1EB4; // Ă + U+0303 -> Ẵ
                case 0x0309: return 0x1EB2; // Ă + U+0309 -> Ẳ
                case 0x0323: return 0x1EB6; // Ă + U+0323 -> Ặ
                default: break;
            }
            break;
        case 0x0103: // ă
            switch (combining) {
                case 0x0300: return 0x1EB1; // ă + U+0300 -> ằ
                case 0x0301: return 0x1EAF; // ă + U+0301 -> ắ
                case 0x0303: return 0x1EB5; // ă + U+0303 -> ẵ
                case 0x0309: return 0x1EB3; // ă + U+0309 -> ẳ
                case 0x0323: return 0x1EB7; // ă + U+0323 -> ặ
                default: break;
            }
            break;
        case 0x0168: // Ũ
            switch (combining) {
                case 0x0301: return 0x1E78; // Ũ + U+0301 -> Ṹ
                case 0x031B: return 0x1EEE; // Ũ + U+031B -> Ữ
                default: break;
            }
            break;
        case 0x0169: // ũ
            switch (combining) {
                case 0x0301: return 0x1E79; // ũ + U+0301 -> ṹ
                case 0x031B: return 0x1EEF; // ũ + U+031B -> ữ
                default: break;
            }
            break;
        case 0x01A0: // Ơ
            switch (combining) {
                case 0x0300: return 0x1EDC; // Ơ + U+0300 -> Ờ
                case 0x0301: return 0x1EDA; // Ơ + U+0301 -> Ớ
                case 0x0303: return 0x1EE0; // Ơ + U+0303 -> Ỡ
                case 0x0309: return 0x1EDE; // Ơ + U+0309 -> Ở
                case 0x0323: return 0x1EE2; // Ơ + U+0323 -> Ợ
                default: break;
            }
            break;
        case 0x01A1: // ơ
            switch (combining) {
                case 0x0300: return 0x1EDD; // ơ + U+0300 -> ờ
                case 0x0301: return 0x1EDB; // ơ + U+0301 -> ớ
                case 0x0303: return 0x1EE1; // ơ + U+0303 -> ỡ
                case 0x0309: return 0x1EDF; // ơ + U+0309 -> ở
                case 0x0323: return 0x1EE3; // ơ + U+0323 -> ợ
                default: break;
            }
            break;
        case 0x01AF: // Ư
            switch (combining) {
                case 0x0300: return 0x1EEA; // Ư + U+0300 -> Ừ
                case 0x0301: return 0x1EE8; // Ư + U+0301 -> Ứ
                case 0x0303: return 0x1EEE; // Ư + U+0303 -> Ữ
                case 0x0309: return 0x1EEC; // Ư + U+0309 -> Ử
                case 0x0323: return 0x1EF0; // Ư + U+0323 -> Ự
                default: break;
            }
            break;
        case 0x01B0: // ư
            switch (combining) {
                case 0x0300: return 0x1EEB; // ư + U+0300 -> ừ
                case 0x0301: return 0x1EE9; // ư + U+0301 -> ứ
                case 0x0303: return 0x1EEF; // ư + U+0303 -> ữ
                case 0x0309: return 0x1EED; // ư + U+0309 -> ử
                case 0x0323: return 0x1EF1; // ư + U+0323 -> ự
                default: break;
            }
            break;
        case 0x1EA0: // Ạ
            switch (combining) {
                case 0x0302: return 0x1EAC; // Ạ + U+0302 -> Ậ
                case 0x0306: return 0x1EB6; // Ạ + U+0306 -> Ặ
                default: break;
            }
            break;
        case 0x1EA1: // ạ
            switch (combining) {
                case 0x0302: return 0x1EAD; // ạ + U+0302 -> ậ
                case 0x0306: return 0x1EB7; // ạ + U+0306 -> ặ
                default: break;
            }
            break;
        case 0x1EB8: // Ẹ
            switch (combining) {
                case 0x0302: return 0x1EC6; // Ẹ + U+0302 -> Ệ
                default: break;
            }
            break;
        case 0x1EB9: // ẹ
            switch (combining) {
                case 0x0302: return 0x1EC7; // ẹ + U+0302 -> ệ
                default: break;
            }
            break;
        case 0x1ECC: // Ọ
            switch (combining) {
                case 0x0302: return 0x1ED8; // Ọ + U+0302 -> Ộ
                case 0x031B: return 0x1EE2; // Ọ + U+031B -> Ợ
                default: break;
            }
            break;
        case 0x1ECD: // ọ
            switch (combining) {
                case 0x0302: return 0x1ED9; // ọ + U+0302 -> ộ
                case 0x031B: return 0x1EE3; // ọ + U+031B -> ợ
                default: break;
            }
            break;
        case 0x1ECE: // Ỏ
            switch (combining) {
                case 0x031B: return 0x1EDE; // Ỏ + U+031B -> Ở
                default: break;
            }
            break;
        case 0x1ECF: // ỏ
            switch (combining) {
                case 0x031B: return 0x1EDF; // ỏ + U+031B -> ở
                default: break;
            }
            break;
        case 0x1EE4: // Ụ
            switch (combining) {
                case 0x031B: return 0x1EF0; // Ụ + U+031B -> Ự
                default: break;
            }
            break;
        case 0x1EE5: // ụ
            switch (combining) {
                case 0x031B: return 0x1EF1; // ụ + U+031B -> ự
                default: break;
            }
            break;
        case 0x1EE6: // Ủ
            switch (combining) {
                case 0x031B: return 0x1EEC; // Ủ + U+031B -> Ử
                default: break;
            }
            break;
        case 0x1EE7: // ủ
            switch (combining) {
                case 0x031B: return 0x1EED; // ủ + U+031B -> ử
                default: break;
            }
            break;
        default: break;
    }
    return 0;
}

uint32_t nextCodepoint(const char *&text) {
    uint32_t cp = rawNextCodepoint(text);
    if (cp == 0) return 0;

    while (cp >= 0x0300 && cp <= 0x036F) {
        cp = rawNextCodepoint(text);
        if (cp == 0) return 0;
    }

    while (text && *text != 0) {
        const char *peek = text;
        const uint32_t nextCp = rawNextCodepoint(peek);
        if (nextCp >= 0x0300 && nextCp <= 0x036F) {
            text = peek;
            const uint32_t composed = composeVietnamese(cp, nextCp);
            if (composed != 0) {
                cp = composed;
            }
        } else {
            break;
        }
    }
    return cp;
}

DecodedGlyph decodeVietnamese(uint32_t cp) {
    if (cp < 128) return {static_cast<char>(cp), Modifier::None, Tone::None};
    switch (cp) {
        case 0x0110: case 0x0111: return {'D', Modifier::Stroke, Tone::None};
        case 0x00C0: case 0x00E0: return {'A', Modifier::None, Tone::Grave};
        case 0x00C1: case 0x00E1: return {'A', Modifier::None, Tone::Acute};
        case 0x00C2: case 0x00E2: return {'A', Modifier::Circumflex, Tone::None};
        case 0x00C3: case 0x00E3: return {'A', Modifier::None, Tone::Tilde};
        case 0x0102: case 0x0103: return {'A', Modifier::Breve, Tone::None};
        case 0x00C8: case 0x00E8: return {'E', Modifier::None, Tone::Grave};
        case 0x00C9: case 0x00E9: return {'E', Modifier::None, Tone::Acute};
        case 0x00CA: case 0x00EA: return {'E', Modifier::Circumflex, Tone::None};
        case 0x00CC: case 0x00EC: return {'I', Modifier::None, Tone::Grave};
        case 0x00CD: case 0x00ED: return {'I', Modifier::None, Tone::Acute};
        case 0x0128: case 0x0129: return {'I', Modifier::None, Tone::Tilde};
        case 0x00D2: case 0x00F2: return {'O', Modifier::None, Tone::Grave};
        case 0x00D3: case 0x00F3: return {'O', Modifier::None, Tone::Acute};
        case 0x00D4: case 0x00F4: return {'O', Modifier::Circumflex, Tone::None};
        case 0x00D5: case 0x00F5: return {'O', Modifier::None, Tone::Tilde};
        case 0x01A0: case 0x01A1: return {'O', Modifier::Horn, Tone::None};
        case 0x00D9: case 0x00F9: return {'U', Modifier::None, Tone::Grave};
        case 0x00DA: case 0x00FA: return {'U', Modifier::None, Tone::Acute};
        case 0x0168: case 0x0169: return {'U', Modifier::None, Tone::Tilde};
        case 0x01AF: case 0x01B0: return {'U', Modifier::Horn, Tone::None};
        case 0x00DD: case 0x00FD: return {'Y', Modifier::None, Tone::Acute};
        default: break;
    }

    // Vietnamese additions in the Latin Extended Additional block are
    // uppercase/lowercase pairs; masking bit zero normalizes each pair.
    const uint32_t pair = cp & ~1U;
    switch (pair) {
        case 0x1EA0: return {'A',Modifier::None,Tone::Dot};
        case 0x1EA2: return {'A',Modifier::None,Tone::Hook};
        case 0x1EA4: return {'A',Modifier::Circumflex,Tone::Acute};
        case 0x1EA6: return {'A',Modifier::Circumflex,Tone::Grave};
        case 0x1EA8: return {'A',Modifier::Circumflex,Tone::Hook};
        case 0x1EAA: return {'A',Modifier::Circumflex,Tone::Tilde};
        case 0x1EAC: return {'A',Modifier::Circumflex,Tone::Dot};
        case 0x1EAE: return {'A',Modifier::Breve,Tone::Acute};
        case 0x1EB0: return {'A',Modifier::Breve,Tone::Grave};
        case 0x1EB2: return {'A',Modifier::Breve,Tone::Hook};
        case 0x1EB4: return {'A',Modifier::Breve,Tone::Tilde};
        case 0x1EB6: return {'A',Modifier::Breve,Tone::Dot};
        case 0x1EB8: return {'E',Modifier::None,Tone::Dot};
        case 0x1EBA: return {'E',Modifier::None,Tone::Hook};
        case 0x1EBC: return {'E',Modifier::None,Tone::Tilde};
        case 0x1EBE: return {'E',Modifier::Circumflex,Tone::Acute};
        case 0x1EC0: return {'E',Modifier::Circumflex,Tone::Grave};
        case 0x1EC2: return {'E',Modifier::Circumflex,Tone::Hook};
        case 0x1EC4: return {'E',Modifier::Circumflex,Tone::Tilde};
        case 0x1EC6: return {'E',Modifier::Circumflex,Tone::Dot};
        case 0x1EC8: return {'I',Modifier::None,Tone::Hook};
        case 0x1ECA: return {'I',Modifier::None,Tone::Dot};
        case 0x1ECC: return {'O',Modifier::None,Tone::Dot};
        case 0x1ECE: return {'O',Modifier::None,Tone::Hook};
        case 0x1ED0: return {'O',Modifier::Circumflex,Tone::Acute};
        case 0x1ED2: return {'O',Modifier::Circumflex,Tone::Grave};
        case 0x1ED4: return {'O',Modifier::Circumflex,Tone::Hook};
        case 0x1ED6: return {'O',Modifier::Circumflex,Tone::Tilde};
        case 0x1ED8: return {'O',Modifier::Circumflex,Tone::Dot};
        case 0x1EDA: return {'O',Modifier::Horn,Tone::Acute};
        case 0x1EDC: return {'O',Modifier::Horn,Tone::Grave};
        case 0x1EDE: return {'O',Modifier::Horn,Tone::Hook};
        case 0x1EE0: return {'O',Modifier::Horn,Tone::Tilde};
        case 0x1EE2: return {'O',Modifier::Horn,Tone::Dot};
        case 0x1EE4: return {'U',Modifier::None,Tone::Dot};
        case 0x1EE6: return {'U',Modifier::None,Tone::Hook};
        case 0x1EE8: return {'U',Modifier::Horn,Tone::Acute};
        case 0x1EEA: return {'U',Modifier::Horn,Tone::Grave};
        case 0x1EEC: return {'U',Modifier::Horn,Tone::Hook};
        case 0x1EEE: return {'U',Modifier::Horn,Tone::Tilde};
        case 0x1EF0: return {'U',Modifier::Horn,Tone::Dot};
        case 0x1EF2: return {'Y',Modifier::None,Tone::Grave};
        case 0x1EF4: return {'Y',Modifier::None,Tone::Dot};
        case 0x1EF6: return {'Y',Modifier::None,Tone::Hook};
        case 0x1EF8: return {'Y',Modifier::None,Tone::Tilde};
        default: return {'?',Modifier::None,Tone::None};
    }
}

int countCodepoints(const char *text) {
    int count = 0;
    while (text && *text) { nextCodepoint(text); ++count; }
    return count;
}

void drawGlyph(Canvas &canvas, int x, int y, DecodedGlyph glyph, uint16_t color, int scale) {
    const auto &columns = patternFor(glyph.base);
    // Keep a separate row for a base modifier and for the tone mark so
    // combined Vietnamese diacritics remain distinguishable.
    const int baseY = y + 3 * scale;
    for (int col = 0; col < 5; ++col) {
        for (int row = 0; row < 7; ++row) {
            if ((columns[col] & (1U << row)) != 0)
                canvas.fillRect(x + col * scale, baseY + row * scale, scale, scale, color);
        }
    }
    if (glyph.modifier == Modifier::Stroke)
        canvas.line(x, baseY + 3 * scale, x + 4 * scale, baseY + 3 * scale, color, scale);
    else if (glyph.modifier == Modifier::Circumflex) {
        canvas.line(x + scale, y + scale, x + 2 * scale, y, color, scale);
        canvas.line(x + 2 * scale, y, x + 3 * scale, y + scale, color, scale);
    } else if (glyph.modifier == Modifier::Breve) {
        canvas.pixel(x + scale, y, color); canvas.pixel(x + 2 * scale, y + scale, color);
        canvas.pixel(x + 3 * scale, y, color);
    } else if (glyph.modifier == Modifier::Horn) {
        canvas.line(x + 4 * scale, baseY, x + 5 * scale, y, color, scale);
    }
    switch (glyph.tone) {
        case Tone::Acute: canvas.line(x + 2 * scale, y + scale, x + 3 * scale, y, color, scale); break;
        case Tone::Grave: canvas.line(x + scale, y, x + 2 * scale, y + scale, color, scale); break;
        case Tone::Hook: canvas.line(x + 2 * scale, y, x + 3 * scale, y, color, scale); canvas.pixel(x + 2 * scale, y + scale, color); break;
        case Tone::Tilde: canvas.pixel(x + scale, y, color); canvas.pixel(x + 2 * scale, y + scale, color); canvas.pixel(x + 3 * scale, y, color); break;
        case Tone::Dot: canvas.fillRect(x + 2 * scale, baseY + 8 * scale, scale, scale, color); break;
        case Tone::None: break;
    }
}
}  // namespace

Canvas::Canvas(uint16_t *pixels, int backingWidth, int backingHeight,
               int logicalWidth, int logicalHeight)
    : pixels_(pixels), backingWidth_(backingWidth), backingHeight_(backingHeight),
      width_(logicalWidth > 0 ? logicalWidth : backingWidth),
      height_(logicalHeight > 0 ? logicalHeight : backingHeight) {}

void Canvas::clear(uint16_t color) {
    std::fill(pixels_, pixels_ + backingWidth_ * backingHeight_, color);
}

void Canvas::pixel(int x, int y, uint16_t color) {
    x += translationX_;
    y += translationY_;
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    const int left = x * backingWidth_ / width_;
    const int right = (x + 1) * backingWidth_ / width_;
    const int top = y * backingHeight_ / height_;
    const int bottom = (y + 1) * backingHeight_ / height_;
    for (int row = top; row < bottom; ++row)
        std::fill(pixels_ + row * backingWidth_ + left,
                  pixels_ + row * backingWidth_ + right, color);
}

void Canvas::fillRect(int x, int y, int width, int height, uint16_t color) {
    x += translationX_;
    y += translationY_;
    const int logicalLeft = std::max(0, x), logicalTop = std::max(0, y);
    const int logicalRight = std::min(width_, x + width);
    const int logicalBottom = std::min(height_, y + height);
    if (logicalRight <= logicalLeft || logicalBottom <= logicalTop) return;
    const int left = logicalLeft * backingWidth_ / width_;
    const int right = logicalRight * backingWidth_ / width_;
    const int top = logicalTop * backingHeight_ / height_;
    const int bottom = logicalBottom * backingHeight_ / height_;
    for (int row = top; row < bottom; ++row)
        std::fill(pixels_ + row * backingWidth_ + left,
                  pixels_ + row * backingWidth_ + right, color);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint16_t color, int thickness) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        fillRect(x0 - thickness / 2, y0 - thickness / 2, thickness, thickness, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

void Canvas::circle(int cx, int cy, int radius, uint16_t color, int thickness) {
    for (int r = std::max(0, radius - thickness + 1); r <= radius; ++r) {
        int x = r, y = 0, error = 1 - r;
        while (x >= y) {
            pixel(cx+x,cy+y,color); pixel(cx+y,cy+x,color); pixel(cx-y,cy+x,color); pixel(cx-x,cy+y,color);
            pixel(cx-x,cy-y,color); pixel(cx-y,cy-x,color); pixel(cx+y,cy-x,color); pixel(cx+x,cy-y,color);
            ++y; if (error < 0) error += 2*y + 1; else { --x; error += 2*(y-x) + 1; }
        }
    }
}

void Canvas::fillCircle(int cx, int cy, int radius, uint16_t color) {
    for (int y = -radius; y <= radius; ++y) {
        int x = 0;
        while ((x + 1) * (x + 1) + y * y <= radius * radius) ++x;
        fillRect(cx - x, cy + y, 2 * x + 1, 1, color);
    }
}

void Canvas::triangle(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t color) {
    line(x0,y0,x1,y1,color); line(x1,y1,x2,y2,color); line(x2,y2,x0,y0,color);
}

void Canvas::alphaPixel(int x, int y, uint16_t color, uint8_t alpha) {
    x += translationX_;
    y += translationY_;
    if (alpha == 0 || x < 0 || y < 0 || x >= width_ || y >= height_) return;
    const int left = x * backingWidth_ / width_;
    const int right = (x + 1) * backingWidth_ / width_;
    const int top = y * backingHeight_ / height_;
    const int bottom = (y + 1) * backingHeight_ / height_;
    for (int row = top; row < bottom; ++row) {
        for (int column = left; column < right; ++column) {
            uint16_t &destination = pixels_[row * backingWidth_ + column];
            if (alpha == 255) {
                destination = color;
                continue;
            }
            const uint32_t inverse = 255U - alpha;
            const uint32_t red = ((((color >> 11U) & 0x1FU) * alpha) +
                                  (((destination >> 11U) & 0x1FU) * inverse) + 127U) / 255U;
            const uint32_t green = ((((color >> 5U) & 0x3FU) * alpha) +
                                    (((destination >> 5U) & 0x3FU) * inverse) + 127U) / 255U;
            const uint32_t blue = (((color & 0x1FU) * alpha) +
                                   ((destination & 0x1FU) * inverse) + 127U) / 255U;
            destination = static_cast<uint16_t>((red << 11U) | (green << 5U) | blue);
        }
    }
}

void Canvas::alphaMask(int x, int y, const assets::AlphaMask &mask, uint16_t color) {
    if (!mask.alpha) return;
    for (uint16_t row = 0; row < mask.height; ++row) {
        for (uint16_t column = 0; column < mask.width; ++column) {
            const std::size_t index = static_cast<std::size_t>(row) * mask.width + column;
            alphaPixel(x + column, y + row, color, mask.alpha[index]);
        }
    }
}

void Canvas::colorBitmap(int x, int y, const assets::ColorBitmap &bitmap) {
    if (!bitmap.pixels || !bitmap.alpha) return;
    for (uint16_t row = 0; row < bitmap.height; ++row) {
        for (uint16_t column = 0; column < bitmap.width; ++column) {
            const std::size_t index = static_cast<std::size_t>(row) * bitmap.width + column;
            alphaPixel(x + column, y + row, bitmap.pixels[index], bitmap.alpha[index]);
        }
    }
}

void Canvas::colorBitmapScaled(int x, int y, const assets::ColorBitmap &bitmap, int destWidth, int destHeight) {
    if (!bitmap.pixels || !bitmap.alpha || destWidth <= 0 || destHeight <= 0) return;
    for (int row = 0; row < destHeight; ++row) {
        const int sourceRow = row * bitmap.height / destHeight;
        for (int column = 0; column < destWidth; ++column) {
            const int sourceColumn = column * bitmap.width / destWidth;
            const std::size_t index = static_cast<std::size_t>(sourceRow) * bitmap.width + sourceColumn;
            alphaPixel(x + column, y + row, bitmap.pixels[index], bitmap.alpha[index]);
        }
    }
}

void Canvas::colorBitmapScaled(int x, int y, const assets::ColorBitmap &bitmap, int size) {
    colorBitmapScaled(x, y, bitmap, size, size);
}

namespace {
const assets::FontGlyph *fontGlyph(const assets::BitmapFont &font, uint32_t codepoint) {
    std::size_t first = 0;
    std::size_t count = font.glyphCount;
    while (count > 0) {
        const std::size_t step = count / 2;
        const std::size_t index = first + step;
        if (font.glyphs[index].codepoint < codepoint) {
            first = index + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    if (first < font.glyphCount && font.glyphs[first].codepoint == codepoint)
        return &font.glyphs[first];
    if (codepoint != static_cast<uint32_t>('?') && codepoint != static_cast<uint32_t>('.'))
        return fontGlyph(font, '?');
    return nullptr;
}

int codepointWidth(const assets::BitmapFont &font, uint32_t codepoint) {
    const assets::FontGlyph *glyph = fontGlyph(font, codepoint);
    return glyph ? glyph->advance : 0;
}
}  // namespace

int Canvas::fontTextWidth(const char *utf8, const assets::BitmapFont &font) const {
    if (!utf8) return 0;
    int width = 0;
    const char *cursor = utf8;
    while (*cursor) width += codepointWidth(font, nextCodepoint(cursor));
    return width;
}

void Canvas::fontText(int x, int y, const char *utf8, const assets::BitmapFont &font,
                      uint16_t color, int maxWidth, bool centered) {
    if (!utf8 || !font.glyphs || !font.bitmap4bpp) return;
    std::array<uint32_t, 64> codepoints{};
    std::size_t count = 0;
    const char *cursor = utf8;
    while (*cursor && count < codepoints.size()) codepoints[count++] = nextCodepoint(cursor);

    std::size_t visible = count;
    bool ellipsis = false;
    int drawnWidth = 0;
    for (std::size_t index = 0; index < count; ++index)
        drawnWidth += codepointWidth(font, codepoints[index]);
    const assets::FontGlyph *dotGlyph = fontGlyph(font, '.');
    const bool hasDot = (dotGlyph != nullptr && dotGlyph->codepoint == '.');
    if (maxWidth > 0 && drawnWidth > maxWidth && hasDot) {
        ellipsis = true;
        const int dotsWidth = 3 * dotGlyph->advance;
        visible = 0;
        drawnWidth = dotsWidth;
        while (visible < count) {
            const int next = codepointWidth(font, codepoints[visible]);
            if (drawnWidth + next > maxWidth) break;
            drawnWidth += next;
            ++visible;
        }
    }
    if (centered && maxWidth > 0) x += (maxWidth - drawnWidth) / 2;

    auto drawCodepoint = [&](uint32_t codepoint) {
        const assets::FontGlyph *glyph = fontGlyph(font, codepoint);
        if (!glyph) return;
        const std::size_t pixelCount = static_cast<std::size_t>(glyph->width) * glyph->height;
        for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
            const uint8_t packed = font.bitmap4bpp[glyph->bitmapOffset + pixelIndex / 2U];
            const uint8_t nibble = (pixelIndex & 1U) == 0 ? packed >> 4U : packed & 0x0FU;
            if (nibble != 0) {
                const int column = static_cast<int>(pixelIndex % glyph->width);
                const int row = static_cast<int>(pixelIndex / glyph->width);
                alphaPixel(x + glyph->xOffset + column, y + glyph->yOffset + row,
                           color, static_cast<uint8_t>(nibble * 17U));
            }
        }
        x += glyph->advance;
    };

    for (std::size_t index = 0; index < visible; ++index) drawCodepoint(codepoints[index]);
    if (ellipsis) for (int index = 0; index < 3; ++index) drawCodepoint('.');
}

void Canvas::fontTextScaled(int x, int y, const char *utf8, const assets::BitmapFont &font,
                            uint16_t color, float scale, int maxWidth, bool centered) {
    if (!utf8 || !font.glyphs || !font.bitmap4bpp || scale <= 0.0f) return;
    if (scale == 1.0f) {
        fontText(x, y, utf8, font, color, maxWidth, centered);
        return;
    }

    std::array<uint32_t, 64> codepoints{};
    std::size_t count = 0;
    const char *cursor = utf8;
    while (*cursor && count < codepoints.size()) codepoints[count++] = nextCodepoint(cursor);

    int unscaledWidth = 0;
    for (std::size_t index = 0; index < count; ++index)
        unscaledWidth += codepointWidth(font, codepoints[index]);
    int drawnWidth = static_cast<int>(std::round(unscaledWidth * scale));

    if (centered && maxWidth > 0) x += (maxWidth - drawnWidth) / 2;

    auto drawCodepointScaled = [&](uint32_t codepoint) {
        const assets::FontGlyph *glyph = fontGlyph(font, codepoint);
        if (!glyph) return;
        const int destW = std::max(1, static_cast<int>(std::round(glyph->width * scale)));
        const int destH = std::max(1, static_cast<int>(std::round(glyph->height * scale)));
        const int glyphX = x + static_cast<int>(std::round(glyph->xOffset * scale));
        const int glyphY = y + static_cast<int>(std::round(glyph->yOffset * scale));

        for (int r = 0; r < destH; ++r) {
            const int srcRow = std::min<int>(glyph->height - 1, static_cast<int>(r / scale));
            for (int c = 0; c < destW; ++c) {
                const int srcCol = std::min<int>(glyph->width - 1, static_cast<int>(c / scale));
                const std::size_t pixelIndex = static_cast<std::size_t>(srcRow) * glyph->width + srcCol;
                const uint8_t packed = font.bitmap4bpp[glyph->bitmapOffset + pixelIndex / 2U];
                const uint8_t nibble = (pixelIndex & 1U) == 0 ? packed >> 4U : packed & 0x0FU;
                if (nibble != 0) {
                    alphaPixel(glyphX + c, glyphY + r, color, static_cast<uint8_t>(nibble * 17U));
                }
            }
        }
        x += static_cast<int>(std::round(glyph->advance * scale));
    };

    for (std::size_t index = 0; index < count; ++index) drawCodepointScaled(codepoints[index]);
}

int Canvas::textWidth(const char *utf8, int scale, int maxCells) const {
    int cells = countCodepoints(utf8);
    if (maxCells >= 0) cells = std::min(cells, maxCells);
    return cells == 0 ? 0 : cells * 6 * scale - scale;
}

void Canvas::text(int x, int y, const char *utf8, uint16_t color, int scale, int maxWidth, bool centered) {
    if (!utf8 || scale <= 0) return;
    const int cellWidth = 6 * scale;
    int maxCells = maxWidth > 0 ? std::max(1, (maxWidth + scale) / cellWidth) : countCodepoints(utf8);
    const int total = countCodepoints(utf8);
    const bool ellipsis = total > maxCells && maxCells >= 3;
    const int drawnCells = std::min(total, maxCells);
    if (centered) x += (maxWidth - (drawnCells * cellWidth - scale)) / 2;

    const char *cursor = utf8;
    for (int index = 0; index < drawnCells; ++index) {
        DecodedGlyph glyph;
        if (ellipsis && index >= drawnCells - 3) glyph = {'.', Modifier::None, Tone::None};
        else glyph = decodeVietnamese(nextCodepoint(cursor));
        drawGlyph(*this, x + index * cellWidth, y, glyph, color, scale);
    }
}

}  // namespace waze_hud
