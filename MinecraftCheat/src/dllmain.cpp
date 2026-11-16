// src/dllmain.cpp
#include <Windows.h>
#include "hooks/gl_hook.h"
#include "hooks/wnd_hook.h"
#include "jvm/jvm_bridge.h"
#include "gui/gui_manager.h"

static HMODULE g_hSelf = nullptr;

DWORD WINAPI CheatMain(LPVOID param) {
    g_hSelf = static_cast<HMODULE>(param);

    Sleep(500);

    JVMBridge::Get().Initialize();
    GUIManager::Get().Initialize();
    WndHook::Get().Initialize();

    if (!GLHook::Get().Initialize()) {
        MessageBoxA(nullptr,
            "SwapBuffers hook failed!\n"
            "Make sure Minecraft is using OpenGL.",
            "VasyCheat", MB_ICONERROR);
    }

    // Ожидаем клавишу END для полной безопасной выгрузки
    while (!(GetAsyncKeyState(VK_END) & 1)) {
        Sleep(50);
    }

    // Пошаговая безопасная очистка хуков
    GLHook::Get().Shutdown();
    WndHook::Get().Shutdown();
    GUIManager::Get().Shutdown();
    JVMBridge::Get().Shutdown();

    Sleep(200);
    FreeLibraryAndExitThread(g_hSelf, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, CheatMain, hModule, 0, nullptr);
    }
    return TRUE;
}
