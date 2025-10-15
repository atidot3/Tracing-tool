#include "ViewerTimeAbsolue.hpp"
#include "utils.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>

// ---------- heuristiques unit ----------
ViewerTimeAbsolue::UnitInfo
ViewerTimeAbsolue::pickUnit(double visibleSpanUs, float contentW) const
{
    // ~90 px by major label → how much label possible ?
    const double targetMaj = std::max(4.0f, contentW / 90.0f);
    const double usPerLabel = visibleSpanUs / targetMaj;

    if (usPerLabel >= 3600.0 * 1e6) return { Unit::Hour,  3600.0 * 1e6, "h" };
    if (usPerLabel >= 60.0 * 1e6) return { Unit::Min,     60.0 * 1e6, "min" };
    if (usPerLabel >= 1.0 * 1e6) return { Unit::Sec,      1.0 * 1e6, "s" };
    if (usPerLabel >= 1.0 * 1e3) return { Unit::Milli,    1.0 * 1e3, "ms" };
    return { Unit::Micro, 1.0, "us" };
}

double ViewerTimeAbsolue::chooseStep(double usPerUnit, double visibleSpanUs, float contentW, float targetPx) const
{
    // pick 1/2/5·10^n unite → to µs
    const double targetSteps = std::max(3.0f, contentW / targetPx);
    const double unitsSpan = visibleSpanUs / usPerUnit;
    const double rawUnits = unitsSpan / targetSteps;

    const double p10 = std::pow(10.0, std::floor(std::log10(std::max(1e-12, rawUnits))));
    double stepUnits = p10;
    if (rawUnits > 2.0 * p10) stepUnits = 2.0 * p10;
    if (rawUnits > 5.0 * p10) stepUnits = 5.0 * p10;

    return stepUnits * usPerUnit;
}

int ViewerTimeAbsolue::decimalsForStepUnits(double stepUnits)
{
    // Decimal max number from step as *unit* (not µs)
    if (stepUnits >= 1.0)  return 0;
    if (stepUnits >= 0.1)  return 1;
    if (stepUnits >= 0.01) return 2;
    return 3;
}

// Label (ex: 40 ms). retalive
std::string ViewerTimeAbsolue::formatRelative(double relUs, const UnitInfo& ui, double majorStepUs)
{
    const double vUnits = relUs / ui.usPerUnit;
    const double stepUnits = majorStepUs / ui.usPerUnit;
    const int    dec = std::min(3, std::max(0, decimalsForStepUnits(stepUnits)));

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f %s", dec, vUnits, ui.suffix);
    return buf;
}

// “+offset” (absolute since _timeMin), compact.
std::string ViewerTimeAbsolue::formatOffsetBadge(double absUs)
{
    // Priority to hight unit, 1 ou 2 max.
    char buf[64];
    if (absUs >= 3600.0 * 1e6) {
        const long long s = (long long)std::llround(absUs / 1e6);
        const long long h = s / 3600, rem = s % 3600;
        const long long m = rem / 60, sec = rem % 60;
        std::snprintf(buf, sizeof(buf), "+%lldh %02lldm %02llds", h, m, sec);
    }
    else if (absUs >= 60.0 * 1e6) {
        const long long s = (long long)std::llround(absUs / 1e6);
        const long long m = s / 60, sec = s % 60;
        std::snprintf(buf, sizeof(buf), "+%lldm %02llds", m, sec);
    }
    else if (absUs >= 1.0 * 1e6) {
        const double sec = absUs / 1e6;
        std::snprintf(buf, sizeof(buf), "+%.3fs", sec);
    }
    else if (absUs >= 1.0 * 1e3) {
        const double ms = absUs / 1e3;
        std::snprintf(buf, sizeof(buf), "+%.3fms", ms);
    }
    else {
        std::snprintf(buf, sizeof(buf), "+%.0fus", absUs);
    }
    return buf;
}

// -------------------- draw --------------------
void ViewerTimeAbsolue::draw(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax, float leftPad, float contentW, double normStart, double normEnd, unsigned long long timeMin, unsigned long long timeMax) const
{
    // Window visible as µs absolus
    const double totalUs = std::max(1.0, double(timeMax - timeMin));
    const double visStart = totalUs * normStart + double(timeMin);
    const double visEnd = totalUs * normEnd + double(timeMin);
    const double spanUs = std::max(1.0, visEnd - visStart);

    // Unit
    const UnitInfo ui = pickUnit(spanUs, contentW);
    const double   majorStep = chooseStep(ui.usPerUnit, spanUs, contentW, 110.0f);
    const int      minorCnt = (majorStep >= 1e6) ? 5 : 4;
    const double   minorStep = majorStep / double(minorCnt);

    // Base aligned (last multiple of majorStep before window)
    const double baseUs = std::floor(visStart / majorStep) * majorStep;

    // Style
    const float vx1 = canvasMin.x + leftPad;
    const float vx2 = canvasMax.x - 6.0f;
    const float rulerTop = canvasMin.y;
    const float majorH = 8.0f;
    const float minorH = 4.0f;
    const ImU32 tickCol = IM_COL32(150, 150, 150, 180);
    const ImU32 textCol = IM_COL32(200, 200, 200, 210);

    // Anchors trait
    dl->AddLine(ImVec2(vx1, rulerTop), ImVec2(vx2, rulerTop), IM_COL32(70, 80, 90, 120), 1.0f);

    // Badge “+offset” (absolute beautifuly)
    {
        const std::string off = formatOffsetBadge(baseUs - double(timeMin));
        auto text_size = ImGui::CalcTextSize(off.c_str()).x;
        dl->AddText(ImVec2((vx1 - 4.0f) - text_size, rulerTop + majorH + 1.0f), textCol, off.c_str());
    }

    // First visible major index
    const double firstKRaw = (visStart - baseUs) / majorStep;
    int          k = (int)std::ceil(firstKRaw - 1e-9);

    // Safe labels
    const float minLabelPx = 90.0f;
    float lastLabelX = -1e9f;

    // majors visibles (+ marge one step)
    for (;; ++k)
    {
        const double majorUsK = baseUs + k * majorStep;
        if (majorUsK > visEnd + majorStep) break;

        const float x = xFromAbsUs(majorUsK, canvasMin, leftPad, contentW,
            normStart, normEnd, timeMin, timeMax);
        if (x >= vx1 - 1.0f && x <= vx2 + 1.0f)
        {
            // Trait major
            dl->AddLine(ImVec2(x, rulerTop), ImVec2(x, rulerTop + majorH), tickCol);

            // Label RELATIF to base (1st not “0” if base before screen)

            // 0, step, 2*step, …
            const double relUs = majorUsK - baseUs;
            if (x - lastLabelX >= minLabelPx)
            {
                const std::string label = formatRelative(relUs, ui, majorStep);
                dl->AddText(ImVec2(x + 3.0f, rulerTop + majorH), textCol, label.c_str());
                lastLabelX = x;
            }
        }

        // Ticks minor between major[k] and major[k+1]
        for (int i = 1; i < minorCnt; ++i)
        {
            const double mu = majorUsK + i * minorStep;
            if (mu > visEnd)  break;
            if (mu < visStart) continue;

            const float mx = xFromAbsUs(mu, canvasMin, leftPad, contentW,
                normStart, normEnd, timeMin, timeMax);
            if (mx >= vx1 - 1.0f && mx <= vx2 + 1.0f)
                dl->AddLine(ImVec2(mx, rulerTop), ImVec2(mx, rulerTop + minorH), tickCol);
        }
    }
}
