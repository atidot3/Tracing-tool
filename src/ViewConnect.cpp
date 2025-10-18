#include "ViewConnect.hpp"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <chrono>
#include <algorithm>
#include <ranges>
#include <queue>

using namespace std::chrono_literals;


static void DrawDashedRect(ImDrawList* dl, const ImRect& r, ImU32 col, float thickness, float dash_len, float gap_len, float phase)
{
    auto draw_edge = [&](ImVec2 a, ImVec2 b)
        {
            ImVec2 dir = ImVec2(b.x - a.x, b.y - a.y);
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len <= 0.001f) return;
            dir.x /= len; dir.y /= len;

            float t = std::fmod(phase, dash_len + gap_len);
            float x = 0.0f;
            while (x < len)
            {
                float seg = std::min(dash_len - t, len - x);
                if (seg > 0.0f)
                {
                    ImVec2 p0 = ImVec2(a.x + (x)*dir.x, a.y + (x)*dir.y);
                    ImVec2 p1 = ImVec2(a.x + (x + seg) * dir.x, a.y + (x + seg) * dir.y);
                    dl->AddLine(p0, p1, col, thickness);
                }
                x += (dash_len - t) + gap_len;
                t = 0.0f;
            }
        };

    draw_edge(ImVec2(r.Min.x, r.Min.y), ImVec2(r.Max.x, r.Min.y)); // top
    draw_edge(ImVec2(r.Max.x, r.Min.y), ImVec2(r.Max.x, r.Max.y)); // right
    draw_edge(ImVec2(r.Max.x, r.Max.y), ImVec2(r.Min.x, r.Max.y)); // bottom
    draw_edge(ImVec2(r.Min.x, r.Max.y), ImVec2(r.Min.x, r.Min.y)); // left
}

// Retourne {hovered, rect}. Utilise InvisibleButton pour la hitbox.
static std::pair<bool, ImRect> FancyDropZone(const char* id, float height = 140.0f, float rounding = 12.0f)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 size = ImVec2(std::max(120.0f, avail.x), height);
    ImRect r(p0, ImVec2(p0.x + size.x, p0.y + size.y));

    // Hitbox
    ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();

    // Colors
    ImU32 bg0 = ImGui::GetColorU32(ImGuiCol_FrameBg);
    ImU32 bg1 = ImGui::GetColorU32(ImGuiCol_WindowBg);
    ImU32 border_col = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_Border);
    ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
    ImU32 text_muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    auto* dl = ImGui::GetWindowDrawList();

    // Shadow subtle
    dl->AddRectFilled(ImVec2(r.Min.x, r.Max.y - 6), ImVec2(r.Max.x, r.Max.y + 10), IM_COL32(0, 0, 0, 40), rounding, 0);

    // Background gradient
    dl->AddRectFilledMultiColor(
        r.Min, r.Max,
        IM_COL32((bg0 >> 0) & 255, (bg0 >> 8) & 255, (bg0 >> 16) & 255, 210),
        IM_COL32((bg0 >> 0) & 255, (bg0 >> 8) & 255, (bg0 >> 16) & 255, 210),
        IM_COL32((bg1 >> 0) & 255, (bg1 >> 8) & 255, (bg1 >> 16) & 255, 230),
        IM_COL32((bg1 >> 0) & 255, (bg1 >> 8) & 255, (bg1 >> 16) & 255, 230)
    );
    // Inner card
    ImRect inner(ImVec2(r.Min.x + 6, r.Min.y + 6), ImVec2(r.Max.x - 6, r.Max.y - 6));
    dl->AddRectFilled(inner.Min, inner.Max, ImGui::GetColorU32(ImGuiCol_ChildBg), rounding);

    // Animated dashed border
    float t = static_cast<float>(ImGui::GetTime());
    float dash_len = 8.0f, gap_len = 6.0f, thickness = 1.6f;
    float phase = hovered ? std::fmod(t * 60.0f, dash_len + gap_len) : 0.0f;
    DrawDashedRect(dl, inner, border_col, thickness, dash_len, gap_len, phase);

    // Little “cloud + arrow” icon (vector-ish)
    ImVec2 c = ImVec2((inner.Min.x + inner.Max.x) * 0.5f, inner.Min.y + 42.0f);
    // Cloud
    dl->AddCircleFilled(ImVec2(c.x - 18, c.y), 10, IM_COL32(255, 255, 255, 30));
    dl->AddCircleFilled(ImVec2(c.x - 6, c.y - 8), 12, IM_COL32(255, 255, 255, 30));
    dl->AddCircleFilled(ImVec2(c.x + 10, c.y), 12, IM_COL32(255, 255, 255, 30));
    dl->AddRectFilled(ImVec2(c.x - 20, c.y), ImVec2(c.x + 20, c.y + 12), IM_COL32(255, 255, 255, 30), 6.0f);
    // Arrow
    ImU32 ar = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    dl->AddLine(ImVec2(c.x, c.y - 12), ImVec2(c.x, c.y + 12), ar, 2.0f);
    dl->AddTriangleFilled(ImVec2(c.x - 6, c.y + 6), ImVec2(c.x + 6, c.y + 6), ImVec2(c.x, c.y + 14), ar);

    // Text centered
    const char* title = "Drop your file here";
    ImVec2 tsz = ImGui::CalcTextSize(title);
    ImVec2 tc = ImVec2((inner.Min.x + inner.Max.x - tsz.x) * 0.5f, c.y + 24.0f);
    dl->AddText(tc, text_col, title);
    ImGui::EndChild();
    return { hovered, inner };
}

namespace dnd {
    inline std::mutex mtx;
    inline std::queue<std::string> queue;

    inline void push(std::string path) {
        std::scoped_lock lk(mtx);
        queue.push(std::move(path));
    }
    inline std::optional<std::string> pop() {
        std::scoped_lock lk(mtx);
        if (queue.empty()) return std::nullopt;
        auto s = std::move(queue.front());
        queue.pop();
        return s;
    }
}

ConnectView::ConnectView(UdpClient& client)
    : _servers{ }
    , _running{ false }
    , _last_scan{ std::chrono::steady_clock::now() }
    , _client{ client }
    , _manualHost{ "127.0.0.1" }
    , _manualPort{ 9999 }
    , _filter{ }
    , _needImmediateRefresh{ true }
    , _filePath{ }
    , _splitRatio{ 0.55f }
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

    auto now = std::chrono::steady_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - _last_scan);
    if (dur > 5s || _needImmediateRefresh)
    {
        _servers = _client.scan();
        _needImmediateRefresh = false;
    }
}

void ConnectView::draw(ImVec2 available, OnConnect onConnect, OnUseFile onUseFile)
{
    scan();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float splitterW = 6.0f;
    float leftW = std::max(120.0f, avail.x * _splitRatio - splitterW * 0.5f);
    float rightW = std::max(120.0f, avail.x - leftW - splitterW);

    ImGui::BeginChild("connect_left", ImVec2(leftW, avail.y), true);
    ImGui::TextUnformatted("Live servers");
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) requestImmediateRefresh();

    ImGui::SetNextItemWidth(220);
    ImGui::InputText("Filter", _filter, sizeof(_filter));
    ImGui::Separator();

    auto match = [&](const ServerInfo& s)->bool
    {
        if (_filter[0] == 0) return true;

        std::string f(_filter);
        std::string n = s.name, h = s.ip;
        std::transform(f.begin(), f.end(), f.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);

        return (n.find(f) != std::string::npos) || (h.find(f) != std::string::npos);
    };

    if (ImGui::BeginTable("servers_tbl", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Host");
        ImGui::TableSetupColumn("Port");
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableHeadersRow();

        for (const auto& s : _servers | std::views::filter(match))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(s.ip.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%hu", s.port);
            ImGui::TableSetColumnIndex(3);
            std::string btn = std::string("Connect##") + s.name;
            if (ImGui::SmallButton(btn.c_str()))
                if (onConnect) onConnect(s);
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Manual connection");
    ImGui::SetNextItemWidth(240); ImGui::InputText("Host", _manualHost, sizeof(_manualHost));
    ImGui::SetNextItemWidth(120); ImGui::InputInt("Port", &_manualPort);
    if (_manualPort < 0) _manualPort = 0;
    if (_manualPort > 65535) _manualPort = 65535;

    bool canConnect = _manualHost[0] && _manualPort > 0;
    ImGui::BeginDisabled(!canConnect);
    if (ImGui::Button("Connect", ImVec2(120, 0)))
    {
        ServerInfo s; s.name = "Manual"; s.ip = _manualHost; s.port = (uint16_t)_manualPort;
        if (onConnect) onConnect(s);
    }
    ImGui::EndDisabled();

    ImGui::EndChild();


    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton("##splitter_v", ImVec2(splitterW, avail.y));
    bool splitterActive = ImGui::IsItemActive();
    bool splitterHovered = ImGui::IsItemHovered();
    if (splitterHovered || splitterActive) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (splitterActive) {
        float delta = ImGui::GetIO().MouseDelta.x;
        _splitRatio = (_splitRatio * avail.x + delta) / avail.x;
        _splitRatio = std::clamp(_splitRatio, 0.2f, 0.8f);
    }

    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_Separator));


    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("connect_right", ImVec2(rightW, avail.y), true);

    ImGui::TextUnformatted("File path");
    ImGui::TextDisabled("Please enter file path or just drag and drop.");
    ImGui::SetNextItemWidth(-120);
    ImGui::InputText("##filePath", _filePath, sizeof(_filePath));
    ImGui::SameLine();
    bool canOpen = _filePath[0] != '\0';
    ImGui::BeginDisabled(!canOpen);
    if (ImGui::Button("Open", ImVec2(110, 0)))
    {
        if (onUseFile)
            onUseFile(_filePath);
    }
    ImGui::EndDisabled();

    ImGui::SetNextItemWidth(-120);

    if (GLFWwindow* window = glfwGetCurrentContext())
    {
        glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths)
        {
            for (int i = 0; i < count; ++i)
                dnd::push(paths[i]);
        });
    }

    auto [hovered, rect] = FancyDropZone("##drop_zone");
    for (int i = 0; i < 16; ++i) {
        if (auto dropped = dnd::pop()) {
            if (hovered) {
                std::strncpy(_filePath, dropped->c_str(), sizeof(_filePath) - 1);
                _filePath[sizeof(_filePath) - 1] = '\0';
                if (onUseFile) onUseFile(_filePath);
            }
            else {
                // Option: quand même remplir le champ, sans ouvrir
                std::strncpy(_filePath, dropped->c_str(), sizeof(_filePath) - 1);
                _filePath[sizeof(_filePath) - 1] = '\0';
            }
        }
        else break;
    }

    _needImmediateRefresh = false;
}
