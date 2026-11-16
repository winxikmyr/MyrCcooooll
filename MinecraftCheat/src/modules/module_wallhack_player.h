// src/modules/module_wallhack_player.h
#pragma once
#include "module_base.h"

class ModuleWallHackPlayer : public ModuleBase {
public:
    bool  box3D      = true;
    bool  filled     = true;
    float boxOpacity = 0.35f;
    float lineWidth  = 1.5f;

    ModuleWallHackPlayer() : ModuleBase("WallHackPlayer", Category::VISUAL, false) {}
};

