// src/overlay/stream_overlay.cpp
#include "stream_overlay.h"
#include "../gui/gui_manager.h"
#include "../gui/clickgui.h"
#include <dwmapi.h>
#include <d3d9.h>
#include <imgui.h>
#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_win32.h>

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

bool StreamOverlay::Start(HWND targetGameHwnd, HINSTANCE hInst) {
    if (m_active.load()) return true;
    m_targetHwnd = targetGameHwnd;
    m_hInstance = hInst ? hInst : GetModuleHandleA(nullptr);
    m_stopRequested = false;

    m_threadHandle = CreateThread(nullptr, 0, OverlayThread, this, 0, nullptr);
    if (!m_threadHandle) return false;

    // Ожидаем готовности окна оверлея
    for (int i = 0; i < 50 && !m_active.load(); ++i) {
        Sleep(20);
    }
    return m_active.load();
}

void StreamOverlay::Stop() {
    if (!m_active.load()) return;
    m_stopRequested = true;
    if (m_overlayHwnd && IsWindow(m_overlayHwnd)) {
        PostMessageA(m_overlayHwnd, WM_CLOSE, 0, 0);
    }
    if (m_threadHandle) {
        WaitForSingleObject(m_threadHandle, 2000);
        CloseHandle(m_threadHandle);
        m_threadHandle = nullptr;
    }
    m_active = false;
    m_overlayHwnd = nullptr;
}

LRESULT CALLBACK StreamOverlay::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (GUIManager::Get().IsClickGUIOpen()) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return 1;
    }
    switch (msg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

DWORD WINAPI StreamOverlay::OverlayThread(LPVOID lpParam) {
    auto* self = reinterpret_cast<StreamOverlay*>(lpParam);

    const char* className = "NativeStreamOverlay";
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = self->m_hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = className;
    RegisterClassExA(&wc);

    RECT rect{ 0, 0, 800, 600 };
    POINT pt{ 0, 0 };
    if (self->m_targetHwnd && IsWindow(self->m_targetHwnd)) {
        GetClientRect(self->m_targetHwnd, &rect);
        ClientToScreen(self->m_targetHwnd, &pt);
    }

    // 0x80800A8u = WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE;
    HWND hwnd = CreateWindowExA(
        exStyle,
        className,
        "Native client overlay",
        WS_POPUP,
        pt.x, pt.y,
        rect.right, rect.bottom,
        nullptr, nullptr, self->m_hInstance, nullptr
    );

    if (!hwnd) {
        UnregisterClassA(className, self->m_hInstance);
        return 0;
    }
    self->m_overlayHwnd = hwnd;

    // Аппаратная прозрачность через DWM и ColorKey
    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY | LWA_ALPHA);
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    // 0x11 = WDA_EXCLUDEFROMCAPTURE (полное исключение оверлея из OBS, Discord, AnyDesk, ShadowPlay)
    SetWindowDisplayAffinity(hwnd, 0x00000011);

    // Инициализация Direct3D9 для независимого оверлея
    LPDIRECT3D9 pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        DestroyWindow(hwnd);
        UnregisterClassA(className, self->m_hInstance);
        return 0;
    }

    D3DPRESENT_PARAMETERS d3dpp{};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
    d3dpp.BackBufferWidth = rect.right > 0 ? rect.right : 800;
    d3dpp.BackBufferHeight = rect.bottom > 0 ? rect.bottom : 600;
    d3dpp.EnableAutoDepthStencil = FALSE;
    d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE; // VSync для синхронизации с монитором

    LPDIRECT3DDEVICE9 pDevice = nullptr;
    if (pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                           D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &pDevice) < 0) {
        if (pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice) < 0) {
            pD3D->Release();
            DestroyWindow(hwnd);
            UnregisterClassA(className, self->m_hInstance);
            return 0;
        }
    }

    // Инициализация ImGui для DX9 оверлея
    ImGuiContext* overlayCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(overlayCtx);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(pDevice);

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    self->m_active = true;

    RECT lastRect{};
    POINT lastPt{};
    DWORD lastPosCheck = 0;

    MSG msg{};
    while (!self->m_stopRequested.load()) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT) {
                self->m_stopRequested = true;
                break;
            }
        }
        if (self->m_stopRequested.load()) break;

        // Синхронизация геометрии и видимости с целевым окном игры
        HWND target = self->m_targetHwnd;
        if (!target || !IsWindow(target)) break;

        if (IsIconic(target)) {
            if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
            Sleep(16);
            continue;
        }

        HWND fg = GetForegroundWindow();
        bool isFocused = (fg == target || fg == hwnd);
        if (!isFocused) {
            if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
            Sleep(16);
            continue;
        }

        if (!IsWindowVisible(hwnd)) {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }

        DWORD now = GetTickCount();
        if (now - lastPosCheck > 100) {
            lastPosCheck = now;
            RECT curRect{};
            POINT curPt{ 0, 0 };
            GetClientRect(target, &curRect);
            ClientToScreen(target, &curPt);

            if (curRect.right != lastRect.right || curRect.bottom != lastRect.bottom ||
                curPt.x != lastPt.x || curPt.y != lastPt.y) {
                lastRect = curRect;
                lastPt = curPt;
                SetWindowPos(hwnd, HWND_TOPMOST, curPt.x, curPt.y, curRect.right, curRect.bottom,
                             SWP_NOACTIVATE | SWP_SHOWWINDOW);

                d3dpp.BackBufferWidth = curRect.right > 0 ? curRect.right : 800;
                d3dpp.BackBufferHeight = curRect.bottom > 0 ? curRect.bottom : 600;
                ImGui_ImplDX9_InvalidateDeviceObjects();
                pDevice->Reset(&d3dpp);
                ImGui_ImplDX9_CreateDeviceObjects();
            }
        }

        // Кликабельность окна: если ClickGUI открыт, убираем WS_EX_TRANSPARENT, чтобы клики попадали в меню
        LONG curExStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
        bool menuOpen = GUIManager::Get().IsClickGUIOpen();
        if (menuOpen && (curExStyle & WS_EX_TRANSPARENT)) {
            SetWindowLongA(hwnd, GWL_EXSTYLE, curExStyle & ~WS_EX_TRANSPARENT);
        } else if (!menuOpen && !(curExStyle & WS_EX_TRANSPARENT)) {
            SetWindowLongA(hwnd, GWL_EXSTYLE, curExStyle | WS_EX_TRANSPARENT);
        }

        // Очистка прозрачного буфера
        pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
        if (pDevice->BeginScene() >= 0) {
            ImGui::SetCurrentContext(overlayCtx);
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // Рендер всех HUD и Menu элементов в прозрачный оверлей
            GUIManager::Get().RenderContentOnly();

            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            pDevice->EndScene();
        }

        HRESULT hr = pDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (hr == D3DERR_DEVICELOST && pDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
            ImGui_ImplDX9_InvalidateDeviceObjects();
            pDevice->Reset(&d3dpp);
            ImGui_ImplDX9_CreateDeviceObjects();
        }
    }

    self->m_active = false;
    ImGui::SetCurrentContext(overlayCtx);
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(overlayCtx);

    if (pDevice) pDevice->Release();
    if (pD3D) pD3D->Release();

    DestroyWindow(hwnd);
    UnregisterClassA(className, self->m_hInstance);
    return 0;
}

