#pragma once

#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <imgui.h>

#include "udp_client.hpp"

/// @brief ConnectView — class/struct documentation.
class ConnectView
{
public:
    // Callbacks (ViewerApp)
    using OnConnect = std::function<void(const ServerInfo&)>;
    using OnUseFile = std::function<void(const std::string_view path)>;

    ConnectView(UdpClient& client);
    ~ConnectView();

    void draw(ImVec2 available, OnConnect onConnect, OnUseFile onUseFile);
    void requestImmediateRefresh();
private:
    void scan();
private:
    std::vector<ServerInfo> _servers;
    std::atomic<bool>       _running;
    std::chrono::steady_clock::time_point _last_scan;

    // UI state
    UdpClient& _client;
    char    _manualHost[128];
    int     _manualPort;
    char    _filter[128];
    bool    _needImmediateRefresh;
    char    _filePath[512];
    float   _splitRatio = 0.55f;
};
