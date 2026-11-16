// src/modules/module_nametags.h
#pragma once
#include "module_base.h"

class ModuleNameTags final : public ModuleBase {
public:
    ModuleNameTags() : ModuleBase("NameTags", Category::VISUAL, /*defaultOn=*/true) {}

    float renderDistance = 256.0f; // Дистанция отображения
    float fontScale      = 1.0f;   // Масштаб тега
    float armorScale     = 1.0f;   // Масштаб отображения брони (0.5x .. 2.5x)
    float bgOpacity      = 0.85f;  // Прозрачность фона
    bool  showPlayers    = true;   // Показывать игроков
    bool  showArmor      = true;   // Показывать броню и предмет в руке
    bool  showHealth     = true;   // Показывать здоровье
    bool  showDistance   = true;   // Показывать дистанцию
    bool  showMobs       = false;  // Показывать мобов
    bool  rustexBypass   = true;   // Обход RustEx: расшифровка реальных ников через оффлайн-UUID из TAB
};
