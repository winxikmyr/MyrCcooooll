// src/modules/module_blockesp.h
#pragma once
#include "module_base.h"
#include <vector>

// 1-в-1 класс BlockData из BlockESP.java
struct BlockData {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double red = 1.0;
    double green = 1.0;
    double blue = 1.0;
};

// 1-в-1 класс BlockESP из BlockESP.java
class ModuleBlockESP : public ModuleBase {
public:
    float range = 300.0f; // SliderSetting("Range", range, 125f, 512f, 1f)
    bool  ores  = true;   // BooleanSetting("Ores", ore)
    bool  food  = true;   // BooleanSetting("Food", food)
    bool  tracers = false; // Трейсеры от прицела к рудам

    // Потокобезопасный список найденных блоков для рендера (CopyOnWriteArrayList в Java)
    std::vector<BlockData> blocks;

    ModuleBlockESP() : ModuleBase("BlockESP", Category::VISUAL, false) {}
};
