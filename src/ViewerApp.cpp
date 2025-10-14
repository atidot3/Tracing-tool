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
#include <algorithm>

// case insensitive
static bool contains_icase(std::string_view hay, std::string_view needle)
{
    if (needle.empty()) return true;
    if (hay.size() < needle.size()) return false;
    auto it = std::search(
        hay.begin(), hay.end(),
        needle.begin(), needle.end(),
        [](char a, char b) { return std::tolower(unsigned(a)) == std::tolower(unsigned(b)); }
    );
    return it != hay.end();
}

// compile regex if needed
void ViewerApp::compileDataFilterIfNeeded()
{
    if (_dataFilterRegex && (_df_lastPattern != _dataFilter || !_df_lastRegex || _df_lastCase != _dataFilterCaseSensitive))
    {
        _df_lastPattern = _dataFilter;
        _df_lastRegex = true;
        _df_lastCase = _dataFilterCaseSensitive;
        try
        {
            std::regex::flag_type flags = std::regex::ECMAScript;
            if (!_dataFilterCaseSensitive)
                flags = (std::regex::flag_type)(flags | std::regex::icase);

            std::regex re(_dataFilter, flags);
            _df_regexValid = true;
        }
        catch (...)
        {
            _df_regexValid = false;
        }
    }
    if (!_dataFilterRegex)
    {
        _df_lastPattern.clear();
        _df_lastRegex = false;
        _df_regexValid = false;
    }
}

bool ViewerApp::passDataFilter(const Event& e) 
{
    if (_dataFilter[0] == '\0')
        return true;

    if (_dataFilterRegex)
    {
        // recompile-on-read : light but simple — or stock regex in field if you prefer
        try
        {
            std::regex::flag_type flags = std::regex::ECMAScript;
            if (!_dataFilterCaseSensitive) flags = (std::regex::flag_type)(flags | std::regex::icase);
            std::regex re(_dataFilter, flags);
            return std::regex_search(e.data, re);
        }
        catch (...)
        {
            return true; // invalid -> no filter
        }
    }
    else
    {
        if (_dataFilterCaseSensitive)
        {
            return e.data.find(_dataFilter) != std::string::npos;
        }

        return contains_icase(e.data, _dataFilter);
    }
}

// ---------- ViewerApp ----------
ViewerApp::ViewerApp()
    : _events{}, _globalStats{}
    , _timeMin{0}, _timeMax{1}
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
    , _df_lastPattern{}
    , _df_lastCase{ false }, _df_lastRegex{ false }, _df_regexValid{ false }
    , _filteredVisible{ 0 }
{}

ViewerApp::~ViewerApp() {}

bool ViewerApp::loadFile(const char* path, uint64_t durMinUs)
{
    if (!path || !*path) return false;

    _parsing = true;
    std::vector<Event> newEvents;
    std::unordered_map<std::string, EventStats> newStats;
    std::string err;
    if (!parse_trace_file(path, newEvents, newStats, durMinUs, &err)) {
        _lastError = err.empty() ? "Failed to parse file" : err;
        _parsing = false;
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(_mtx);
        _events.swap(newEvents);
        _globalStats.swap(newStats);

        uint64_t tmin = UINT64_MAX, tmax = 0;
        for (auto& e : _events) { tmin = std::min(tmin, e.ts); tmax = std::max(tmax, e.ts + e.dur); }
        if (tmin == UINT64_MAX) { tmin = 0; tmax = 1; }
        _timeMin = tmin; _timeMax = tmax;

        const double den = double(_timeMax - _timeMin) > 0.0 ? double(_timeMax - _timeMin) : 1.0;
        for (auto& e : _events) {
            e.normStart = (double(e.ts) - double(_timeMin)) / den;
            e.normEnd = (double(e.ts + e.dur) - double(_timeMin)) / den;
            e.normStart = std::clamp(e.normStart, 0.0, 1.0);
            e.normEnd = std::clamp(e.normEnd, 0.0, 1.0);
        }

        _vp.zoom = 1.f; _vp.offset = 0.0; _vp.panY = 0.f;
        _selected = nullptr;
        _parsedCount = _events.size();
    }
    _parsing = false; _lastError.clear();
    std::snprintf(_filepath, sizeof(_filepath), "%s", path);

    std::filesystem::file_time_type mt;
    if (std::filesystem::exists(path)) _fileMTime = std::filesystem::last_write_time(path);
    return true;
}

bool ViewerApp::reloadFilePreserveView(uint64_t durMinUs) {
    if (_filepath[0] == '\0') return false;
    std::vector<Event> tmp; std::unordered_map<std::string, EventStats> tmpStats; std::string err;
    if (!parse_trace_file(_filepath, tmp, tmpStats, durMinUs, &err)) { _lastError = err; return false; }

    auto keep = _vp;
    {
        std::lock_guard<std::mutex> lk(_mtx);
        _events.swap(tmp);
        _globalStats.swap(tmpStats);

        uint64_t tmin = UINT64_MAX, tmax = 0;
        for (auto& e : _events) { tmin = std::min(tmin, e.ts); tmax = std::max(tmax, e.ts + e.dur); }
        if (tmin == UINT64_MAX) { tmin = 0; tmax = 1; }
        _timeMin = tmin; _timeMax = tmax;

        const double den = double(_timeMax - _timeMin) > 0.0 ? double(_timeMax - _timeMin) : 1.0;
        for (auto& e : _events) {
            e.normStart = (double(e.ts) - double(_timeMin)) / den;
            e.normEnd = (double(e.ts + e.dur) - double(_timeMin)) / den;
            e.normStart = std::clamp(e.normStart, 0.0, 1.0);
            e.normEnd = std::clamp(e.normEnd, 0.0, 1.0);
        }
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

void ViewerApp::drawEventBox(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, ImU32 color, bool hovered, bool selected)
{
    // Slightly lighten color if hovered, and use a translucent white overlay if selected
    ImU32 fill = color;
    if (hovered)   fill = color::AdjustRGB(color, +20);
    if (selected)  fill = IM_COL32(255, 255, 255, 40);
    dl->AddRectFilled(p1, p2, fill, 5.0f);
    dl->AddRect(p1, p2, IM_COL32(0, 0, 0, 140), 5.0f, 0, 1.0f);
}

void ViewerApp::drawTopBottomAccent(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, ImU32 topColor, ImU32 bottomColor)
{
    dl->AddRectFilled(ImVec2(p1.x, p1.y), ImVec2(p2.x, p1.y + 2.f), topColor); dl->AddRectFilled(ImVec2(p1.x, p2.y - 2.f), ImVec2(p2.x, p2.y), bottomColor);
}

void ViewerApp::drawCenteredLabel(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, const char* text, ImU32 color)
{
    float textHeight = ImGui::GetTextLineHeight();
    float textWidth = ImGui::CalcTextSize(text).x;
    
    // not enough space to draw text
    if (p2.x - p1.x <= 8.f) return;

    ImVec2 pos;
    pos.x = p1.x + (p2.x - p1.x - textWidth) * 0.5f;
    pos.y = p1.y + (p2.y - p1.y - textHeight) * 0.5f;
    dl->AddText(pos, color, text);
}

// ---------- UI ----------
void ViewerApp::drawUI()
{
    // Controls
    ImGui::Begin("Controls");
    ImGui::InputText("Trace path", _filepath, sizeof(_filepath));
    ImGui::SameLine();
    if (ImGui::Button("Load trace") && !_parsing) {
        loadFile(_filepath, uint64_t(std::max(0, _dur_min_us)));
    }
    ImGui::InputInt("Min dur (us)", &_dur_min_us);
    ImGui::SetNextItemWidth(180);
    ImGui::SliderFloat("Zoom", &_vp.zoom, 0.05f, 200000.f, "%.2fx");
    ImGui::Text("Offset: %.4f  PanY: %.1f", _vp.offset, _vp.panY);
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
    // Info
    ImGui::SameLine();
    ImGui::TextDisabled("(data)");

    _autoReloadTimer += ImGui::GetIO().DeltaTime;
    if (_autoReload && _filepath[0] && _autoReloadTimer >= (double)_autoReloadInterval) {
        _autoReloadTimer = 0.0;
        updateAutoReload(_filepath);
    }
    ImGui::End();

    ImGuiIO& io = ImGui::GetIO();
    _anim.tick(io.DeltaTime, _vp.zoom, _vp.offset);

    // Timeline
    ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 canvasMax(canvasMin.x + avail.x, canvasMin.y + avail.y);
    dl->AddRectFilled(canvasMin, canvasMax, IM_COL32(10, 18, 24, 255));
    drawTimeline(dl, canvasMin, canvasMax);
    
    if (_showSelectedPanel && _selected) {
        _selectedPanel.draw(_selected, _events, _mtx, _timeMin, _showSelectedPanel);
    }

    ImGui::End();
}

// ======================================================================
// 2) One category block (label + sublanes + events)
//    - groups are bright like events (no dark “group box” anymore)
//    - keeps hoveredEvent / hoveredGroup / _selected
// ======================================================================
void ViewerApp::drawCategoryBlock(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax, float leftPad, const std::string& catName, const std::vector<std::vector<Event*>>& lanes, double timeMin, double timeMax, double normStart, double normEnd, float& curY, Event*& hoveredEvent, std::vector<Event*>& hoveredGroup, size_t& visibleEventsCount)
{
    constexpr float kLaneH = 38.f;
    constexpr float kRectH = 22.f;
    constexpr float kCatGap = 10.f;

    const int subCount = (int)std::max<size_t>(1, lanes.size());
    const float catH = subCount * kLaneH;

    // Left label
    dl->AddRectFilled(ImVec2(canvasMin.x + 8, curY - 6.f),
        ImVec2(canvasMin.x + leftPad - 6, curY + catH + 6.f),
        IM_COL32(8, 40, 55, 220), 6.f);
    dl->AddText(ImVec2(canvasMin.x + 16, curY + 6.f),
        IM_COL32(180, 200, 220, 255), catName.c_str());

    // Mini-ruler on each sublane + background
    const float vx1 = canvasMin.x + leftPad + 1.0f;
    const float vx2 = canvasMax.x - 6.0f;
    const float contentW = std::max(1.0f, canvasMax.x - canvasMin.x - leftPad - 8.0f);
    const double totalUs = std::max<double>(1.0, timeMax - timeMin);
    const double visStartUs = totalUs * normStart + timeMin;
    const double visEndUs = totalUs * normEnd + timeMin;
    const double spanUs = std::max(1.0, visEndUs - visStartUs);

    auto xfrom = [&](double n) -> float { return canvasMin.x + leftPad + float((n - normStart) * (double)_vp.zoom * contentW); };
    auto clamp_to_view_x = [&](float& x1, float& x2)
    {
        if (x2 < vx1) x2 = vx1;
        if (x1 > vx2) x1 = vx2;
        x1 = std::max(x1, vx1);
        x2 = std::min(x2, vx2);
    };

    // fonds sublanes
    for (int li = 0; li < subCount; ++li)
    {
        const float y = curY + li * kLaneH;
        ImU32 bg = (li % 2 == 0) ? IM_COL32(25, 35, 40, 180) : IM_COL32(28, 40, 45, 180);
        dl->AddRectFilled(ImVec2(canvasMin.x + leftPad, y), ImVec2(canvasMax.x - 6, y + kLaneH), bg, 6.f);
        dl->AddLine(ImVec2(canvasMin.x + leftPad, y + kLaneH - 1.f), ImVec2(canvasMax.x - 6, y + kLaneH - 1.f), IM_COL32(0, 0, 0, 60));
    }

    // Events (per sublane) – adaptive grouping, bright groups
    for (int li = 0; li < subCount; ++li)
    {
        const float laneY = curY + li * kLaneH;

        std::vector<Event*> vis; vis.reserve(lanes[li].size());
        for (Event* e : lanes[li]) {
            if (e->normEnd < normStart || e->normStart > normEnd) continue;
            if (!passDataFilter(*e)) continue;
            vis.push_back(e);
        }
        visibleEventsCount += vis.size();

        struct G { float x1, x2; std::vector<Event*> ev; };
        std::vector<G> groups; groups.reserve(vis.size());

        auto x_from_norm = [&](double n) { return xfrom(n); };
        const float minGapPx = std::clamp<float>(10.f / std::sqrt(std::max(1.f, _vp.zoom)), 1.5f, 40.f);
        const bool forceFull = (vis.size() < 80 && _vp.zoom > 20.f);

        if (forceFull) {
            for (Event* e : vis)
                groups.push_back({ x_from_norm(e->normStart), x_from_norm(e->normEnd), { e } });
        }
        else {
            bool has = false; double s = 0, e = 0; std::vector<Event*> bucket;
            for (Event* ev : vis) {
                const float x1 = x_from_norm(ev->normStart);
                if (!has) { has = true; s = ev->normStart; e = ev->normEnd; bucket = { ev }; }
                else {
                    const float gap = x1 - x_from_norm(e);
                    if (gap < minGapPx) { e = std::max(e, ev->normEnd); bucket.push_back(ev); }
                    else { groups.push_back({ x_from_norm(s), x_from_norm(e), bucket }); s = ev->normStart; e = ev->normEnd; bucket = { ev }; }
                }
            }
            if (has) groups.push_back({ x_from_norm(s), x_from_norm(e), bucket });
        }

        // Draw
        ImGuiIO& io = ImGui::GetIO();
        for (auto& g : groups)
        {
            float gx1 = g.x1 + 4.f, gx2 = g.x2 - 4.f;
            if (gx2 < gx1) gx2 = gx1 + 1.f;
            clamp_to_view_x(gx1, gx2);

            ImVec2 p1(gx1, laneY + (kLaneH - kRectH) * 0.5f);
            ImVec2 p2(gx2, laneY + (kLaneH + kRectH) * 0.5f);

            // representative color (first event)
            ImU32 col = color::getColorU32(g.ev.front()->color);
            const bool gHovered = (io.MousePos.x >= p1.x && io.MousePos.x <= p2.x &&
                io.MousePos.y >= p1.y && io.MousePos.y <= p2.y);

            // Bright style for both single & group
            drawEventBox(dl, p1, p2, col, gHovered, (_selected && g.ev.size() == 1 && _selected == g.ev.front()));
            if (gHovered || (_selected && g.ev.size() == 1 && _selected == g.ev.front()))
                drawTopBottomAccent(dl, p1, p2,
                    color::Lighten(col, +35, 200),
                    color::Lighten(col, -35, 200));

            // Label
            if ((p2.x - p1.x) >= 28.0f) {
                if (g.ev.size() == 1) {
                    Event* e = g.ev.front();
                    std::string lab = e->name.empty() ? e->category : e->name;
                    lab = elideToWidth(lab, p2.x - p1.x - 10.f);
                    if (!lab.empty())
                        drawCenteredLabel(dl, p1, p2, lab.c_str(), IM_COL32(25, 25, 25, 235));
                }
                else {
                    char buf[32]; std::snprintf(buf, sizeof(buf), "(%zu)", g.ev.size());
                    drawCenteredLabel(dl, p1, p2, buf, IM_COL32(240, 240, 240, 235));
                }
            }

            // Interaction
            if (g.ev.size() == 1) {
                Event* e = g.ev.front();
                if (gHovered) hoveredEvent = e;
                if (gHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    _selected = e;
                    _showSelectedPanel = true;
                }
                if (gHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    ImGui::OpenPopup("evt_ctx");
                    _selected = e;
                    _showSelectedPanel = true;
                }
            }
            else {
                if (gHovered) hoveredGroup = g.ev;
                if (gHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    _selected = g.ev.front();
                    _showSelectedPanel = true;
                }
                if (gHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                {
                    ImGui::OpenPopup("evt_ctx");
                    _selected = g.ev.front();
                    _showSelectedPanel = true;
                }
            }
        }
    }

    curY += kCatGap + subCount * kLaneH;
}

// ======================================================================
// 3) Timeline (main) – orchestrates: input, ruler, categories, tooltips
// ======================================================================
void ViewerApp::drawTimeline(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax)
{
    ImGuiIO& io = ImGui::GetIO();
    constexpr float kLeftPad = 150.f;
    constexpr float kTopPad = 26.f;

    const float contentW = std::max(1.0f, canvasMax.x - canvasMin.x - kLeftPad - 8.0f);

    // Interactive zone
    ImGui::SetCursorScreenPos(canvasMin);
    ImGui::InvisibleButton("timeline_canvas", ImVec2(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    // ---- Zoom cursorr (animated) ----
    if (hovered)
    {
        const float wheel = io.MouseWheel;
        const double power = 1.2;
        if (wheel != 0.f)
        {
            // Zoom “log” factor
            const double factor = std::pow(power, wheel);

            // current span as [0..1] (normale)
            const double spanN = 1.0 / std::max(1e-9, double(_vp.zoom));
            const double oldStartN = _vp.offset;
            const double oldEndN = _vp.offset + spanN;

            // cursor position in context
            const float mx = io.MousePos.x - (canvasMin.x + kLeftPad);
            const float cx = std::clamp(mx / contentW, 0.f, 1.f);

            // new span (normale) + clamp
            double newSpanN = spanN / factor;
            // bornes = [min,max] zoom
            newSpanN = std::clamp(newSpanN, 1.0 / 200000.0, 1.0 / 0.05);

            // Temporal center (normalized) under cursor
            const double tAtCursorN = oldStartN + cx * spanN;
            double newStartN = tAtCursorN - cx * newSpanN;
            newStartN = std::clamp(newStartN, 0.0, std::max(0.0, 1.0 - newSpanN));
            const double newEndN = newStartN + newSpanN;

            // animation
            _anim.begin(newStartN, newEndN, _vp.zoom, _vp.offset);
        }
    }

    // ---- Pan (keed direct, smooth mouse) ----
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const ImVec2 d = io.MouseDelta;
        const double spanN = 1.0 / std::max(1e-9, double(_vp.zoom));
        _vp.offset = std::clamp(_vp.offset - (double)d.x / contentW * spanN, 0.0,
            std::max(0.0, 1.0 - spanN));
        _vp.panY += d.y;
    }
    // Pan
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const ImVec2 d = io.MouseDelta;
        const double span = 1.0 / (double)_vp.zoom;
        _vp.offset = std::clamp(_vp.offset - (double)d.x / contentW * span,
            0.0, std::max(0.0, 1.0 - 1.0 / (double)_vp.zoom));
        _vp.panY += d.y;
    }

    // Visible window
    const double normStart = _vp.offset;
    const double normEnd = _vp.offset + 1.0 / (double)_vp.zoom;

    // draw top timeline ruler absolute
    _absRuler.draw(dl, canvasMin, canvasMax, kLeftPad, contentW, normStart, normEnd, _timeMin, _timeMax);

    // Layout categories (built on the fly)
    float curY = canvasMin.y + kTopPad + 6.f + _vp.panY;

    Event* hoveredEvent = nullptr;
    std::vector<Event*> hoveredGroup;
    size_t visibleEventsCount = 0;

    std::unordered_map<std::string, std::vector<std::vector<Event*>>> rows; rows.reserve(64);
    {
        std::lock_guard<std::mutex> lk(_mtx);

        // collect categories
        std::unordered_map<std::string, std::vector<Event*>> byCat;
        byCat.reserve(64);
        for (Event& e : _events) byCat[e.category].push_back(&e);

        // pack into non-overlapping lanes (by ts)
        for (auto& kv : byCat) {
            auto& evs = kv.second;
            std::sort(evs.begin(), evs.end(),
                [](const Event* a, const Event* b) { return a->ts < b->ts; });
            std::vector<std::vector<Event*>> lanes;
            for (Event* e : evs) {
                bool placed = false;
                for (auto& lane : lanes) {
                    if (lane.empty() || lane.back()->normEnd <= e->normStart) {
                        lane.push_back(e); placed = true; break;
                    }
                }
                if (!placed) { lanes.emplace_back(); lanes.back().push_back(e); }
            }
            rows.emplace(kv.first, std::move(lanes));
        }
    }

    // draw each category
    for (auto& kv : rows) {
        drawCategoryBlock(dl, canvasMin, canvasMax, kLeftPad,
            kv.first, kv.second,
            (double)_timeMin, (double)_timeMax,
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
        ImGui::Text("Start:    %s", fmtTime(double(e->ts - _timeMin)).c_str());
        ImGui::Text("Duration: %s", fmtTime(double(e->dur)).c_str());
        ImGui::Text("Data:     %s", e->data.c_str());
        auto it = _globalStats.find(e->name);
        const EventStats* S = (it != _globalStats.end() ? &it->second
            : (e->stats.count ? &e->stats : nullptr));
        if (S)
        {
            ImGui::Separator();
            ImGui::Text("count = %llu", (unsigned long long)S->count);
            ImGui::Text("avg   = %s", fmtTime(S->avg_us).c_str());
            ImGui::Text("min   = %s", fmtTime(double(S->min_us)).c_str());
            ImGui::Text("max   = %s", fmtTime(double(S->max_us)).c_str());
        }
        ImGui::EndTooltip();
    }
    else if (!hoveredGroup.empty()) {
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

    // Context menu
    if (ImGui::BeginPopup("evt_ctx")) {
        const bool hasSel = (_selected != nullptr);
        if (ImGui::MenuItem("Clear selection", nullptr, false, hasSel))
        {
            _selected = nullptr;
            _showSelectedPanel = false;
        }
        ImGui::EndPopup();
    }

    // Status bar (range is absolute)
    const double totalUs = std::max<double>(1.0, (double)(_timeMax - _timeMin));
    const double visStartUs = totalUs * normStart + _timeMin;
    const double visEndUs = totalUs * normEnd + _timeMin;
    const double spanUs = std::max(1.0, visEndUs - visStartUs);

    ImVec2 sMin(canvasMin.x, canvasMax.y - 22.f), sMax(canvasMax.x, canvasMax.y);
    dl->AddRectFilled(sMin, sMax, IM_COL32(18, 23, 28, 255));
    char left[160];  std::snprintf(left, sizeof(left), "Zoom: %.2fx  |  Offset: %.3f", _vp.zoom, _vp.offset);
    char right[200]; std::snprintf(right, sizeof(right), "Range: %s  |  Visible events: %zu",
        fmtTime(spanUs).c_str(), visibleEventsCount);
    dl->AddText(ImVec2(sMin.x + 8, sMin.y + 3), IM_COL32(200, 200, 200, 255), left);
    float rw = ImGui::CalcTextSize(right).x;
    dl->AddText(ImVec2(sMax.x - rw - 8, sMin.y + 3), IM_COL32(200, 200, 200, 255), right);
}
