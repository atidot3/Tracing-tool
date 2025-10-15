#pragma once
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <imgui.h>

// 
struct ServerInfo
{
    std::string _name;               // ex: "Profiler @ host123"
    std::string _host;               // ex: "192.168.1.23"
    uint16_t    _port;               // ex: 9999
    double      _last_seen_s;        // refresh scanner

    ServerInfo(std::string& name, std::string& host, uint16_t& port, double& last_seen)
        : _name{ name }
        , _host{ host }
        , _port{ port }
        , _last_seen_s{ last_seen }
    {

    }
    ServerInfo(std::string_view name, std::string_view host, uint16_t port, double last_seen)
        : _name{ name }
        , _host{ host }
        , _port{ port }
        , _last_seen_s{ last_seen }
    {

    }
    ServerInfo()
        : _name{}
        , _host{}
        , _port{ 0 }
        , _last_seen_s{ 0.0 }
    {

    }
};

class ConnectView
{
public:
    // Callbacks (ViewerApp)
    using OnConnect = std::function<void(const ServerInfo&)>; // on_connected se connecte
    using OnUseFile = std::function<void()>;                  // switch to file mode

    ConnectView();
    ~ConnectView();

    // à appeler à chaque frame
    void draw(ImVec2 available, OnConnect onConnect, OnUseFile onUseFile);

    // si tu veux déclencher un refresh immédiat côté UI
    void requestImmediateRefresh();

private:
    // --- scanning thread (mock par défaut) ---
    void startScanner();
    void stopScanner();
    void scanLoop();

private:
    std::vector<ServerInfo> _servers; // data affichée
    std::mutex              _mtx;
    std::atomic<bool>       _running{ false };
    std::thread             _th;
    double                  _now_s = 0.0; // alimenté par draw()

    // UI state
    char    _manualHost[128] = "127.0.0.1";
    int     _manualPort = 9999;
    char    _filter[128] = "";
    bool    _onlyAlive = true;
    bool    _sortByRecent = true;
    bool    _needImmediateRefresh = false;
};
