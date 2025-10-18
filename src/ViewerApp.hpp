#pragma once
#include "ViewerTimeAbsolue.hpp"
#include "ViewerSelectedPanel.hpp"
#include "ViewportAnim.hpp"
#include "ViewConnect.hpp"
#include "model.hpp"
#include "filter.hpp"

#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>
#include <filesystem>

/// @brief ViewerApp — class/struct documentation.
class ViewerApp
{
    enum class AppView { Startup, Text, Live };
public:
    ViewerApp();
    ~ViewerApp();

    void drawUI();
    bool loadFile(const char* path, uint64_t durMinUs);
    bool reloadFilePreserveView(uint64_t durMinUs);
    void updateAutoReload(const char* path);

private:
    /// @brief Viewport — class/struct documentation.
    struct Viewport
    {
        // how many “screens” fit in total
        float  zoom;
        // normalized left bound
        double offset;
        // vertical pan in pixels
        float  panY;

        Viewport()
            : zoom(1.f)
            , offset(0.0)
            , panY(0.f)
        {

        }
    };

    //
    void cleanup();
    // live pass
    void tick_live();
    // rendering helpers
    void drawMenu();
    void drawCategoryBlock(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax, float leftPad, const std::string& catName, const std::vector<std::vector<Event*>>& lanes, uint64_t timeMin, uint64_t timeMax, double normStart, double normEnd, float& curY, Event*& hoveredEvent, std::vector<Event*>& hoveredGroup, size_t& visibleEventsCount);
    void drawTimeline(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax);
    void drawEventBox(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, ImU32 color, bool hovered, bool selected);
    void drawTopBottomAccent(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, ImU32 topColor, ImU32 bottomColor);
    void drawCenteredLabel(ImDrawList* dl, const ImVec2& p1, const ImVec2& p2, const char* text, ImU32 color);
    void drawMetricsBottom(ImDrawList* dl, const ImVec2& canvasMin, const ImVec2& canvasMax, float leftPad, float contentW, float startY, double normStart, double normEnd);

    // file mtimes
    bool getFileMTime(const char* path, std::filesystem::file_time_type& out) const;

    // filters
    bool passDataFilter(const Event& e);
    void compileDataFilterIfNeeded();
private:
    std::vector<Event> _events;
    // by name
    std::unordered_map<std::string, EventStats> _globalStats;
    std::vector<Metric> _metrics;
    std::mutex _mtxMetrics;

    uint64_t _timeMin, _timeMax;

    Viewport _vp;
    Event* _selected;

    // UI
    int  _dur_min_us;
    bool _parsing;
    size_t _parsedCount;
    char _filepath[1024];
    std::string _lastError;

    // auto reload
    bool _autoReload;
    // seconds
    float _autoReloadInterval;
    double _autoReloadTimer;
    std::filesystem::file_time_type _fileMTime;

    // concurrency
    std::mutex _mtx;

    // viewport
    AppView     _view;
    UdpClient _client;
    ConnectView _connectView;

    ViewportAnim _anim;
    ViewerTimeAbsolue _absRuler;

    ViewerSelectedPanel _selectedPanel;
    bool _showSelectedPanel;

    // filtering
    char _dataFilter[128];
    bool _dataFilterCaseSensitive;
    bool _dataFilterRegex;
    // compiled cache
    CompiledFilter _compiledFilter;
    std::string _filter_cached;
    bool _filter_case_cached = false;
    bool _filter_regex_cached = false;
    // metrics sorted flag
    bool _metricsSorted = false;
    mutable size_t _filteredVisible;
};
