// src/gui/clickgui.cpp
#include "clickgui.h"
#include "gui_manager.h"
#include "notification_manager.h"
#include "config_manager.h"
#include "hud.h"
#include "../jvm/jvm_bridge.h"
#include "../hooks/gl_hook.h"
#include <imgui.h>
#include <Windows.h>
#include <objidl.h>
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include <string>
#include <cmath>

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

static GLuint s_avatarTex = 0;
static bool   s_avatarChecked = false;

bool g_hasIconFont = false;

static void EnsureAvatarLoaded() {
    if (s_avatarTex != 0) return;
    static int s_retryAttempts = 0;
    if (s_retryAttempts > 50) return;
    s_retryAttempts++;

    std::vector<std::string> paths;

    // Приоритетная пользовательская аватарка (D:\ava.jpg по запросу пользователя)
    paths.push_back("D:\\ava.jpg");
    paths.push_back("D:\\ava.png");
    paths.push_back("D:\\ava.jpeg");
    paths.push_back("D:\\avatar.jpg");
    paths.push_back("D:\\avatar.png");
    paths.push_back("C:\\Users\\PC\\Desktop\\Легитка вась\\ava.jpg");
    paths.push_back("C:\\Users\\PC\\Desktop\\Легитка вась\\ava.png");

    char dllPath[MAX_PATH] = { 0 };
    HMODULE hMod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&EnsureAvatarLoaded), &hMod);
    if (hMod) {
        GetModuleFileNameA(hMod, dllPath, MAX_PATH);
        std::string d = dllPath;
        size_t p = d.find_last_of("\\/");
        if (p != std::string::npos) {
            std::string dir = d.substr(0, p);
            paths.push_back(dir + "\\ava.jpg");
            paths.push_back(dir + "\\ava.png");
            paths.push_back(dir + "\\avatar.png");
            paths.push_back(dir + "\\avatar.jpg");
            paths.push_back(dir + "\\avatar.jpeg");
        }
    }

    paths.push_back("C:\\Users\\PC\\Desktop\\Легитка вась\\avatar.png");
    paths.push_back("C:\\Users\\PC\\Desktop\\Легитка вась\\avatar.jpg");
    paths.push_back("avatar.png");
    paths.push_back("avatar.jpg");

    char appdata[MAX_PATH] = { 0 };
    GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    if (appdata[0] != '\0') {
        std::string cfgDir = std::string(appdata) + "\\.minecraft\\myr_legit\\";
        paths.push_back(cfgDir + "avatar.png");
        paths.push_back(cfgDir + "avatar.jpg");
    }

    std::string foundPath;
    for (const auto& p : paths) {
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            foundPath = p;
            break;
        }
    }

    if (foundPath.empty()) return;

    static bool s_gdiInitialized = false;
    static ULONG_PTR s_gdiToken = 0;
    if (!s_gdiInitialized) {
        Gdiplus::GdiplusStartupInput gdiInput;
        if (Gdiplus::GdiplusStartup(&s_gdiToken, &gdiInput, NULL) == Gdiplus::Ok) {
            s_gdiInitialized = true;
        }
    }

    if (s_gdiInitialized) {
        wchar_t wpath[MAX_PATH] = { 0 };
        MultiByteToWideChar(CP_ACP, 0, foundPath.c_str(), -1, wpath, MAX_PATH);
        Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(wpath);
        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
            int w = bmp->GetWidth();
            int h = bmp->GetHeight();
            if (w > 0 && h > 0) {
                Gdiplus::Rect rect(0, 0, w, h);
                Gdiplus::BitmapData data;
                if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
                    GLint lastTex = 0;
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTex);

                    glGenTextures(1, &s_avatarTex);
                    glBindTexture(GL_TEXTURE_2D, s_avatarTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, data.Scan0);
                    glBindTexture(GL_TEXTURE_2D, lastTex);

                    bmp->UnlockBits(&data);
                }
            }
            delete bmp;
        }
    }
}

struct AccentPalette {
    const char* name;
    ImU32 primary;
    ImU32 secondary;
    ImVec4 primaryVec;
    ImVec4 secondaryVec;
};

static const AccentPalette s_accents[] = {
    { "Violet", IM_COL32(120, 104, 255, 255), IM_COL32(84, 140, 255, 255), ImVec4(0.471f, 0.408f, 1.000f, 1.0f), ImVec4(0.329f, 0.549f, 1.000f, 1.0f) },
    { "Blue",   IM_COL32(67, 140, 255, 255),  IM_COL32(47, 196, 232, 255), ImVec4(0.263f, 0.549f, 1.000f, 1.0f), ImVec4(0.184f, 0.769f, 0.910f, 1.0f) },
    { "Mint",   IM_COL32(53, 201, 154, 255),  IM_COL32(84, 168, 255, 255), ImVec4(0.208f, 0.788f, 0.604f, 1.0f), ImVec4(0.329f, 0.659f, 1.000f, 1.0f) },
    { "Rose",   IM_COL32(228, 93, 150, 255),  IM_COL32(139, 99, 255, 255), ImVec4(0.894f, 0.365f, 0.588f, 1.0f), ImVec4(0.545f, 0.388f, 1.000f, 1.0f) }
};

static const char* GetModuleDescription(const std::string& name) {
    if (name == "AimBot")         return "Плавное наведение на цель (Aim Assist)";
    if (name == "HUD")            return "Отображение ника, FPS и задержки сети";
    if (name == "ArmorHUD")       return "Индикатор прочности надетой экипировки";
    if (name == "NameTags")       return "Расширенные никнеймы, броня и полоска HP";
    if (name == "ItemESP")        return "Подсветка предметов на земле и иконок";
    if (name == "Arrows")         return "Стрелки-указатели направления к игрокам";
    if (name == "AspectRatio")    return "Кастомное соотношение сторон экрана";
    if (name == "ViewModel")      return "Настройка положения оружия и рук";
    if (name == "WallHackPlayer") return "ESP силуэты игроков сквозь стены";
    if (name == "WallHack")       return "ESP на мобов и животных";
    if (name == "BlockESP")       return "3D подсветка руд, ягод и саженцев";
    if (name == "Tracers")        return "Линии-трассеры от прицела к игрокам";
    if (name == "Titles")         return "Масштабирование серверных титров и надписей";
    if (name == "ScreenProtect")  return "Скрытие чита на записи и демонстрации экрана";
    if (name == "NameProtect")    return "Замена собственного никнейма на защищенный";
    if (name == "Crosshair")      return "Кастомный прицел с настройкой размера и зазора";
    if (name == "GuiWalk")        return "Возможность ходить при открытом меню";
    if (name == "AutoMine")       return "Автоматическая добыча руды и деревьев";
    if (name == "MCF")            return "Добавление в друзья кликом на колесико мыши";
    return "Модуль чит-клиента";
}

#include <unordered_map>

// ── Кастомный анимированный тумблер (iOS / Figma Toggle Switch с физикой) ───────────────
static bool CustomToggle(const char* str_id, bool* v, const ImVec4& accentCol, float s = 1.0f) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    float height = 20.0f * s;
    float width = 36.0f * s;
    float radius = height * 0.5f;

    // Расширенная область клика для мгновенного и надежного нажатия
    ImGui::SetCursorScreenPos(ImVec2(p.x - 3.0f * s, p.y - 3.0f * s));
    ImGui::InvisibleButton(str_id, ImVec2(width + 6.0f * s, height + 6.0f * s));
    bool clicked = false;
    if (ImGui::IsItemClicked(0) || (ImGui::IsItemHovered() && ImGui::GetIO().MouseClicked[0])) {
        *v = !(*v);
        clicked = true;
    }

    // Уникальный ID для каждого отдельного элемента (исключает конфликт ключей)
    ImGuiID id = ImGui::GetID(str_id);
    static std::unordered_map<ImGuiID, float> s_toggleAnims;

    auto it = s_toggleAnims.find(id);
    if (it == s_toggleAnims.end()) {
        s_toggleAnims[id] = *v ? 1.0f : 0.0f;
    }

    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    float& anim = s_toggleAnims[id];
    float target = *v ? 1.0f : 0.0f;

    // Плавная пластичная анимация переключения (длительность ~0.28 сек)
    float factor = 1.0f - std::exp(-7.0f * dt);
    anim += (target - anim) * factor;
    if (fabsf(target - anim) < 0.001f) anim = target;

    // Фон тумблера: темный благородный при выключенном, акцентный при включенном
    ImVec4 bgOff(0.12f, 0.14f, 0.20f, 1.0f);
    ImVec4 bgCol(
        bgOff.x + (accentCol.x - bgOff.x) * anim,
        bgOff.y + (accentCol.y - bgOff.y) * anim,
        bgOff.z + (accentCol.z - bgOff.z) * anim,
        1.0f
    );
    draw->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::ColorConvertFloat4ToU32(bgCol), radius);

    // Аккуратная рамка для четкости
    ImU32 borderCol = IM_COL32(42, 48, 66, (int)(180.0f * (1.0f - anim * 0.4f)));
    draw->AddRect(p, ImVec2(p.x + width, p.y + height), borderCol, radius, 0, 1.0f * s);

    float thumbRadius = radius - 2.5f * s;
    float thumbMinX = p.x + radius;
    float thumbMaxX = p.x + width - radius;
    float thumbX = thumbMinX + (thumbMaxX - thumbMinX) * anim;
    float thumbY = p.y + radius;

    // Эффект физического растягивания при переключении (squash & stretch)
    float stretch = std::sin(anim * 3.14159265f) * 2.2f * s;
    float thumbHalfW = thumbRadius + stretch * 0.5f;

    // Мягкая тень под бегунком для объема
    draw->AddRectFilled(
        ImVec2(thumbX - thumbHalfW, thumbY - thumbRadius + 1.2f * s),
        ImVec2(thumbX + thumbHalfW, thumbY + thumbRadius + 1.2f * s),
        IM_COL32(0, 0, 0, (int)(75.0f * (0.3f + 0.7f * anim))),
        thumbRadius
    );

    // Неоновое свечение бегунка при активном состоянии
    if (anim > 0.02f) {
        ImVec4 glowCol = accentCol;
        glowCol.w = 0.45f * anim;
        draw->AddRectFilled(
            ImVec2(thumbX - thumbHalfW - 3.0f * s, thumbY - thumbRadius - 3.0f * s),
            ImVec2(thumbX + thumbHalfW + 3.0f * s, thumbY + thumbRadius + 3.0f * s),
            ImGui::ColorConvertFloat4ToU32(glowCol),
            thumbRadius + 3.0f * s
        );
    }

    // Цвет бегунка: от чистого благородного серебра (OFF) до чистого белого #FFFFFF (ON)
    ImVec4 thOff(0.82f, 0.85f, 0.92f, 1.0f);
    ImVec4 thOn(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 thCol(
        thOff.x + (thOn.x - thOff.x) * anim,
        thOff.y + (thOn.y - thOff.y) * anim,
        thOff.z + (thOn.z - thOff.z) * anim,
        1.0f
    );

    draw->AddRectFilled(
        ImVec2(thumbX - thumbHalfW, thumbY - thumbRadius),
        ImVec2(thumbX + thumbHalfW, thumbY + thumbRadius),
        ImGui::ColorConvertFloat4ToU32(thCol),
        thumbRadius
    );

    return clicked;
}

// ── Кастомный чекбокс (Figma Rounded Checkbox с ровным текстом) ───────────────
static bool CustomCheckbox(const char* label, const char* desc, bool* v, const ImVec4& accentCol, float s = 1.0f) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    float boxSz = 18.0f * s;
    float itemH = desc ? (boxSz + 22.0f * s) : (boxSz + 6.0f * s);

    ImGui::InvisibleButton(label, ImVec2(ImGui::GetContentRegionAvail().x, itemH));
    bool clicked = false;
    if (ImGui::IsItemClicked()) {
        *v = !(*v);
        clicked = true;
    }

    ImU32 bgCol = *v ? ImGui::ColorConvertFloat4ToU32(accentCol) : IM_COL32(28, 33, 46, 255);
    ImU32 borderCol = *v ? ImGui::ColorConvertFloat4ToU32(accentCol) : IM_COL32(52, 58, 77, 255);

    draw->AddRectFilled(p, ImVec2(p.x + boxSz, p.y + boxSz), bgCol, 5.0f * s);
    draw->AddRect(p, ImVec2(p.x + boxSz, p.y + boxSz), borderCol, 5.0f * s, 0, 1.0f * s);

    if (*v) {
        ImVec2 p1 = ImVec2(p.x + 4.5f * s, p.y + 9.0f * s);
        ImVec2 p2 = ImVec2(p.x + 7.5f * s, p.y + 12.5f * s);
        ImVec2 p3 = ImVec2(p.x + 13.5f * s, p.y + 5.0f * s);
        draw->AddLine(p1, p2, IM_COL32(255, 255, 255, 255), 2.0f * s);
        draw->AddLine(p2, p3, IM_COL32(255, 255, 255, 255), 2.0f * s);
    }

    float textX = p.x + boxSz + 10.0f * s;
    draw->AddText(ImVec2(textX, p.y - 1.0f * s), IM_COL32(230, 235, 248, 255), label);
    if (desc) {
        draw->AddText(ImVec2(textX, p.y + 18.0f * s), IM_COL32(101, 109, 128, 255), desc);
    }

    return clicked;
}

// ── Модерновый слайдер (Figma Setting Block: Label слева, значение справа, полоса на всю ширину) ──
static bool DrawModernSliderFloat(const char* label, const char* id, float* v, float v_min, float v_max, const char* format, ImU32 accentCol, float s) {
    ImGui::PushID(id);
    float availW = ImGui::GetContentRegionAvail().x;

    char valStr[32];
    snprintf(valStr, sizeof(valStr), format, *v);

    // Верхняя строка: Лейбл и текущее значение в акцентном цвете
    ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.86f, 1.0f), "%s", label);
    ImVec2 vSz = ImGui::CalcTextSize(valStr);
    if (availW - vSz.x > 50.0f) {
        ImGui::SameLine(availW - vSz.x);
        ImGui::TextColored(ImColor(accentCol).Value, "%s", valStr);
    }

    // Нижняя строка: Аккуратный скругленный слайдер без обрезки текста
    ImGui::SetNextItemWidth(availW);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(28, 33, 46, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(36, 42, 58, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(40, 47, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,     accentCol);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 14.0f * s);

    bool changed = ImGui::SliderFloat("##slider", v, v_min, v_max, "");

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    ImGui::Spacing();
    ImGui::PopID();
    return changed;
}

static bool DrawModernSliderInt(const char* label, const char* id, int* v, int v_min, int v_max, const char* format, ImU32 accentCol, float s) {
    ImGui::PushID(id);
    float availW = ImGui::GetContentRegionAvail().x;

    char valStr[32];
    snprintf(valStr, sizeof(valStr), format, *v);

    ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.86f, 1.0f), "%s", label);
    ImVec2 vSz = ImGui::CalcTextSize(valStr);
    if (availW - vSz.x > 50.0f) {
        ImGui::SameLine(availW - vSz.x);
        ImGui::TextColored(ImColor(accentCol).Value, "%s", valStr);
    }

    ImGui::SetNextItemWidth(availW);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(28, 33, 46, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(36, 42, 58, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(40, 47, 65, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,     accentCol);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 14.0f * s);

    bool changed = ImGui::SliderInt("##slider", v, v_min, v_max, "");

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    ImGui::Spacing();
    ImGui::PopID();
    return changed;
}

ClickGUI::ClickGUI() {
    // VISUAL
    m_modules.push_back(std::make_unique<ModuleHUD>());
    m_modules.push_back(std::make_unique<ModuleArmorHUD>());
    m_modules.push_back(std::make_unique<ModuleNameTags>());
    m_modules.push_back(std::make_unique<ModuleItemESP>());
    m_modules.push_back(std::make_unique<ModuleArrows>());
    m_modules.push_back(std::make_unique<ModuleAspect>());
    m_modules.push_back(std::make_unique<ModuleViewModel>());
    m_modules.push_back(std::make_unique<ModuleWallHackPlayer>());
    m_modules.push_back(std::make_unique<ModuleWallHack>());
    m_modules.push_back(std::make_unique<ModuleBlockESP>());
    m_modules.push_back(std::make_unique<ModuleTracers>());
    m_modules.push_back(std::make_unique<ModuleTitles>());
    m_modules.push_back(std::make_unique<ModuleScreenProtect>());
    m_modules.push_back(std::make_unique<ModuleNameProtect>());
    m_modules.push_back(std::make_unique<ModuleCrosshair>());

    // COMBAT
    m_modules.push_back(std::make_unique<ModuleAimBot>());

    // MOVEMENT
    m_modules.push_back(std::make_unique<ModuleGuiWalk>());

    // MISC
    m_modules.push_back(std::make_unique<ModuleAutoMine>());
    m_modules.push_back(std::make_unique<ModuleMCF>());

    // Авто-загрузка конфигурации по умолчанию
    ConfigManager::Get().LoadDefault(this);

    // Выбираем первый модуль по умолчанию
    for (auto& mod : m_modules) {
        if (mod->GetCategory() == m_activeCat) {
            m_selectedModule = mod.get();
            break;
        }
    }
}

ModuleHUD* ClickGUI::GetHUDModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "HUD") return dynamic_cast<ModuleHUD*>(mod.get());
    }
    return nullptr;
}

ModuleArmorHUD* ClickGUI::GetArmorHUDModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "ArmorHUD") return dynamic_cast<ModuleArmorHUD*>(mod.get());
    }
    return nullptr;
}

ModuleNameTags* ClickGUI::GetNameTagsModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "NameTags") return dynamic_cast<ModuleNameTags*>(mod.get());
    }
    return nullptr;
}

ModuleItemESP* ClickGUI::GetItemESPModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "ItemESP") return dynamic_cast<ModuleItemESP*>(mod.get());
    }
    return nullptr;
}

ModuleViewModel* ClickGUI::GetViewModelModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "ViewModel") return dynamic_cast<ModuleViewModel*>(mod.get());
    }
    return nullptr;
}

ModuleGuiWalk* ClickGUI::GetGuiWalkModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "GuiWalk") return dynamic_cast<ModuleGuiWalk*>(mod.get());
    }
    return nullptr;
}

ModuleAutoMine* ClickGUI::GetAutoMineModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "AutoMine") return dynamic_cast<ModuleAutoMine*>(mod.get());
    }
    return nullptr;
}

ModuleMCF* ClickGUI::GetMCFModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "MCF") return dynamic_cast<ModuleMCF*>(mod.get());
    }
    return nullptr;
}

ModuleAimBot* ClickGUI::GetAimBotModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "AimBot") return dynamic_cast<ModuleAimBot*>(mod.get());
    }
    return nullptr;
}

ModuleArrows* ClickGUI::GetArrowsModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "Arrows") return dynamic_cast<ModuleArrows*>(mod.get());
    }
    return nullptr;
}

ModuleAspect* ClickGUI::GetAspectModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "AspectRatio" || mod->Name() == "Aspect") return dynamic_cast<ModuleAspect*>(mod.get());
    }
    return nullptr;
}

ModuleScreenProtect* ClickGUI::GetScreenProtectModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "ScreenProtect") return dynamic_cast<ModuleScreenProtect*>(mod.get());
    }
    return nullptr;
}

ModuleWallHackPlayer* ClickGUI::GetWallHackPlayerModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "WallHackPlayer") return dynamic_cast<ModuleWallHackPlayer*>(mod.get());
    }
    return nullptr;
}

ModuleWallHack* ClickGUI::GetWallHackModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "WallHack") return dynamic_cast<ModuleWallHack*>(mod.get());
    }
    return nullptr;
}

ModuleBlockESP* ClickGUI::GetBlockESPModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "BlockESP") return dynamic_cast<ModuleBlockESP*>(mod.get());
    }
    return nullptr;
}

ModuleTracers* ClickGUI::GetTracersModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "Tracers") return dynamic_cast<ModuleTracers*>(mod.get());
    }
    return nullptr;
}

ModuleTitles* ClickGUI::GetTitlesModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "Titles") return dynamic_cast<ModuleTitles*>(mod.get());
    }
    return nullptr;
}

ModuleNameProtect* ClickGUI::GetNameProtectModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "NameProtect") return dynamic_cast<ModuleNameProtect*>(mod.get());
    }
    return nullptr;
}

ModuleCrosshair* ClickGUI::GetCrosshairModule() const {
    for (const auto& mod : m_modules) {
        if (mod->Name() == "Crosshair") return dynamic_cast<ModuleCrosshair*>(mod.get());
    }
    return nullptr;
}

bool ClickGUI::IsHUDEnabled() const {
    auto* m = GetHUDModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsArmorHUDEnabled() const {
    auto* m = GetArmorHUDModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsNameTagsEnabled() const {
    auto* m = GetNameTagsModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsItemESPEnabled() const {
    auto* m = GetItemESPModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsViewModelEnabled() const {
    auto* m = GetViewModelModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsGuiWalkEnabled() const {
    auto* m = GetGuiWalkModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsAutoMineEnabled() const {
    auto* m = GetAutoMineModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsMCFEnabled() const {
    auto* m = GetMCFModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsAimBotEnabled() const {
    auto* m = GetAimBotModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsArrowsEnabled() const {
    auto* m = GetArrowsModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsAspectEnabled() const {
    auto* m = GetAspectModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsScreenProtectEnabled() const {
    auto* m = GetScreenProtectModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsWallHackPlayerEnabled() const {
    auto* m = GetWallHackPlayerModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsWallHackEnabled() const {
    auto* m = GetWallHackModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsBlockESPEnabled() const {
    auto* m = GetBlockESPModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsTracersEnabled() const {
    auto* m = GetTracersModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsTitlesEnabled() const {
    auto* m = GetTitlesModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsNameProtectEnabled() const {
    auto* m = GetNameProtectModule();
    return m ? m->IsEnabled() : false;
}

bool ClickGUI::IsCrosshairEnabled() const {
    auto* m = GetCrosshairModule();
    return m ? m->IsEnabled() : false;
}

ImU32 ClickGUI::GetPrimaryAccentColor() const {
    int idx = (m_accentIndex >= 0 && m_accentIndex < 4) ? m_accentIndex : 0;
    return s_accents[idx].primary;
}

ImU32 ClickGUI::GetSecondaryAccentColor() const {
    int idx = (m_accentIndex >= 0 && m_accentIndex < 4) ? m_accentIndex : 0;
    return s_accents[idx].secondary;
}

// ── 1. TopBar (Верхняя панель: Логотип, Статус, Горячие клавиши, Шестеренка) ─
void ClickGUI::DrawTopBar(float s, float winW, float winH, const ImVec2& winPos) {
    (void)winH;
    EnsureAvatarLoaded();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto& accent = s_accents[m_accentIndex];
    float topbarH = 76.0f * s;

    // Мягкое неоновое свечение (Soft Glow) по верхнему краю
    if (m_softGlow) {
        draw->AddRectFilledMultiColor(
            ImVec2(winPos.x + 30.0f * s, winPos.y),
            ImVec2(winPos.x + winW - 30.0f * s, winPos.y + 3.0f * s),
            accent.secondary, accent.primary, accent.primary, accent.secondary
        );
    }

    // Линия границы под топбаром
    draw->AddLine(
        ImVec2(winPos.x, winPos.y + topbarH),
        ImVec2(winPos.x + winW, winPos.y + topbarH),
        IM_COL32(36, 41, 56, 255),
        1.0f * s
    );

    // ── Брендинг (Brand Badge [M] или Пользовательская Аватарка + Название) ─────
    float badgeSz = 38.0f * s;
    float badgeX = winPos.x + 24.0f * s;
    float badgeY = winPos.y + (topbarH - badgeSz) * 0.5f;

    if (s_avatarTex != 0) {
        draw->AddImageRounded((ImTextureID)(intptr_t)s_avatarTex,
                              ImVec2(badgeX, badgeY),
                              ImVec2(badgeX + badgeSz, badgeY + badgeSz),
                              ImVec2(0, 0), ImVec2(1, 1),
                              IM_COL32_WHITE, 10.0f * s);
        draw->AddRect(ImVec2(badgeX, badgeY), ImVec2(badgeX + badgeSz, badgeY + badgeSz),
                      accent.primary, 10.0f * s, 0, 1.5f * s);
    } else {
        DrawStealthCatLogo(draw, ImVec2(badgeX, badgeY), badgeSz, accent.primary, accent.primary, s);
    }

    float brandTextX = badgeX + badgeSz + 14.0f * s;
    float brandTextY = badgeY + (badgeSz - ImGui::GetFontSize()) * 0.5f;
    draw->AddText(ImVec2(brandTextX, brandTextY), IM_COL32(255, 255, 255, 255), "MyrLegit");

    // ── Status Pill [● INJECTED] с красивым мягким неоновым мерцанием (Breathing) ─
    ImVec2 bSz = ImGui::CalcTextSize("MyrLegit");
    float pillX = brandTextX + bSz.x + 18.0f * s;
    float pillH = 26.0f * s;
    float pillW = 96.0f * s;
    float pillY = winPos.y + (topbarH - pillH) * 0.5f;

    // Мягкая гармоничная пульсация дыхания (breathing pulse)
    float timeSec = (float)ImGui::GetTime();
    float pulse = (sinf(timeSec * 3.2f) + 1.0f) * 0.5f; // 0.0 .. 1.0

    // Внешнее мягкое неоновое свечение (aura)
    int auraAlpha = (int)(25.0f + 60.0f * pulse);
    draw->AddRectFilled(ImVec2(pillX - 3.0f * s, pillY - 3.0f * s),
                        ImVec2(pillX + pillW + 3.0f * s, pillY + pillH + 3.0f * s),
                        IM_COL32(46, 213, 115, auraAlpha), 999.0f);

    // Фоновая заливка плашки с мягким переливом
    int bgAlpha = (int)(32.0f + 38.0f * pulse);
    draw->AddRectFilled(ImVec2(pillX, pillY), ImVec2(pillX + pillW, pillY + pillH),
                        IM_COL32(35, 135, 85, bgAlpha), 999.0f);

    // Аккуратная светящаяся рамка
    int borderAlpha = (int)(75.0f + 85.0f * pulse);
    draw->AddRect(ImVec2(pillX, pillY), ImVec2(pillX + pillW, pillY + pillH),
                  IM_COL32(75, 230, 145, borderAlpha), 999.0f, 0, 1.2f * s);

    // Пульсирующая неоновая точка
    ImVec2 dotPos(pillX + 12.0f * s, pillY + pillH * 0.5f);
    float dotAuraR = (3.5f + 1.5f * pulse) * s;
    draw->AddCircleFilled(dotPos, dotAuraR, IM_COL32(46, 213, 115, (int)(45.0f + 70.0f * pulse)));
    draw->AddCircleFilled(dotPos, 2.8f * s, IM_COL32(130, 255, 180, 255));

    // Текст "INJECTED" с мягким мерцанием яркости
    ImU32 textCol = IM_COL32((int)(170 + 85 * pulse), 255, (int)(205 + 50 * pulse), 255);
    draw->AddText(ImVec2(pillX + 22.0f * s, pillY + (pillH - ImGui::GetFontSize()) * 0.5f), textCol, "INJECTED");

    // ── Кнопка Настроек ClickGUI [ ⚙ ] ───────────────────────────────────────
    float gearSz = 34.0f * s;
    float gearX = winPos.x + winW - gearSz - 24.0f * s;
    float gearY = winPos.y + (topbarH - gearSz) * 0.5f;

    ImGui::SetCursorScreenPos(ImVec2(gearX, gearY));
    if (ImGui::InvisibleButton("##GearSettingsBtn", ImVec2(gearSz, gearSz))) {
        m_showSettings = !m_showSettings;
    }
    bool isGearHovered = ImGui::IsItemHovered();
    ImU32 gearBg = m_showSettings ? IM_COL32(48, 56, 78, 255) : (isGearHovered ? IM_COL32(38, 44, 62, 255) : IM_COL32(28, 33, 47, 255));
    draw->AddRectFilled(ImVec2(gearX, gearY), ImVec2(gearX + gearSz, gearY + gearSz), gearBg, 9.0f * s);
    draw->AddRect(ImVec2(gearX, gearY), ImVec2(gearX + gearSz, gearY + gearSz), m_showSettings ? accent.primary : IM_COL32(48, 55, 75, 255), 9.0f * s);

    // Векторная шестеренка
    ImVec2 gearCenter(gearX + gearSz * 0.5f, gearY + gearSz * 0.5f);
    ImU32 iconCol = m_showSettings ? IM_COL32(255, 255, 255, 255) : (isGearHovered ? IM_COL32(220, 228, 248, 255) : IM_COL32(140, 148, 170, 255));
    draw->AddCircle(gearCenter, 4.5f * s, iconCol, 16, 2.0f * s);
    for (int a = 0; a < 8; ++a) {
        float angle = a * (3.14159265f / 4.0f);
        float c = cosf(angle), si = sinf(angle);
        draw->AddLine(
            ImVec2(gearCenter.x + c * 5.0f * s, gearCenter.y + si * 5.0f * s),
            ImVec2(gearCenter.x + c * 8.5f * s, gearCenter.y + si * 8.5f * s),
            iconCol, 2.0f * s
        );
    }

    // ── Горячие клавиши (RSHIFT / END) ──────────────────────────────────────
    const char* hotkeysHint = "[RSHIFT] закрыть   [END] выгрузить";
    ImVec2 kSz = ImGui::CalcTextSize(hotkeysHint);
    float keysX = gearX - kSz.x - 20.0f * s;
    float keysY = winPos.y + (topbarH - kSz.y) * 0.5f;

    draw->AddText(ImVec2(keysX, keysY), IM_COL32(111, 119, 141, 255), hotkeysHint);
}

// ── Иконка категории (FontAwesome 6 с векторным фоллбэком) ───────────────────
static void DrawCategoryIcon(ImDrawList* draw, Category cat, const ImVec2& pos, ImU32 col, float s) {
    if (g_hasIconFont) {
        const char* iconStr = "";
        switch (cat) {
            case Category::COMBAT:   iconStr = "\xEF\x81\x9B"; break; // fa-crosshairs
            case Category::VISUAL:   iconStr = "\xEF\x81\xAE"; break; // fa-eye
            case Category::MOVEMENT: iconStr = "\xEF\x9C\x8C"; break; // fa-person-running
            case Category::MISC:     iconStr = "\xEF\x87\x9E"; break; // fa-sliders
            case Category::CONFIGS:  iconStr = "\xEF\x81\xBB"; break; // fa-folder
        }
        draw->AddText(pos, col, iconStr);
        return;
    }

    // Векторный рендер иконок
    float cx = pos.x + 8.0f * s;
    float cy = pos.y + 9.0f * s;
    switch (cat) {
        case Category::COMBAT: {
            draw->AddCircle(ImVec2(cx, cy), 5.5f * s, col, 16, 1.4f * s);
            draw->AddCircleFilled(ImVec2(cx, cy), 1.8f * s, col);
            draw->AddLine(ImVec2(cx - 7.5f * s, cy), ImVec2(cx - 3.5f * s, cy), col, 1.4f * s);
            draw->AddLine(ImVec2(cx + 3.5f * s, cy), ImVec2(cx + 7.5f * s, cy), col, 1.4f * s);
            draw->AddLine(ImVec2(cx, cy - 7.5f * s), ImVec2(cx, cy - 3.5f * s), col, 1.4f * s);
            draw->AddLine(ImVec2(cx, cy + 3.5f * s), ImVec2(cx, cy + 7.5f * s), col, 1.4f * s);
            break;
        }
        case Category::VISUAL: {
            ImVec2 pL(cx - 7.0f * s, cy);
            ImVec2 pR(cx + 7.0f * s, cy);
            ImVec2 cTop1(cx - 3.5f * s, cy - 5.0f * s), cTop2(cx + 3.5f * s, cy - 5.0f * s);
            ImVec2 cBot1(cx - 3.5f * s, cy + 5.0f * s), cBot2(cx + 3.5f * s, cy + 5.0f * s);
            draw->AddBezierCubic(pL, cTop1, cTop2, pR, col, 1.5f * s);
            draw->AddBezierCubic(pL, cBot1, cBot2, pR, col, 1.5f * s);
            draw->AddCircleFilled(ImVec2(cx, cy), 2.2f * s, col);
            break;
        }
        case Category::MOVEMENT: {
            ImVec2 ch1_1(cx - 6.0f * s, cy - 5.5f * s);
            ImVec2 ch1_2(cx - 1.0f * s, cy);
            ImVec2 ch1_3(cx - 6.0f * s, cy + 5.5f * s);
            draw->AddLine(ch1_1, ch1_2, col, 1.8f * s);
            draw->AddLine(ch1_2, ch1_3, col, 1.8f * s);

            ImVec2 ch2_1(cx + 0.5f * s, cy - 5.5f * s);
            ImVec2 ch2_2(cx + 5.5f * s, cy);
            ImVec2 ch2_3(cx + 0.5f * s, cy + 5.5f * s);
            draw->AddLine(ch2_1, ch2_2, col, 1.8f * s);
            draw->AddLine(ch2_2, ch2_3, col, 1.8f * s);
            break;
        }
        case Category::MISC: {
            float y1 = cy - 4.5f * s;
            float y2 = cy;
            float y3 = cy + 4.5f * s;
            float lx = cx - 6.5f * s;
            float rx = cx + 6.5f * s;
            draw->AddLine(ImVec2(lx, y1), ImVec2(rx, y1), col, 1.2f * s);
            draw->AddLine(ImVec2(lx, y2), ImVec2(rx, y2), col, 1.2f * s);
            draw->AddLine(ImVec2(lx, y3), ImVec2(rx, y3), col, 1.2f * s);
            draw->AddCircleFilled(ImVec2(cx - 2.0f * s, y1), 2.0f * s, col);
            draw->AddCircleFilled(ImVec2(cx + 2.5f * s, y2), 2.0f * s, col);
            draw->AddCircleFilled(ImVec2(cx - 1.0f * s, y3), 2.0f * s, col);
            break;
        }
        case Category::CONFIGS: {
            float fx1 = cx - 6.5f * s, fy1 = cy - 4.5f * s;
            float fx2 = cx + 6.5f * s, fy2 = cy + 5.0f * s;
            draw->AddRectFilled(ImVec2(fx1, fy1 - 2.0f * s), ImVec2(fx1 + 5.5f * s, fy1), col, 1.0f * s);
            draw->AddRect(ImVec2(fx1, fy1), ImVec2(fx2, fy2), col, 2.0f * s, 0, 1.3f * s);
            break;
        }
    }
}

// ── 2. Sidebar (Боковое меню категорий точь-в-точь по Figma) ──────────────────
void ClickGUI::DrawSidebar(float s, float winW, float winH, const ImVec2& winPos) {
    (void)winW;
    EnsureAvatarLoaded();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto& accent = s_accents[m_accentIndex];

    float topbarH = 76.0f * s;
    float footerH = 32.0f * s;
    float sidebarW = (m_compactMode ? 200.0f : 230.0f) * s;

    // Фон боковой панели
    draw->AddRectFilled(
        ImVec2(winPos.x, winPos.y + topbarH),
        ImVec2(winPos.x + sidebarW, winPos.y + winH - footerH),
        IM_COL32(11, 14, 23, 240)
    );

    // Правая разделительная линия
    draw->AddLine(
        ImVec2(winPos.x + sidebarW, winPos.y + topbarH),
        ImVec2(winPos.x + sidebarW, winPos.y + winH - footerH),
        IM_COL32(35, 39, 54, 255),
        1.0f * s
    );

    // ── Кнопки Категорий ────────────────────────────────────────────────────
    const struct { const char* label; Category cat; } navCats[] = {
        { "Combat",   Category::COMBAT },
        { "Visuals",  Category::VISUAL },
        { "Movement", Category::MOVEMENT },
        { "Misc",     Category::MISC },
        { "Configs",  Category::CONFIGS }
    };

    float startY = winPos.y + topbarH + 18.0f * s;
    float btnH = (m_compactMode ? 40.0f : 46.0f) * s;
    float btnW = sidebarW - 24.0f * s;

    // Плавное физическое скольжение активной плашки между разделами
    int activeCatIdx = 0;
    for (int k = 0; k < 5; ++k) {
        if (m_activeCat == navCats[k].cat) { activeCatIdx = k; break; }
    }

    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    static float s_animCatY = -1.0f;
    float targetCatY = startY + (float)activeCatIdx * (btnH + 6.0f * s);
    if (s_animCatY < 0.0f) s_animCatY = targetCatY;
    s_animCatY += (targetCatY - s_animCatY) * (1.0f - std::exp(-12.0f * dt));

    // Мягкий фон скользящего индикатора активной категории со скруглением
    draw->AddRectFilled(
        ImVec2(winPos.x + 12.0f * s, s_animCatY),
        ImVec2(winPos.x + 12.0f * s + btnW, s_animCatY + btnH),
        IM_COL32(28, 36, 56, 230), 10.0f * s
    );
    draw->AddRect(
        ImVec2(winPos.x + 12.0f * s, s_animCatY),
        ImVec2(winPos.x + 12.0f * s + btnW, s_animCatY + btnH),
        IM_COL32(48, 56, 82, 140), 10.0f * s, 0, 1.0f * s
    );
    // Левая акцентная неоновая полоска со скруглением, плавно скользящая вместе с плашкой
    draw->AddRectFilled(
        ImVec2(winPos.x + 15.0f * s, s_animCatY + 10.0f * s),
        ImVec2(winPos.x + 18.0f * s, s_animCatY + btnH - 10.0f * s),
        accent.primary, 2.0f * s
    );

    for (int i = 0; i < 5; ++i) {
        float curY = startY + (float)i * (btnH + 6.0f * s);
        bool isActive = (m_activeCat == navCats[i].cat);

        ImGui::SetCursorScreenPos(ImVec2(winPos.x + 12.0f * s, curY));
        ImGui::PushID(navCats[i].label);

        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(23, 27, 41, 150));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(30, 36, 54, 200));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * s);

        if (ImGui::Button("##CatBtn", ImVec2(btnW, btnH))) {
            m_activeCat = navCats[i].cat;
            // Переключаем выбранный модуль на первый в этой категории
            m_selectedModule = nullptr;
            for (auto& mod : m_modules) {
                if (mod->GetCategory() == m_activeCat) {
                    m_selectedModule = mod.get();
                    break;
                }
            }
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);

        // Иконка и Текст категории
        ImU32 textCol = isActive ? IM_COL32(255, 255, 255, 255) : IM_COL32(126, 134, 155, 255);
        ImU32 iconCol = isActive ? accent.primary : IM_COL32(85, 93, 116, 255);

        float textY = curY + (btnH - ImGui::GetFontSize()) * 0.5f;
        DrawCategoryIcon(draw, navCats[i].cat, ImVec2(winPos.x + 22.0f * s, textY), iconCol, s);
        draw->AddText(ImVec2(winPos.x + 48.0f * s, textY), textCol, navCats[i].label);

        // Бейдж включенных модулей справа
        if (navCats[i].cat != Category::CONFIGS) {
            int enabledInCat = 0;
            for (const auto& mod : m_modules) {
                if (mod->GetCategory() == navCats[i].cat && mod->IsEnabled()) enabledInCat++;
            }
            char cntBuf[16];
            snprintf(cntBuf, sizeof(cntBuf), "%d", enabledInCat);
            ImU32 badgeCol = isActive ? accent.primary : IM_COL32(85, 93, 116, 255);
            draw->AddText(ImVec2(winPos.x + sidebarW - 32.0f * s, textY), badgeCol, cntBuf);
        }

        ImGui::PopID();
    }

    // ── Карточка Профиля внизу сайдбара ─────────────────────────────────────
    float profH = 64.0f * s;
    float profW = sidebarW - 24.0f * s;
    ImVec2 profPos(winPos.x + 12.0f * s, winPos.y + winH - footerH - profH - 12.0f * s);

    draw->AddRectFilled(profPos, ImVec2(profPos.x + profW, profPos.y + profH), IM_COL32(19, 23, 34, 255), 12.0f * s);
    draw->AddRect(profPos, ImVec2(profPos.x + profW, profPos.y + profH), IM_COL32(34, 39, 55, 255), 12.0f * s);

    // Аватар
    float avSz = 36.0f * s;
    ImVec2 avPos(profPos.x + 10.0f * s, profPos.y + (profH - avSz) * 0.5f);
    if (s_avatarTex != 0) {
        draw->AddImageRounded((ImTextureID)(intptr_t)s_avatarTex,
                              avPos, ImVec2(avPos.x + avSz, avPos.y + avSz),
                              ImVec2(0, 0), ImVec2(1, 1),
                              IM_COL32_WHITE, 9.0f * s);
        draw->AddRect(avPos, ImVec2(avPos.x + avSz, avPos.y + avSz),
                      accent.primary, 9.0f * s, 0, 1.0f * s);
    } else {
        draw->AddRectFilled(avPos, ImVec2(avPos.x + avSz, avPos.y + avSz), IM_COL32(37, 43, 63, 255), 9.0f * s);
        draw->AddText(ImVec2(avPos.x + 12.0f * s, avPos.y + 8.0f * s), accent.primary, "M");
    }

    std::string uName = JVMBridge::Get().Data().username;
    if (uName.empty() || uName == "Unknown") uName = "nightuser";

    // Убрали Lifetime, имя аккуратно выровнено по вертикали
    float nameY = profPos.y + (profH - ImGui::GetFontSize()) * 0.5f;
    draw->AddText(ImVec2(profPos.x + 54.0f * s, nameY), IM_COL32(235, 240, 252, 255), uName.c_str());

    // Онлайн точка
    draw->AddCircleFilled(ImVec2(profPos.x + profW - 14.0f * s, profPos.y + profH * 0.5f), 3.0f * s, IM_COL32(83, 215, 153, 255));
}

// ── 3. Content (Сплит-окно: Список модулей + Карточка настроек) ────────────────
void ClickGUI::DrawContent(float s, float winW, float winH, const ImVec2& winPos) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto& accent = s_accents[m_accentIndex];

    float topbarH = 76.0f * s;
    float footerH = 32.0f * s;
    float sidebarW = (m_compactMode ? 200.0f : 230.0f) * s;
    float contentX = winPos.x + sidebarW + 24.0f * s;
    float contentY = winPos.y + topbarH + 20.0f * s;
    float contentW = winW - sidebarW - 48.0f * s;
    float contentH = winH - topbarH - footerH - 40.0f * s;

    // ── Заголовок контента (Eyebrow + Category Title + Поиск) ─────────────────
    const char* catName = (m_activeCat == Category::COMBAT)   ? "Combat" :
                          (m_activeCat == Category::VISUAL)   ? "Visuals" :
                          (m_activeCat == Category::MOVEMENT) ? "Movement" :
                          (m_activeCat == Category::MISC)     ? "Misc" : "Configs";

    char eyebrowBuf[64];
    snprintf(eyebrowBuf, sizeof(eyebrowBuf), "MODULES / %s", (m_activeCat == Category::CONFIGS) ? "CONFIGS" : catName);
    draw->AddText(ImVec2(contentX, contentY), IM_COL32(85, 95, 118, 255), eyebrowBuf);
    draw->AddText(ImVec2(contentX, contentY + 18.0f * s), IM_COL32(255, 255, 255, 255), (m_activeCat == Category::CONFIGS) ? "Конфигурации" : catName);

    // Поле поиска (Search Box)
    if (m_activeCat != Category::CONFIGS) {
        float searchW = 220.0f * s;
        float searchH = 34.0f * s;
        float searchX = contentX + contentW - searchW;
        float searchY = contentY + 4.0f * s;

        ImGui::SetCursorScreenPos(ImVec2(searchX, searchY));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(18, 22, 33, 255));
        ImGui::PushStyleColor(ImGuiCol_Border,  IM_COL32(40, 45, 61, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f * s);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f * s);
        ImGui::SetNextItemWidth(searchW);

        ImGui::InputTextWithHint("##SearchModule", "Поиск модуля...", m_searchQuery, sizeof(m_searchQuery));

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    float mainAreaY = contentY + 54.0f * s;
    float mainAreaH = contentH - 54.0f * s;

    // ── Вкладка Конфигураций (Figma Config Cards) ─────────────────────────────
    if (m_activeCat == Category::CONFIGS) {
        float cardGap = 16.0f * s;
        float leftCardW = (contentW - cardGap) * 0.42f;
        float rightCardW = (contentW - cardGap) * 0.58f;

        // Левая карточка: Новый конфиг
        ImGui::SetCursorScreenPos(ImVec2(contentX, mainAreaY));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(18, 22, 32, 255));
        ImGui::PushStyleColor(ImGuiCol_Border,  IM_COL32(41, 46, 63, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 13.0f * s);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f * s);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * s, 18.0f * s));

        ImGui::BeginChild("##NewConfigCard", ImVec2(leftCardW, mainAreaH), true);
        ImGui::TextColored(ImVec4(0.84f, 0.86f, 0.92f, 1.0f), "Новый конфиг");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        static char s_newCfgName[64] = "default";
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.72f, 1.0f), "Название:");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##cfgNameInp", s_newCfgName, sizeof(s_newCfgName));

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, accent.primary);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent.secondary);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 200));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * s);

        if (ImGui::Button("Сохранить конфигурацию", ImVec2(ImGui::GetContentRegionAvail().x, 38.0f * s))) {
            if (strlen(s_newCfgName) > 0) {
                ConfigManager::Get().SaveConfig(s_newCfgName, this);
            }
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(32, 42, 65, 255));
        if (ImGui::Button("Открыть папку конфигов", ImVec2(ImGui::GetContentRegionAvail().x, 34.0f * s))) {
            char appdata[MAX_PATH];
            GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
            std::string dir = std::string(appdata) + "\\.minecraft\\myr_legit\\configs";
            CreateDirectoryA(dir.c_str(), NULL);
            ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();

        std::string statusMsg = ConfigManager::Get().GetLastStatus();
        if (!statusMsg.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.6f, 1.0f), "%s", statusMsg.c_str());
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);

        // Правая карточка: Список сохраненных конфигов
        float rightCardX = contentX + leftCardW + cardGap;
        ImGui::SetCursorScreenPos(ImVec2(rightCardX, mainAreaY));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(18, 22, 32, 255));
        ImGui::PushStyleColor(ImGuiCol_Border,  IM_COL32(41, 46, 63, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 13.0f * s);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f * s);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * s, 18.0f * s));

        ImGui::BeginChild("##SavedConfigsCard", ImVec2(rightCardW, mainAreaH), true);
        ImGui::TextColored(ImVec4(0.84f, 0.86f, 0.92f, 1.0f), "Сохранённые конфиги");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        std::vector<std::string> cfgList = ConfigManager::Get().GetConfigList();
        if (cfgList.empty()) {
            ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.62f, 1.0f), "Нет сохраненных конфигураций.");
        } else {
            for (const auto& cName : cfgList) {
                ImGui::PushID(cName.c_str());
                float itemW = ImGui::GetContentRegionAvail().x;
                float delBtnW = 28.0f * s;
                float saveBtnW = 82.0f * s;
                float loadBtnW = 82.0f * s;
                float spacing = 6.0f * s;

                ImGui::BeginGroup();
                ImGui::TextColored(ImVec4(0.9f, 0.92f, 0.98f, 1.0f), "%s", cName.c_str());
                ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.62f, 1.0f), ".cfg файл");
                ImGui::EndGroup();

                ImGui::SameLine(itemW - delBtnW - saveBtnW - loadBtnW - spacing * 2);
                if (ImGui::Button("Загрузить", ImVec2(loadBtnW, 30.0f * s))) {
                    ConfigManager::Get().LoadConfig(cName, this);
                    strncpy_s(s_newCfgName, cName.c_str(), sizeof(s_newCfgName) - 1);
                }

                ImGui::SameLine(0, spacing);
                ImGui::PushStyleColor(ImGuiCol_Button, accent.primary);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent.secondary);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 200));
                if (ImGui::Button("Сохранить", ImVec2(saveBtnW, 30.0f * s))) {
                    ConfigManager::Get().SaveConfig(cName, this);
                    strncpy_s(s_newCfgName, cName.c_str(), sizeof(s_newCfgName) - 1);
                }
                ImGui::PopStyleColor(3);

                ImGui::SameLine(0, spacing);
                if (ImGui::Button("X", ImVec2(delBtnW, 30.0f * s))) {
                    ConfigManager::Get().DeleteConfig(cName);
                }

                ImGui::Separator();
                ImGui::PopID();
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        return;
    }

    // ── Сплит-лейаут для Модулей: Левая колонка (Список) + Правая колонка (Настройки) ─
    float colGap = 16.0f * s;
    float leftColW = (contentW - colGap) * 0.49f;
    float rightColW = contentW - colGap - leftColW;

    // ── 1. Левая колонка: Список модулей (Широкая, без обрезки текста) ─────────
    ImGui::SetCursorScreenPos(ImVec2(contentX, mainAreaY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f * s);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(45, 52, 72, 140));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(70, 80, 110, 200));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, accent.primary);

    ImGui::BeginChild("##ModuleRowsList", ImVec2(leftColW, mainAreaH), false);
    ImDrawList* listDraw = ImGui::GetWindowDrawList();

    std::string qLower = m_searchQuery;
    for (char& c : qLower) c = static_cast<char>(tolower(c));

    int shownCount = 0;
    for (auto& mod : m_modules) {
        if (mod->GetCategory() != m_activeCat) continue;

        std::string modLower = mod->Name();
        for (char& c : modLower) c = static_cast<char>(tolower(c));
        if (!qLower.empty() && modLower.find(qLower) == std::string::npos) continue;

        shownCount++;
        bool isSelected = (m_selectedModule == mod.get());
        bool isEnabled = mod->IsEnabled();
        float rowH = (m_compactMode ? 56.0f : 66.0f) * s;
        ImVec2 curPos = ImGui::GetCursorScreenPos();

        ImGui::PushID(mod->Name().c_str());

        // Оставляем комфортный отступ 32px справа, чтобы полоса прокрутки никогда не накладывалась на карточки
        float cardW = leftColW - 32.0f * s;

        // Фон карточки модуля (Hover и Selected)
        ImU32 cardBg = isSelected ? IM_COL32(23, 26, 39, 255) : IM_COL32(19, 23, 34, 255);
        ImU32 cardBorder = isSelected ? accent.primary : IM_COL32(36, 41, 56, 180);

        listDraw->AddRectFilled(curPos, ImVec2(curPos.x + cardW, curPos.y + rowH), cardBg, 12.0f * s);
        listDraw->AddRect(curPos, ImVec2(curPos.x + cardW, curPos.y + rowH), cardBorder, 12.0f * s, 0, 1.0f * s);

        // Индикатор статуса (28x28 квадрат с точкой)
        float dotBoxSz = 28.0f * s;
        ImVec2 dotBoxPos(curPos.x + 10.0f * s, curPos.y + (rowH - dotBoxSz) * 0.5f);
        listDraw->AddRectFilled(dotBoxPos, ImVec2(dotBoxPos.x + dotBoxSz, dotBoxPos.y + dotBoxSz), IM_COL32(29, 34, 48, 255), 8.0f * s);
        listDraw->AddCircleFilled(ImVec2(dotBoxPos.x + dotBoxSz * 0.5f, dotBoxPos.y + dotBoxSz * 0.5f), 3.0f * s,
                                  isEnabled ? accent.primary : IM_COL32(80, 87, 106, 255));

        // Элементы управления справа: Toggle и Bind (всегда строго внутри карточки!)
        float toggleW = 36.0f * s;
        float toggleH = 20.0f * s;
        float toggleX = curPos.x + cardW - toggleW - 10.0f * s;
        float toggleY = curPos.y + (rowH - toggleH) * 0.5f;

        float bindBtnW = 46.0f * s;
        float bindBtnH = 20.0f * s;
        float bindX = toggleX - bindBtnW - 8.0f * s;
        float bindY = curPos.y + (rowH - bindBtnH) * 0.5f;

        // Название и краткое описание (С защитой от перекрытия кнопок)
        float textStartX = curPos.x + 46.0f * s;
        float textMaxX = bindX - 6.0f * s;

        listDraw->PushClipRect(ImVec2(curPos.x, curPos.y), ImVec2(textMaxX, curPos.y + rowH), true);
        listDraw->AddText(ImVec2(textStartX, curPos.y + 11.0f * s), IM_COL32(255, 255, 255, 255), mod->Name().c_str());
        listDraw->AddText(ImVec2(textStartX, curPos.y + 32.0f * s), IM_COL32(101, 109, 128, 255), GetModuleDescription(mod->Name()));
        listDraw->PopClipRect();

        // Невидимая кнопка выбора карточки
        float clickableW = bindX - curPos.x - 4.0f * s;
        ImGui::SetCursorScreenPos(curPos);
        if (ImGui::InvisibleButton("##SelectModuleRow", ImVec2(clickableW, rowH))) {
            m_selectedModule = mod.get();
        }

        // Кнопка бинда клавиши
        ImGui::SetCursorScreenPos(ImVec2(bindX, bindY));
        char bindText[32];
        if (m_bindingModule == mod.get()) {
            snprintf(bindText, sizeof(bindText), "[...]");
        } else {
            snprintf(bindText, sizeof(bindText), "[%s]", mod->GetKeyName().c_str());
        }

        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(31, 37, 51, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(45, 52, 72, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  accent.primary);
        ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(120, 130, 155, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * s);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

        if (ImGui::Button(bindText, ImVec2(bindBtnW, bindBtnH))) {
            m_bindingModule = (m_bindingModule == mod.get()) ? nullptr : mod.get();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);

        // Кастомный Toggle Switch с уникальным ID для безошибочного клика
        ImGui::SetCursorScreenPos(ImVec2(toggleX, toggleY));
        std::string rowTglId = std::string("##RowTgl_") + mod->Name();
        if (CustomToggle(rowTglId.c_str(), &isEnabled, accent.primaryVec, s)) {
            mod->Toggle();
            NotificationManager::Get().Add(mod->Name(), mod->IsEnabled());
        }

        // Перемещаем курсор к следующей строке
        ImGui::SetCursorScreenPos(ImVec2(curPos.x, curPos.y + rowH));
        ImGui::Dummy(ImVec2(0, 0));

        ImGui::PopID();
    }

    if (shownCount == 0) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.40f, 0.45f, 0.58f, 1.0f), "Модули не найдены...");
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    // ── 2. Правая колонка: Карточка настроек выбранного модуля ───────────────
    float rightColX = contentX + leftColW + colGap;
    ImGui::SetCursorScreenPos(ImVec2(rightColX, mainAreaY));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(18, 22, 32, 255));
    ImGui::PushStyleColor(ImGuiCol_Border,  IM_COL32(41, 46, 63, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 13.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * s, 20.0f * s));

    ImGui::BeginChild("##ModuleSettingsCard", ImVec2(rightColW, mainAreaH), true);

    if (m_selectedModule) {
        DrawModuleSettings(m_selectedModule, s);
    } else {
        ImGui::TextColored(ImVec4(0.40f, 0.45f, 0.58f, 1.0f), "Выберите модуль слева для настройки...");
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

// ── Отрисовка элементов управления выбранного модуля ──────────────────────────
void ClickGUI::DrawModuleSettings(ModuleBase* mod, float s) {
    const auto& accent = s_accents[m_accentIndex];
    bool en = mod->IsEnabled();

    // Заголовок настроек модуля
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", mod->Name().c_str());
    ImGui::SameLine(0, 10.0f * s);
    if (en) {
        ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.66f, 1.0f), "[ACTIVE]");
    } else {
        ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 0.8f), "[DISABLED]");
    }
    ImGui::TextColored(ImVec4(0.42f, 0.47f, 0.58f, 1.0f), "%s", GetModuleDescription(mod->Name()));
    ImGui::EndGroup();

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 35.0f * s);
    std::string cardHeadTglId = std::string("##CardHeadTgl_") + mod->Name();
    if (CustomToggle(cardHeadTglId.c_str(), &en, accent.primaryVec, s)) {
        mod->Toggle();
        NotificationManager::Get().Add(mod->Name(), mod->IsEnabled());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── 1. AimBot (NoDesynce / BloodAim) ─────────────────────────────────────
    if (mod->Name() == "AimBot") {
        auto* aim = dynamic_cast<ModuleAimBot*>(mod);
        if (aim) {
            DrawModernSliderFloat("Угол обзора (FOV)", "##aimFov", &aim->fov, 1.0f, 360.0f, "%.1f°", accent.primary, s);
            DrawModernSliderFloat("Дистанция (Range)", "##aimRange", &aim->range, 1.0f, 500.0f, "%.0f m", accent.primary, s);
            DrawModernSliderFloat("Предикт движения (Predict)", "##aimPredict", &aim->predict, 0.0f, 10.0f, "%.1f", accent.primary, s);
            DrawModernSliderFloat("V-Predict (Вертикальный)", "##aimVPredict", &aim->vPredict, 0.0f, 2.0f, "%.1f", accent.primary, s);
            DrawModernSliderFloat("Плавность (Smoothness)", "##aimSmooth", &aim->smoothness, 1.0f, 7.0f, "%.1f", accent.primary, s);
            ImGui::Spacing();
            CustomCheckbox("Отображать область FOV", "Отображает радиус наведения прицела", &aim->drawFov, accent.primaryVec, s);

            if (aim->drawFov) {
                ImGui::PushID("AimFovColorSection");
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.85f, 0.90f, 1.0f, 1.0f), "Цвет круга FOV:");
                ImGui::SameLine();
                ImGui::ColorEdit4("##aimFovCol", aim->fovColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::SameLine();
                if (ImGui::Button("Сброс##fovDef")) {
                    aim->fovColor[0] = 0.49f; aim->fovColor[1] = 0.78f; aim->fovColor[2] = 1.0f; aim->fovColor[3] = 0.70f;
                }
                ImGui::PopID();
            }

            CustomCheckbox("Наведение в тело (Body Aim)", "Наводить на тело вместо головы", &aim->bodyAim, accent.primaryVec, s);
            CustomCheckbox("Сквозь стены (Through Walls)", "Игнорировать видимость цели", &aim->walls, accent.primaryVec, s);
            CustomCheckbox("Авто-выстрел (AutoShoot)", "Автоматическая стрельба при наведении", &aim->autoShoot, accent.primaryVec, s);
            CustomCheckbox("Игнорировать ботов", "Не ассистить по ботам и NPC", &aim->bots, accent.primaryVec, s);
        }
    }
    // ── 2. HUD ───────────────────────────────────────────────────────────────
    else if (mod->Name() == "HUD") {
        auto* hudMod = dynamic_cast<ModuleHUD*>(mod);
        if (hudMod) {
            CustomCheckbox("Показывать Ник (User)", "Отображать ваш игровой никнейм", &hudMod->showUser, accent.primaryVec, s);
            CustomCheckbox("Показывать FPS", "Счетчик кадров в секунду", &hudMod->showFPS, accent.primaryVec, s);
            CustomCheckbox("Показывать Ping", "Задержка до игрового сервера", &hudMod->showPing, accent.primaryVec, s);
        }
    }
    // ── 3. ArmorHUD ──────────────────────────────────────────────────────────
    else if (mod->Name() == "ArmorHUD") {
        auto* ahMod = dynamic_cast<ModuleArmorHUD*>(mod);
        if (ahMod) {
            DrawModernSliderFloat("Масштаб экипировки", "##ahScale", &ahMod->scale, 0.5f, 2.0f, "%.2fx", accent.primary, s);
            DrawModernSliderFloat("Высота панели (Y)", "##ahPosY", &ahMod->armorPosY, 10.0f, 300.0f, "%.0f px", accent.primary, s);
        }
    }
    // ── 4. NameTags ──────────────────────────────────────────────────────────
    else if (mod->Name() == "NameTags") {
        auto* nt = dynamic_cast<ModuleNameTags*>(mod);
        if (nt) {
            DrawModernSliderFloat("Дистанция отображения", "##ntDist", &nt->renderDistance, 40.0f, 500.0f, "%.0f m", accent.primary, s);
            DrawModernSliderFloat("Высота над игроком", "##ntHeight", &nt->yOffset, 0.0f, 6.0f, "%.2f m", accent.primary, s);
            DrawModernSliderFloat("Размер текста (Шрифт)", "##ntFontScale", &nt->fontScale, 0.5f, 3.0f, "%.2fx", accent.primary, s);
            DrawModernSliderFloat("Прозрачность плашки", "##ntOpacity", &nt->bgOpacity, 0.1f, 1.0f, "%.2f", accent.primary, s);
            ImGui::Spacing();
            CustomCheckbox("Отображать броню над ником", "Иконки и прочность экипировки цели", &nt->showArmor, accent.primaryVec, s);
            if (nt->showArmor) {
                ImGui::Indent(14.0f * s);
                DrawModernSliderFloat("Размер брони", "##ntArmorScale", &nt->armorScale, 0.5f, 2.5f, "%.1fx", accent.primary, s);
                ImGui::Unindent(14.0f * s);
            }
    
            CustomCheckbox("Дистанция в метрах", "Расстояние до игрока в метрах", &nt->showDistance, accent.primaryVec, s);
            CustomCheckbox("RustEx Bypass (Реальные ники)", "Деобфускация никнеймов со стоек брони", &nt->rustexBypass, accent.primaryVec, s);
        }
    }
    // ── 5. ItemESP ───────────────────────────────────────────────────────────
    else if (mod->Name() == "ItemESP") {
        auto* esp = dynamic_cast<ModuleItemESP*>(mod);
        if (esp) {
            DrawModernSliderFloat("Радиус поиска (Range)", "##espRange", &esp->range, 20.0f, 300.0f, "%.0f m", accent.primary, s);
            CustomCheckbox("Рамка вокруг предмета (Box)", "2D бокс вокруг выпавшей вещи", &esp->drawBox, accent.primaryVec, s);
            CustomCheckbox("Вещи на земле (Ground)", "Показывать спавн лута на земле", &esp->showGroundItems, accent.primaryVec, s);
            CustomCheckbox("Иконка предмета (Icon)", "Текстура или иконка ресурс-пака", &esp->drawIcon, accent.primaryVec, s);
            if (esp->drawIcon) {
                DrawModernSliderFloat("Размер иконки", "##espIconSize", &esp->iconSize, 8.0f, 64.0f, "%.0f px", accent.primary, s);
            }
            CustomCheckbox("Только иконка", "Скрыть текст и оставить только значок", &esp->drawIconOnly, accent.primaryVec, s);
            if (!esp->drawIconOnly) {
                CustomCheckbox("Название предмета", "Текстовое имя вещи", &esp->drawName, accent.primaryVec, s);
                CustomCheckbox("Дистанция до предмета", "Расстояние в метрах", &esp->drawDistance, accent.primaryVec, s);
            }
            DrawModernSliderFloat("Размер шрифта дистанции/имени", "##espTextScale", &esp->labelScaleMul, 0.35f, 2.8f, "%.2fx", accent.primary, s);
            DrawModernSliderFloat("Увеличение шрифта вдалеке", "##espBoost", &esp->distantFontBoost, 0.0f, 2.0f, "%.2fx", accent.primary, s);
        }
    }
    // ── 6. Arrows ────────────────────────────────────────────────────────────
    else if (mod->Name() == "Arrows") {
        auto* arr = dynamic_cast<ModuleArrows*>(mod);
        if (arr) {
            DrawModernSliderFloat("Радиус стрелок", "##arrRad", &arr->arrowRadius, 20.0f, 250.0f, "%.0f px", accent.primary, s);
            DrawModernSliderFloat("Радиус текста", "##arrTextRad", &arr->textRadius, 20.0f, 280.0f, "%.0f px", accent.primary, s);
            CustomCheckbox("Показывать дистанцию", "Метры на стрелке", &arr->showDistance, accent.primaryVec, s);
            CustomCheckbox("Только вне экрана", "Скрывать стрелку если игрок виден перед прицелом", &arr->onlyOffscreen, accent.primaryVec, s);
            CustomCheckbox("Игнорировать ботов", "Не указывать на NPC и ботов", &arr->ignoreBot, accent.primaryVec, s);
        }
    }
    // ── 7. AspectRatio ───────────────────────────────────────────────────────
    else if (mod->Name() == "AspectRatio") {
        auto* asp = dynamic_cast<ModuleAspect*>(mod);
        if (asp) {
            DrawModernSliderFloat("Ширина экрана (Aspect)", "##aspVal", &asp->aspectRatio, 0.5f, 3.0f, "%.2f", accent.primary, s);
            if (ImGui::Button("Сбросить по умолчанию (1.0)")) {
                asp->aspectRatio = 1.0f;
            }
        }
    }
    // ── 8. ViewModel ─────────────────────────────────────────────────────────
    else if (mod->Name() == "ViewModel") {
        auto* vm = dynamic_cast<ModuleViewModel*>(mod);
        if (vm) {
            DrawModernSliderFloat("Смещение X", "##vmX", &vm->leftX, -2.0f, 2.0f, "%.2f", accent.primary, s);
            DrawModernSliderFloat("Смещение Y", "##vmY", &vm->leftY, -2.0f, 2.0f, "%.2f", accent.primary, s);
            DrawModernSliderFloat("Смещение Z", "##vmZ", &vm->leftZ, -2.0f, 2.0f, "%.2f", accent.primary, s);
            DrawModernSliderFloat("Масштаб рук (Scale)", "##vmScale", &vm->leftScale, 0.1f, 3.0f, "%.2fx", accent.primary, s);
        }
    }
    // ── 9. AutoMine ──────────────────────────────────────────────────────────
    else if (mod->Name() == "AutoMine") {
        auto* am = dynamic_cast<ModuleAutoMine*>(mod);
        if (am) {
            DrawModernSliderFloat("Дистанция ломания", "##amDist", &am->distance, 1.0f, 10.0f, "%.1f m", accent.primary, s);
            if (am->hasTarget) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.f), "Цель: [%.0f, %.0f, %.0f]",
                                   am->targetBlockPos.x, am->targetBlockPos.y, am->targetBlockPos.z);
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.65f, 0.75f, 1.f), "Поиск руды рядом со стойкой брони...");
            }
        }
    }
    // ── 10. GuiWalk ──────────────────────────────────────────────────────────
    else if (mod->Name() == "GuiWalk") {
        ImGui::TextColored(ImVec4(0.6f, 0.85f, 0.6f, 1.f), "Ходьба активна во всех меню");
        ImGui::TextWrapped("W, A, S, D и Пробел работают при открытом сундуке, инвентаре, чате или меню чита.");
    }
    // ── 11. MCF (Middle Click Friend) ────────────────────────────────────────
    else if (mod->Name() == "MCF") {
        auto* mcf = dynamic_cast<ModuleMCF*>(mod);
        if (mcf) {
            CustomCheckbox("Быстрое добавление в друзья", "Клик на колесико мыши по игроку", &mcf->onFriend, accent.primaryVec, s);
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.f, 1.f), "Список друзей (%zu):", FriendManager::Get().GetFriends().size());
            for (const auto& fr : FriendManager::Get().GetFriends()) {
                ImGui::BulletText("%s", fr.c_str());
                ImGui::SameLine();
                ImGui::PushID(fr.c_str());
                if (ImGui::SmallButton("Удалить")) {
                    FriendManager::Get().RemoveFriend(fr);
                }
                ImGui::PopID();
            }
        }
    }
    // ── 12. WallHackPlayer ───────────────────────────────────────────────────
    else if (mod->Name() == "WallHackPlayer") {
        auto* whp = dynamic_cast<ModuleWallHackPlayer*>(mod);
        if (whp) {
            CustomCheckbox("3D Box вокруг игрока", "Объемный каркасный бокс", &whp->box3D, accent.primaryVec, s);
            CustomCheckbox("Заливка силуэта", "Полупрозрачный Chams", &whp->filled, accent.primaryVec, s);
            DrawModernSliderFloat("Прозрачность", "##whpOp", &whp->boxOpacity, 0.05f, 1.0f, "%.2f", accent.primary, s);
            DrawModernSliderFloat("Толщина линий", "##whpLW", &whp->lineWidth, 1.0f, 4.0f, "%.1f px", accent.primary, s);
        }
    }
    // ── 13. WallHack (Мобы и животные) ───────────────────────────────────────
    else if (mod->Name() == "WallHack") {
        auto* wh = dynamic_cast<ModuleWallHack*>(mod);
        if (wh) {
            CustomCheckbox("Подсветка игроков", "Отображать игроков сквозь стены", &wh->playerMode, accent.primaryVec, s);
            CustomCheckbox("Враждебные мобы (Mobs)", "Зомби, скелеты, пауки", &wh->mobMode, accent.primaryVec, s);
            CustomCheckbox("Мирные животные (Animals)", "Коровы, овцы, свиньи", &wh->animalMode, accent.primaryVec, s);
            CustomCheckbox("3D Box", "Объемный каркас", &wh->box3D, accent.primaryVec, s);
            CustomCheckbox("Chams (Заливка силуэта)", "Заливка модели сущности", &wh->chams, accent.primaryVec, s);
            DrawModernSliderFloat("Прозрачность", "##whOp", &wh->opacity, 0.05f, 1.0f, "%.2f", accent.primary, s);
            DrawModernSliderFloat("Толщина линий", "##whLW", &wh->lineWidth, 1.0f, 4.0f, "%.1f px", accent.primary, s);
        }
    }
    // ── 14. BlockESP ─────────────────────────────────────────────────────────
    else if (mod->Name() == "BlockESP") {
        auto* be = dynamic_cast<ModuleBlockESP*>(mod);
        if (be) {
            DrawModernSliderFloat("Радиус (Range)", "##beRange", &be->range, 16.0f, 700.0f, "%.0f m", accent.primary, s);
            CustomCheckbox("Руды (Ores)", "Алмазы, Золото, Железо, Изумруды, Сера", &be->ores, accent.primaryVec, s);
            CustomCheckbox("Еда и Растения (Food)", "Саженцы, Пионы, Урожай", &be->food, accent.primaryVec, s);
            CustomCheckbox("Трейсеры (Traicer) к рудам", "Линии от прицела к найденным рудам", &be->tracers, accent.primaryVec, s);
        }
    }
    // ── 15. Tracers ──────────────────────────────────────────────────────────
    else if (mod->Name() == "Tracers") {
        ImGui::TextColored(ImVec4(0.9f, 0.95f, 1.f, 1.f), "Линии трассеров от прицела к игрокам:");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.20f, 0.20f, 1.f), "• Враги: Темно-красный цвет");
        ImGui::TextColored(ImVec4(0.25f, 0.90f, 0.40f, 1.f), "• Друзья (FriendManager): Зеленый цвет");
    }
    // ── 16. Titles ───────────────────────────────────────────────────────────
    else if (mod->Name() == "Titles") {
        auto* tt = dynamic_cast<ModuleTitles*>(mod);
        if (tt) {
            DrawModernSliderFloat("Масштаб (Mashtab)", "##ttScale", &tt->mashtab, 0.10f, 3.0f, "%.2fx", accent.primary, s);
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.40f, 0.80f, 1.0f, 1.f), "Изменение размера больших серверных титров на экране.");
            ImGui::TextColored(ImVec4(0.55f, 0.60f, 0.70f, 1.f), "Не затрагивает карту и другие элементы интерфейса.");
        }
    }
    // ── 17. ScreenProtect ────────────────────────────────────────────────────
    else if (mod->Name() == "ScreenProtect") {
        ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.55f, 1.f), "ScreenProtect (Защита от записи и стрима):");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.75f, 0.80f, 0.90f, 1.f), "• OBS Studio / Discord / Bandicam не видят оверлей чита.");
        ImGui::TextColored(ImVec4(0.75f, 0.80f, 0.90f, 1.f), "• Чистый экран в Game Capture / Window Capture.");
    }
    // ── 18. NameProtect ──────────────────────────────────────────────────────
    else if (mod->Name() == "NameProtect") {
        auto* np = dynamic_cast<ModuleNameProtect*>(mod);
        if (np) {
            char nickBuf[64] = { 0 };
            strncpy(nickBuf, np->protectedNick.c_str(), sizeof(nickBuf) - 1);
            ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.95f, 1.f), "Защищенный никнейм:");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::InputText("##npNick", nickBuf, sizeof(nickBuf))) {
                np->protectedNick = nickBuf;
            }
            ImGui::Spacing();
            CustomCheckbox("Скрывать в друзьях", "Заменять имя и в списках друзей", &np->hideFriends, accent.primaryVec, s);
        }
    }
    // ── 19. Crosshair ────────────────────────────────────────────────────────
    else if (mod->Name() == "Crosshair") {
        auto* ch = dynamic_cast<ModuleCrosshair*>(mod);
        if (ch) {
            DrawModernSliderFloat("Размер (Size)", "##chSize", &ch->size, 1.0f, 30.0f, "%.1f px", accent.primary, s);
            DrawModernSliderFloat("Зазор (Gap)", "##chGap", &ch->gap, 0.0f, 25.0f, "%.1f px", accent.primary, s);
            DrawModernSliderFloat("Толщина (Thickness)", "##chThick", &ch->thickness, 0.5f, 6.0f, "%.1f px", accent.primary, s);
            CustomCheckbox("Точка в центре (Dot)", "Отображать точку в центре прицела", &ch->dot, accent.primaryVec, s);
        }
    }
}

// ── 4. Settings Modal (Панель настроек оформления ClickGUI) ───────────────────
void ClickGUI::DrawSettingsModal(float s, float winW, float winH, const ImVec2& winPos) {
    (void)winH;
    const auto& accent = s_accents[m_accentIndex];

    // Затемняющий оверлей для акцента на модалке и защиты от наложения кликов
    ImGui::GetForegroundDrawList()->AddRectFilled(winPos, ImVec2(winPos.x + winW, winPos.y + winH), IM_COL32(0, 0, 0, 85), 18.0f * s);

    float modalW = 420.0f * s;
    float modalH = 580.0f * s;
    float modalX = winPos.x + winW - modalW - 24.0f * s;
    float modalY = winPos.y + 70.0f * s;

    ImGui::SetNextWindowPos(ImVec2(modalX, modalY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(modalW, modalH), ImGuiCond_Always);
    ImGui::SetNextWindowFocus();

    ImGuiWindowFlags mFlags = ImGuiWindowFlags_NoTitleBar |
                              ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(18, 22, 33, 255));
    ImGui::PushStyleColor(ImGuiCol_Border,   IM_COL32(52, 58, 80, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * s, 16.0f * s));

    if (ImGui::Begin("##GuiSettingsPopup", &m_showSettings, mFlags)) {
        ImGui::SetWindowFocus();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float availW = ImGui::GetContentRegionAvail().x;

        // 1. Заголовок модалки
        ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.92f, 0.94f, 0.98f, 1.0f), "Настройки ClickGUI");
        ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.62f, 1.0f), "Персонализация интерфейса");
        ImGui::EndGroup();

        // Кнопка закрытия [X]
        float closeBtnSz = 26.0f * s;
        ImGui::SameLine(availW - closeBtnSz + 18.0f * s);
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(32, 38, 54, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(50, 58, 80, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  accent.primary);
        ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(160, 170, 195, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * s);

        if (ImGui::Button("X##CloseSettingsModal", ImVec2(closeBtnSz, closeBtnSz))) {
            m_showSettings = false;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 2. Масштаб интерфейса
        char scBuf[16];
        snprintf(scBuf, sizeof(scBuf), "%.0f%%", m_guiScale * 100.0f);
        ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.86f, 1.0f), "Масштаб интерфейса");
        ImVec2 scSz = ImGui::CalcTextSize(scBuf);
        ImGui::SameLine(availW - scSz.x);
        ImGui::TextColored(ImColor(accent.primary).Value, "%s", scBuf);

        ImGui::SetNextItemWidth(availW);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(30, 35, 48, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(38, 44, 60, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(44, 52, 70, 255));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,     accent.primary);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(255, 255, 255, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 999.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);

        if (ImGui::SliderFloat("##GuiScaleRange", &m_guiScale, 0.75f, 1.40f, "")) {
            ImGui::GetIO().FontGlobalScale = m_guiScale;
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);

        ImGui::TextColored(ImVec4(0.38f, 0.42f, 0.54f, 1.0f), "75%%");
        ImGui::SameLine(availW - ImGui::CalcTextSize("140%").x);
        ImGui::TextColored(ImVec4(0.38f, 0.42f, 0.54f, 1.0f), "140%%");

        ImGui::Spacing();

        // 3. Цветовой акцент
        ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.86f, 1.0f), "Цветовой акцент");
        float dotSz = 32.0f * s;
        float dotGap = 12.0f * s;
        ImVec2 swStart = ImGui::GetCursorScreenPos();
        for (int i = 0; i < 4; ++i) {
            float btnX = swStart.x + (float)i * (dotSz + dotGap);
            float btnY = swStart.y + 4.0f * s;
            ImGui::SetCursorScreenPos(ImVec2(btnX, btnY));
            ImGui::PushID(i);

            bool isCur = (m_accentIndex == i);
            if (ImGui::InvisibleButton("##AccentSelect", ImVec2(dotSz, dotSz))) {
                m_accentIndex = i;
            }

            draw->AddRectFilled(ImVec2(btnX, btnY), ImVec2(btnX + dotSz, btnY + dotSz), s_accents[i].primary, 8.0f * s);
            if (isCur) {
                draw->AddRect(ImVec2(btnX - 2.0f * s, btnY - 2.0f * s),
                              ImVec2(btnX + dotSz + 2.0f * s, btnY + dotSz + 2.0f * s),
                              IM_COL32(255, 255, 255, 255), 10.0f * s, 0, 2.0f * s);
                // Галочка
                ImVec2 p1(btnX + 8.5f * s, btnY + 16.0f * s);
                ImVec2 p2(btnX + 13.5f * s, btnY + 21.0f * s);
                ImVec2 p3(btnX + 23.5f * s, btnY + 11.0f * s);
                draw->AddLine(p1, p2, IM_COL32(255, 255, 255, 255), 2.5f * s);
                draw->AddLine(p2, p3, IM_COL32(255, 255, 255, 255), 2.5f * s);
            }
            ImGui::PopID();
        }

        ImGui::SetCursorScreenPos(ImVec2(swStart.x, swStart.y + dotSz + 14.0f * s));
        ImGui::Dummy(ImVec2(0, 4.0f * s));

        // 4. Переключатели
        CustomCheckbox("Компактный режим", "Уменьшенные размеры строк", &m_compactMode, accent.primaryVec, s);
        CustomCheckbox("Мягкое свечение", "Неоновая подсветка шапки чита", &m_softGlow, accent.primaryVec, s);

        ImGui::Spacing();

        // 5. Кнопка сброса
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(28, 33, 46, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(40, 47, 65, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  accent.primary);
        ImGui::PushStyleColor(ImGuiCol_Text,          IM_COL32(140, 148, 170, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * s);

        if (ImGui::Button("Сбросить настройки", ImVec2(availW, 32.0f * s))) {
            m_guiScale = 1.0f;
            m_accentIndex = 0;
            m_compactMode = false;
            m_softGlow = true;
            ImGui::GetIO().FontGlobalScale = 1.0f;
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

// ── 5. Footer (Подвал: Версия чита, живой FPS и Ping) ─────────────────────────
void ClickGUI::DrawFooter(float s, float winW, float winH, const ImVec2& winPos) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float footerH = 32.0f * s;
    float footerY = winPos.y + winH - footerH;

    // Разделительная линия над подвалом
    draw->AddLine(
        ImVec2(winPos.x, footerY),
        ImVec2(winPos.x + winW, footerY),
        IM_COL32(33, 37, 52, 255),
        1.0f * s
    );

    // Левый текст: MyrLegit v1.0.0 (С достаточным отступом чтобы не слипались)
    draw->AddText(ImVec2(winPos.x + 24.0f * s, footerY + 8.0f * s), IM_COL32(80, 88, 107, 255), "MyrLegit");
    draw->AddText(ImVec2(winPos.x + 95.0f * s, footerY + 8.0f * s), s_accents[m_accentIndex].primary, "v1.0.0");

    // Правый текст: Реальный живой FPS и Ping
    int curFps = GLHook::GetExactFPS();
    if (curFps <= 0) {
        curFps = static_cast<int>(std::round(ImGui::GetIO().Framerate));
    }
    if (curFps <= 0) {
        curFps = JVMBridge::Get().Data().fps;
    }
    if (curFps <= 0) curFps = 60;

    int curPing = JVMBridge::Get().Data().ping;
    if (curPing < 0) curPing = 0;

    char perfBuf[64];
    snprintf(perfBuf, sizeof(perfBuf), "%d FPS   *   %d ms", curFps, curPing);
    ImVec2 perfSz = ImGui::CalcTextSize(perfBuf);
    float perfX = winPos.x + winW - perfSz.x - 24.0f * s;
    draw->AddText(ImVec2(perfX, footerY + 8.0f * s), IM_COL32(90, 98, 118, 255), perfBuf);
}

// ── Главная функция отрисовки ClickGUI ─────────────────────────────────────────
void ClickGUI::Draw(float animProgress) {
    float s = (m_guiScale >= 0.70f && m_guiScale <= 1.50f) ? m_guiScale : 1.0f;
    ImGui::GetIO().FontGlobalScale = s;

    // Кинематографичное плавное затемнение фона игры при открытом ClickGUI
    ImVec2 dispSz = ImGui::GetIO().DisplaySize;
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), dispSz,
        IM_COL32(6, 8, 15, (int)(150.0f * animProgress))
    );

    // Выразительная плавная анимация появления (Scale Pop 0.82 -> 1.0)
    float t = animProgress;
    float easeOut = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t); // Cubic ease-out
    float scale = 0.82f + 0.18f * easeOut;

    // Размеры окна увеличены для идеального простора всех вкладок (1300x750)
    float baseWinW = (m_compactMode ? 1080.0f : 1300.0f) * s;
    float baseWinH = (m_compactMode ? 680.0f : 750.0f) * s;

    float winW = baseWinW * scale;
    float winH = baseWinH * scale;

    ImVec2 winPos((dispSz.x - winW) * 0.5f, (dispSz.y - winH) * 0.5f);
    if (winPos.x < 10.0f) winPos.x = 10.0f;
    if (winPos.y < 10.0f) winPos.y = 10.0f;

    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                            ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse |
                            ImGuiWindowFlags_NoCollapse;

    if (animProgress < 0.98f && !GUIManager::Get().IsClickGUIOpen()) {
        flags |= ImGuiWindowFlags_NoInputs;
    }

    // Стили основного контейнера Figma с плавной прозрачностью
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, animProgress);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(14, 17, 27, 248));
    ImGui::PushStyleColor(ImGuiCol_Border,   IM_COL32(36, 41, 56, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("##MyrLegitClickGUI", nullptr, flags)) {
        DrawTopBar(s, winW, winH, winPos);
        DrawSidebar(s, winW, winH, winPos);
        DrawContent(s, winW, winH, winPos);
        DrawFooter(s, winW, winH, winPos);
    }
    ImGui::End();

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);

    // Окно настроек рендерится поверх основного окна как отдельный top-level popup
    if (m_showSettings) {
        DrawSettingsModal(s, winW, winH, winPos);
    }

    // Восстанавливаем глобальный масштаб шрифта для HUD и всех визуалов
    ImGui::GetIO().FontGlobalScale = 1.0f;
}
