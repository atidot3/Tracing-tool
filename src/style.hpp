#pragma once
#include <imgui.h>

// Call this once after ImGui::CreateContext() and before your first frame.
inline void SetupImGuiStylePro()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 8.0f;
    s.FrameRounding  = 6.0f;
    s.ChildRounding  = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding = 6.0f;
    s.PopupRounding = 8.0f;
    s.TabRounding = 8.0f;

    s.FramePadding = ImVec2(10, 8);
    s.ItemSpacing  = ImVec2(9, 8);
    s.WindowPadding= ImVec2(14, 12);

    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize  = 1.0f;
    s.PopupBorderSize  = 1.0f;

    // Subtle, modern dark palette
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]           = ImVec4(0.88f, 0.92f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled]   = ImVec4(0.55f, 0.62f, 0.68f, 1.00f);
    c[ImGuiCol_WindowBg]       = ImVec4(0.09f, 0.11f, 0.14f, 1.00f);
    c[ImGuiCol_ChildBg]        = ImVec4(0.08f, 0.10f, 0.13f, 0.70f);
    c[ImGuiCol_PopupBg]        = ImVec4(0.10f, 0.13f, 0.16f, 0.98f);
    c[ImGuiCol_Border]         = ImVec4(0.15f, 0.18f, 0.21f, 0.60f);
    c[ImGuiCol_BorderShadow]   = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.12f, 0.16f, 0.19f, 0.80f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.22f, 0.25f, 0.80f);
    c[ImGuiCol_FrameBgActive]  = ImVec4(0.18f, 0.24f, 0.28f, 0.90f);
    c[ImGuiCol_TitleBg]        = ImVec4(0.07f, 0.09f, 0.11f, 1.00f);
    c[ImGuiCol_TitleBgActive]  = ImVec4(0.10f, 0.13f, 0.16f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]=ImVec4(0.07f, 0.09f, 0.11f, 1.00f);
    c[ImGuiCol_MenuBarBg]      = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_ScrollbarBg]    = ImVec4(0.08f, 0.10f, 0.12f, 0.70f);
    c[ImGuiCol_ScrollbarGrab]  = ImVec4(0.22f, 0.28f, 0.33f, 0.80f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.26f, 0.34f, 0.40f, 0.80f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.30f, 0.38f, 0.45f, 0.85f);
    c[ImGuiCol_CheckMark]      = ImVec4(0.42f, 0.80f, 0.58f, 1.00f);
    c[ImGuiCol_SliderGrab]     = ImVec4(0.42f, 0.80f, 0.58f, 1.00f);
    c[ImGuiCol_SliderGrabActive]=ImVec4(0.52f, 0.88f, 0.66f, 1.00f);
    c[ImGuiCol_Button]         = ImVec4(0.18f, 0.24f, 0.28f, 1.00f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.22f, 0.30f, 0.35f, 1.00f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.26f, 0.34f, 0.40f, 1.00f);
    c[ImGuiCol_Header]         = ImVec4(0.18f, 0.24f, 0.28f, 1.00f);
    c[ImGuiCol_HeaderHovered]  = ImVec4(0.22f, 0.30f, 0.35f, 1.00f);
    c[ImGuiCol_HeaderActive]   = ImVec4(0.26f, 0.34f, 0.40f, 1.00f);
    c[ImGuiCol_Separator]      = ImVec4(0.23f, 0.30f, 0.35f, 0.60f);
    c[ImGuiCol_ResizeGrip]     = ImVec4(0.30f, 0.38f, 0.45f, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.30f, 0.38f, 0.45f, 0.67f);
    c[ImGuiCol_ResizeGripActive]  = ImVec4(0.30f, 0.38f, 0.45f, 1.00f);
    c[ImGuiCol_Tab]            = ImVec4(0.12f, 0.16f, 0.19f, 1.00f);
    c[ImGuiCol_TabHovered]     = ImVec4(0.20f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_TabActive]      = ImVec4(0.18f, 0.24f, 0.28f, 1.00f);
    c[ImGuiCol_TableHeaderBg]  = ImVec4(0.12f, 0.16f, 0.19f, 1.00f);
}
