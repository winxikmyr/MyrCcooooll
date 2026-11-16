// src/gui/gui_manager.h
#pragma once
#include <Windows.h>
#include <memory>
#include "clickgui.h"
#include "hud.h"
#include "hud_plus.h"
#include "nametags.h"

class GUIManager {
public:
    static GUIManager& Get() { static GUIManager i; return i; }

    void Initialize();
    void Shutdown();

    // Вызывается из Hooked_wglSwapBuffers каждый кадр
    void Render(HDC hdc);

    bool IsClickGUIOpen() const { return m_guiOpen; }
    void ToggleClickGUI();

    ClickGUI* GetClickGUI() const { return m_clickGui.get(); }

    void DrawItemESP(const class ModuleItemESP* esp);

private:
    GUIManager() = default;
    void InitImGui(HDC hdc);
    void ApplyStyle();

    bool m_imguiReady = false;
    bool m_guiOpen    = false;
    bool m_prevRShift = false;

    HWND m_hwnd = nullptr;

    std::unique_ptr<ClickGUI> m_clickGui;
    std::unique_ptr<HUD>      m_hud;
    std::unique_ptr<HUDPlus>  m_hudPlus;
};
