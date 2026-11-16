// src/modules/module_wallhack.h
#pragma once
#include "module_base.h"

class ModuleWallHack : public ModuleBase {
public:
    bool  playerMode = true;   // Отображение игроков сквозь стены
    bool  mobMode    = true;   // Враждебные монстры/мобы сквозь стены
    bool  animalMode = false;  // Мирные животные сквозь стены
    bool  box3D      = true;   // 3D объемный бокс
    bool  chams      = true;   // Заливка силуэта (Chams) сквозь стены
    float opacity    = 0.35f;  // Прозрачность
    float lineWidth  = 1.5f;   // Толщина линий

    ModuleWallHack() : ModuleBase("WallHack", Category::VISUAL, false) {}
};


