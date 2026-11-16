// src/modules/module_mcf.h
#pragma once
#include "module_base.h"
#include <string>

class ModuleMCF : public ModuleBase {
public:
    bool onFriend = true;
    std::string lastMessage;
    DWORD lastMessageTime = 0;

    ModuleMCF() : ModuleBase("MCF", Category::MISC, true) {}

    void SetNotification(const std::string& msg) {
        lastMessage = msg;
        lastMessageTime = GetTickCount();
    }
};

