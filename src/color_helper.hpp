// ColorHelpers.hpp
#pragma once
#include <string>
#include <string_view>
#include <cstdint>

namespace color
{
    enum class Color : uint8_t {
        Default = 0,
        Red, Green, Blue, Yellow, Cyan, Magenta,
        Orange, Purple, Teal, Lime, Pink, Indigo, Amber, Slate, Gray, Brown, White
    };

    inline auto color_to_hex = [](Color c) -> const char*
        {
            switch (c)
            {
                case Color::White:   return "#F1F0F2";
                case Color::Red:     return "#D53E3E";
                case Color::Green:   return "#16A34A";
                case Color::Blue:    return "#0EA5E9";
                case Color::Yellow:  return "#FACC15";
                case Color::Cyan:    return "#06B6D4";
                case Color::Magenta: return "#C026D3";
                case Color::Orange:  return "#EA580C";
                case Color::Purple:  return "#7C3AED";
                case Color::Teal:    return "#14B8A6";
                case Color::Lime:    return "#65A30D";
                case Color::Pink:    return "#EC4899";
                case Color::Indigo:  return "#6366F1";
                case Color::Amber:   return "#F59E0B";
                case Color::Slate:   return "#64748B";
                case Color::Gray:    return "#9CA3AF";
                case Color::Brown:   return "#92400E";
                default:             return "#AAAAAA";
            }
        };

    static inline bool parseHexRGB(std::string_view s, uint8_t& R, uint8_t& G, uint8_t& B, uint8_t& A) {
        if (s.size() != 7 && s.size() != 9) return false;
        if (s[0] != '#') return false;
        auto hex = [](char c)->int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
            };
        auto rd2 = [&](int i)->int { int a = hex(s[i]), b = hex(s[i + 1]); return (a < 0 || b < 0) ? -1 : ((a << 4) | b); };
        int r = rd2(1), g = rd2(3), b = rd2(5); if (r < 0 || g < 0 || b < 0) return false;
        int a = 255; if (s.size() == 9) { a = rd2(7); if (a < 0) return false; }
        R = (uint8_t)r; G = (uint8_t)g; B = (uint8_t)b; A = (uint8_t)a; return true;
    }

    /**
     * - colorHex : "#RRGGBB"
     * - fallbackKey : optionnal, only if colorHex est invalid/missing
     */
    static inline uint32_t getColorU32(const std::string& colorHex)
    {
        uint8_t r, g, b, a;
        if (parseHexRGB(colorHex, r, g, b, a))
        {
            return IM_COL32(r, g, b, a);
        }
        return IM_COL32(170, 170, 170, 255);
    }
    /**
     * - color : "Color::Red"
     */
    static inline uint32_t getColorU32(Color co)
    {
        uint8_t r, g, b, a;
        auto hex = color_to_hex(co);
        if (parseHexRGB(hex, r, g, b, a))
        {
            return IM_COL32(r, g, b, a);
        }
        return IM_COL32(170, 170, 170, 255);
    }

    static inline ImU32 AdjustRGB(ImU32 col, int d)
    {
        int r = (col) & 0xFF, g = (col >> 8) & 0xFF, b = (col >> 16) & 0xFF, a = (col >> 24) & 0xFF;
        r = std::clamp(r + d, 0, 255); g = std::clamp(g + d, 0, 255); b = std::clamp(b + d, 0, 255);
        return IM_COL32(r, g, b, a);
    }

    static inline ImU32 AlphaMul(ImU32 c, float a)
    {
        const int r = (c >> 0) & 255;
        const int g = (c >> 8) & 255;
        const int b = (c >> 16) & 255;
        int A = int(((c >> 24) & 255) * a);
        if (A > 255) A = 255; if (A < 0) A = 0;
        return IM_COL32(r, g, b, A);
    }

    static inline ImU32 LerpImU32(ImU32 a, ImU32 b, float t)
    {
        auto ch = [&](int sh) { int A = (a >> sh) & 255, B = (b >> sh) & 255; return (int)(A + (B - A) * t); };
        return IM_COL32(ch(0), ch(8), ch(16), ch(24));
    }

    static inline ImU32 Lighten(ImU32 c, int delta = 40, int alpha = 255)
    {
        int r = std::min(255, (int)((c >> 0) & 255) + delta);
        int g = std::min(255, (int)((c >> 8) & 255) + delta);
        int b = std::min(255, (int)((c >> 16) & 255) + delta);
        return IM_COL32(r, g, b, alpha);
    }
}