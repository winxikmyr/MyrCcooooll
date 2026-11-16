// src/gui/hud.h
#pragma once
#include <string>
#include <imgui.h>

class ModuleHUD;

std::string GetInjectorUsername();
void DrawStealthCatLogo(ImDrawList* draw, const ImVec2& pos, float size, ImU32 badgeBg, ImU32 accentCol, float uScale = 1.0f);

class HUD {
public:
    HUD() = default;
    void Draw(const ModuleHUD* settings);
};
