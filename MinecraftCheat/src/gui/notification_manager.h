// src/gui/notification_manager.h
#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <Windows.h>
#include <imgui.h>

struct Notification {
    std::string text;
    bool state;       // true = enabled (green), false = disabled (red)
    DWORD startTime;
    float durationMs; // default 2600ms
    float animSlide;  // 0.0 to 1.0
};

class NotificationManager {
public:
    static NotificationManager& Get() {
        static NotificationManager instance;
        return instance;
    }

    void Add(const std::string& text, bool state, float durationMs = 2600.0f) {
        Notification n;
        n.text = text;
        n.state = state;
        n.startTime = GetTickCount();
        n.durationMs = durationMs;
        n.animSlide = 0.0f;
        m_notifications.push_back(n);

        // Ограничиваем очередь максимум 6 активными плашками
        if (m_notifications.size() > 6) {
            m_notifications.erase(m_notifications.begin());
        }
    }

    void Render(ImDrawList* draw, ImVec2 disp, ImFont* font, float scale = 1.0f) {
        if (!draw || m_notifications.empty() || disp.x <= 0 || disp.y <= 0) return;

        DWORD now = GetTickCount();
        float cardW = 275.0f * scale;
        float cardH = 56.0f * scale;
        float spacing = 10.0f * scale;
        float marginX = 24.0f * scale;
        float marginY = 28.0f * scale;

        float curY = disp.y - marginY - cardH;

        for (auto it = m_notifications.begin(); it != m_notifications.end(); ) {
            float elapsed = static_cast<float>(now - it->startTime);
            if (elapsed >= it->durationMs) {
                it = m_notifications.erase(it);
                continue;
            }

            // Плавная анимация появления (0..250ms) и исчезновения (duration - 250ms..duration)
            float alphaFactor = 1.0f;
            if (elapsed < 250.0f) {
                alphaFactor = elapsed / 250.0f;
                it->animSlide = alphaFactor;
            } else if (elapsed > it->durationMs - 250.0f) {
                alphaFactor = (it->durationMs - elapsed) / 250.0f;
                it->animSlide = alphaFactor;
            } else {
                it->animSlide = 1.0f;
            }

            // Выплывание справа с плавным сглаживанием
            float slideOffset = (1.0f - it->animSlide) * (cardW + marginX);
            float x1 = disp.x - marginX - cardW + slideOffset;
            float y1 = curY;
            float x2 = x1 + cardW;
            float y2 = y1 + cardH;

            int alpha = static_cast<int>(alphaFactor * 245.0f);
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;

            // 1. Глубокая тень
            draw->AddRectFilled(
                ImVec2(x1 - 3.f, y1 - 3.f),
                ImVec2(x2 + 3.f, y2 + 3.f),
                IM_COL32(5, 7, 12, alpha / 3),
                9.0f
            );

            // 2. Фон карточки Glassmorphism (темный полупрозрачный)
            draw->AddRectFilled(
                ImVec2(x1, y1),
                ImVec2(x2, y2),
                IM_COL32(14, 17, 26, alpha),
                8.0f
            );

            // 3. Аккуратная рамка карточки
            ImU32 accentCol = it->state ? IM_COL32(75, 235, 115, alpha) : IM_COL32(255, 75, 75, alpha);
            draw->AddRect(
                ImVec2(x1, y1),
                ImVec2(x2, y2),
                IM_COL32(50, 65, 95, static_cast<int>(alpha * 0.65f)),
                8.0f,
                0,
                1.2f
            );

            // 4. Яркий цветной вертикальный индикатор слева
            draw->AddRectFilled(
                ImVec2(x1 + 4.f, y1 + 7.f),
                ImVec2(x1 + 9.f, y2 - 7.f),
                accentCol,
                3.0f
            );

            // 5. Текст статуса и заголовка (крупнее и читабельнее, масштабируется)
            float baseFont = (font ? font->FontSize : 18.0f) * scale;
            float statusSz = baseFont * 0.75f;
            float titleSz  = baseFont * 0.95f;

            const char* statusText = it->state ? "ВКЛЮЧЕНО" : "ОТКЛЮЧЕНО";
            ImU32 statusCol = it->state ? IM_COL32(95, 250, 130, alpha) : IM_COL32(255, 105, 105, alpha);

            if (font) {
                // Тень текста
                draw->AddText(font, statusSz, ImVec2(x1 + 20.f * scale, y1 + 9.f * scale), IM_COL32(0, 0, 0, alpha), statusText);
                draw->AddText(font, statusSz, ImVec2(x1 + 19.f * scale, y1 + 8.f * scale), statusCol, statusText);

                draw->AddText(font, titleSz, ImVec2(x1 + 20.f * scale, y1 + 27.f * scale), IM_COL32(0, 0, 0, alpha), it->text.c_str());
                draw->AddText(font, titleSz, ImVec2(x1 + 19.f * scale, y1 + 26.f * scale), IM_COL32(245, 248, 255, alpha), it->text.c_str());
            }

            // 6. Полоса прогресса времени жизни снизу карточки
            float progress = 1.0f - (elapsed / it->durationMs);
            if (progress < 0.0f) progress = 0.0f;
            float barW = (cardW - 14.f) * progress;
            draw->AddRectFilled(
                ImVec2(x1 + 7.f, y2 - 4.0f),
                ImVec2(x1 + 7.f + barW, y2 - 1.5f),
                accentCol,
                1.5f
            );

            curY -= (cardH + spacing);
            ++it;
        }
    }

private:
    NotificationManager() = default;
    std::vector<Notification> m_notifications;
};
