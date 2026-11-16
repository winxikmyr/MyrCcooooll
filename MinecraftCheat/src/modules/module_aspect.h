// src/modules/module_aspect.h
#pragma once
#include "module_base.h"

class ModuleAspect : public ModuleBase {
public:
    float aspectRatio = 1.0f; // 0.6 .. 2.5 (1.0 = стандартное соотношение сторон)

    ModuleAspect() : ModuleBase("AspectRatio", Category::VISUAL, false) {}
};

