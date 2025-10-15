#include "ViewConnect.hpp"
#include <chrono>
#include <algorithm>

ConnectView::ConnectView()
{
    startScanner();
}

ConnectView::~ConnectView()
{
    stopScanner();
}

void ConnectView::startScanner()
{
    stopScanner();
    _running = true;
    _th = std::thread(&ConnectView::scanLoop, this);
}

void ConnectView::stopScanner()
{
    if (_running.exchange(false))
    {
        if (_th.joinable())
            _th.join();
    }
}

void ConnectView::requestImmediateRefresh()
{
    _needImmediateRefresh = true;
}

// Thread scan : MOCK default (cycle few serveurs demo effect).
void ConnectView::scanLoop()
{
    using namespace std::chrono_literals;
    double t = 0.0;
    while (_running)
    {
        {
            std::lock_guard<std::mutex> lk(_mtx);
            const double now = t;

            // remove all 10s older entry
            _servers.erase(std::remove_if(_servers.begin(), _servers.end(), [&](const ServerInfo& s) { return (now - s._last_seen_s) > 10.0; }), _servers.end());

            // MOCK: “discovery” periodicly 2 servers
            // S1
            {
                auto it = std::find_if(_servers.begin(), _servers.end(), [](const ServerInfo& s) { return s._name == "Profiler A"; });
                if (it == _servers.end())
                {
                    _servers.push_back(ServerInfo{ "Profiler A", "192.168.0.10", 9999, t });
                }
                else
                {
                    const_cast<ServerInfo&>(*it)._last_seen_s = t;
                }
            }
            // S2
            if (((int)t % 6) < 4) { // up/down simulation
                auto it = std::find_if(_servers.begin(), _servers.end(),[](const ServerInfo& s) { return s._name == "Profiler B"; });
                if (it == _servers.end())
                {
                    _servers.push_back(ServerInfo{ "Profiler B", "192.168.0.42", 10001, t });
                }
                else
                {
                    const_cast<ServerInfo&>(*it)._last_seen_s = t;
                }
            }
        }
        for (int i = 0; i < 10 && _running; i++) std::this_thread::sleep_for(100ms), t += 0.1;
    }
}

void ConnectView::draw(ImVec2 available, OnConnect onConnect, OnUseFile onUseFile) {
    // time (approx) pour last_seen
    _now_s += ImGui::GetIO().DeltaTime;

    ImGui::BeginChild("connect_root", available, true);

    ImGui::TextUnformatted("Live servers");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) requestImmediateRefresh();

    ImGui::SameLine();
    ImGui::Checkbox("Only alive (<3s)", &_onlyAlive);
    ImGui::SameLine();
    ImGui::Checkbox("Sort by recent", &_sortByRecent);

    ImGui::SetNextItemWidth(220);
    ImGui::InputText("Filter", _filter, sizeof(_filter));

    ImGui::Separator();

    // Snapshot côté UI
    std::vector<ServerInfo> list;
    {
        std::lock_guard<std::mutex> lk(_mtx);
        list = _servers;
    }

    // Filter
    auto match = [&](const ServerInfo& s)->bool {
        if (_onlyAlive && (_now_s - s._last_seen_s) > 3.0) return false;
        if (_filter[0] == 0) return true;
        std::string f(_filter);
        std::string n = s._name, h = s._host;
        std::transform(f.begin(), f.end(), f.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        return (n.find(f) != std::string::npos) || (h.find(f) != std::string::npos);
        };
    list.erase(std::remove_if(list.begin(), list.end(), [&](const ServerInfo& s) {return !match(s); }), list.end());

    // Sort
    if (_sortByRecent) {
        std::sort(list.begin(), list.end(), [&](const ServerInfo& a, const ServerInfo& b) {
            return a._last_seen_s > b._last_seen_s;
            });
    }

    // Table
    if (ImGui::BeginTable("servers_tbl", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Host");
        ImGui::TableSetupColumn("Port");
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();

        for (const auto& s : list) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(s._name.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(s._host.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%hu", s._port);
            ImGui::TableSetColumnIndex(3);
            bool alive = (_now_s - s._last_seen_s) <= 3.0;
            ImGui::BeginDisabled(!alive);
            if (alive)
            {
                std::string btn = std::string("Connect##") + s._name;
                if (ImGui::SmallButton(btn.c_str())) {
                    if (onConnect) onConnect(s);
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Manual connection");
    ImGui::SetNextItemWidth(240); ImGui::InputText("Host", _manualHost, sizeof(_manualHost));
    ImGui::SetNextItemWidth(120); ImGui::InputInt("Port", &_manualPort);
    if (_manualPort < 0) _manualPort = 0; if (_manualPort > 65535) _manualPort = 65535;
    bool canConnect = _manualHost[0] && _manualPort > 0;
    ImGui::BeginDisabled(!canConnect);
    if (canConnect)
    {
        if (ImGui::Button("Connect", ImVec2(120, 0)))
        {
            ServerInfo s; s._name = "Manual"; s._host = _manualHost; s._port = (uint16_t)_manualPort; s._last_seen_s = _now_s;
            if (onConnect) onConnect(s);
        }
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Use file mode", ImVec2(140, 0))) {
        if (onUseFile) onUseFile();
    }

    ImGui::EndChild();

    // “Refresh” manuel: on prévient le thread via un flag → ici mock: rien à faire (le thread tourne).
    _needImmediateRefresh = false;
}
