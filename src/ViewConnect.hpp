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
    using OnConnect = std::function<void(const ServerInfo&)>; // on_connected se connecte
    using OnUseFile = std::function<void()>;                  // switch to file mode

    ConnectView(UdpClient& client);
    ~ConnectView();

    //  appeler  chaque frame
    void draw(ImVec2 available, OnConnect onConnect, OnUseFile onUseFile);

    // si tu veux dclencher un refresh immdiat ct UI
    void requestImmediateRefresh();
private:
    void scan();
private:
    std::vector<ServerInfo> _servers; // data affiche
    std::mutex              _mtx;
    std::atomic<bool>       _running{ false };
    std::thread             _th;

    // UI state
    UdpClient& _client;
    char    _manualHost[128] = "127.0.0.1";
    int     _manualPort = 9999;
    char    _filter[128] = "";
    bool    _needImmediateRefresh = false;
};
