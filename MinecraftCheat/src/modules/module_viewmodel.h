// src/modules/module_viewmodel.h
#pragma once
#include "module_base.h"

class ModuleViewModel final : public ModuleBase {
public:
    ModuleViewModel() : ModuleBase("ViewModel", Category::VISUAL, /*defaultOn=*/false) {}

    // Правая рука
    float rightX     = 0.0f;
    float rightY     = 0.0f;
    float rightZ     = 0.0f;
    float rightScale = 1.0f;

    // Левая рука
    float leftX      = 0.0f;
    float leftY      = 0.0f;
    float leftZ      = 0.0f;
    float leftScale  = 1.0f;
};

