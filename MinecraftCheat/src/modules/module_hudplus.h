// src/modules/module_hudplus.h
#pragma once
#include "module_base.h"

class ModuleHUDPlus final : public ModuleBase {
public:
    ModuleHUDPlus() : ModuleBase("HUD+", Category::VISUAL, /*defaultOn=*/true) {}

    bool roundedHotbar = true;   // Закругленный хотбар
    bool armorHUD      = true;   // Нативное отображение брони над хотбаром
    bool chatAnim      = true;   // Анимация сообщений чата

    float armorPosY    = 30.0f;  // Высота над хотбаром
};
