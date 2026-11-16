// src/modules/module_guiwalk.h
#pragma once
#include "module_base.h"

class ModuleGuiWalk : public ModuleBase {
public:
    ModuleGuiWalk() : ModuleBase("GuiWalk", Category::MOVEMENT, false) {}
};

