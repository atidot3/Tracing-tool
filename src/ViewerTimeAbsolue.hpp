#pragma once
#include <imgui.h>
#include <string>

class ViewerTimeAbsolue {
public:
    enum class Unit { Micro, Milli, Sec, Min, Hour };

    struct UnitInfo {
        Unit   kind;
        double usPerUnit;   // to µs
        const char* suffix; // "µs","ms","s","min","h"
    };

    // Draw absolute line (top)
    // timeMin/timeMax : absolues (µs)
    // normStart/normEnd : window visible normalized [0..1]
    void draw(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax, float leftPad, float contentW, double normStart, double normEnd, unsigned long long timeMin, unsigned long long timeMax) const;

private:
    // pick unit and not span visibility
    UnitInfo pickUnit(double visibleSpanUs, float contentW) const;
    double   chooseStep(double usPerUnit, double visibleSpanUs, float contentW, float targetPx) const;

    // x screen from absolute time (µs)
    static float xFromAbsUs(double absUs, const ImVec2& canvasMin, float leftPad, float contentW, double normStart, double normEnd, unsigned long long timeMin, unsigned long long timeMax);

    // Format label **base related** (ex: 40 ms, 2 s, …)
    static std::string formatRelative(double relUs, const UnitInfo& ui, double majorStepUs);

    // Text “+offset” on left (**absolue** value tronqued)
    static std::string formatOffsetBadge(double absUsFromMin);

    // Utils
    static int decimalsForStepUnits(double stepUnits);
};
