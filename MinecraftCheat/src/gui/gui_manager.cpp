// src/gui/gui_manager.cpp
#include "gui_manager.h"
#include "clickgui.h"
#include "hud.h"
#include "notification_manager.h"
#include "../hooks/wnd_hook.h"
#include "../hooks/gl_hook.h"
#include "../jvm/jvm_bridge.h"
#include "../modules/module_itemesp.h"
#include "../modules/blood_aim_math.h"
#include "armor_textures.h"

#include <imgui.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_win32.h>

#include <GL/gl.h>

// ── Обертки для безопасного сохранения современных стейтов OpenGL ────────────
typedef void (APIENTRY *PFNGLACTIVETEXTUREPROC) (GLenum texture);
typedef void (APIENTRY *PFNGLUSEPROGRAMPROC) (GLuint program);
typedef void (APIENTRY *PFNGLBINDBUFFERPROC) (GLenum target, GLuint buffer);

static PFNGLACTIVETEXTUREPROC pfn_glActiveTexture = nullptr;
static PFNGLUSEPROGRAMPROC    pfn_glUseProgram    = nullptr;
static PFNGLBINDBUFFERPROC    pfn_glBindBuffer    = nullptr;
static bool                   g_glProcsLoaded     = false;

static void LoadGLProcs() {
    if (g_glProcsLoaded) return;
    pfn_glActiveTexture = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTexture");
    if (!pfn_glActiveTexture) pfn_glActiveTexture = (PFNGLACTIVETEXTUREPROC)wglGetProcAddress("glActiveTextureARB");

    pfn_glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
    if (!pfn_glUseProgram) pfn_glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgramObjectARB");

    pfn_glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBuffer");
    if (!pfn_glBindBuffer) pfn_glBindBuffer = (PFNGLBINDBUFFERPROC)wglGetProcAddress("glBindBufferARB");
    g_glProcsLoaded = true;
}

static inline float WrapDegrees(float value) {
    value = fmodf(value, 360.0f);
    if (value >= 180.0f) value -= 360.0f;
    if (value < -180.0f) value += 360.0f;
    return value;
}

static inline double WrapDegreesD(double value) {
    value = fmod(value, 360.0);
    if (value >= 180.0) value -= 360.0;
    if (value < -180.0) value += 360.0;
    return value;
}

// ── Тёмная тема чита ──────────────────────────────────────────────────────────
void GUIManager::ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 8.f;
    s.ChildRounding     = 6.f;
    s.FrameRounding     = 4.f;
    s.GrabRounding      = 4.f;
    s.ScrollbarRounding = 4.f;
    s.TabRounding       = 4.f;
    s.WindowBorderSize  = 1.f;
    s.FrameBorderSize   = 0.f;
    s.WindowPadding     = {12.f, 10.f};
    s.ItemSpacing       = {8.f,  6.f};
    s.FramePadding      = {6.f,  4.f};

    auto& c = s.Colors;
    c[ImGuiCol_WindowBg]          = {0.07f, 0.08f, 0.12f, 0.96f};
    c[ImGuiCol_ChildBg]           = {0.09f, 0.10f, 0.15f, 1.00f};
    c[ImGuiCol_Border]            = {0.16f, 0.18f, 0.28f, 1.00f};
    c[ImGuiCol_FrameBg]           = {0.12f, 0.14f, 0.20f, 1.00f};
    c[ImGuiCol_FrameBgHovered]    = {0.16f, 0.19f, 0.28f, 1.00f};
    c[ImGuiCol_FrameBgActive]     = {0.20f, 0.24f, 0.34f, 1.00f};
    c[ImGuiCol_TitleBg]           = {0.05f, 0.06f, 0.09f, 1.00f};
    c[ImGuiCol_TitleBgActive]     = {0.07f, 0.08f, 0.13f, 1.00f};
    c[ImGuiCol_Header]            = {0.15f, 0.35f, 0.70f, 0.60f};
    c[ImGuiCol_HeaderHovered]     = {0.20f, 0.45f, 0.85f, 0.80f};
    c[ImGuiCol_HeaderActive]      = {0.25f, 0.50f, 1.00f, 1.00f};
    c[ImGuiCol_Button]            = {0.15f, 0.35f, 0.70f, 0.80f};
    c[ImGuiCol_ButtonHovered]     = {0.20f, 0.45f, 0.85f, 0.90f};
    c[ImGuiCol_ButtonActive]      = {0.25f, 0.50f, 1.00f, 1.00f};
    c[ImGuiCol_CheckMark]         = {0.40f, 0.75f, 1.00f, 1.00f};
    c[ImGuiCol_SliderGrab]        = {0.30f, 0.60f, 1.00f, 1.00f};
    c[ImGuiCol_SliderGrabActive]  = {0.40f, 0.75f, 1.00f, 1.00f};
    c[ImGuiCol_Text]              = {0.88f, 0.90f, 1.00f, 1.00f};
    c[ImGuiCol_TextDisabled]      = {0.35f, 0.40f, 0.55f, 1.00f};
    c[ImGuiCol_Separator]         = {0.16f, 0.18f, 0.28f, 1.00f};
    c[ImGuiCol_SeparatorHovered]  = {0.25f, 0.50f, 1.00f, 0.80f};
    c[ImGuiCol_SeparatorActive]   = {0.25f, 0.50f, 1.00f, 1.00f};
    c[ImGuiCol_ScrollbarBg]       = {0.05f, 0.06f, 0.09f, 1.00f};
    c[ImGuiCol_ScrollbarGrab]     = {0.18f, 0.20f, 0.30f, 1.00f};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.22f, 0.25f, 0.38f, 1.00f};
    c[ImGuiCol_ScrollbarGrabActive]  = {0.28f, 0.32f, 0.48f, 1.00f};
    c[ImGuiCol_Tab]               = {0.10f, 0.12f, 0.18f, 1.00f};
    c[ImGuiCol_TabHovered]        = {0.20f, 0.45f, 0.85f, 0.90f};
    c[ImGuiCol_TabActive]         = {0.15f, 0.35f, 0.70f, 1.00f};
    c[ImGuiCol_PopupBg]           = {0.07f, 0.08f, 0.12f, 0.96f};
}

// ── Initialize ────────────────────────────────────────────────────────────────
void GUIManager::Initialize() {
    m_clickGui = std::make_unique<ClickGUI>();
    m_hud      = std::make_unique<HUD>();
    m_hudPlus  = std::make_unique<HUDPlus>();
}

// ── Shutdown ─────────────────────────────────────────────────────────────────
void GUIManager::Shutdown() {
    if (m_imguiReady) {
        ImGui_ImplOpenGL2_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }
    // Вернуть курсор в нормальное состояние
    if (m_guiOpen) ShowCursor(FALSE);
}

// ── InitImGui ─────────────────────────────────────────────────────────────────
// Вызывается один раз при первом кадре (OpenGL контекст уже готов)
void GUIManager::InitImGui(HDC hdc) {
    m_hwnd = WindowFromDC(hdc);
    if (!m_hwnd) m_hwnd = WndHook::Get().Window;
    if (!m_hwnd) m_hwnd = WndHook::Get().FindGameWindow();
    if (!m_hwnd) m_hwnd = GetForegroundWindow();
    if (!m_hwnd) m_hwnd = GetActiveWindow();
    if (!m_hwnd) return; // Не инициализируем с пустым HWND, чтобы повторить на следующем кадре!

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // не сохраняем imgui.ini
    io.LogFilename = nullptr;

    // Загрузка сглаженного и плотного шрифта Windows с поддержкой кириллицы
    ImFontConfig fontCfg;
    fontCfg.OversampleH = 4;
    fontCfg.OversampleV = 4;
    fontCfg.PixelSnapH  = false;
    fontCfg.RasterizerMultiply = 1.18f; // Мягкая плотность глифов (Semibold look) и плавное сглаживание

    const char* fontCandidates[] = {
        "C:\\Windows\\Fonts\\segoeuib.ttf",  // Segoe UI Bold/Semibold — современный, плотный и сглаженный
        "C:\\Windows\\Fonts\\segoeui.ttf",   // Segoe UI Regular
        "C:\\Windows\\Fonts\\tahomabd.ttf",  // Tahoma Bold
        "C:\\Windows\\Fonts\\arialbd.ttf",   // Arial Bold
        "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\arial.ttf"
    };

    bool fontLoaded = false;
    for (const char* fpath : fontCandidates) {
        if (GetFileAttributesA(fpath) != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF(fpath, 17.5f, &fontCfg, io.Fonts->GetGlyphRangesCyrillic());
            fontLoaded = true;
            break;
        }
    }
    if (!fontLoaded) {
        io.Fonts->AddFontDefault();
    }

    // ── Загрузка иконок FontAwesome 6 (MergeMode = true) ─────────────────────
    static const ImWchar icon_ranges[] = { 0xE000, 0xF900, 0 };
    ImFontConfig iconCfg;
    iconCfg.MergeMode = true;
    iconCfg.PixelSnapH = true;
    iconCfg.OversampleH = 3;
    iconCfg.OversampleV = 3;
    iconCfg.GlyphOffset = ImVec2(0.0f, 1.0f);

    std::vector<std::string> iconPaths;
    HMODULE hMod = GetModuleHandleA("MyrLegit.dll");
    if (hMod) {
        char dllPath[MAX_PATH] = { 0 };
        GetModuleFileNameA(hMod, dllPath, MAX_PATH);
        std::string d(dllPath);
        size_t p = d.find_last_of("\\/");
        if (p != std::string::npos) {
            std::string dir = d.substr(0, p);
            iconPaths.push_back(dir + "\\font_awesome.ttf");
        }
    }
    iconPaths.push_back("C:\\Users\\PC\\Desktop\\Легитка вась\\БИЛДЫ\\font_awesome.ttf");
    iconPaths.push_back("C:\\Users\\PC\\Desktop\\Легитка вась\\MinecraftCheat\\font_awesome.ttf");
    iconPaths.push_back("C:\\Users\\PC\\Desktop\\Легитка вась\\font_awesome.ttf");
    iconPaths.push_back("font_awesome.ttf");
    char appdata[MAX_PATH] = { 0 };
    GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    if (appdata[0] != '\0') {
        std::string cfgDir = std::string(appdata) + "\\.minecraft\\myr_legit\\";
        iconPaths.push_back(cfgDir + "font_awesome.ttf");
    }

    bool iconLoaded = false;
    for (const auto& ip : iconPaths) {
        if (GetFileAttributesA(ip.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (io.Fonts->AddFontFromFileTTF(ip.c_str(), 16.0f, &iconCfg, icon_ranges)) {
                iconLoaded = true;
                break;
            }
        }
    }
    g_hasIconFont = iconLoaded;

    ApplyStyle();

    // Масштабируем интерфейс для комфортного размера (на 30% больше)
    ImGuiStyle& s = ImGui::GetStyle();
    s.ScaleAllSizes(1.30f);

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplOpenGL2_Init();

    m_imguiReady = true;
}

// ── ToggleClickGUI ────────────────────────────────────────────────────────────
void GUIManager::ToggleClickGUI() {
    m_guiOpen = !m_guiOpen;

    if (m_guiOpen) {
        // Освобождаем мышь из-под LWJGL
        ClipCursor(nullptr);
        ShowCursor(TRUE);
    } else {
        // Возвращаем курсор в невидимое состояние
        ShowCursor(FALSE);
    }
}

// ── Render — вызывается из Hooked_wglSwapBuffers ─────────────────────────────
void GUIManager::Render(HDC hdc) {
    // Ленивая инициализация ImGui
    if (!m_imguiReady) InitImGui(hdc);
    if (!m_imguiReady) return;

    // Обновляем JVM данные с защитой от исключений с частотой не чаще 125 Гц (каждые 8мс)
    // Предотвращает миллионы JNI вызовов в секунду при 2000-2500 FPS
    static DWORD s_lastTickTime = 0;
    DWORD now = GetTickCount();
    if (now - s_lastTickTime >= 8) {
        s_lastTickTime = now;
        try {
            JVMBridge::Get().Tick();
        } catch (...) {}
    }

    // RSHIFT — edge trigger (нажатие, не удержание)
    bool currKey = ((GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0);
    if (currKey && !m_prevRShift) ToggleClickGUI();
    m_prevRShift = currKey;

    // ── Сохраняем расширенный стейт OpenGL (шейдеры, VBO, текстуры) ──────────
    LoadGLProcs();

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT | GL_VIEWPORT_BIT | GL_SCISSOR_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glMatrixMode(GL_PROJECTION); glPushMatrix();
    glMatrixMode(GL_MODELVIEW);  glPushMatrix();

    GLint last_program = 0;
    if (pfn_glUseProgram) {
        glGetIntegerv(0x8B8D /* GL_CURRENT_PROGRAM */, &last_program);
        pfn_glUseProgram(0);
    }

    GLint last_array_buffer = 0;
    GLint last_element_buffer = 0;
    if (pfn_glBindBuffer) {
        glGetIntegerv(0x8894 /* GL_ARRAY_BUFFER_BINDING */, &last_array_buffer);
        glGetIntegerv(0x8895 /* GL_ELEMENT_ARRAY_BUFFER_BINDING */, &last_element_buffer);
        pfn_glBindBuffer(0x8894 /* GL_ARRAY_BUFFER */, 0);
        pfn_glBindBuffer(0x8895 /* GL_ELEMENT_ARRAY_BUFFER */, 0);
    }

    GLint last_active_texture = 0x84C0 /* GL_TEXTURE0 */;
    GLint last_tex_unit0 = 0;
    GLint last_tex_unit1 = 0;
    if (pfn_glActiveTexture) {
        glGetIntegerv(0x84E0 /* GL_ACTIVE_TEXTURE */, &last_active_texture);

        pfn_glActiveTexture(0x84C1 /* GL_TEXTURE1 */);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_tex_unit1);

        pfn_glActiveTexture(0x84C0 /* GL_TEXTURE0 */);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_tex_unit0);
    } else {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_tex_unit0);
    }

    // ── ImGui frame ───────────────────────────────────────────────────────────
    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplWin32_NewFrame();

    // Защита: если Win32 backend вернул нулевой DisplaySize (например, полноэкранный режим без границы)
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
        GLint vp[4]{};
        glGetIntegerv(GL_VIEWPORT, vp);
        if (vp[2] > 0 && vp[3] > 0) {
            io.DisplaySize = ImVec2(static_cast<float>(vp[2]), static_cast<float>(vp[3]));
        }
    }

    ImGui::NewFrame();

    // HUD — отображается, если включен в ClickGUI
    if (m_hud && m_clickGui && m_clickGui->IsHUDEnabled()) {
        m_hud->Draw(m_clickGui->GetHUDModule());
    }

    // ArmorHUD — отображается, если включен в ClickGUI
    if (m_hudPlus && m_clickGui && m_clickGui->IsArmorHUDEnabled()) {
        m_hudPlus->Draw(m_clickGui->GetArmorHUDModule());
    }

    // Синхронизация настроек модулей с JVMBridge и fast-path флагов OpenGL
    if (m_clickGui) {
        auto* vm = m_clickGui->GetViewModelModule();
        if (vm && vm->IsEnabled()) {
            g_optViewModel = true;
            g_vmRightX = vm->rightX; g_vmRightY = vm->rightY; g_vmRightZ = vm->rightZ; g_vmRightScale = vm->rightScale;
            g_vmLeftX = vm->leftX;   g_vmLeftY = vm->leftY;   g_vmLeftZ = vm->leftZ;   g_vmLeftScale = vm->leftScale;
        } else {
            g_optViewModel = false;
        }

        auto* asp = m_clickGui->GetAspectModule();
        g_optAspect = (asp && asp->IsEnabled() && std::fabs(asp->aspectRatio - 1.0f) > 0.005f);
        g_aspectRatioVal = (asp && g_optAspect) ? asp->aspectRatio : 1.0f;
        JVMBridge::Get().SetAspectRatio(g_aspectRatioVal);

        auto* tit = m_clickGui->GetTitlesModule();
        g_optTitles = (tit && tit->IsEnabled() && std::fabs(tit->mashtab - 1.0f) > 0.005f);
        g_titlesScaleVal = (tit && g_optTitles) ? tit->mashtab : 1.0f;

        // Динамическое включение/выключение тяжелых MinHook хуков (сохраняет 2500 FPS когда выключены)
        GLHook::Get().SetViewModelHookActive(g_optViewModel);
        GLHook::Get().SetAspectRatioHookActive(g_optAspect);
        GLHook::Get().SetTitlesHookActive(g_optTitles);

        auto* beMod = m_clickGui->GetBlockESPModule();
        if (beMod) {
            JVMBridge::Get().SetBlockESPSettings(beMod->IsEnabled(), beMod->range, beMod->ores, beMod->food);
        }

        JVMBridge::Get().SetAutoMineEnabled(m_clickGui->IsAutoMineEnabled());

        g_hideCrosshair = m_clickGui && m_clickGui->IsCrosshairEnabled();
        JVMBridge::Get().SetHideVanillaCrosshair(g_hideCrosshair);
    }
    JVMBridge::Get().UpdateRenderMatrices();

    const auto& mcData = JVMBridge::Get().Data();
    bool canDrawWorldHUD = mcData.isInWorld;
    bool isF5Front = (mcData.thirdPersonView == 2);

    // NameProtect: скрытие собственного ника в чате
    if (canDrawWorldHUD && m_clickGui && m_clickGui->IsNameProtectEnabled()) {
        auto* np = m_clickGui->GetNameProtectModule();
        if (np && !np->protectedNick.empty()) {
            std::string myNick = !mcData.username.empty() ? mcData.username : GetInjectorUsername();
            if (!myNick.empty() && myNick != np->protectedNick) {
                static auto s_lastChatSanitize = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastChatSanitize).count() > 80) {
                    s_lastChatSanitize = now;
                    JVMBridge::Get().SanitizeChat(myNick, np->protectedNick);
                }
            }
        }
    }

    // NameTags — отображаются только в мире без меню паузы/инвентаря (и не в виде от 3-го лица спереди F5)
    if (canDrawWorldHUD && !isF5Front && m_clickGui && m_clickGui->IsNameTagsEnabled()) {
        NameTags::Draw(m_clickGui->GetNameTagsModule());
    }

    // ItemESP (1-в-1 по ItemESP.java: 2D Box projection, drawX - 2, drawY, Fonts.drawStringWithOutline)
    if (canDrawWorldHUD && !isF5Front && m_clickGui && m_clickGui->IsItemESPEnabled()) {
        auto* esp = m_clickGui->GetItemESPModule();
        if (esp) {
            const auto& items = JVMBridge::Get().Data().droppedItems;
            unsigned int atlasId = JVMBridge::Get().Data().atlasTextureId;
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            ImFont* font = ImGui::GetFont();

            for (const auto& item : items) {
                if (item.isGroundStand && !esp->showGroundItems) continue;
                if (item.distance > esp->range) continue;

                // Защита от свиней, дверей, спящих игроков
                if (item.isGroundStand) {
                    if (!item.hasIcon && item.maxU <= item.minU && item.maxV <= item.minV) continue;
                    if (item.name.empty() || item.name == "Armor Stand" || item.name == "ground" || item.name == "Стойка для брони") continue;
                }

                // 1. AABB 1-в-1 по ItemESP.java:
                // double width = entity.width / 1.5D; (в MC EntityItem width = 0.25)
                // double height = entity.height + 0.2F; (в MC EntityItem height = 0.25)
                double entWidth = item.isGroundStand ? (0.35 / 1.5) : (0.25 / 1.5);
                double entHeight = item.isGroundStand ? (0.35 + 0.2) : (0.25 + 0.2);

                Vec3 corners[8] = {
                    { item.pos.x - entWidth, item.pos.y,             item.pos.z - entWidth },
                    { item.pos.x - entWidth, item.pos.y + entHeight, item.pos.z - entWidth },
                    { item.pos.x + entWidth, item.pos.y,             item.pos.z - entWidth },
                    { item.pos.x + entWidth, item.pos.y + entHeight, item.pos.z - entWidth },
                    { item.pos.x - entWidth, item.pos.y,             item.pos.z + entWidth },
                    { item.pos.x - entWidth, item.pos.y + entHeight, item.pos.z + entWidth },
                    { item.pos.x + entWidth, item.pos.y,             item.pos.z + entWidth },
                    { item.pos.x + entWidth, item.pos.y + entHeight, item.pos.z + entWidth }
                };

                float minX = 1e9f, maxX = -1e9f;
                float minY = 1e9f, maxY = -1e9f;
                bool anyVisible = false;

                for (int c = 0; c < 8; ++c) {
                    Vec2 sc;
                    if (JVMBridge::Get().WorldToScreen(corners[c], sc)) {
                        minX = std::min(minX, sc.x);
                        maxX = std::max(maxX, sc.x);
                        minY = std::min(minY, sc.y);
                        maxY = std::max(maxY, sc.y);
                        anyVisible = true;
                    }
                }

                if (!anyVisible) {
                    Vec2 centerSc;
                    Vec3 cPos = { item.pos.x, item.pos.y + 0.15, item.pos.z };
                    if (!JVMBridge::Get().WorldToScreen(cPos, centerSc)) continue;
                    minX = centerSc.x - 10.f; maxX = centerSc.x + 10.f;
                    minY = centerSc.y - 10.f; maxY = centerSc.y + 10.f;
                }

                double posX = minX;
                double posY = minY;
                double endPosX = maxX;
                double endPosY = maxY;

                // Опциональная 2D рамка вокруг предмета (Draw Box)
                if (esp->drawBox) {
                    draw->AddRect(ImVec2(static_cast<float>(posX), static_cast<float>(posY)),
                                  ImVec2(static_cast<float>(endPosX), static_cast<float>(endPosY)),
                                  IM_COL32(255, 255, 255, 180), 0.f, 0, 1.0f);
                }

                std::string tag = item.name.empty() ? "Item" : item.name;
                if (tag.rfind("item.", 0) == 0) tag = tag.substr(5);
                if (tag.rfind("tile.", 0) == 0) tag = tag.substr(5);

                float fontSize = (font ? font->FontSize * 0.72f : 12.f);
                ImVec2 tagTextSize = font ? font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, tag.c_str()) : ImVec2(30.f, 12.f);
                float tagWidth = tagTextSize.x;

                // Координаты отрисовки ТОЧЬ В ТОЧЬ по ItemESP.java:
                // float drawX = (float) (posX + (endPosX - posX) / 2.0F - tagWidth / 2);
                // float drawY = (float) (posY - 10);
                float drawX = static_cast<float>(posX + (endPosX - posX) * 0.5f - tagWidth * 0.5f);
                float drawY = static_cast<float>(posY - 10.0f);

                // Дистанция: String.format("%.1f м", distance)
                char dis[32];
                snprintf(dis, sizeof(dis), "%.1f м", item.distance);

                // Fonts.nunito12.drawStringWithOutline(dis, drawX + 14, drawY + 3, -1)
                if (esp->drawDistance && font) {
                    ImVec2 dPos(drawX + 14.0f, drawY + 3.0f);
                    draw->AddText(font, fontSize, ImVec2(dPos.x - 1.f, dPos.y), IM_COL32(0, 0, 0, 255), dis);
                    draw->AddText(font, fontSize, ImVec2(dPos.x + 1.f, dPos.y), IM_COL32(0, 0, 0, 255), dis);
                    draw->AddText(font, fontSize, ImVec2(dPos.x, dPos.y - 1.f), IM_COL32(0, 0, 0, 255), dis);
                    draw->AddText(font, fontSize, ImVec2(dPos.x, dPos.y + 1.f), IM_COL32(0, 0, 0, 255), dis);
                    draw->AddText(font, fontSize, dPos, IM_COL32(255, 255, 255, 255), dis);
                }

                // Отрисовка имени предмета с контуром (если включено drawName)
                if (esp->drawName && font) {
                    char fullTag[128];
                    if (item.count > 1) snprintf(fullTag, sizeof(fullTag), "%s x%d", tag.c_str(), item.count);
                    else snprintf(fullTag, sizeof(fullTag), "%s", tag.c_str());

                    ImVec2 fullTagSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, fullTag);
                    ImVec2 nPos(static_cast<float>(posX + (endPosX - posX) * 0.5f - fullTagSz.x * 0.5f), drawY - fullTagSz.y - 1.0f);
                    draw->AddText(font, fontSize, ImVec2(nPos.x - 1.f, nPos.y), IM_COL32(0, 0, 0, 255), fullTag);
                    draw->AddText(font, fontSize, ImVec2(nPos.x + 1.f, nPos.y), IM_COL32(0, 0, 0, 255), fullTag);
                    draw->AddText(font, fontSize, ImVec2(nPos.x, nPos.y - 1.f), IM_COL32(0, 0, 0, 255), fullTag);
                    draw->AddText(font, fontSize, ImVec2(nPos.x, nPos.y + 1.f), IM_COL32(0, 0, 0, 255), fullTag);
                    draw->AddText(font, fontSize, nPos, IM_COL32(255, 255, 255, 255), fullTag);
                }

                // Отрисовка иконки предмета: mc.getRenderItem().renderItemAndEffectIntoGUI(item, (int) drawX - 2, (int) drawY)
                if (esp->drawIcon) {
                    float iconSz = 16.0f;
                    float ix = drawX - 2.0f;
                    float iy = drawY;

                    bool drawn = false;
                    if (atlasId > 0 && item.hasIcon && (item.maxU > item.minU) && (item.maxV > item.minV)) {
                        // Тень предмета
                        draw->AddImage(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(atlasId)),
                                       ImVec2(ix + 1.0f, iy + 1.0f),
                                       ImVec2(ix + iconSz + 1.0f, iy + iconSz + 1.0f),
                                       ImVec2(item.minU, item.minV),
                                       ImVec2(item.maxU, item.maxV),
                                       IM_COL32(0, 0, 0, 200));
                        // Иконка в цветах текстурного пака
                        draw->AddImage(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(atlasId)),
                                       ImVec2(ix, iy),
                                       ImVec2(ix + iconSz, iy + iconSz),
                                       ImVec2(item.minU, item.minV),
                                       ImVec2(item.maxU, item.maxV),
                                       IM_COL32(255, 255, 255, 255));
                        drawn = true;
                    } else {
                        GLuint aTex = ArmorTextures::GetTexture(-1, item.name, item.itemId);
                        if (aTex != 0) {
                            draw->AddImage(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(aTex)),
                                           ImVec2(ix, iy),
                                           ImVec2(ix + iconSz, iy + iconSz));
                            drawn = true;
                        }
                    }

                    // Количество в стаке (E()I в нижнем правом углу иконки)
                    if (item.count > 1 && font) {
                        char cntBuf[16];
                        snprintf(cntBuf, sizeof(cntBuf), "%d", item.count);
                        float cntSz = fontSize * 0.85f;
                        ImVec2 cSz = font->CalcTextSizeA(cntSz, FLT_MAX, 0.f, cntBuf);
                        draw->AddText(font, cntSz, ImVec2(ix + iconSz - cSz.x + 1.f, iy + iconSz - cSz.y + 1.f), IM_COL32(0, 0, 0, 255), cntBuf);
                        draw->AddText(font, cntSz, ImVec2(ix + iconSz - cSz.x, iy + iconSz - cSz.y), IM_COL32(255, 255, 255, 255), cntBuf);
                    }
                }
            }
        }
    }

    // Arrows — стрелочки направления к игрокам (математически точная проекция направления)
    if (canDrawWorldHUD && m_clickGui && m_clickGui->IsArrowsEnabled()) {
        auto* arr = m_clickGui->GetArrowsModule();
        if (arr) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            ImFont* font = ImGui::GetFont();
            ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
            constexpr double kPI = 3.14159265358979323846;

            double yawRad = mcData.camYaw * (kPI / 180.0);
            double sinY = sin(yawRad);
            double cosY = cos(yawRad);

            for (const auto& pl : mcData.players) {
                if (pl.distance < 0.8f) continue;
                if (!mcData.username.empty() && mcData.username != "Player" && pl.name == mcData.username && pl.distance < 1.0f) continue;
                if (arr->ignoreBot && (pl.isBot || pl.name == "Zombie" || pl.name == "Animal")) continue;
                std::string realName = pl.realName.empty() ? pl.name : pl.realName;

                bool isFriend = FriendManager::Get().IsFriend(realName);
                ImU32 arrowColor = isFriend ? IM_COL32(75, 230, 110, 255) : IM_COL32(255, 75, 75, 255);

                // Если игрок прямо перед глазами на экране и включен onlyOffscreen — не загромождаем экран стрелкой
                if (arr->onlyOffscreen) {
                    Vec2 scPos;
                    if (JVMBridge::Get().WorldToScreen({ pl.pos.x, pl.pos.y + pl.height * 0.5, pl.pos.z }, scPos)) {
                        ImVec2 disp = ImGui::GetIO().DisplaySize;
                        if (scPos.x >= 25.0f && scPos.x <= disp.x - 25.0f &&
                            scPos.y >= 25.0f && scPos.y <= disp.y - 25.0f) {
                            continue;
                        }
                    }
                }

                // Точный математический расчет угла и позиции стрелок (1-в-1 как в Arrows.java из 71181)
                double diffX = pl.pos.x - mcData.camPos.x;
                double diffZ = pl.pos.z - mcData.camPos.z;
                double yawDeg = (atan2(diffZ, diffX) * (180.0 / kPI)) - mcData.camYaw - 90.0;
                double circleAngleRad = (yawDeg - 90.0) * (kPI / 180.0);

                float dirX = static_cast<float>(cos(circleAngleRad));
                float dirY = static_cast<float>(sin(circleAngleRad));

                float arrowX = center.x + dirX * arr->arrowRadius;
                float arrowY = center.y + dirY * arr->arrowRadius;

                // Наконечник стрелки указывает строго в направлении от центра к цели
                float tipX = arrowX + dirX * 10.0f;
                float tipY = arrowY + dirY * 10.0f;

                float perpX = -dirY;
                float perpY =  dirX;

                float baseLeftX  = arrowX - dirX * 5.0f + perpX * 7.0f;
                float baseLeftY  = arrowY - dirY * 5.0f + perpY * 7.0f;

                float baseRightX = arrowX - dirX * 5.0f - perpX * 7.0f;
                float baseRightY = arrowY - dirY * 5.0f - perpY * 7.0f;

                draw->AddTriangleFilled(ImVec2(tipX, tipY), ImVec2(baseLeftX, baseLeftY), ImVec2(baseRightX, baseRightY), arrowColor);
                draw->AddTriangle(ImVec2(tipX, tipY), ImVec2(baseLeftX, baseLeftY), ImVec2(baseRightX, baseRightY), IM_COL32(10, 10, 15, 220), 1.2f);

                if (arr->showDistance && font) {
                    char dBuf[32];
                    snprintf(dBuf, sizeof(dBuf), "%.1fm", pl.distance);
                    float tSz = font->FontSize * 0.65f;
                    ImVec2 tDim = font->CalcTextSizeA(tSz, FLT_MAX, 0.f, dBuf);

                    float textX = center.x + dirX * arr->textRadius - tDim.x * 0.5f;
                    float textY = center.y + dirY * arr->textRadius - tDim.y * 0.5f;

                    draw->AddText(font, tSz, ImVec2(textX + 1.f, textY + 1.f), IM_COL32(0, 0, 0, 220), dBuf);
                    draw->AddText(font, tSz, ImVec2(textX, textY), arrowColor, dBuf);
                }
            }
        }
    }



    // Хелпер отрисовки бокса сущности сквозь стены (WallHack / WallHackPlayer)
    auto DrawEntityBox = [&](ImDrawList* draw, const PlayerTagInfo& ent, ImU32 col, bool is3D, bool chams, float opacity, float lineW) {
        if (ent.distance < 0.2f) return;
        double w = (ent.width > 0.0 ? ent.width : 0.6) * 0.5;
        double h = (ent.height > 0.0 ? ent.height : 1.8);

        Vec2 scFeet, scHead;
        bool hasFeet = JVMBridge::Get().WorldToScreen(ent.pos, scFeet);
        bool hasHead = JVMBridge::Get().WorldToScreen({ ent.pos.x, ent.pos.y + h, ent.pos.z }, scHead);

        if (hasFeet || hasHead) {
            float bottomY = hasFeet ? scFeet.y : (scHead.y + 60.0f);
            float topY    = hasHead ? scHead.y : (scFeet.y - 60.0f);
            float centerX = hasFeet ? scFeet.x : scHead.x;
            float boxH    = std::fabs(bottomY - topY);
            if (boxH < 5.0f) boxH = 5.0f;
            float boxW    = boxH * 0.52f;
            float leftX   = centerX - boxW * 0.5f;
            float rightX  = centerX + boxW * 0.5f;

            if (chams) {
                ImU32 alphaByte = static_cast<ImU32>(std::max(0.05f, std::min(1.0f, opacity)) * 255.0f);
                ImU32 chamsCol = (col & 0x00FFFFFF) | (alphaByte << 24);
                draw->AddRectFilled(ImVec2(leftX, std::min(topY, bottomY)), ImVec2(rightX, std::max(topY, bottomY)), chamsCol, 3.0f);
            }
            if (!is3D) {
                draw->AddRect(ImVec2(leftX, std::min(topY, bottomY)), ImVec2(rightX, std::max(topY, bottomY)), col, 3.0f, 0, lineW);
            }
        }

        if (is3D) {
            Vec3 pMin = { ent.pos.x - w, ent.pos.y, ent.pos.z - w };
            Vec3 pMax = { ent.pos.x + w, ent.pos.y + h, ent.pos.z + w };
            Vec3 c[8] = {
                {pMin.x, pMin.y, pMin.z}, {pMax.x, pMin.y, pMin.z},
                {pMax.x, pMin.y, pMax.z}, {pMin.x, pMin.y, pMax.z},
                {pMin.x, pMax.y, pMin.z}, {pMax.x, pMax.y, pMin.z},
                {pMax.x, pMax.y, pMax.z}, {pMin.x, pMax.y, pMax.z}
            };
            Vec2 sc[8];
            bool sc_ok[8];
            int visibleCount = 0;
            for (int i = 0; i < 8; ++i) {
                sc_ok[i] = JVMBridge::Get().WorldToScreen(c[i], sc[i]);
                if (sc_ok[i]) visibleCount++;
            }
            if (visibleCount >= 2) {
                if (chams && visibleCount >= 4) {
                    ImU32 fillAlpha = static_cast<ImU32>(std::max(0.05f, std::min(1.0f, opacity * 0.35f)) * 255.0f);
                    ImU32 fillCol = (col & 0x00FFFFFF) | (fillAlpha << 24);
                    auto DrawQuad = [&](int i1, int i2, int i3, int i4) {
                        if (sc_ok[i1] && sc_ok[i2] && sc_ok[i3] && sc_ok[i4]) {
                            draw->AddQuadFilled(ImVec2(sc[i1].x, sc[i1].y), ImVec2(sc[i2].x, sc[i2].y),
                                                ImVec2(sc[i3].x, sc[i3].y), ImVec2(sc[i4].x, sc[i4].y), fillCol);
                        }
                    };
                    DrawQuad(0, 1, 2, 3);
                    DrawQuad(4, 5, 6, 7);
                    DrawQuad(0, 1, 5, 4);
                    DrawQuad(2, 3, 7, 6);
                    DrawQuad(1, 2, 6, 5);
                    DrawQuad(3, 0, 4, 7);
                }
                auto DrawEdge = [&](int i1, int i2) {
                    if (sc_ok[i1] && sc_ok[i2]) {
                        draw->AddLine(ImVec2(sc[i1].x, sc[i1].y), ImVec2(sc[i2].x, sc[i2].y), col, lineW);
                    }
                };
                // Нижний контур
                DrawEdge(0, 1); DrawEdge(1, 2); DrawEdge(2, 3); DrawEdge(3, 0);
                // Верхний контур
                DrawEdge(4, 5); DrawEdge(5, 6); DrawEdge(6, 7); DrawEdge(7, 4);
                // Вертикальные стойки
                DrawEdge(0, 4); DrawEdge(1, 5); DrawEdge(2, 6); DrawEdge(3, 7);
            }
        }
    };

    // ── WallHackPlayer (3D боксы игроков сквозь стены) ──────────────────────
    if (canDrawWorldHUD && m_clickGui && m_clickGui->IsWallHackPlayerEnabled()) {
        auto* whp = m_clickGui->GetWallHackPlayerModule();
        if (whp) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            for (const auto& pl : mcData.players) {
                std::string rName = pl.realName.empty() ? pl.name : pl.realName;
                bool isFriend = FriendManager::Get().IsFriend(rName);
                ImU32 col = isFriend ? IM_COL32(75, 230, 110, 240) : (pl.isBot ? IM_COL32(255, 140, 30, 240) : IM_COL32(255, 65, 65, 240));
                DrawEntityBox(draw, pl, col, whp->box3D, whp->filled, whp->boxOpacity, whp->lineWidth);
            }
            // Стойки для брони
            for (const auto& asPos : mcData.armorStands) {
                PlayerTagInfo asInfo;
                asInfo.pos = asPos;
                asInfo.width = 0.5;
                asInfo.height = 1.975;
                asInfo.distance = static_cast<float>(sqrt((asPos.x - mcData.camPos.x)*(asPos.x - mcData.camPos.x) + (asPos.y - mcData.camPos.y)*(asPos.y - mcData.camPos.y) + (asPos.z - mcData.camPos.z)*(asPos.z - mcData.camPos.z)));
                ImU32 standCol = IM_COL32(230, 200, 50, 240);
                DrawEntityBox(draw, asInfo, standCol, whp->box3D, whp->filled, whp->boxOpacity, whp->lineWidth);
            }
        }
    }

    // ── WallHack (Мобы, монстры, животные и игроки сквозь стены) ────────────
    if (canDrawWorldHUD && m_clickGui && m_clickGui->IsWallHackEnabled()) {
        auto* wh = m_clickGui->GetWallHackModule();
        if (wh) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            // Игроки
            if (wh->playerMode) {
                for (const auto& pl : mcData.players) {
                    std::string rName = pl.realName.empty() ? pl.name : pl.realName;
                    bool isFriend = FriendManager::Get().IsFriend(rName);
                    ImU32 col = isFriend ? IM_COL32(75, 230, 110, 240) : (pl.isBot ? IM_COL32(255, 140, 30, 240) : IM_COL32(255, 65, 65, 240));
                    DrawEntityBox(draw, pl, col, wh->box3D, wh->chams, wh->opacity, wh->lineWidth);
                }
            }
            // Враждебные мобы
            if (wh->mobMode) {
                ImU32 mobCol = IM_COL32(255, 140, 30, 230);
                for (const auto& mob : mcData.mobs) {
                    DrawEntityBox(draw, mob, mobCol, wh->box3D, wh->chams, wh->opacity, wh->lineWidth);
                }
            }
            // Мирные животные
            if (wh->animalMode) {
                ImU32 anmCol = IM_COL32(80, 220, 90, 230);
                for (const auto& anm : mcData.animals) {
                    DrawEntityBox(draw, anm, anmCol, wh->box3D, wh->chams, wh->opacity, wh->lineWidth);
                }
            }
            // Стойки для брони
            for (const auto& asPos : mcData.armorStands) {
                PlayerTagInfo asInfo;
                asInfo.pos = asPos;
                asInfo.width = 0.5;
                asInfo.height = 1.975;
                asInfo.distance = static_cast<float>(sqrt((asPos.x - mcData.camPos.x)*(asPos.x - mcData.camPos.x) + (asPos.y - mcData.camPos.y)*(asPos.y - mcData.camPos.y) + (asPos.z - mcData.camPos.z)*(asPos.z - mcData.camPos.z)));
                ImU32 standCol = IM_COL32(230, 200, 50, 240);
                DrawEntityBox(draw, asInfo, standCol, wh->box3D, wh->chams, wh->opacity, wh->lineWidth);
            }
        }
    }

    // ── BlockESP (3D боксы для найденных руд и растений) ────────────────────
    if (canDrawWorldHUD && m_clickGui && m_clickGui->IsBlockESPEnabled()) {
        auto* be = m_clickGui->GetBlockESPModule();
        if (be) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            ImFont* font = ImGui::GetFont();
            float rSq = be->range * be->range;

            auto RenderBlockList = [&](const std::vector<BlockESPInfo>& blkList) {
                int renderedCount = 0;
                ImVec2 crosshair(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);

                for (const auto& blk : blkList) {
                    if (renderedCount >= 800) break;

                    double cx = blk.isEntity ? blk.pos.x : (blk.pos.x + 0.5);
                    double cy = blk.isEntity ? (blk.pos.y + 0.4) : (blk.pos.y + 0.5);
                    double cz = blk.isEntity ? blk.pos.z : (blk.pos.z + 0.5);
                    double dx = cx - mcData.camPos.x;
                    double dy = cy - mcData.camPos.y;
                    double dz = cz - mcData.camPos.z;
                    double distSq = dx*dx + dy*dy + dz*dz;
                    if (distSq > rSq) continue;

                    Vec3 pMin, pMax;
                    if (blk.isEntity) {
                        pMin = { blk.pos.x - 0.5, blk.pos.y, blk.pos.z - 0.5 };
                        pMax = { blk.pos.x + 0.5, blk.pos.y + 1.0, blk.pos.z + 0.5 };
                    } else {
                        pMin = blk.pos;
                        double height = (blk.name == "Peony" || blk.name == "Ткань") ? 2.0 : 1.0;
                        pMax = { blk.pos.x + 1.0, blk.pos.y + height, blk.pos.z + 1.0 };
                    }
                    Vec3 c[8] = {
                        {pMin.x, pMin.y, pMin.z}, {pMax.x, pMin.y, pMin.z},
                        {pMax.x, pMin.y, pMax.z}, {pMin.x, pMin.y, pMax.z},
                        {pMin.x, pMax.y, pMin.z}, {pMax.x, pMax.y, pMin.z},
                        {pMax.x, pMax.y, pMax.z}, {pMin.x, pMax.y, pMax.z}
                    };
                    Vec2 sc[8];
                    bool sc_ok[8];
                    int visCount = 0;
                    float sumX = 0.0f, sumY = 0.0f;
                    for (int i = 0; i < 8; ++i) {
                        sc_ok[i] = JVMBridge::Get().WorldToScreen(c[i], sc[i]);
                        if (sc_ok[i]) {
                            visCount++;
                            sumX += sc[i].x;
                            sumY += sc[i].y;
                        }
                    }

                    int rI = (int)(blk.red * 255.0);
                    int gI = (int)(blk.green * 255.0);
                    int bI = (int)(blk.blue * 255.0);
                    ImU32 col = IM_COL32(rI, gI, bI, 240);

                    // Рендерим контур куба, если хотя бы одна вершина спроецирована на экран
                    if (visCount >= 1) {
                        auto DrawEdge = [&](int i1, int i2) {
                            if (sc_ok[i1] && sc_ok[i2]) {
                                draw->AddLine(ImVec2(sc[i1].x, sc[i1].y), ImVec2(sc[i2].x, sc[i2].y), col, 1.5f);
                            }
                        };
                        // 1-в-1 как в blockEspFrame из BlockESP.java: аккуратный 3D каркас куба
                        // Нижний контур
                        DrawEdge(0, 1); DrawEdge(1, 2); DrawEdge(2, 3); DrawEdge(3, 0);
                        // Верхний контур
                        DrawEdge(4, 5); DrawEdge(5, 6); DrawEdge(6, 7); DrawEdge(7, 4);
                        // Вертикальные стойки
                        DrawEdge(0, 4); DrawEdge(1, 5); DrawEdge(2, 6); DrawEdge(3, 7);

                        // Трейсеры от прицела к рудам (Traicer в настройках BlockESP)
                        if (be->tracers && visCount > 0) {
                            draw->AddLine(crosshair, ImVec2(sumX / visCount, sumY / visCount), col, 1.2f);
                        }

                        renderedCount++;
                    }
                }
            };

            RenderBlockList(mcData.espBlocks);
            RenderBlockList(mcData.entityOreBlocks);
        }
    }

    // ── Tracers (Линии от прицела к игрокам: темно-красные, друзья — зеленые) ───
    if (canDrawWorldHUD && m_clickGui && m_clickGui->IsTracersEnabled()) {
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        ImVec2 crosshair(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);

        for (const auto& pl : mcData.players) {
            std::string rName = pl.realName.empty() ? pl.name : pl.realName;
            bool isFriend = FriendManager::Get().IsFriend(rName);
            ImU32 col = isFriend ? IM_COL32(50, 220, 80, 230) : IM_COL32(180, 20, 20, 230); // Темно-красный, друзья - зеленый

            Vec3 targetPos = { pl.pos.x, pl.pos.y + pl.height * 0.5, pl.pos.z };
            Vec2 scPos;
            if (JVMBridge::Get().WorldToScreen(targetPos, scPos)) {
                draw->AddLine(crosshair, ImVec2(scPos.x, scPos.y), col, 1.5f);
            }
        }
    }

    // ── Custom Crosshair ──────────────────────────────────────────────────
    if (canDrawWorldHUD && m_clickGui && m_clickGui->IsCrosshairEnabled()) {
        auto* ch = m_clickGui->GetCrosshairModule();
        if (ch) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
            ImU32 col = IM_COL32(255, 255, 255, 240);
            ImU32 shadowCol = IM_COL32(0, 0, 0, 180);
            float g = ch->gap;
            float sz = ch->size;
            float th = ch->thickness;

            if (ch->dot) {
                draw->AddRectFilled(ImVec2(center.x - 1.5f, center.y - 1.5f), ImVec2(center.x + 1.5f, center.y + 1.5f), col);
            }
            auto DrawCrossLine = [&](ImVec2 p1, ImVec2 p2) {
                draw->AddLine(p1, p2, shadowCol, th + 1.5f);
                draw->AddLine(p1, p2, col, th);
            };
            DrawCrossLine(ImVec2(center.x - g - sz, center.y), ImVec2(center.x - g, center.y));
            DrawCrossLine(ImVec2(center.x + g, center.y), ImVec2(center.x + g + sz, center.y));
            DrawCrossLine(ImVec2(center.x, center.y - g - sz), ImVec2(center.x, center.y - g));
            DrawCrossLine(ImVec2(center.x, center.y + g), ImVec2(center.x, center.y + g + sz));
        }
    }

    // ── AimBot — точь-в-точь как в NoDesynce (BloodAim) ───────────────────────
    if (canDrawWorldHUD && m_clickGui && m_clickGui->IsAimBotEnabled()) {
        auto* aim = m_clickGui->GetAimBotModule();
        if (aim) {
            ImDrawList* draw = ImGui::GetBackgroundDrawList();
            ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);

            // Отрисовка круга FOV с кастомным цветом (aim->fovColor)
            if (aim->drawFov) {
                float gameFov = (mcData.fov > 10.0f) ? mcData.fov : 70.0f;
                float radius = (aim->fov / gameFov) * (ImGui::GetIO().DisplaySize.y * 0.46f);
                ImU32 fovCol = IM_COL32(
                    (int)(aim->fovColor[0] * 255.0f),
                    (int)(aim->fovColor[1] * 255.0f),
                    (int)(aim->fovColor[2] * 255.0f),
                    (int)(aim->fovColor[3] * 255.0f)
                );
                draw->AddCircle(center, radius, fovCol, 80, 1.8f);
            }

            // При открытом меню чита или активном GUI-экране (инвентарь, сундук) не наводимся
            if (m_guiOpen || mcData.hasScreen) {
                aim->hasTarget = false;
                aim->ticks = 0;
            } else {
                double myFeetY = (mcData.playerPos.y != 0.0) ? mcData.playerPos.y : (mcData.camPos.y - 1.62);
                BloodAim::Point playerPos{ mcData.camPos.x, myFeetY, mcData.camPos.z };
                float eyeHeight = 1.62f;
                float yaw = mcData.camYaw;
                float pitch = mcData.camPitch;

                const PlayerTagInfo* bestTarget = nullptr;
                float bestScore = 1e10f;

                for (const auto& pl : mcData.players) {
                    if (pl.distance < 0.25f) continue;
                    if (pl.distance > aim->range) continue;
                    if (!mcData.username.empty() && mcData.username != "Player" && pl.name == mcData.username) continue;

                    std::string realName = pl.realName.empty() ? pl.name : pl.realName;
                    if (FriendManager::Get().IsFriend(realName)) continue;
                    if (pl.health <= 0.0f) continue;
                    if (!aim->bots && (pl.isBot || pl.name == "Zombie" || pl.realName == "Zombie")) continue;
                    if (!aim->walls && !pl.canBeSeen && pl.distance > 3.5f) continue;

                    // Точь-в-точь выбор цели по MouseDistance из NoDesynce:
                    BloodAim::Point targetPos{ pl.pos.x, pl.pos.y, pl.pos.z };
                    float score = BloodAim::MouseDistance(targetPos, playerPos, eyeHeight, yaw, pitch);

                    if (score <= aim->fov && score < bestScore) {
                        bestScore = score;
                        bestTarget = &pl;
                    }
                }

                if (bestTarget) {
                    aim->hasTarget = true;
                    aim->ticks++;

                    BloodAim::Point position{ bestTarget->rawPos.x, bestTarget->rawPos.y, bestTarget->rawPos.z };
                    BloodAim::Point lastRender{ bestTarget->lastRender.x, bestTarget->lastRender.y, bestTarget->lastRender.z };
                    BloodAim::Point previousTick{ bestTarget->prevTick.x, bestTarget->prevTick.y, bestTarget->prevTick.z };

                    double partial = mcData.timerPartialTicks > 0.0f ? mcData.timerPartialTicks : 1.0;

                    // Predict точь-в-точь из NoDesynce
                    position = BloodAim::Predict(position, lastRender, previousTick, playerPos, partial,
                                                 aim->predict, aim->range, aim->vPredict);

                    // Расчет целевых углов Face точь-в-точь из NoDesynce (с поддержкой BAim / bodyAim)
                    auto facing = BloodAim::Face(position, playerPos, eyeHeight, yaw, pitch, aim->bodyAim);

                    if (std::isfinite(facing.yaw) && std::isfinite(facing.pitch)) {
                        float finalYaw = facing.yaw;
                        float finalPitch = facing.pitch;

                        // Настройка плавности: если smoothness == 1.0f — моментальный snap, иначе плавная интерполяция
                        if (aim->smoothness > 1.0f) {
                            float dy = static_cast<float>(BloodAim::Wrap(facing.yaw - yaw)) / aim->smoothness;
                            float dp = static_cast<float>(BloodAim::Wrap(facing.pitch - pitch)) / aim->smoothness;
                            finalYaw = yaw + dy;
                            finalPitch = pitch + dp;
                        }

                        finalPitch = std::clamp(finalPitch, -90.0f, 90.0f);
                        JVMBridge::Get().SetPlayerRotation(finalYaw, finalPitch);

                        if (aim->autoShoot && aim->ticks > 2) {
                            DWORD now = GetTickCount();
                            if (now - aim->lastShootTime >= 100) {
                                aim->lastShootTime = now;
                                JVMBridge::Get().PlayerSwingArm();
                            }
                        }
                    }
                } else {
                    aim->hasTarget = false;
                    aim->ticks = 0;
                }
            }
        }
    }

    // AutoMine — логика копания руды (1-в-1 как в AutoMine.java)
    if (canDrawWorldHUD && m_clickGui && m_clickGui->IsAutoMineEnabled()) {
        auto* am = m_clickGui->GetAutoMineModule();
        if (am) {
            Vec3 bPos, aPos;
            if (JVMBridge::Get().QueryAutoMineTarget(am->distance, bPos, aPos)) {
                am->hasTarget = true;
                am->targetBlockPos = bPos;
                am->targetAimPos = aPos;

                // Наведение на стойку для брони (1-в-1 как в AutoMine.java: player.posY + player.getEyeHeight())
                double myEyeX = (mcData.playerPos.x != 0.0) ? mcData.playerPos.x : mcData.camPos.x;
                double myEyeY = (mcData.playerPos.y != 0.0 ? mcData.playerPos.y : (mcData.camPos.y - 1.62)) + 1.62;
                double myEyeZ = (mcData.playerPos.z != 0.0) ? mcData.playerPos.z : mcData.camPos.z;

                double diffX = aPos.x - myEyeX;
                double diffY = aPos.y - myEyeY;
                double diffZ = aPos.z - myEyeZ;
                double dist2D = sqrt(diffX * diffX + diffZ * diffZ);
                constexpr double kPI = 3.14159265358979323846;
                float targetYaw = static_cast<float>((atan2(diffZ, diffX) * 180.0 / kPI) - 90.0);
                float targetPitch = static_cast<float>(-(atan2(diffY, dist2D) * 180.0 / kPI));
                JVMBridge::Get().SetPlayerRotation(targetYaw, targetPitch);

                static DWORD lastMineTime = 0;
                DWORD now = GetTickCount();
                if (now - lastMineTime >= 230) {
                    lastMineTime = now;
                    JVMBridge::Get().SendDiggingPacket(static_cast<int>(bPos.x),
                                                      static_cast<int>(bPos.y),
                                                      static_cast<int>(bPos.z), 0);
                    JVMBridge::Get().PlayerSwingArm();
                }
            } else {
                am->hasTarget = false;
            }

            if (am->hasTarget) {
                ImDrawList* draw = ImGui::GetBackgroundDrawList();
                Vec3 minP = am->targetBlockPos;
                Vec3 maxP = { minP.x + 1.0, minP.y + 1.0, minP.z + 1.0 };
                Vec3 bCorners[8] = {
                    { minP.x, minP.y, minP.z }, { maxP.x, minP.y, minP.z },
                    { maxP.x, minP.y, maxP.z }, { minP.x, minP.y, maxP.z },
                    { minP.x, maxP.y, minP.z }, { maxP.x, maxP.y, minP.z },
                    { maxP.x, maxP.y, maxP.z }, { minP.x, maxP.y, maxP.z }
                };
                Vec2 sCorners[8];
                bool allOk = true;
                for (int i = 0; i < 8; ++i) {
                    if (!JVMBridge::Get().WorldToScreen(bCorners[i], sCorners[i])) { allOk = false; break; }
                }
                if (allOk) {
                    ImU32 boxCol = IM_COL32(75, 230, 110, 220);
                    draw->AddLine(ImVec2(sCorners[0].x, sCorners[0].y), ImVec2(sCorners[1].x, sCorners[1].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[1].x, sCorners[1].y), ImVec2(sCorners[2].x, sCorners[2].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[2].x, sCorners[2].y), ImVec2(sCorners[3].x, sCorners[3].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[3].x, sCorners[3].y), ImVec2(sCorners[0].x, sCorners[0].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[4].x, sCorners[4].y), ImVec2(sCorners[5].x, sCorners[5].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[5].x, sCorners[5].y), ImVec2(sCorners[6].x, sCorners[6].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[6].x, sCorners[6].y), ImVec2(sCorners[7].x, sCorners[7].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[7].x, sCorners[7].y), ImVec2(sCorners[4].x, sCorners[4].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[0].x, sCorners[0].y), ImVec2(sCorners[4].x, sCorners[4].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[1].x, sCorners[1].y), ImVec2(sCorners[5].x, sCorners[5].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[2].x, sCorners[2].y), ImVec2(sCorners[6].x, sCorners[6].y), boxCol, 1.5f);
                    draw->AddLine(ImVec2(sCorners[3].x, sCorners[3].y), ImVec2(sCorners[7].x, sCorners[7].y), boxCol, 1.5f);
                }
            }
        }
    }

    // GuiWalk — ходьба при открытых экранах/меню (1-в-1 как в GuiWalk.java)
    if (m_clickGui && m_clickGui->IsGuiWalkEnabled()) {
        if (mcData.hasScreen && !mcData.isChatOpen) {
            bool fwd = (GetAsyncKeyState('W') & 0x8000) != 0;
            bool bck = (GetAsyncKeyState('S') & 0x8000) != 0;
            bool lft = (GetAsyncKeyState('A') & 0x8000) != 0;
            bool rgt = (GetAsyncKeyState('D') & 0x8000) != 0;
            bool jmp = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
            JVMBridge::Get().UpdateGuiWalkKeys(fwd, bck, lft, rgt, jmp);
        }
    }

    // MCF (Middle Click Friend) — добавление/удаление в друзья по клику на колёсико мыши по хитбоксу игрока
    {
        static bool s_prevMButton = false;
        bool currMB = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        if (currMB && !s_prevMButton) {
            auto* mcf = m_clickGui ? m_clickGui->GetMCFModule() : nullptr;
            if (mcf && mcf->IsEnabled() && mcf->onFriend) {
                std::string targetName = "";

                // Направление взгляда камеры игрока
                constexpr double kPI = 3.14159265358979323846;
                double yawRad   = mcData.camYaw * (kPI / 180.0);
                double pitchRad = mcData.camPitch * (kPI / 180.0);

                double dirX = -sin(yawRad) * cos(pitchRad);
                double dirY = -sin(pitchRad);
                double dirZ =  cos(yawRad) * cos(pitchRad);

                Vec3 eyePos = mcData.camPos;
                float bestHitDist = 6.0f; // Максимальная дистанция ограничена 6 блоками

                for (const auto& pl : mcData.players) {
                    if (pl.distance < 0.2f || pl.distance > 6.0f) continue;
                    if (!mcData.username.empty() && mcData.username != "Player" && pl.name == mcData.username) continue;
                    if (pl.name == "Zombie" || pl.realName == "Zombie") continue;

                    // Хитбокс игрока Minecraft (0.6x1.8x0.6)
                    double hPad = 0.35;
                    double minX = pl.pos.x - hPad;
                    double maxX = pl.pos.x + hPad;
                    double minY = pl.pos.y;
                    double maxY = pl.pos.y + pl.height;
                    double minZ = pl.pos.z - hPad;
                    double maxZ = pl.pos.z + hPad;

                    // Ray-AABB intersection test
                    double tmin = 0.0, tmax = 6.0;

                    auto testAxis = [&](double rO, double rD, double bMin, double bMax) -> bool {
                        if (fabs(rD) < 1e-6) {
                            return (rO >= bMin && rO <= bMax);
                        }
                        double invD = 1.0 / rD;
                        double t1 = (bMin - rO) * invD;
                        double t2 = (bMax - rO) * invD;
                        if (t1 > t2) std::swap(t1, t2);
                        tmin = std::max(tmin, t1);
                        tmax = std::min(tmax, t2);
                        return (tmin <= tmax);
                    };

                    if (testAxis(eyePos.x, dirX, minX, maxX) &&
                        testAxis(eyePos.y, dirY, minY, maxY) &&
                        testAxis(eyePos.z, dirZ, minZ, maxZ)) {
                        if (tmin < bestHitDist) {
                            bestHitDist = static_cast<float>(tmin);
                            targetName = pl.realName.empty() ? pl.name : pl.realName;
                        }
                    }
                }

                if (!targetName.empty()) {
                    bool added = FriendManager::Get().ToggleFriend(targetName);
                    if (added) {
                        NotificationManager::Get().Add("Added " + targetName + " to friends", true);
                    } else {
                        NotificationManager::Get().Add("Removed " + targetName + " from friends", false);
                    }
                }
            }
        }
        s_prevMButton = currMB;
    }

    // ── 1. Назначение бинда клавиши (Listen Keybind) ─────────────────────────
    static bool s_wasKeyBindingDown[256]{};
    static ModuleBase* s_lastActiveBindMod = nullptr;

    if (m_clickGui && m_clickGui->GetBindingModule()) {
        auto* bindMod = m_clickGui->GetBindingModule();

        // При первом открытии диалога бинда снимаем отпечаток текущих зажатых клавиш
        if (s_lastActiveBindMod != bindMod) {
            s_lastActiveBindMod = bindMod;
            for (int k = 1; k < 256; ++k) {
                s_wasKeyBindingDown[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
            }
        } else {
            for (int k = 1; k < 256; ++k) {
                if (k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON) continue;
                if (k == VK_RSHIFT || k == VK_INSERT) continue;

                bool isDown = (GetAsyncKeyState(k) & 0x8000) != 0;
                if (isDown && !s_wasKeyBindingDown[k]) {
                    if (k == VK_ESCAPE || k == VK_DELETE) {
                        bindMod->SetKey(0);
                        NotificationManager::Get().Add(bindMod->Name() + " unbinded", false);
                    } else {
                        bindMod->SetKey(k);
                        NotificationManager::Get().Add(bindMod->Name() + " bound to " + bindMod->GetKeyName(), true);
                    }
                    m_clickGui->SetBindingModule(nullptr);
                    s_lastActiveBindMod = nullptr;
                    break;
                }
                s_wasKeyBindingDown[k] = isDown;
            }
        }
    } else {
        s_lastActiveBindMod = nullptr;
    }

    if (m_clickGui && !m_clickGui->GetBindingModule()) {
        // ── 2. Срабатывание биндов модулей (Toggle Modules via Keybind) ────────
        // Блокируем срабатывание биндов при открытом меню чита, инвентаре, сундуке, чате или экране меню
        if (!m_guiOpen && !io.WantTextInput && !mcData.hasScreen && !mcData.isChatOpen) {
            for (const auto& mod : m_clickGui->Modules()) {
                int key = mod->GetKey();
                if (key > 0 && (GetAsyncKeyState(key) & 1)) {
                    mod->Toggle();
                    NotificationManager::Get().Add(mod->Name(), mod->IsEnabled());
                }
            }
        }
    }

    // ClickGUI — по RSHIFT / INSERT с плавной анимацией открытия и мгновенным закрытием
    static float s_guiAnimProgress = 0.0f;
    float dt = io.DeltaTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    if (m_guiOpen) {
        float openSpeed = 12.0f;
        s_guiAnimProgress += (1.0f - s_guiAnimProgress) * (1.0f - std::exp(-openSpeed * dt));
        if (s_guiAnimProgress > 0.99f) s_guiAnimProgress = 1.0f;
    } else {
        // Мгновенное и чистое закрытие без зависания и задержек на экране
        s_guiAnimProgress = 0.0f;
    }

    if (s_guiAnimProgress > 0.001f && m_clickGui) {
        m_clickGui->Draw(s_guiAnimProgress);
    }

    // Отрисовка очереди уведомлений (Notifications справа снизу) с учетом GUI Scale
    float notifyScale = (m_clickGui ? m_clickGui->GetGUIScale() : 1.0f);
    NotificationManager::Get().Render(ImGui::GetForegroundDrawList(), io.DisplaySize, ImGui::GetFont(), notifyScale);

    // Рендерим ImGui
    ImGui::Render();
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

    // ── Полное восстановление расширенного стейта OpenGL ──────────────────────
    if (pfn_glBindBuffer) {
        pfn_glBindBuffer(0x8894 /* GL_ARRAY_BUFFER */, last_array_buffer);
        pfn_glBindBuffer(0x8895 /* GL_ELEMENT_ARRAY_BUFFER */, last_element_buffer);
    }

    if (pfn_glActiveTexture) {
        pfn_glActiveTexture(0x84C1 /* GL_TEXTURE1 */);
        glBindTexture(GL_TEXTURE_2D, last_tex_unit1);

        pfn_glActiveTexture(0x84C0 /* GL_TEXTURE0 */);
        glBindTexture(GL_TEXTURE_2D, last_tex_unit0);

        pfn_glActiveTexture(last_active_texture);
    } else {
        glBindTexture(GL_TEXTURE_2D, last_tex_unit0);
    }

    if (pfn_glUseProgram) {
        pfn_glUseProgram(last_program);
    }

    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
}

