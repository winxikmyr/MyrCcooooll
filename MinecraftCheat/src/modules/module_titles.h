// src/modules/module_titles.h
#pragma once
#include "module_base.h"

class ModuleTitles : public ModuleBase {
public:
    float mashtab = 1.0f; // 0.1f .. 3.0f

    ModuleTitles() : ModuleBase("Titles", Category::VISUAL, true) {}
};

