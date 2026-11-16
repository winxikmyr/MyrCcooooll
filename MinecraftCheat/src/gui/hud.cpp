// src/gui/hud.cpp
#include "hud.h"
#include "gui_manager.h"
#include "clickgui.h"
#include "../hooks/gl_hook.h"
#include "../jvm/jvm_bridge.h"
#include "../modules/module_hud.h"
#include <imgui.h>
#include <Windows.h>
#include <string>

std::string GetInjectorUsername() {
    static std::string cachedUser = "";
    static bool checked = false;
    if (!checked) {
        checked = true;
        char dllPath[MAX_PATH] = { 0 };
        HMODULE hMod = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&GetInjectorUsername), &hMod);
        if (hMod) {
            GetModuleFileNameA(hMod, dllPath, MAX_PATH);
            std::string dir = dllPath;
            size_t pos = dir.find_last_of("\\/");
            if (pos != std::string::npos) dir = dir.substr(0, pos);
            std::string userFile = dir + "\\user.dat";
            FILE* f = fopen(userFile.c_str(), "r");
            if (!f) {
                char tempPath[MAX_PATH] = { 0 };
                GetTempPathA(MAX_PATH, tempPath);
                std::string tFile = std::string(tempPath) + "myr_user.dat";
                f = fopen(tFile.c_str(), "r");
            }
            if (f) {
                char buf[128] = { 0 };
                if (fgets(buf, sizeof(buf), f)) {
                    cachedUser = buf;
                    while (!cachedUser.empty() && (cachedUser.back() == '\r' || cachedUser.back() == '\n' || cachedUser.back() == ' '))
                        cachedUser.pop_back();
                }
                fclose(f);
            }
        }
    }
    return cachedUser;
}

// ── Красивый логотип кота для MyrLegit (Кот и скрытный) ──────────────────────
void DrawStealthCatLogo(ImDrawList* draw, const ImVec2& pos, float size, ImU32 badgeBg, ImU32 accentCol, float uScale) {
    if (!draw || size <= 4.0f) return;
    (void)accentCol;

    float s = size / 22.0f;
    float round = 6.0f * s;

    // 1. Фон бэйджа с мягким скруглением углов
    draw->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), badgeBg, round);
    // Тонкий внутренний блик по контуру бэйджа для объема и контраста
    draw->AddRect(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(255, 255, 255, 45), round, 0, 1.0f * s);

    float cx = pos.x + size * 0.5f;
    float headY = pos.y + size * 0.5f + 0.8f * s;

    // Цветовая палитра: скрытный теневой кот (MyrLegit Stealth Cat)
    ImU32 shadowCatCol = IM_COL32(18, 20, 30, 248);       // Глубокий темный силуэт ниндзя-кота
    ImU32 innerEarCol  = IM_COL32(145, 130, 245, 180);     // Акцентные ушки внутри
    ImU32 eyeGlowCol   = IM_COL32(90, 225, 255, 255);      // Неоновый бирюзовый блеск глаз (скрытный кибер-кот)
    ImU32 eyeWhiteCol  = IM_COL32(255, 255, 255, 255);     // Яркий центр глаз
    ImU32 whiskerCol   = IM_COL32(230, 235, 255, 220);     // Тонкие стильные усы
    ImU32 noseCol      = IM_COL32(245, 160, 195, 230);     // Аккуратный носик

    // 2. Уши кота (2 острых треугольника)
    // Левое ухо
    ImVec2 eL1(cx - 0.5f * s, headY - 3.0f * s);
    ImVec2 eL2(cx - 5.6f * s, headY - 8.2f * s);
    ImVec2 eL3(cx - 7.6f * s, headY - 1.2f * s);
    draw->AddTriangleFilled(eL1, eL2, eL3, shadowCatCol);

    // Правое ухо
    ImVec2 eR1(cx + 0.5f * s, headY - 3.0f * s);
    ImVec2 eR2(cx + 5.6f * s, headY - 8.2f * s);
    ImVec2 eR3(cx + 7.6f * s, headY - 1.2f * s);
    draw->AddTriangleFilled(eR1, eR2, eR3, shadowCatCol);

    // Внутренние акцентные треугольники ушей
    draw->AddTriangleFilled(
        ImVec2(cx - 2.0f * s, headY - 3.2f * s),
        ImVec2(cx - 5.2f * s, headY - 6.8f * s),
        ImVec2(cx - 6.4f * s, headY - 2.0f * s),
        innerEarCol
    );
    draw->AddTriangleFilled(
        ImVec2(cx + 2.0f * s, headY - 3.2f * s),
        ImVec2(cx + 5.2f * s, headY - 6.8f * s),
        ImVec2(cx + 6.4f * s, headY - 2.0f * s),
        innerEarCol
    );

    // 3. Основная форма мордочки кота (выпуклый 6-угольник)
    ImVec2 facePts[6] = {
        ImVec2(cx, headY - 3.0f * s),
        ImVec2(cx + 7.6f * s, headY - 1.2f * s),
        ImVec2(cx + 6.4f * s, headY + 3.2f * s),
        ImVec2(cx, headY + 7.2f * s),
        ImVec2(cx - 6.4f * s, headY + 3.2f * s),
        ImVec2(cx - 7.6f * s, headY - 1.2f * s)
    };
    draw->AddConvexPolyFilled(facePts, 6, shadowCatCol);

    // Внешняя тонкая обводка контура кота (сглаживание Anti-aliasing)
    ImVec2 fullContour[8] = {
        ImVec2(cx, headY - 3.0f * s),
        ImVec2(cx + 5.6f * s, headY - 8.2f * s),
        ImVec2(cx + 7.6f * s, headY - 1.2f * s),
        ImVec2(cx + 6.4f * s, headY + 3.2f * s),
        ImVec2(cx, headY + 7.2f * s),
        ImVec2(cx - 6.4f * s, headY + 3.2f * s),
        ImVec2(cx - 7.6f * s, headY - 1.2f * s),
        ImVec2(cx - 5.6f * s, headY - 8.2f * s)
    };
    draw->AddPolyline(fullContour, 8, IM_COL32(255, 255, 255, 35), ImDrawFlags_Closed, 1.0f * s);

    // 4. Скрытные прищуренные кошачьи глаза (Ninja Cat Eyes)
    // Левый глаз: наклонный хищный разрез
    ImVec2 eyeL1(cx - 1.5f * s, headY + 0.5f * s);
    ImVec2 eyeL2(cx - 3.6f * s, headY - 0.1f * s);
    ImVec2 eyeL3(cx - 5.6f * s, headY - 0.8f * s);
    draw->AddLine(eyeL1, eyeL2, eyeGlowCol, 1.8f * s);
    draw->AddLine(eyeL2, eyeL3, eyeGlowCol, 1.4f * s);
    draw->AddCircleFilled(ImVec2(cx - 3.4f * s, headY + 0.1f * s), 0.75f * s, eyeWhiteCol);

    // Правый глаз
    ImVec2 eyeR1(cx + 1.5f * s, headY + 0.5f * s);
    ImVec2 eyeR2(cx + 3.6f * s, headY - 0.1f * s);
    ImVec2 eyeR3(cx + 5.6f * s, headY - 0.8f * s);
    draw->AddLine(eyeR1, eyeR2, eyeGlowCol, 1.8f * s);
    draw->AddLine(eyeR2, eyeR3, eyeGlowCol, 1.4f * s);
    draw->AddCircleFilled(ImVec2(cx + 3.4f * s, headY + 0.1f * s), 0.75f * s, eyeWhiteCol);

    // 5. Носик и тонкие усы кота
    draw->AddTriangleFilled(
        ImVec2(cx - 0.9f * s, headY + 2.7f * s),
        ImVec2(cx + 0.9f * s, headY + 2.7f * s),
        ImVec2(cx, headY + 3.8f * s),
        noseCol
    );

    // Усики (по 2 с каждой стороны, утонченные)
    draw->AddLine(ImVec2(cx - 2.2f * s, headY + 3.4f * s), ImVec2(cx - 6.6f * s, headY + 2.6f * s), whiskerCol, 0.9f * s);
    draw->AddLine(ImVec2(cx - 2.0f * s, headY + 4.3f * s), ImVec2(cx - 5.8f * s, headY + 5.0f * s), whiskerCol, 0.9f * s);

    draw->AddLine(ImVec2(cx + 2.2f * s, headY + 3.4f * s), ImVec2(cx + 6.6f * s, headY + 2.6f * s), whiskerCol, 0.9f * s);
    draw->AddLine(ImVec2(cx + 2.0f * s, headY + 4.3f * s), ImVec2(cx + 6.8f * s, headY + 5.0f * s), whiskerCol, 0.9f * s);
}

void HUD::Draw(const ModuleHUD* settings) {
    if (!settings || !settings->IsEnabled()) return;

    bool showUser = settings->showUser;
    bool showFPS  = settings->showFPS;
    bool showPing = settings->showPing;

    if (!showUser && !showFPS && !showPing) return;

    const MCData& d = JVMBridge::Get().Data();

    float uScale = 1.0f;
    auto* clickGui = GUIManager::Get().GetClickGUI();
    if (clickGui) uScale = clickGui->GetGUIScale();

    struct NettyxTheme {
        ImU32 bg   = IM_COL32(10, 10, 10, 255);
        ImU32 win  = IM_COL32(17, 17, 17, 250);
        ImU32 line = IM_COL32(36, 36, 36, 255);
        ImU32 tx   = IM_COL32(242, 242, 242, 255);
        ImU32 mu   = IM_COL32(122, 122, 122, 255);
        ImU32 ac   = IM_COL32(120, 95, 245, 255);
        ImU32 at   = IM_COL32(255, 255, 255, 255);
    } th;
    if (clickGui) {
        th.ac = clickGui->GetPrimaryAccentColor();
    }

    bool isGuiOpen = GUIManager::Get().IsClickGUIOpen();
    auto* mutSettings = const_cast<ModuleHUD*>(settings);

    // Позиция по умолчанию: правый верхний угол (12px отступ)
    ImVec2 dispSz = ImGui::GetIO().DisplaySize;
    if (mutSettings->posX < 0.0f || mutSettings->posY < 0.0f || (mutSettings->posX == 10.0f && mutSettings->posY == 10.0f)) {
        mutSettings->posX = dispSz.x - 390.0f * uScale;
        mutSettings->posY = 14.0f * uScale;
    }

    if (isGuiOpen) {
        ImGui::SetNextWindowPos({mutSettings->posX, mutSettings->posY}, ImGuiCond_Appearing);
    } else {
        ImGui::SetNextWindowPos({mutSettings->posX, mutSettings->posY}, ImGuiCond_Always);
    }
    ImGui::SetNextWindowSize({0.f, 0.f});

    ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoSavedSettings;

    if (!isGuiOpen) {
        kFlags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * uScale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,   IM_COL32(0, 0, 0, 0));

    if (!ImGui::Begin("##NettyxWatermark", nullptr, kFlags)) {
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        ImGui::End();
        return;
    }

    if (isGuiOpen) {
        ImVec2 p = ImGui::GetWindowPos();
        mutSettings->posX = p.x;
        mutSettings->posY = p.y;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 curPos = ImGui::GetCursorScreenPos();
    float barH = 34.0f * uScale;

    // Сбор данных
    std::string injUser = GetInjectorUsername();
    std::string dispUser = (!injUser.empty()) ? injUser : (!d.username.empty() ? d.username : "nightuser");
    auto* cgui = GUIManager::Get().GetClickGUI();
    if (cgui && cgui->IsNameProtectEnabled()) {
        auto* np = cgui->GetNameProtectModule();
        if (np && !np->protectedNick.empty()) dispUser = np->protectedNick;
    }

    int displayFPS = GLHook::GetExactFPS();
    if (displayFPS <= 0) displayFPS = static_cast<int>(std::round(ImGui::GetIO().Framerate));
    if (displayFPS <= 0) displayFPS = d.fps;
    if (displayFPS <= 0) displayFPS = 60;
    char fpsBuf[32];
    snprintf(fpsBuf, sizeof(fpsBuf), "%d fps", displayFPS);

    int ping = d.ping;
    char pingBuf[32];
    if (ping < 0) snprintf(pingBuf, sizeof(pingBuf), "-- ms");
    else snprintf(pingBuf, sizeof(pingBuf), "%d ms", ping);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char timeBuf[32];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

    // Расчет ширины единого Nettyx-контейнера (#wm)
    float padLeft = 8.0f * uScale;
    float iconSz = 22.0f * uScale;
    float gap = 9.0f * uScale;
    float divW = 1.0f * uScale;
    float divPad = 8.0f * uScale;

    ImVec2 titleSz = ImGui::CalcTextSize("MyrLegit");
    ImVec2 verSz   = ImGui::CalcTextSize("v1.0");
    ImVec2 userSz  = ImGui::CalcTextSize(dispUser.c_str());
    ImVec2 fpsSz   = ImGui::CalcTextSize(fpsBuf);
    ImVec2 pingSz  = ImGui::CalcTextSize(pingBuf);
    ImVec2 timeSz  = ImGui::CalcTextSize(timeBuf);

    float totalW = padLeft + iconSz + gap + titleSz.x + 8.0f * uScale + (verSz.x + 10.0f * uScale);
    if (showUser) totalW += divPad + divW + divPad + (14.0f * uScale + 5.0f * uScale + userSz.x);
    if (showFPS)  totalW += divPad + divW + divPad + (14.0f * uScale + 5.0f * uScale + fpsSz.x);
    if (showPing) totalW += divPad + divW + divPad + (14.0f * uScale + 5.0f * uScale + pingSz.x);
    totalW += divPad + divW + divPad + (14.0f * uScale + 5.0f * uScale + timeSz.x) + 12.0f * uScale;

    // 1. Мягкая тень (box-shadow: 0 8px 24px rgba(0,0,0,.25))
    draw->AddRectFilled(
        ImVec2(curPos.x + 2.0f * uScale, curPos.y + 4.0f * uScale),
        ImVec2(curPos.x + totalW + 2.0f * uScale, curPos.y + barH + 6.0f * uScale),
        IM_COL32(0, 0, 0, 75), 8.0f * uScale
    );

    // 2. Фон плашки (var(--win))
    draw->AddRectFilled(
        curPos, ImVec2(curPos.x + totalW, curPos.y + barH),
        th.win, 8.0f * uScale
    );
    // 3. Рамка (1px solid var(--line))
    draw->AddRect(
        curPos, ImVec2(curPos.x + totalW, curPos.y + barH),
        th.line, 8.0f * uScale, 0, 1.0f * uScale
    );
    // 4. Верхняя акцентная линия (#wm:before { height: 2px; background: var(--ac); })
    draw->AddRectFilled(
        curPos, ImVec2(curPos.x + totalW, curPos.y + 2.0f * uScale),
        th.ac, 8.0f * uScale, ImDrawFlags_RoundCornersTop
    );

    // Отрисовка внутреннего содержимого
    float x = curPos.x + padLeft;
    float textY = curPos.y + (barH - ImGui::GetFontSize()) * 0.5f;

    // Логотип MyrLegit (Скрытный Кот)
    float mkY = curPos.y + (barH - iconSz) * 0.5f;
    DrawStealthCatLogo(draw, ImVec2(x, mkY), iconSz, th.ac, th.ac, uScale);

    x += iconSz + gap;

    // Название .nm
    draw->AddText(ImVec2(x, textY), th.tx, "MyrLegit");
    x += titleSz.x + 7.0f * uScale;

    // Бейдж версии em { font-size: 10px; border: 1px solid var(--line); border-radius: 4px; }
    float vBadgeH = 17.0f * uScale;
    float vBadgeW = verSz.x + 8.0f * uScale;
    float vBadgeY = curPos.y + (barH - vBadgeH) * 0.5f;
    draw->AddRect(ImVec2(x, vBadgeY), ImVec2(x + vBadgeW, vBadgeY + vBadgeH), th.line, 4.0f * uScale);
    draw->AddText(ImVec2(x + 4.0f * uScale, curPos.y + (barH - ImGui::GetFontSize()) * 0.5f), th.mu, "v1.0");
    x += vBadgeW;

    auto DrawDivider = [&](float& curX) {
        curX += divPad;
        float dH = 14.0f * uScale;
        float dY = curPos.y + (barH - dH) * 0.5f;
        draw->AddLine(ImVec2(curX, dY), ImVec2(curX, dY + dH), th.line, 1.0f * uScale);
        curX += divPad;
    };

    // Пользователь
    if (showUser) {
        DrawDivider(x);
        // Векторная иконка пользователя
        float uIconX = x + 6.0f * uScale;
        float uIconY = curPos.y + barH * 0.5f;
        draw->AddCircle(ImVec2(uIconX, uIconY - 3.0f * uScale), 3.0f * uScale, th.mu, 12, 1.2f * uScale);
        // Нижняя часть иконки пользователя (полукруг вместо AddArc)
        draw->PathArcTo(ImVec2(uIconX, uIconY + 4.0f * uScale), 5.5f * uScale, 3.1415f, 0.0f, 8);
        draw->PathStroke(th.mu, false, 1.2f * uScale);
        x += 16.0f * uScale;
        draw->AddText(ImVec2(x, textY), th.tx, dispUser.c_str());
        x += userSz.x;
    }

    // FPS
    if (showFPS) {
        DrawDivider(x);
        // Векторная иконка пульса
        float fIconY = curPos.y + barH * 0.5f;
        draw->AddLine(ImVec2(x, fIconY), ImVec2(x + 3.0f * uScale, fIconY), th.mu, 1.2f * uScale);
        draw->AddLine(ImVec2(x + 3.0f * uScale, fIconY), ImVec2(x + 5.0f * uScale, fIconY - 5.0f * uScale), th.mu, 1.2f * uScale);
        draw->AddLine(ImVec2(x + 5.0f * uScale, fIconY - 5.0f * uScale), ImVec2(x + 8.0f * uScale, fIconY + 5.0f * uScale), th.mu, 1.2f * uScale);
        draw->AddLine(ImVec2(x + 8.0f * uScale, fIconY + 5.0f * uScale), ImVec2(x + 10.0f * uScale, fIconY), th.mu, 1.2f * uScale);
        draw->AddLine(ImVec2(x + 10.0f * uScale, fIconY), ImVec2(x + 13.0f * uScale, fIconY), th.mu, 1.2f * uScale);
        x += 16.0f * uScale;
        draw->AddText(ImVec2(x, textY), th.tx, fpsBuf);
        x += fpsSz.x;
    }

    // Ping
    if (showPing) {
        DrawDivider(x);
        // Векторная иконка сигнала
        float pIconY = curPos.y + barH * 0.5f + 4.0f * uScale;
        for (int b = 0; b < 4; ++b) {
            float bH = (float)(b + 1) * 2.8f * uScale;
            draw->AddLine(ImVec2(x + (float)b * 3.2f * uScale, pIconY),
                          ImVec2(x + (float)b * 3.2f * uScale, pIconY - bH),
                          th.mu, 1.5f * uScale);
        }
        x += 16.0f * uScale;
        draw->AddText(ImVec2(x, textY), th.tx, pingBuf);
        x += pingSz.x;
    }

    // Часы
    {
        DrawDivider(x);
        // Векторная иконка часов
        float cIconX = x + 6.0f * uScale;
        float cIconY = curPos.y + barH * 0.5f;
        draw->AddCircle(ImVec2(cIconX, cIconY), 5.5f * uScale, th.mu, 16, 1.2f * uScale);
        draw->AddLine(ImVec2(cIconX, cIconY), ImVec2(cIconX, cIconY - 3.2f * uScale), th.mu, 1.2f * uScale);
        draw->AddLine(ImVec2(cIconX, cIconY), ImVec2(cIconX + 2.4f * uScale, cIconY + 1.2f * uScale), th.mu, 1.2f * uScale);
        x += 16.0f * uScale;
        draw->AddText(ImVec2(x, textY), th.tx, timeBuf);
    }

    ImGui::Dummy(ImVec2(totalW, barH));

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    ImGui::End();
}
