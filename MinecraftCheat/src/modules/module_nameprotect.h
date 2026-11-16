// src/modules/module_nameprotect.h
#pragma once
#include "module_base.h"
#include <string>

class ModuleNameProtect : public ModuleBase {
public:
    std::string protectedNick = "Protected";
    bool hideFriends = true;

    ModuleNameProtect() : ModuleBase("NameProtect", Category::VISUAL, false) {}
};

