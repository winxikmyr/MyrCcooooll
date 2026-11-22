// src/modules/module_arrows.h
#pragma once
#include "module_base.h"

class ModuleArrows : public ModuleBase {
public:
    float arrowRadius   = 70.0f; // Радиус отображения стрелочек от центра экрана
    float textRadius    = 92.0f; // Радиус отображения текста дистанции
    bool  showDistance  = true;
    bool  onlyOffscreen = false; // "всегда показывались"
    bool  ignoreBot     = true;

    ModuleArrows() : ModuleBase("Arrows", Category::VISUAL, false) {}
};

