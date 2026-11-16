// src/hooks/wnd_hook.cpp
#include "wnd_hook.h"
#include "../gui/gui_manager.h"

// ImGui Win32 WndProc handler (объявлен в imgui_impl_win32.h)
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ── Поиск окна Minecraft ──────────────────────────────────────────────────────
struct EnumCtx { DWORD pid; HWND hwnd; };

static BOOL CALLBACK EnumCB(HWND hwnd, LPARAM lp) {
    auto* c = reinterpret_cast<EnumCtx*>(lp);
    DWORD wPid = 0;
    GetWindowThreadProcessId(hwnd, &wPid);
    if (wPid == c->pid && IsWindowVisible(hwnd)) {
        char cls[64]{};
        GetClassNameA(hwnd, cls, sizeof(cls));
        // LWJGL 2.x создаёт класс "LWJGL" или похожий
        c->hwnd = hwnd;
        // Продолжаем искать если не нашли LWJGL-класс
        if (strncmp(cls, "LWJGL", 5) == 0) return FALSE; // нашли!
    }
    return TRUE;
}

HWND WndHook::FindGameWindow() {
    // 1) Быстрый поиск по LWJGL классу
    HWND hwnd = FindWindowA("LWJGL", nullptr);
    if (hwnd) return hwnd;

    // 2) Перебор окон нашего процесса
    EnumCtx ctx{ GetCurrentProcessId(), nullptr };
    EnumWindows(EnumCB, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

bool WndHook::Initialize() {
    Window = FindGameWindow();
    if (!Window) return false;

    Original = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(Window, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(Hooked_WndProc)));
    return Original != nullptr;
}

void WndHook::Shutdown() {
    if (Window && Original)
        SetWindowLongPtrA(Window, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(Original));
    Window   = nullptr;
    Original = nullptr;
}

// ── Наш WndProc ──────────────────────────────────────────────────────────────
LRESULT CALLBACK Hooked_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (GUIManager::Get().IsClickGUIOpen()) {
        // Передаём ввод ImGui
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return 1;

        // Блокируем клики мыши чтобы они не попали в LWJGL
        if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
            msg == WM_MOUSEMOVE   || msg == WM_MOUSEWHEEL)
            return 0;
    }
    return CallWindowProcA(WndHook::Get().Original, hwnd, msg, wParam, lParam);
}

