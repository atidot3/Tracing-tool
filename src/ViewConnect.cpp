#include "ViewConnect.hpp"

#include <chrono>
#include <algorithm>

ConnectView::ConnectView(UdpClient& client)
    : _client{ client }
{
}

ConnectView::~ConnectView()
{
}

void ConnectView::requestImmediateRefresh()
{
    _needImmediateRefresh = true;
}

void ConnectView::scan()
{
    std::vector<std::string> data;
    _client.tick(data);
    if (_servers.empty())
        _servers = _client.scan();
}

void ConnectView::draw(ImVec2 available, OnConnect onConnect, OnUseFile onUseFile)
{
    scan();

    ImGui::BeginChild("connect_root", available, true);

    ImGui::TextUnformatted("Live servers");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) requestImmediateRefresh();

    ImGui::SetNextItemWidth(220);
    ImGui::InputText("Filter", _filter, sizeof(_filter));

    ImGui::Separator();

    // Filter
    auto match = [&](const ServerInfo& s)->bool
    {
        if (_filter[0] == 0)
            return true;

        std::string f(_filter);
        std::string n = s.name, h = s.ip;
        std::transform(f.begin(), f.end(), f.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);

        return (n.find(f) != std::string::npos) || (h.find(f) != std::string::npos);
    };
    //_servers.erase(std::remove_if(_servers.begin(), _servers.end(), [&](const ServerInfo& s) {return !match(s); }), _servers.end());

    // Table
    if (ImGui::BeginTable("servers_tbl", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Host");
        ImGui::TableSetupColumn("Port");
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();

        for (const auto& s : _servers)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(s.ip.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%hu", s.port);
            ImGui::TableSetColumnIndex(3);
            std::string btn = std::string("Connect##") + s.name;
            if (ImGui::SmallButton(btn.c_str()))
            {
                if (onConnect)
                    onConnect(s);
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
            ServerInfo s; s.name = "Manual"; s.ip = _manualHost; s.port = (uint16_t)_manualPort;
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
