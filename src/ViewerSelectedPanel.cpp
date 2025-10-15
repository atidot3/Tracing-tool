#include "ViewerSelectedPanel.hpp"
#include "utils.hpp"
#include <unordered_map>
#include <mutex>
#include <algorithm>

// -------------------------------------------------------------
// Samll bar filled with texte overlay (percentages)
// -------------------------------------------------------------
/*static*/ void ViewerSelectedPanel::drawBar(float fraction01, const char* rightLabel, float width, float height, ImU32 fill)
{
    fraction01 = std::clamp(fraction01, 0.0f, 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p1 = ImGui::GetCursorScreenPos();
    ImVec2 p2 = ImVec2(p1.x + width, p1.y + height);

    // font
    dl->AddRectFilled(p1, p2, IM_COL32(35, 40, 45, 255), 3.0f);

    // bar
    float w = width * fraction01;
    if (w > 1.0f) {
        dl->AddRectFilled(p1, ImVec2(p1.x + w, p2.y), fill ? fill : IM_COL32(255, 156, 74, 220), 3.0f);
    }
    // border
    dl->AddRect(p1, p2, IM_COL32(0, 0, 0, 140), 3.0f, 0, 1.0f);

    // right label
    ImGui::SetCursorScreenPos(ImVec2(p2.x + 8, p1.y - 2));
    ImGui::TextUnformatted(rightLabel);

    // step cursor
    ImGui::SetCursorScreenPos(ImVec2(p1.x, p2.y + 6));
}

// -------------------------------------------------------------
// Selected event information screen
// -------------------------------------------------------------
void ViewerSelectedPanel::draw(const Event* sel, const std::vector<Event>& events, std::mutex& eventsMtx, uint64_t timeMin, bool& p_open)
{
    if (!sel) return;

    // --- Focus auto ---
    static const Event* s_lastSel = nullptr;
    bool wantFocus = false;
    if (!s_lastSel || s_lastSel != sel) { s_lastSel = sel; wantFocus = true; }
    if (wantFocus) ImGui::SetNextWindowFocus();

    ImGui::SetNextWindowSize(ImVec2(530, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("Event info", &p_open, flags)) {
        ImGui::End();
        return;
    }

    // Title
    ImGui::PushStyleColor(ImGuiCol_Text, color::getColorU32(sel->color));
    ImGui::Text("%s", sel->name.empty() ? sel->category.c_str() : sel->name.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();

    // raw element data
    ImGui::Text("Category : %s", sel->category.c_str());
    ImGui::Text("Start     : %s", fmtTime(double(sel->ts - timeMin)).c_str());
    ImGui::Text("Duration  : %s", fmtTime(double(sel->dur)).c_str());
    ImGui::Text("Data      : %s", sel->data.empty() ? "-" : sel->data.c_str());
    ImGui::Spacing();

    // ================== Aggregate ==================
    // -> Global stats : same category & name
    // -> Enfants : all events with same data as 'sel', grouped by type (category::name)
    uint64_t gCount = 0;
    double   gSumUs = 0.0, gMinUs = 1e300, gMaxUs = 0.0;
    // key = "category::name"
    std::unordered_map<std::string, Row> byType;

    const std::string selData = sel->data;
    const bool hasSelData = !selData.empty();

    {
        std::lock_guard<std::mutex> lk(eventsMtx);
        for (const Event& e : events)
        {
            // ---- Global stats on selection ----
            if (e.category == sel->category && e.name == sel->name)
            {
                const double d = double(e.dur);
                gCount++;
                gSumUs += d;
                gMinUs = std::min(gMinUs, d);
                gMaxUs = std::max(gMaxUs, d);
            }

            // ---- Children : same data as selected, <peu importe le type whatever type ----
            if (hasSelData && e.data == selData)
            {
                const std::string typeKey = e.category + "::" + (e.name.empty() ? e.category : e.name);
                auto& row = byType[typeKey];

                // init only once
                if (row.first_ts == UINT64_MAX)
                {
                    row.key = typeKey;
                    row.col_u32 = color::getColorU32(e.color);
                    row.first_ts = e.ts;
                    row.min_us = 1e300;
                    row.max_us = 0.0;
                    row.sum_us = 0.0;
                    row.count = 0;
                }

                // aggragate
                const double d = double(e.dur);
                row.count += 1;
                row.sum_us += d;
                row.min_us = std::min(row.min_us, d);
                row.max_us = std::max(row.max_us, d);
                if (e.ts < row.first_ts) row.first_ts = e.ts;
            }
        }
    }

    // ================== Global stats (same category & name) ==================
    if (ImGui::CollapsingHeader("Global stats (same category & name)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (gCount <= 1) {
            ImGui::Text("Single sample");
            ImGui::Text("This occurrence: %s", fmtTime(double(sel->dur)).c_str());
        }
        else {
            const double gAvgUs = gSumUs / double(gCount);
            ImGui::Text("count = %llu", (unsigned long long)gCount);
            ImGui::Text("avg   = %s", fmtTime(gAvgUs).c_str());
            ImGui::Text("min   = %s", fmtTime(gMinUs).c_str());
            ImGui::Text("max   = %s", fmtTime(gMaxUs).c_str());
        }
    }

    ImGui::Spacing();

    // ================== Group by same data (all types) ==================
    if (ImGui::CollapsingHeader("Group by data (same 'data' across all types)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // sum all percent
        double totalSum = 0.0;
        for (const auto& kv : byType)
            totalSum += kv.second.sum_us;

        // same type selected first (if present), then sum DESC
        std::vector<Row> rows; rows.reserve(byType.size());
        for (const auto& kv : byType)
            rows.push_back(kv.second);

        const std::string selTypeKey = sel->category + std::string("::") +
            (sel->name.empty() ? sel->category : sel->name);

        std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b)
        {
                if (a.first_ts != b.first_ts) return a.first_ts < b.first_ts;
                return a.key < b.key;
        });

        for (const Row& r : rows)
        {
            const double avg = (r.count ? r.sum_us / double(r.count) : 0.0);
            const float fract = (totalSum > 0.0) ? float(r.sum_us / totalSum) : 0.0f;

            ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(40, 40, 45, 180));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(55, 55, 60, 200));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(55, 55, 60, 220));
            bool open = ImGui::CollapsingHeader((r.key + "  (count=" + std::to_string(r.count) + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor(3);

            char right[128];
            std::snprintf(right, sizeof(right), "%.1f%%  (%s)", 100.0 * double(fract), fmtTime(r.sum_us).c_str());
            drawBar(fract, right, 260.0f, 10.0f, r.col_u32);

            if (open) {
                ImGui::Indent();
                ImGui::Text("sum=%s   avg=%s   min=%s   max=%s",
                    fmtTime(r.sum_us).c_str(),
                    fmtTime(avg).c_str(),
                    fmtTime(r.min_us).c_str(),
                    fmtTime(r.max_us).c_str());
                ImGui::Unindent();
            }
            ImGui::Spacing();
        }

        if (rows.empty()) {
            if (hasSelData) ImGui::TextDisabled("No events share this data value.");
            else            ImGui::TextDisabled("Selected event has empty 'data'.");
        }
    }

    ImGui::End();
}
