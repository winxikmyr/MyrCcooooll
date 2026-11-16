// src/modules/module_armorhud.h
#pragma once
#include "module_base.h"

class ModuleArmorHUD final : public ModuleBase {
public:
    ModuleArmorHUD() : ModuleBase("ArmorHUD", Category::VISUAL, /*defaultOn=*/true) {}

    float scale     = 1.0f;   // Масштаб отображения
    float armorPosY = 62.0f;  // Отступ снизу
};

