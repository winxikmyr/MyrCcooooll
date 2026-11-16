// src/modules/module_automine.h
#pragma once
#include "module_base.h"
#include "../jvm/jvm_bridge.h"

class ModuleAutoMine : public ModuleBase {
public:
    float distance = 4.0f; // Радиус поиска ArmorStand (0..10)
    bool  hasTarget = false;
    Vec3  targetBlockPos{0, 0, 0};
    Vec3  targetAimPos{0, 0, 0};

    ModuleAutoMine() : ModuleBase("AutoMine", Category::MISC, false) {}
};
