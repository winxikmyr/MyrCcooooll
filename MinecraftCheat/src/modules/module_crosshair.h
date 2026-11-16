// src/modules/module_crosshair.h
#pragma once
#include "module_base.h"

class ModuleCrosshair : public ModuleBase {
public:
    float size      = 6.0f;
    float gap       = 3.5f;
    float thickness = 1.5f;
    bool  dot       = true;
    bool  dynamicT  = false;

    ModuleCrosshair() : ModuleBase("Crosshair", Category::VISUAL, false) {}
};

