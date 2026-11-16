// src/gui/hud_plus.h
#pragma once

class ModuleArmorHUD;

class HUDPlus {
public:
    HUDPlus() = default;
    void Draw(const ModuleArmorHUD* settings);

private:
    void DrawArmorHUD(float yOffset, float userScaleMul);
};
