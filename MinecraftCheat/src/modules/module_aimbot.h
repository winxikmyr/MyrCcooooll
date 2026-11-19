// src/modules/module_aimbot.h
#pragma once
#include "module_base.h"

class ModuleAimBot : public ModuleBase {
public:
    float fov         = 48.0f;
    float range       = 300.0f;
    float predict     = 0.0f; // Без упреждения по умолчанию: при необходимости настраивается вручную
    float vPredict    = 0.0f; // 0.0 = между глаз, выше = выше
    float smoothness  = 1.0f; // 1.0 = snap, до 7.0 = плавно
    bool  bodyAim     = false;
    bool  drawFov     = false;
    float fovColor[4] = { 0.49f, 0.78f, 1.0f, 0.70f }; // Настраиваемый цвет круга FOV (RGBA)
    bool  walls       = true;
    bool  bots        = true;
    bool  autoShoot   = false;

    // Внутреннее состояние
    bool  hasTarget   = false;
    int   ticks       = 0;
    DWORD lastShootTime = 0;

    ModuleAimBot() : ModuleBase("AimBot", Category::COMBAT, false) {}
};
