// src/hooks/wnd_hook.h
// Хук WndProc окна Minecraft → перехватываем ввод для ImGui
#pragma once
#include <Windows.h>

class WndHook {
public:
    static WndHook& Get() { static WndHook i; return i; }

    bool Initialize();
    void Shutdown();

    HWND    Window   = nullptr;
    WNDPROC Original = nullptr;
    static HWND FindGameWindow();

private:
    WndHook() = default;
};

LRESULT CALLBACK Hooked_WndProc(HWND, UINT, WPARAM, LPARAM);

