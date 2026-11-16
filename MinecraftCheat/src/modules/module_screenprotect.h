// src/modules/module_screenprotect.h
#pragma once
#include "module_base.h"

class ModuleScreenProtect : public ModuleBase {
public:
    ModuleScreenProtect() : ModuleBase("ScreenProtect", Category::VISUAL, false) {}
};

