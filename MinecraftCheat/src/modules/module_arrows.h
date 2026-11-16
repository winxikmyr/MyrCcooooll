// src/modules/module_arrows.h
#pragma once
#include "module_base.h"

class ModuleArrows : public ModuleBase {
public:
    float arrowRadius   = 60.0f; // Радиус отображения стрелочек от центра экрана
    float textRadius    = 78.0f; // Радиус отображения текста дистанции
    bool  showDistance  = true;
    bool  onlyOffscreen = true;
    bool  ignoreBot     = true;

    ModuleArrows() : ModuleBase("Arrows", Category::VISUAL, false) {}
};

