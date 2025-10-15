#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <cstdint>

#include <imgui.h>

#include "model.hpp"
#include "color_helper.hpp"

/// @brief ViewerSelectedPanel — class/struct documentation.
class ViewerSelectedPanel
{
public:
    // Draws the info window if `sel` is not null.
    // - events/eventsMtx: full dataset to compute aggregates
    // - timeMin: to format absolute start (relative to file start)
    void draw(const Event* sel, const std::vector<Event>& events, std::mutex& eventsMtx, uint64_t timeMin, bool& p_open);
private:
    /// @brief Row — class/struct documentation.
    struct Row
    {
        std::string key;
        uint64_t count;
        double sum_us;
        double min_us;
        double max_us;
        uint64_t first_ts;
        ImU32 col_u32;

        Row()
            : key{ }
            , count{ 0 }
            , sum_us{ 0 }
            , min_us{ 1e300 }
            , max_us{ 0 }
            , first_ts{ UINT64_MAX }
            , col_u32{ 0 }
        {

        }
    };

    // Small helper to draw a nice bar with overlay text
    static void drawBar(float fraction01, const char* rightLabel, float width = 260.f, float height = 10.f, ImU32 fill = 0);
};
