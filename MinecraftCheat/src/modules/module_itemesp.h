// src/modules/module_itemesp.h
#pragma once
#include "module_base.h"

class ModuleItemESP final : public ModuleBase {
public:
    ModuleItemESP() : ModuleBase("ItemESP", Category::VISUAL, /*defaultOn=*/true) {}

    float range            = 150.0f; // Радиус поиска предметов (20..300)
    bool  drawBox          = false;  // 3D/2D рамка вокруг предмета
    bool  drawName         = false;  // Название предмета (отключено по запросу)
    bool  drawDistance     = false;  // Дистанция в метрах (отключено по запросу)
    bool  drawIcon         = true;   // Отображение иконки
    float iconSize         = 20.0f;  // Размер иконки на экране (8 - 64)
    float labelScaleMul    = 1.0f;   // Масштаб подписи (Размер текста)
    float distantFontBoost = 1.0f;   // Увеличение шрифта на расстоянии (0..3)
    bool  drawIconOnly     = true;   // Только иконка как парящий предмет без текста и рамок
    bool  showGroundItems  = true;   // Предметы на земле (ArmorStand / Ground)
};
