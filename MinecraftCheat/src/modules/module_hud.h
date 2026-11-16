// src/modules/module_hud.h
#pragma once
#include "module_base.h"

class ModuleHUD final : public ModuleBase {
public:
    ModuleHUD() : ModuleBase("HUD", Category::VISUAL, /*defaultOn=*/true) {}

    bool showUser = true;
    bool showFPS  = true;
    bool showPing = true;

    float posX = 16.f;
    float posY = 16.f;
};
