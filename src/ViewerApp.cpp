#include "ViewerApp.hpp"
#include "parser.hpp"
#include "color_helper.hpp"
#include "utils.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <regex>

// === Local helpers (performance & dedup) =====================================
namespace
{
    /// Compute time bounds (min start, max end) in a single pass.
    /// Falls back to [0,1] when the input is empty.
    template <class Range>
    inline std::pair<std::uint64_t, std::uint64_t> computeTimeBounds(const Range& rng) noexcept
    {
        std::uint64_t tmin = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t tmax = 0;
        for (const auto& e : rng)
        {
            if (e.ts < tmin) tmin = e.ts;
            const auto end = e.ts + e.dur;
            if (end > tmax) tmax = end;
        }
        if (tmin == std::numeric_limits<std::uint64_t>::max())
            return {0ull, 1ull};
        if (tmax <= tmin) {
            // avoid degenerate span
            return {tmin, tmin + 1ull};
        }
        return {tmin, tmax};
    }

    /// Normalize [ts, ts+dur] to [0,1] range given absolute [tmin, tmax].
    /// Precomputes inverse denominator to avoid divisions in loops.
    template <class Range>
    inline void normalizeEvents(Range& rng, std::uint64_t tmin, std::uint64_t tmax) noexcept {
        const double denom = static_cast<double>(tmax - tmin);
        const double invDen = denom > 0.0 ? (1.0 / denom) : 1.0;
        for (auto& e : rng) {
            const double s = (static_cast<double>(e.ts) - static_cast<double>(tmin)) * invDen;
            const double en = (static_cast<double>(e.ts + e.dur) - static_cast<double>(tmin)) * invDen;
            e.normStart = std::clamp(s, 0.0, 1.0);
            e.normEnd = std::clamp(en, 0.0, 1.0);
        }
    }

    /// Normalize events starting at a given index (partial refresh path).
    template <class Vec>
    inline void normalizeEventsFrom(Vec& v, std::size_t beginIdx, std::uint64_t tmin, std::uint64_t tmax) noexcept {
        if (beginIdx >= v.size()) return;
        const double denom = static_cast<double>(tmax - tmin);
        const double invDen = denom > 0.0 ? (1.0 / denom) : 1.0;
        for (std::size_t i = beginIdx; i < v.size(); ++i) {
            auto& e = v[i];
            const double s = (static_cast<double>(e.ts) - static_cast<double>(tmin)) * invDen;
            const double en = (static_cast<double>(e.ts + e.dur) - static_cast<double>(tmin)) * invDen;
            e.normStart = std::clamp(s, 0.0, 1.0);
            e.normEnd = std::clamp(en, 0.0, 1.0);
        }
    }
} // namespace


// ---------- Data filter ----------
void ViewerApp::compileDataFilterIfNeeded()
{
    const char* patt = _dataFilter;
    const bool cs = _dataFilterCaseSensitive;
    const bool rx = _dataFilterRegex;

    // Recompile only if the UI state changed
    if (_filter_cached != patt || _filter_case_cached != cs || _filter_regex_cached != rx) {
        _filter_cached = patt;
        _filter_case_cached = cs;
        _filter_regex_cached = rx;
        _compiledFilter.compile(_filter_cached, cs, rx);
    }
}

bool ViewerApp::passDataFilter(const Event& e)
{
    if (_dataFilter[0] == '\0') return true;
    compileDataFilterIfNeeded();

    if (_dataFilterRegex)
    {
        try
        {
            std::regex::flag_type flags = std::regex::ECMAScript;
            if (!_dataFilterCaseSensitive) flags = (std::regex::flag_type)(flags | std::regex::icase);
            std::regex re(_dataFilter, flags);
            return std::regex_search(e.data, re);
        }
        catch (...) { return true; }
    }
    else
    {
        if (_dataFilterCaseSensitive) return e.data.find(_dataFilter) != std::string::npos;
        return contains_icase_ascii(e.data, _dataFilter);
    }
}

// ---------- ViewerApp ----------
ViewerApp::ViewerApp()
    : _events{}, _globalStats{}, _metrics{}
    , _mtxMetrics{}
    , _timeMin{ 0 }, _timeMax{ 1 }
    , _view{ AppView::Startup }
    , _client{ 9000, 9010, 1000 }
    , _connectView{ _client }
    , _vp{}
    , _selected{ nullptr }
    , _dur_min_us{ 0 }
    , _parsing{ false }
    , _parsedCount{ 0 }
    , _filepath{ "trace.json" }, _lastError{ }
    , _autoReload{ true }
    , _autoReloadInterval{ 1.0f }, _autoReloadTimer{ 0.0 }
    , _fileMTime{}
    , _mtx{}, _anim{}, _absRuler{}, _selectedPanel{}
    , _dataFilter{ 0 }
    , _dataFilterCaseSensitive{ false }, _dataFilterRegex{ false }
    , _filteredVisible{ 0 }
{
}
ViewerApp::~ViewerApp() {}

bool ViewerApp::loadFile(const char* path, uint64_t durMinUs)
{
    if (!path || !*path) return false;

    _parsing = true;
    std::vector<Event> newEvents;
    std::unordered_map<std::string, EventStats> newStats;
    std::string err;
    std::string data;
    if (!read_file(path, data))
    {
        _lastError = err.empty() ? "Failed to open file" : err;
        _parsing = false;
        return false;
    }
    if (!parse_trace_payload(data, newEvents, newStats, _metrics, durMinUs, &err))
    {
        _lastError = err.empty() ? "Failed to parse file" : err;
        _parsing = false;
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(_mtx);
        _events.swap(newEvents);
        _globalStats.swap(newStats);

        auto [tmin, tmax] = computeTimeBounds(_events);
        _timeMin = tmin; _timeMax = tmax;


        normalizeEvents(_events, _timeMin, _timeMax);


        std::sort(_metrics.begin(), _metrics.end(), [](const Metric& a, const Metric& b) { return a.ts < b.ts; });

        _vp.zoom = 1.f; _vp.offset = 0.0; _vp.panY = 0.f;
        _selected = nullptr;
        _parsedCount = _events.size();
    }
    _parsing = false; _lastError.clear();
    std::snprintf(_filepath, sizeof(_filepath), "%s", path);

    if (std::filesystem::exists(path)) _fileMTime = std::filesystem::last_write_time(path);
    return true;
}

bool ViewerApp::reloadFilePreserveView(uint64_t durMinUs) {
    if (_filepath[0] == '\0') return false;
    std::vector<Event> tmp; std::unordered_map<std::string, EventStats> tmpStats; std::string err;
    if (!parse_trace_payload(_filepath, tmp, tmpStats, _metrics, durMinUs, &err)) { _lastError = err; return false; }

    auto keep = _vp;
    {
        std::lock_guard<std::mutex> lk(_mtx);
        _events.swap(tmp);
        _globalStats.swap(tmpStats);

        auto [tmin, tmax] = computeTimeBounds(_events);
        _timeMin = tmin; _timeMax = tmax;


        normalizeEvents(_events, _timeMin, _timeMax);


        std::sort(_metrics.begin(), _metrics.end(), [](const Metric& a, const Metric& b) { return a.ts < b.ts; });

        _parsedCount = _events.size();
    }
    _vp = keep; _lastError.clear();

    if (std::filesystem::exists(_filepath)) _fileMTime = std::filesystem::last_write_time(_filepath);
    return true;
}

bool ViewerApp::getFileMTime(const char* path, std::filesystem::file_time_type& out) const {
    try { out = std::filesystem::last_write_time(path); return true; }
    catch (...) { return false; }
}

void ViewerApp::updateAutoReload(const char* path) {
    if (!path || !*path) return;
    std::filesystem::file_time_type cur;
    if (!getFileMTime(path, cur)) return;
    if (_fileMTime.time_since_epoch().count() == 0) { _fileMTime = cur; return; }
    if (cur != _fileMTime) reloadFilePreserveView((uint64_t)std::max(0, _dur_min_us));
}

// tiny draw helpers
void ViewerApp::drawEventBox(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, ImU32 color, bool hovered, bool selected)
{
    ImU32 fill = color;
    if (hovered)   fill = color::AdjustRGB(color, +20);
    if (selected)  fill = IM_COL32(255, 255, 255, 40);
    dl->AddRectFilled(p1, p2, fill, 5.0f);
    dl->AddRect(p1, p2, IM_COL32(0, 0, 0, 140), 5.0f, 0, 1.0f);
}

void ViewerApp::drawTopBottomAccent(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, ImU32 topColor, ImU32 bottomColor)
{
    dl->AddRectFilled(ImVec2(p1.x, p1.y), ImVec2(p2.x, p1.y + 2.f), topColor);
    dl->AddRectFilled(ImVec2(p1.x, p2.y - 2.f), ImVec2(p2.x, p2.y), bottomColor);
}

void ViewerApp::drawCenteredLabel(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, const char* text, ImU32 color)
{
    float textHeight = ImGui::GetTextLineHeight();
    float textWidth = ImGui::CalcTextSize(text).x;
    if (p2.x - p1.x <= 8.f) return;
    ImVec2 pos;
    pos.x = p1.x + (p2.x - p1.x - textWidth) * 0.5f;
    pos.y = p1.y + (p2.y - p1.y - textHeight) * 0.5f;
    dl->AddText(pos, color, text);
}

// =============== metrics bottom (CPU/RAM) ===============
void ViewerApp::drawMetricsBottom(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax, float leftPad, float contentW, float startY, double normStart, double normEnd)
{
    if (_metrics.empty()) return;

    constexpr float kTrackH = 78.f;
    constexpr float kGap = 8.f;
    constexpr float kPtR = 1.25f;
    constexpr float kThick = 1.5f;
    constexpr ImU32 kCpuProc = IM_COL32(70, 205, 255, 240); // process line
    constexpr ImU32 kCpuTot = IM_COL32(230, 80, 80, 240);  // total line
    constexpr ImU32 kRamCol = IM_COL32(255, 190, 60, 230);
    constexpr ImU32 kGridCol = IM_COL32(130, 140, 150, 60);
    constexpr ImU32 kBoxCol = IM_COL32(20, 32, 38, 190);
    constexpr ImU32 kTextCol = IM_COL32(200, 200, 200, 220);

    const double totalUs = std::max(1.0, double(_timeMax - _timeMin));
    const double visStart = totalUs * normStart + double(_timeMin);
    const double visEnd = totalUs * normEnd + double(_timeMin);
    const double spanUs = std::max(1.0, visEnd - visStart);

    auto xx = [&](double absUs)->float {
        return xFromAbsUs(absUs, canvasMin, leftPad, contentW, normStart, normEnd, _timeMin, _timeMax);
        };
    auto drawGridX = [&](float yTop, float yH) {
        const double tickUs = nice_step_us(spanUs, 8);
        const double first = std::floor(visStart / tickUs) * tickUs;
        for (double t = first; t <= visEnd + 0.5 * tickUs; t += tickUs) {
            float x = xx(t);
            dl->AddLine(ImVec2(x, yTop), ImVec2(x, yTop + yH), kGridCol);
        }
        };
    auto labelX = [&](float textW) {
        float x = (canvasMin.x + leftPad) - 6.0f - textW;
        float left = canvasMin.x + 8.0f;
        return (x < left) ? left : x;
        };
    // Interpolation générique d’une série m->field
    auto sampleAt = [&](double t, auto getter)->double {
        if (_metrics.empty()) return 0.0;
        size_t lo = 0, hi = _metrics.size();
        while (lo + 1 < hi) {
            size_t mid = (lo + hi) >> 1;
            if (_metrics[mid].ts <= t) lo = mid;
            else hi = mid;
        }
        double v0 = double(getter(_metrics[lo]));
        if (lo + 1 < _metrics.size()) {
            size_t n = lo + 1;
            double v1 = double(getter(_metrics[n]));
            double t0 = double(_metrics[lo].ts), t1 = double(_metrics[n].ts);
            double a = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0;
            a = std::clamp(a, 0.0, 1.0);
            return v0 + (v1 - v0) * a;
        }
        return v0;
        };

    // -------- CPU track --------
    {
        float y = startY + 10.f;
        float h = kTrackH;
        const float vx1 = canvasMin.x + leftPad;
        const float vx2 = canvasMax.x - 6.f;

        dl->AddRectFilled(ImVec2(vx1, y), ImVec2(vx2, y + h), kBoxCol, 6.f);
        drawGridX(y, h);

        // ticks + labels
        std::vector<int> ticks;
        if (h < 60.0f)        ticks = { 0,100 };
        else if (h < 90.0f)   ticks = { 0,50,100 };
        else                  ticks = { 0,25,50,75,100 };
        for (int v : ticks) {
            float yy = y + (1.f - (v / 100.f)) * h;
            dl->AddLine(ImVec2(vx1, yy), ImVec2(vx2, yy), kGridCol);
        }
        const float fs = ImGui::GetFontSize();
        auto putPct = [&](int v)
        {
            float yy = y + (1.f - (v / 100.f)) * h;
            char buf[16]; std::snprintf(buf, sizeof(buf), "%d%%", v);
            ImVec2 tsz = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2(labelX(tsz.x), yy - tsz.y * 0.5f), kTextCol, buf);
        };
        putPct(0);
        putPct(25);
        putPct(50);
        putPct(75);
        putPct(100);
        ImVec2 tsz = ImGui::CalcTextSize("CPU (%)");
        dl->AddText(ImVec2(labelX(tsz.x) - tsz.x, y - fs - 2.0f), kTextCol, "CPU (%)");

        auto cpuToY = [&](double pct)->float
        {
            pct = std::clamp(pct, 0.0, 100.0);
            return y + (1.f - float(pct / 100.0)) * h;
        };
        auto drawSeries = [&](auto getter, ImU32 col)
        {
            // indices visibles (avec marge)
            const double pad = spanUs * 0.10;
            const double qMin = visStart - pad;
            const double qMax = visEnd + pad;
            size_t i0 = 0, i1 = _metrics.size();
            while (i0 < i1 && double(_metrics[i0].ts) < qMin) ++i0;
            while (i1 > i0 && double(_metrics[i1 - 1].ts) > qMax) --i1;

            ImVec2 last(-1, -1);
            if (i0 >= i1) {
                ImVec2 a(xx(visStart), cpuToY(sampleAt(visStart, getter)));
                ImVec2 b(xx(visEnd), cpuToY(sampleAt(visEnd, getter)));
                dl->AddLine(a, b, col, kThick);
                return;
            }
            // ancre gauche
            last = ImVec2(xx(visStart), cpuToY(sampleAt(visStart, getter)));
            // samples
            for (size_t i = i0; i < i1; ++i) {
                const auto& m = _metrics[i];
                float x = xx(double(m.ts));
                if (x < vx1 - 2.f || x > vx2 + 2.f) continue;
                ImVec2 cur(x, cpuToY(double(getter(m))));
                if (last.x > 0) dl->AddLine(last, cur, col, kThick);
                last = cur;
                dl->AddCircleFilled(cur, kPtR, col);
            }
            // bord droit
            ImVec2 r(xx(visEnd), cpuToY(sampleAt(visEnd, getter)));
            if (last.x > 0) dl->AddLine(last, r, col, kThick);
        };

        drawSeries([&](const Metric& m) { return m.cpu_total; }, kCpuTot);
        drawSeries([&](const Metric& m) { return m.cpu;       }, kCpuProc);

        // Hover
        ImGuiIO& io = ImGui::GetIO();
        if (io.MousePos.x >= vx1 && io.MousePos.x <= vx2 &&
            io.MousePos.y >= y && io.MousePos.y <= y + h)
        {
            const double tUs = double(_timeMin) + (normStart + (normEnd - normStart) *
                std::clamp((io.MousePos.x - (canvasMin.x + leftPad)) / std::max(1.0f, contentW), 0.0f, 1.0f))
                * (double)(_timeMax - _timeMin);

            size_t best = 0; double bestD = 1e300;
            for (size_t i = 0; i < _metrics.size(); ++i) {
                double d = std::abs(double(_metrics[i].ts) - tUs);
                if (d < bestD) { bestD = d; best = i; }
            }
            const auto& m = _metrics[best];

            dl->AddLine(ImVec2(io.MousePos.x, y), ImVec2(io.MousePos.x, y + h),
                IM_COL32(255, 255, 255, 60), 1.0f);
            ImGui::BeginTooltip();
            ImGui::Text("CPU @ %s", fmtTime(double(m.ts - _timeMin)).c_str());
            ImGui::Separator();
            ImGui::Text("total:   %.1f%%", double(m.cpu_total));
            ImGui::Text("process: %.1f%%", double(m.cpu));
            ImGui::EndTooltip();
        }

        startY = y + h + kGap;
    }

    // -------- RAM track --------
    {
        float y = startY;
        float h = kTrackH;
        const float vx1 = canvasMin.x + leftPad;
        const float vx2 = canvasMax.x - 6.f;

        dl->AddRectFilled(ImVec2(vx1, y), ImVec2(vx2, y + h), kBoxCol, 6.f);
        drawGridX(y, h);

        // borne Y globale (avec pad)
        double ramMin = +1e300, ramMax = -1e300;
        for (const auto& m : _metrics) { ramMin = std::min(ramMin, double(m.ram_used)); ramMax = std::max(ramMax, double(m.ram_used)); }
        if (!(ramMax > ramMin)) { ramMin = 0.0; ramMax = 1.0; }
        const double ramPad = std::max(0.5, 0.05 * (ramMax - ramMin));
        const double mn = ramMin - ramPad, mx = ramMax + ramPad;

        // ticks Y
        const double step = nice_step_us(mx - mn, 4);
        const double first = std::ceil(mn / step) * step;
        auto labelX = [&](float textW)
        {
            float x = (canvasMin.x + leftPad) - 6.0f - textW;
            float left = canvasMin.x + 8.0f;
            return (x < left) ? left : x;
        };
        for (double v = first; v <= mx + 1e-9; v += step)
        {
            float yy = y + (1.f - float((v - mn) / (mx - mn))) * h;
            dl->AddLine(ImVec2(vx1, yy), ImVec2(vx2, yy), kGridCol);
            char lab[32];
            if (v >= 1024.0) std::snprintf(lab, sizeof(lab), "%.2f GB", v / 1024.0);
            else             std::snprintf(lab, sizeof(lab), "%.0f MB", v);
            ImVec2 tsz = ImGui::CalcTextSize(lab);
            dl->AddText(ImVec2(labelX(tsz.x), yy - tsz.y * 0.5f), kTextCol, lab);
        }
        ImVec2 tsz = ImGui::CalcTextSize("RAM (MB)");
        dl->AddText(ImVec2(labelX(tsz.x) - tsz.x, y - ImGui::GetFontSize() - 2.0f), kTextCol, "RAM (MB)");

        auto ramToY = [&](double v) -> float
        {
            return y + (1.f - float((v - mn) / (mx - mn))) * h;
        };

        // visibles (marge)
        const double pad = spanUs * 0.10;
        const double qMin = visStart - pad;
        const double qMax = visEnd + pad;
        size_t i0 = 0, i1 = _metrics.size();
        while (i0 < i1 && double(_metrics[i0].ts) < qMin) ++i0;
        while (i1 > i0 && double(_metrics[i1 - 1].ts) > qMax) --i1;

        ImVec2 last(-1, -1);
        if (i0 >= i1) {
            ImVec2 a(xx(visStart), ramToY(sampleAt(visStart, [&](const Metric& m) { return double(m.ram_used); })));
            ImVec2 b(xx(visEnd), ramToY(sampleAt(visEnd, [&](const Metric& m) { return double(m.ram_used); })));
            dl->AddLine(a, b, kRamCol, kThick);
        }
        else {
            last = ImVec2(xx(visStart), ramToY(sampleAt(visStart, [&](const Metric& m) { return double(m.ram_used); })));
            for (size_t i = i0; i < i1; ++i) {
                const auto& m = _metrics[i];
                float x = xx(double(m.ts));
                if (x < vx1 - 2.f || x > vx2 + 2.f) continue;
                ImVec2 cur(x, ramToY(double(m.ram_used)));
                if (last.x > 0) dl->AddLine(last, cur, kRamCol, kThick);
                last = cur;
                dl->AddCircleFilled(cur, kPtR, kRamCol);
            }
            ImVec2 r(xx(visEnd), ramToY(sampleAt(visEnd, [&](const Metric& m) { return double(m.ram_used); })));
            if (last.x > 0) dl->AddLine(last, r, kRamCol, kThick);
        }

        // Hover
        ImGuiIO& io = ImGui::GetIO();
        if (io.MousePos.x >= vx1 && io.MousePos.x <= vx2 &&
            io.MousePos.y >= y && io.MousePos.y <= y + h)
        {
            const double tUs = double(_timeMin) + (normStart + (normEnd - normStart) *
                std::clamp((io.MousePos.x - (canvasMin.x + leftPad)) / std::max(1.0f, contentW), 0.0f, 1.0f))
                * (double)(_timeMax - _timeMin);

            size_t best = 0; double bestD = 1e300;
            for (size_t i = 0; i < _metrics.size(); ++i) {
                double d = std::abs(double(_metrics[i].ts) - tUs);
                if (d < bestD) { bestD = d; best = i; }
            }
            const auto& m = _metrics[best];

            double used = double(m.ram_used);
            double total = std::max(1.0, double(m.ram_total));
            double pct = used / total * 100.0;

            dl->AddLine(ImVec2(io.MousePos.x, y), ImVec2(io.MousePos.x, y + h),
                IM_COL32(255, 255, 255, 60), 1.0f);

            ImGui::BeginTooltip();
            ImGui::Text("RAM @ %s", fmtTime(double(m.ts - _timeMin)).c_str());
            ImGui::Separator();
            if (used >= 1024.0) ImGui::Text("used:  %.2f GB (%.1f%%)", used / 1024.0, pct);
            else                ImGui::Text("used:  %.0f MB (%.1f%%)", used, pct);
            if (total >= 1024.0) ImGui::Text("total: %.2f GB", total / 1024.0);
            else                 ImGui::Text("total: %.0f MB", total);
            ImGui::EndTooltip();
        }
    }
}

// ---------- Categories ----------
void ViewerApp::drawCategoryBlock(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax,
    float leftPad, const std::string& catName,
    const std::vector<std::vector<Event*>>& lanes,
    uint64_t timeMin, uint64_t timeMax,
    double normStart, double normEnd,
    float& curY, Event*& hoveredEvent,
    std::vector<Event*>& hoveredGroup, size_t& visibleEventsCount)
{
    constexpr float kLaneH = 38.f;
    constexpr float kRectH = 22.f;
    constexpr float kCatGap = 10.f;
    constexpr float kMinBoxW = 18.f;
    constexpr float kGapPx = 2.f;

    const int subCount = (int)std::max<size_t>(1, lanes.size());

    const float vx1 = canvasMin.x + leftPad + 1.0f;
    const float vx2 = canvasMax.x - 6.0f;
    const float contentW = std::max(1.0f, canvasMax.x - canvasMin.x - leftPad - 8.0f);

    auto x_from_abs = [&](double absUs)->float {
        return xFromAbsUs(absUs, canvasMin, leftPad, contentW, normStart, normEnd, timeMin, timeMax);
        };
    auto clamp_to_view_x = [&](float& x1, float& x2) {
        if (x2 < vx1) x2 = vx1;
        if (x1 > vx2) x1 = vx2;
        x1 = std::max(x1, vx1);
        x2 = std::min(x2, vx2);
        };

    // Lanes visibles (au moins 1 event visible & filtré)
    std::vector<int> visibleLanes; visibleLanes.reserve(subCount);
    for (int li = 0; li < subCount; ++li) {
        bool any = false;
        for (Event* e : lanes[li]) {
            if (e->normEnd < normStart || e->normStart > normEnd) continue;
            if (!passDataFilter(*e)) continue;
            any = true; break;
        }
        if (any) visibleLanes.push_back(li);
    }
    if (visibleLanes.empty()) return;

    const float catH = (float)visibleLanes.size() * kLaneH;

    // Bande gauche (label)
    dl->AddRectFilled(ImVec2(canvasMin.x + 8, curY - 6.f),
        ImVec2(canvasMin.x + leftPad - 6, curY + catH + 6.f),
        IM_COL32(8, 40, 55, 220), 6.f);
    dl->AddText(ImVec2(canvasMin.x + 16, curY + 6.f),
        IM_COL32(180, 200, 220, 255), catName.c_str());

    // Fond des lanes visibles (packées)
    for (int packed = 0; packed < (int)visibleLanes.size(); ++packed) {
        float y = curY + packed * kLaneH;
        ImU32 bg = (packed % 2 == 0) ? IM_COL32(25, 35, 40, 180) : IM_COL32(28, 40, 45, 180);
        dl->AddRectFilled(ImVec2(canvasMin.x + leftPad, y), ImVec2(canvasMax.x - 6, y + kLaneH), bg, 6.f);
        dl->AddLine(ImVec2(canvasMin.x + leftPad, y + kLaneH - 1.f), ImVec2(canvasMax.x - 6, y + kLaneH - 1.f), IM_COL32(0, 0, 0, 60));
    }

    // Dessin des events (grouping adaptatif)
    for (int packed = 0; packed < (int)visibleLanes.size(); ++packed) {
        int li = visibleLanes[packed];
        float laneY = curY + packed * kLaneH;

        // collecte visible+filtrée
        std::vector<Event*> vis; vis.reserve(lanes[li].size());
        for (Event* e : lanes[li]) {
            if (e->normEnd < normStart || e->normStart > normEnd) continue;
            if (!passDataFilter(*e)) continue;
            vis.push_back(e);
        }
        visibleEventsCount += vis.size();
        if (vis.empty()) continue;

        std::sort(vis.begin(), vis.end(), [](const Event* a, const Event* b) { return a->ts < b->ts; });

        /// @brief G — class/struct documentation.
        struct G { float x1, x2; std::vector<Event*> ev; };
        std::vector<G> groups; groups.reserve(vis.size());

        // gap adaptatif (zoom & volume)
        float baseGapPx = 10.f / std::sqrt(std::max(1.f, _vp.zoom));
        baseGapPx = std::clamp(baseGapPx, 1.0f, 40.0f);
        const size_t targetGroups = 140;
        float adapt = 1.0f;
        if (vis.size() > targetGroups * 2) adapt = std::min(4.0f, std::sqrt(float(vis.size()) / float(targetGroups)));
        else if (vis.size() < targetGroups / 2) adapt = 0.7f;
        const float minGapPx = std::clamp(baseGapPx * adapt, 0.5f, 80.0f);

        float curX1 = -1.f, curX2 = -1.f;
        std::vector<Event*> bucket;
        auto flush = [&]() {
            if (bucket.empty()) return;
            float gx1 = curX1 + kGapPx, gx2 = curX2 - kGapPx;
            clamp_to_view_x(gx1, gx2);
            if (gx2 < gx1) gx2 = gx1 + 1.f;
            groups.push_back({ gx1, gx2, bucket });
            bucket.clear(); curX1 = curX2 = -1.f;
            };

        for (Event* e : vis) {
            float x1 = x_from_abs(double(e->ts));
            float x2 = x_from_abs(double(e->ts + e->dur));
            if (x2 - x1 < kMinBoxW) x2 = x1 + kMinBoxW;

            if (bucket.empty()) {
                curX1 = x1; curX2 = x2; bucket = { e };
            }
            else {
                if (x1 <= (curX2 + minGapPx)) { // chevauchement OU proximité
                    curX2 = std::max(curX2, x2);
                    bucket.push_back(e);
                }
                else {
                    flush();
                    curX1 = x1; curX2 = x2; bucket = { e };
                }
            }
        }
        flush();

        ImGuiIO& io = ImGui::GetIO();
        for (auto& g : groups) {
            ImVec2 p1(g.x1, laneY + (kLaneH - kRectH) * 0.5f);
            ImVec2 p2(g.x2, laneY + (kLaneH + kRectH) * 0.5f);

            ImU32 col = color::getColorU32(g.ev.front()->color);
            bool gHovered = (io.MousePos.x >= p1.x && io.MousePos.x <= p2.x && io.MousePos.y >= p1.y && io.MousePos.y <= p2.y);

            drawEventBox(dl, p1, p2, col, gHovered, (_selected && g.ev.size() == 1 && _selected == g.ev.front()));
            if (gHovered || (_selected && g.ev.size() == 1 && _selected == g.ev.front()))
                drawTopBottomAccent(dl, p1, p2, color::Lighten(col, +35, 200), color::Lighten(col, -35, 200));

            // label
            if ((p2.x - p1.x) >= 28.0f) {
                if (g.ev.size() == 1) {
                    Event* e = g.ev.front();
                    std::string lab = e->name.empty() ? e->category : e->name;
                    lab = elideToWidth(lab, p2.x - p1.x - 10.f);
                    if (!lab.empty()) drawCenteredLabel(dl, p1, p2, lab.c_str(), IM_COL32(25, 25, 25, 235));
                }
                else {
                    char buf[32]; std::snprintf(buf, sizeof(buf), "(%zu)", g.ev.size());
                    drawCenteredLabel(dl, p1, p2, buf, IM_COL32(240, 240, 240, 235));
                }
            }

            // interaction
            if (g.ev.size() == 1) {
                Event* e = g.ev.front();
                if (gHovered) hoveredEvent = e;
                if (gHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { _selected = e; _showSelectedPanel = true; }
                if (gHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { ImGui::OpenPopup("evt_ctx"); _selected = e; _showSelectedPanel = true; }
            }
            else {
                if (gHovered) hoveredGroup = g.ev;
                if (gHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { _selected = g.ev.front(); _showSelectedPanel = true; }
                if (gHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { ImGui::OpenPopup("evt_ctx"); _selected = g.ev.front(); _showSelectedPanel = true; }
            }
        }
    }

    curY += kCatGap + (float)visibleLanes.size() * kLaneH;
}

// =============== timeline (main) ===============
void ViewerApp::drawTimeline(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax)
{
    ImGuiIO& io = ImGui::GetIO();

    constexpr float kLeftPad = 150.f;
    constexpr float kTopPad = 26.f;
    constexpr float kRightPad = 6.f;
    const float contentW = std::max(1.0f, canvasMax.x - canvasMin.x - kLeftPad - kRightPad);

    ImGui::SetCursorScreenPos(canvasMin);
    ImGui::InvisibleButton("timeline_canvas", ImVec2(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    // --- helpers communs ---
    auto computeMinSpanN = [&]()
    {
        double minDur = 1e300;
        for (const auto& e : _events) if (e.dur > 0) minDur = std::min(minDur, double(e.dur));
        for (size_t i = 1; i < _metrics.size(); ++i) {
            const double d = double(_metrics[i].ts) - double(_metrics[i - 1].ts);
            if (d > 0) minDur = std::min(minDur, d);
        }
        if (!(minDur < 1e290)) minDur = 1.0; // fallback
        const double totalUs = std::max(1.0, double(_timeMax - _timeMin));
        const double minSpanNData = std::max(1e-18, (minDur / totalUs) * 0.25);
        const double minSpanNHard = 1e-18; // zoom quasi infini
        // Choisir la plus PETITE borne => autorise de zoomer plus profond que la plus petite durée
        return std::min(minSpanNHard, minSpanNData);
    };
    auto cursorCX = [&](float contentW)->float {
        float mx = io.MousePos.x - (canvasMin.x + kLeftPad);
        return std::clamp(mx / std::max(1.0f, contentW), 0.f, 1.f);
        };

    if (hovered)
    {
        const float wheel = io.MouseWheel;
        if (wheel != 0.f)
{
            const double spanN = 1.0 / std::max(1e-15, double(_vp.zoom));
            const double level = 1.0 / std::max(1e-15, spanN);
            const double base = (io.KeyShift ? 1.05 : io.KeyCtrl ? 1.18 : 1.12);
            const double adapt = 1.0 + 0.14 * std::clamp(std::log10(level + 1.0), 0.0, 8.0);
            const double power = base * adapt;
            const double factor = std::pow(power, wheel);

            const double minSpanN = computeMinSpanN();           // borne zoom-in “profonde”
            const float  cx = cursorCX(contentW);

            double newSpanN = std::clamp(spanN / factor, minSpanN, 1.0 / 0.02); // zoom-out max ≈ 50x
            const double tAtCursor = _vp.offset + cx * spanN;
            double newStart = std::clamp(tAtCursor - cx * newSpanN, 0.0, std::max(0.0, 1.0 - newSpanN));
            const double newEnd = newStart + newSpanN;

            // << clé : on repasse par l’animateur central >>
            _anim.begin(newStart, newEnd, _vp.zoom, _vp.offset);
        }

        // Double-click: zoom in/out "saut"
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const double spanN = 1.0 / std::max(1e-15, double(_vp.zoom));
            const float  cx = cursorCX(contentW);
            const double minSpanN = computeMinSpanN();
            const double factor = (io.KeyCtrl ? 20.0 : 8.0);
            double newSpanN = std::clamp(spanN / factor, minSpanN, 1.0 / 0.02);

            const double tAtCursor = _vp.offset + cx * spanN;
            double newStart = std::clamp(tAtCursor - cx * newSpanN, 0.0, std::max(0.0, 1.0 - newSpanN));
            const double newEnd = newStart + newSpanN;

            _anim.begin(newStart, newEnd, _vp.zoom, _vp.offset);
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right)) {
            const double spanN = 1.0 / std::max(1e-15, double(_vp.zoom));
            const float  cx = cursorCX(contentW);
            const double factor = (io.KeyCtrl ? 0.04 : 0.12); // gros zoom-out
            double newSpanN = std::clamp(spanN / factor, 1.0 / 0.02, 1.0 / 0.02);

            const double tAtCursor = _vp.offset + cx * spanN;
            double newStart = std::clamp(tAtCursor - cx * newSpanN, 0.0, std::max(0.0, 1.0 - newSpanN));
            const double newEnd = newStart + newSpanN;

            _anim.begin(newStart, newEnd, _vp.zoom, _vp.offset);
        }
    }

    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 d = io.MouseDelta;
        const double spanN = 1.0 / std::max(1e-9, double(_vp.zoom));
        _vp.offset = std::clamp(_vp.offset - (double)d.x / contentW * spanN, 0.0, std::max(0.0, 1.0 - spanN));
        _vp.panY += d.y;
    }

    const double normStart = _vp.offset;
    const double normEnd = _vp.offset + 1.0 / (double)_vp.zoom;
    const double totalUs = double(_timeMax - _timeMin);
    const double visStart = totalUs * normStart + double(_timeMin);
    const double visEnd = totalUs * normEnd + double(_timeMin);
    const double spanUs = std::max(1.0, visEnd - visStart);

    _absRuler.draw(dl, canvasMin, canvasMax, kLeftPad, contentW, normStart, normEnd, _timeMin, _timeMax);

    float curY = canvasMin.y + kTopPad + 6.f + _vp.panY;

    Event* hoveredEvent = nullptr;
    std::vector<Event*> hoveredGroup;
    size_t visibleEventsCount = 0;

    // Catégories -> lanes
    std::unordered_map<std::string, std::vector<std::vector<Event*>>> rows;
    rows.reserve(64);
    {
        std::lock_guard<std::mutex> lk(_mtx);
        std::unordered_map<std::string, std::vector<Event*>> byCat;
        byCat.reserve(64);
        for (Event& e : _events) byCat[e.category].push_back(&e);

        for (auto& kv : byCat) {
            auto& evs = kv.second;
            std::sort(evs.begin(), evs.end(), [](const Event* a, const Event* b) { return a->ts < b->ts; });
            std::vector<std::vector<Event*>> lanes;
            for (Event* e : evs) {
                bool placed = false;
                for (auto& lane : lanes) {
                    if (lane.empty() || lane.back()->normEnd <= e->normStart) { lane.push_back(e); placed = true; break; }
                }
                if (!placed) { lanes.emplace_back(); lanes.back().push_back(e); }
            }
            rows.emplace(kv.first, std::move(lanes));
        }
    }

    for (auto& kv : rows) {
        drawCategoryBlock(dl, canvasMin, canvasMax, kLeftPad,
            kv.first, kv.second,
            _timeMin, _timeMax,
            normStart, normEnd,
            curY, hoveredEvent, hoveredGroup, visibleEventsCount);
    }

    // Tooltips
    if (hoveredEvent) {
        Event* e = hoveredEvent;
        ImGui::BeginTooltip();
        ImGui::Text("%s", e->name.empty() ? e->category.c_str() : e->name.c_str());
        ImGui::Separator();
        ImGui::Text("Category: %s", e->category.c_str());
        ImGui::Text("Start:    %s", fmtTime(double(e->ts - (double)_timeMin)).c_str());
        ImGui::Text("Duration: %s", fmtTime(double(e->dur)).c_str());
        ImGui::Text("Data:     %s", e->data.c_str());
        auto it = _globalStats.find(e->name);
        const EventStats* S = (it != _globalStats.end() ? &it->second : (e->stats.count ? &e->stats : nullptr));
        if (S) {
            ImGui::Separator();
            ImGui::Text("count = %llu", (unsigned long long)S->count);
            ImGui::Text("avg   = %s", fmtTime(S->avg_us).c_str());
            ImGui::Text("min   = %s", fmtTime(double(S->min_us)).c_str());
            ImGui::Text("max   = %s", fmtTime(double(S->max_us)).c_str());
        }
        ImGui::EndTooltip();
    }
    else if (!hoveredGroup.empty()) {
        /// @brief Agg — class/struct documentation.
        struct Agg { uint64_t n = 0; double sum = 0, mn = 1e300, mx = 0; };
        std::unordered_map<std::string, Agg> agg;
        for (Event* e : hoveredGroup) {
            const std::string key = e->category + "::" + (e->name.empty() ? e->category : e->name);
            auto& a = agg[key]; a.n++; const double d = (double)e->dur;
            a.sum += d; a.mn = std::min(a.mn, d); a.mx = std::max(a.mx, d);
        }
        ImGui::BeginTooltip();
        ImGui::Text("Group: %zu events", hoveredGroup.size());
        ImGui::Separator();
        for (const auto& kv : agg) {
            const auto& k = kv.first; const auto& a = kv.second;
            const double avg = a.n ? (a.sum / double(a.n)) : 0.0;
            ImGui::Text("%s  (count=%llu)  min=%s  max=%s  avg=%s",
                k.c_str(), (unsigned long long)a.n,
                fmtTime(a.mn).c_str(), fmtTime(a.mx).c_str(), fmtTime(avg).c_str());
        }
        ImGui::EndTooltip();
    }

    if (ImGui::BeginPopup("evt_ctx")) {
        const bool hasSel = (_selected != nullptr);
        if (ImGui::MenuItem("Clear selection", nullptr, false, hasSel)) { _selected = nullptr; _showSelectedPanel = false; }
        ImGui::EndPopup();
    }

    // bottom metrics
    {
        float tracksTop = curY + 6.f;
        drawMetricsBottom(dl, canvasMin, canvasMax, kLeftPad, contentW, tracksTop, normStart, normEnd);
    }

    // Status bar
    ImVec2 sMin(canvasMin.x, canvasMax.y - 22.f), sMax(canvasMax.x, canvasMax.y);
    dl->AddRectFilled(sMin, sMax, IM_COL32(18, 23, 28, 255));
    char left[160];  std::snprintf(left, sizeof(left), "Zoom: %.2fx  |  Offset: %.6f   |   PanY: %.1f", _vp.zoom, _vp.offset, _vp.panY);
    char right[200]; std::snprintf(right, sizeof(right), "Range: %s  |  Visible events: %zu",
        fmtTime(std::max(1.0, double(_timeMax - _timeMin) * (normEnd - normStart))).c_str(), visibleEventsCount);
    dl->AddText(ImVec2(sMin.x + 8, sMin.y + 3), IM_COL32(200, 200, 200, 255), left);
    float rw = ImGui::CalcTextSize(right).x;
    dl->AddText(ImVec2(sMax.x - rw - 8, sMin.y + 3), IM_COL32(200, 200, 200, 255), right);
}

// --- drawUI (controls + timeline host) ---
void ViewerApp::drawUI()
{
    if (_view == AppView::Startup)
    {
        // Fenêtre centrée & grande avec marge (plein écran - marges)
        ImGuiViewport* vp = ImGui::GetMainViewport();
        const float margin = 40.0f;
        ImVec2 pos = ImVec2(vp->WorkPos.x + margin, vp->WorkPos.y + margin);
        ImVec2 size = ImVec2(vp->WorkSize.x - 2 * margin, vp->WorkSize.y - 2 * margin);

        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize;

        ImGui::Begin("Connect", nullptr, flags);
        ImVec2 avail = ImGui::GetContentRegionAvail();
        _connectView.draw(avail,
            [this](const ServerInfo& s)
            {
                if (_client.connected())
                    _client.stop_session();
                _client.start_session(s);
                _view = AppView::Live;
            },
            [this]()
            {
                if (_client.connected())
                    _client.stop_session();
                _view = AppView::Text;
            }
        );
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }

    else if (_view == AppView::Live)
    {
        if (_client.connected())
        {
            std::vector<std::string> read;
            _client.tick(read);

            // === Capture de l’état AVANT extension des bornes ===
            const double oldTotal = std::max(1.0, double(_timeMax - _timeMin));
            const double spanN_old = 1.0 / std::max(1e-15, double(_vp.zoom));
            const double absSpan_old = spanN_old * oldTotal;
            const double leftAbs_old = double(_timeMin) + _vp.offset * oldTotal;
            const double rightGapN = 1.0 - (_vp.offset + spanN_old);

            // Hysteresis pour le “follow live” (à droite)
            static bool s_followLive = false;
            if (!s_followLive && rightGapN < 0.03) s_followLive = true;   // entrer en follow
            if (s_followLive && rightGapN > 0.06) s_followLive = false;  // sortir du follow

            const size_t prevE = _events.size();
            const size_t prevM = _metrics.size();

            for (const auto& str : read)
            {
                std::string err;
                if (!parse_trace_payload(str, _events, _globalStats, _metrics, 0, &err))
                    _lastError = err.empty() ? "Failed to parse: " : err;
                if (_events.size() == prevE && _metrics.size() == prevM)
                    break;
            }
            uint64_t newMin = UINT64_MAX, newMax = 0;

            for (size_t i = prevE; i < _events.size(); ++i)
            {
                const auto& e = _events[i];
                newMin = std::min(newMin, e.ts);
                newMax = std::max(newMax, e.ts + std::max<uint64_t>(e.dur, 1));
            }
            for (size_t i = prevM; i < _metrics.size(); ++i)
            {
                const auto& m = _metrics[i];
                newMin = std::min(newMin, m.ts);
                newMax = std::max(newMax, m.ts + 1);
            }
            // 3) Mise à jour de la fenêtre temporelle globale (avec ancrage viewport)
            bool expanded = false;
            const double spanN = 1.0 / std::max(1e-15, double(_vp.zoom));
            const double anchorCx = 0.5; // ancre par défaut = centre
            const double anchorAbs = double(_timeMin) + (_vp.offset + spanN * anchorCx) * oldTotal;

            if (newMin != UINT64_MAX)
            {
                uint64_t oldMin = _timeMin, oldMax = _timeMax;
                if (prevE == 0 && prevM == 0 && _timeMin == 0 && _timeMax <= 1)
                {
                    _timeMin = newMin;
                    _timeMax = newMax;
                }
                else
                {
                    _timeMax = std::max(_timeMax, newMax);
                }
                expanded = (oldMin != _timeMin) || (oldMax != _timeMax);

                // --- recalcule l’offset pour que anchorAbs retombe au même x écran ---
                if (expanded)
                {
                    const double newTotal = std::max(1.0, double(_timeMax - _timeMin));

                    // ⚠️ NE PLUS TOUCHER AU ZOOM : il reste tel quel
                    const double spanN = 1.0 / std::max(1e-15, double(_vp.zoom));

                    // Hysteresis pour le "live follow" (évite les micro-oscillations)
                    const double rightGapN = 1.0 - (_vp.offset + spanN);
                    static bool s_followLive = false;
                    if (!s_followLive && rightGapN < 0.03) s_followLive = true;   // entrer en mode follow
                    if (s_followLive && rightGapN > 0.06) s_followLive = false;  // sortir du follow

                    double newOffset;
                    if (s_followLive) {
                        // pin à droite avec un léger pad visuel (pas de changement de zoom)
                        const double tailPadN = 0.005;
                        newOffset = std::max(0.0, 1.0 - tailPadN - spanN);
                    }
                    else {
                        // conserver la même ancre absolue, avec spanN FIXE (zoom constant)
                        newOffset = (anchorAbs - double(_timeMin)) / newTotal - spanN * anchorCx;
                        newOffset = std::clamp(newOffset, 0.0, std::max(0.0, 1.0 - spanN));
                    }

                    // Deadzone pour couper le jitter numérique
                    if (std::abs(newOffset - _vp.offset) > 1e-9)
                        _vp.offset = newOffset;
                }
            }

            // 4) Normalisation
            normalizeEvents(_events, _timeMin, _timeMax);


            // === Ajuste le viewport pour éviter le “dezoom” perçu ===
            // Objectif : conserver le span ABSOLU et l’ancre visuelle.
            // - si followLive: on PIN la vue à droite (avec tailPad), zoom compensé
            // - sinon: on garde l’ANCRE GAUCHE et le span absolu constants

            const double newTotal = std::max(1.0, double(_timeMax - _timeMin));
            const double newSpanN = std::clamp(absSpan_old / newTotal, 1e-18, 1.0);
            const double targetZoom = 1.0 / newSpanN;

            const double epsZoom = 1e-10;
            const double epsOff = 1e-10;

            // Applique le zoom compensatoire seulement si ça change vraiment
            if (std::abs(targetZoom - _vp.zoom) > epsZoom)
                _vp.zoom = targetZoom;

            double newOffset;
            if (s_followLive) {
                // pin à droite avec un léger pad visuel (sans changer le span absolu)
                const double tailPadN = 0.005;
                newOffset = std::max(0.0, 1.0 - tailPadN - newSpanN);
            }
            else {
                // conserve l’ancre GAUCHE absolue
                newOffset = (leftAbs_old - double(_timeMin)) / newTotal;
                newOffset = std::clamp(newOffset, 0.0, std::max(0.0, 1.0 - newSpanN));
            }
            if (std::abs(newOffset - _vp.offset) > epsOff)
                _vp.offset = newOffset;
            if (expanded)
                normalizeEvents(_events, _timeMin, _timeMax);
            else
                normalizeEventsFrom(_events, prevE, _timeMin, _timeMax);

            // 5) Garder _metrics trié par ts (insertion stable amortie)
            if (_metrics.size() > prevM) {
                // Si le nouveau bloc n’est pas déjà en fin triée, on fusionne
                auto mid = _metrics.begin() + (ptrdiff_t)prevM;
                if (prevM > 0 && _metrics[prevM - 1].ts > _metrics.back().ts) {
                    std::inplace_merge(_metrics.begin(), mid, _metrics.end(),
                        [](const Metric& a, const Metric& b) { return a.ts < b.ts; });
                }
                // Sinon, rien à faire : les ajouts sont déjà en ordre croissant
            }

            _parsedCount = _events.size();
        }
    }

    // Controls
    ImGui::Begin("Controls");
    const char* modeStr = (_view == AppView::Live) ? "Live (UDP)" : "Text (file)";
    ImGui::Text("Mode: %s", modeStr);
    ImGui::SameLine();
    if (ImGui::SmallButton("Back to start"))
    {
        _events = {};
        _globalStats = {};
        _metrics = {};
        _timeMin = 0;
        _timeMax = 1;
        _selected = nullptr;
        _dur_min_us = 0;
        _parsing = false;
        _parsedCount = 0;
        _lastError = {};
        _view = AppView::Startup;
        ImGui::End();
        return;
    }

    ImGui::Separator();

    if (_view == AppView::Text) {
        ImGui::InputText("Trace path", _filepath, sizeof(_filepath));
        ImGui::SameLine();
        if (ImGui::Button("Load trace") && !_parsing) {
            loadFile(_filepath, uint64_t(std::max(0, _dur_min_us)));
        }
    }

    ImGui::InputInt("Min dur (us)", &_dur_min_us);
    ImGui::SetNextItemWidth(180);
    ImGui::Text("Parsed: %zu", _parsedCount);
    ImGui::Checkbox("Auto-reload", &_autoReload);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Interval (s)", &_autoReloadInterval, 0.2f, 5.0f, "%.1f");
    if (!_lastError.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Error: %s", _lastError.c_str());

    ImGui::SeparatorText("Filter");
    ImGui::SetNextItemWidth(260.f);
    ImGui::SameLine();
    ImGui::Checkbox("Regex", &_dataFilterRegex);
    ImGui::SameLine();
    ImGui::Checkbox("Case", &_dataFilterCaseSensitive);
    ImGui::InputText("Regex filter", _dataFilter, sizeof(_filepath));
    ImGui::Text("Visible after filter: %zu", _filteredVisible);
    ImGui::SameLine();
    ImGui::TextDisabled("(data)");

    if (_view == AppView::Text) {
        _autoReloadTimer += ImGui::GetIO().DeltaTime;
        if (_autoReload && _filepath[0] && _autoReloadTimer >= (double)_autoReloadInterval) {
            _autoReloadTimer = 0.0;
            updateAutoReload(_filepath);
        }
    }
    ImGui::End();

    // Animation & timeline
    ImGuiIO& io = ImGui::GetIO();
    _anim.tick(io.DeltaTime, _vp.zoom, _vp.offset);

    ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    ImVec2 availCR = ImGui::GetContentRegionAvail();
    ImVec2 canvasMax(canvasMin.x + availCR.x, canvasMin.y + availCR.y);
    dl->AddRectFilled(canvasMin, canvasMax, IM_COL32(10, 18, 24, 255));
    drawTimeline(dl, canvasMin, canvasMax);

    if (_showSelectedPanel && _selected) {
        _selectedPanel.draw(_selected, _events, _mtx, _timeMin, _showSelectedPanel);
    }
    ImGui::End();
}
