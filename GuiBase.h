#pragma once
#include "bakkesmod/plugin/pluginwindow.h"
#include "IMGUI/imgui.h"

class PluginWindowBase : public BakkesMod::Plugin::PluginWindow {
public:
    void Render() override {}
    void SetImGuiContext(uintptr_t ctx) override {
        ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
    }
    std::string GetMenuName() override { return "custommaps"; }
    std::string GetMenuTitle() override { return "Custom Maps"; }
    bool ShouldBlockInput() override { return false; }
    bool IsActiveOverlay() override { return true; }
    void OnOpen() override {}
    void OnClose() override {}
};