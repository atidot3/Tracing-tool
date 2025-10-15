#pragma once

#include <imgui.h>

#include <cmath>
#include <string>

inline std::string fmtTime(double us)
{
    // safe entry
    if (!std::isfinite(us)) us = 0.0;
    if (us < 0.0) us = 0.0;

    char buf[64];

    // < 1 ms  -> µs
    if (us < 1e3)
    {
        // 0 to 3 decimales max
        std::snprintf(buf, sizeof(buf), "%.0f us", us);
        return buf;
    }

    // < 1 s -> ms (up to 3 decimales)
    if (us < 1e6)
    {
        double ms = us / 1e3;
        // keep 0–3 decimales from magnitude
        if (ms >= 100.0) std::snprintf(buf, sizeof(buf), "%.0f ms", ms);
        else if (ms >= 10.0)  std::snprintf(buf, sizeof(buf), "%.1f ms", ms);
        else                  std::snprintf(buf, sizeof(buf), "%.3f ms", ms);
        return buf;
    }

    // < 60 s -> secondes (up to 3 decimales)
    if (us < 60.0 * 1e6)
    {
        double s = us / 1e6;
        if (s >= 10.0) std::snprintf(buf, sizeof(buf), "%.2f s", s);
        else                std::snprintf(buf, sizeof(buf), "%.3f s", s);
        return buf;
    }

    // < 1 h -> mm:ss.mmm
    if (us < 3600.0 * 1e6)
    {
        uint64_t total_ms = static_cast<uint64_t>(std::llround(us / 1e3));
        uint64_t mm = total_ms / 60000;
        uint64_t ss = (total_ms / 1000) % 60;
        uint64_t ms = total_ms % 1000;
        std::snprintf(buf, sizeof(buf), "%02llu:%02llu.%03llu",
            (unsigned long long)mm,
            (unsigned long long)ss,
            (unsigned long long)ms);
        return buf;
    }

    // >= 1 h -> hh:mm:ss
    {
        uint64_t total_s = static_cast<uint64_t>(std::llround(us / 1e6));
        uint64_t hh = total_s / 3600;
        uint64_t mm = (total_s / 60) % 60;
        uint64_t ss = total_s % 60;
        std::snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu",
            (unsigned long long)hh,
            (unsigned long long)mm,
            (unsigned long long)ss);
        return buf;
    }
}

inline std::string elideToWidth(const std::string& s, float maxPx)
{
    if (maxPx <= 0.f || s.empty()) return {};
    if (ImGui::CalcTextSize(s.c_str()).x <= maxPx) return s;
    static constexpr const char* dots = "...";
    float wd = ImGui::CalcTextSize(dots).x;
    if (wd >= maxPx) return {};
    int lo = 0, hi = int(s.size());
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        std::string st = s.substr(0, mid) + dots;
        if (ImGui::CalcTextSize(st.c_str()).x <= maxPx) lo = mid; else hi = mid - 1;
    }
    return s.substr(0, lo) + dots;
}

inline double nice_step_us(double rangeUs, int targetTicks)
{
    if (rangeUs <= 0) return 1.0;
    double rough = rangeUs / std::max(1, targetTicks);
    double p10 = std::pow(10.0, std::floor(std::log10(rough)));
    double r = rough / p10;
    double s = (r < 1.5 ? 1.0 : (r < 3.5 ? 2.0 : (r < 7.5 ? 5.0 : 10.0)));
    return s * p10;
}

// x screen from absolute time (µs)
inline float xFromAbsUs(double absUs, const ImVec2& canvasMin, float leftPad, float contentW, double normStart, double normEnd, unsigned long long timeMin, unsigned long long timeMax)
{
    const double totalUs = std::max(1.0, double(timeMax - timeMin));
    // normalised [0..1]
    const double tn = (absUs - double(timeMin)) / totalUs;
    // width visible as N
    const double zoomFactor = 1.0 / std::max(1e-12, (normEnd - normStart));
    // [0..1] window size
    const double nx = (tn - normStart) * zoomFactor;
    return canvasMin.x + leftPad + float(nx * contentW);
}