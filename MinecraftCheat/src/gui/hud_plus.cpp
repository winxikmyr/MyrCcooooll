// src/gui/hud_plus.cpp
#include "hud_plus.h"
#include "gui_manager.h"
#include "clickgui.h"
#include "../modules/module_armorhud.h"
#include "../jvm/jvm_bridge.h"
#include "armor_textures.h"
#include <imgui.h>
#include <Windows.h>
#include <cmath>
#include <algorithm>

void HUDPlus::Draw(const ModuleArmorHUD* settings) {
    if (!settings || !settings->IsEnabled()) return;

    const MCData& d = JVMBridge::Get().Data();
    if (!d.isInWorld) return;

    DrawArmorHUD(settings->armorPosY, settings->scale);
}

// ── Нативный ArmorHUD 1-в-1 по коду Swill Client Java ───────────────────────
// ScaledResolution sr = new ScaledResolution(mc);
// int x = sr.getScaledWidth() / 2 + 15;
// int y = sr.getScaledHeight() - 55; // Чуть выше полоски голода
// for (int i = 0; i <= 3; i++) {
//     ItemStack stack = mc.player.inventory.armorInventory.get(i);
//     if (stack == null || stack.getItem() == null) continue;
//     int ix = x + (3 - i) * 20;
//     mc.getRenderItem().renderItemAndEffectIntoGUI(stack, ix, y);
//     mc.getRenderItem().renderItemOverlays(mc.fontRendererObj, stack, ix, y);
// }
void HUDPlus::DrawArmorHUD(float yOffset, float userScaleMul) {
    const MCData& d = JVMBridge::Get().Data();
    if (!d.isInWorld) return;
    if (d.hasScreen && !d.isChatOpen) return;

    // Считаем сколько надето предметов брони на тело
    int armorCount = 0;
    for (int i = 0; i < 4; ++i) {
        if (!d.equipment[i].isEmpty) ++armorCount;
    }
    if (armorCount == 0) return;

    ImVec2 disp = ImGui::GetIO().DisplaySize;
    if (disp.x <= 0 || disp.y <= 0) return;

    float guiScale = (d.guiScale > 0) ? static_cast<float>(d.guiScale) : 2.0f;
    float userScale = (userScaleMul > 0.1f) ? userScaleMul : 1.0f;
    auto* clickGui = GUIManager::Get().GetClickGUI();
    if (clickGui) userScale *= clickGui->GetGUIScale();

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (!draw) return;

    // Расчет ScaledResolution (координаты точно соответствуют Java коду):
    float scaledW = disp.x / guiScale;
    float scaledH = disp.y / guiScale;

    // int x = sr.getScaledWidth() / 2 + 15;
    // int y = sr.getScaledHeight() - 55;
    float baseX = (scaledW * 0.5f) + 15.0f;
    float baseY = (yOffset > 0.1f) ? (scaledH - yOffset) : (scaledH - 55.0f);

    float effScale = guiScale * userScale;
    float iconSize = 16.0f * effScale;
    float slotStep = 20.0f * effScale;

    // В Minecraft armorInventory:
    // i = 0 (boots), i = 1 (legs), i = 2 (chest), i = 3 (helmet)
    // int ix = x + (3 - i) * 20;
    // В m_data.equipment:
    // 0 = helmet (i = 3 -> 3 - 3 = 0 -> ix = x + 0 * 20)
    // 1 = chest  (i = 2 -> 3 - 2 = 1 -> ix = x + 1 * 20)
    // 2 = legs   (i = 1 -> 3 - 1 = 2 -> ix = x + 2 * 20)
    // 3 = boots  (i = 0 -> 3 - 0 = 3 -> ix = x + 3 * 20)
    for (int slot = 0; slot < 4; ++slot) {
        const auto& arm = d.equipment[slot];
        if (arm.isEmpty) continue;

        float ix = (baseX * guiScale) + (static_cast<float>(slot) * slotStep);
        float iy = (baseY * guiScale);

        // 1. Отрисовка спрайта предмета (renderItemAndEffectIntoGUI)
        if (d.atlasTextureId > 0 && arm.hasIcon &&
            arm.maxU > arm.minU && arm.maxV > arm.minV) {
            draw->AddImage(
                reinterpret_cast<ImTextureID>(static_cast<intptr_t>(d.atlasTextureId)),
                ImVec2(ix, iy),
                ImVec2(ix + iconSize, iy + iconSize),
                ImVec2(arm.minU, arm.minV),
                ImVec2(arm.maxU, arm.maxV)
            );
        } else {
            GLuint aTex = ArmorTextures::GetTexture(slot, arm.name, arm.itemId);
            if (aTex != 0) {
                draw->AddImage(
                    reinterpret_cast<ImTextureID>(static_cast<intptr_t>(aTex)),
                    ImVec2(ix, iy),
                    ImVec2(ix + iconSize, iy + iconSize)
                );
            } else {
                // Векторный аккуратный силуэт, если текстура еще загружается
                ImU32 iconCol = IM_COL32(220, 225, 235, 240);
                float cx = ix + iconSize * 0.5f;
                float cy = iy + iconSize * 0.5f;
                float r  = iconSize * 0.32f;
                if (slot == 0) {
                    draw->AddRectFilled(ImVec2(cx - r, cy - r * 0.75f), ImVec2(cx + r, cy + r * 0.35f), iconCol, 2.f);
                    draw->AddRectFilled(ImVec2(cx - r * 0.9f, cy + r * 0.35f), ImVec2(cx - r * 0.35f, cy + r * 0.9f), iconCol, 1.f);
                    draw->AddRectFilled(ImVec2(cx + r * 0.35f, cy + r * 0.35f), ImVec2(cx + r * 0.9f, cy + r * 0.9f), iconCol, 1.f);
                } else if (slot == 1) {
                    draw->AddRectFilled(ImVec2(cx - r * 0.65f, cy - r * 0.8f), ImVec2(cx + r * 0.65f, cy + r * 0.9f), iconCol, 2.f);
                    draw->AddRectFilled(ImVec2(cx - r, cy - r * 0.7f), ImVec2(cx - r * 0.65f, cy - r * 0.1f), iconCol, 1.f);
                    draw->AddRectFilled(ImVec2(cx + r * 0.65f, cy - r * 0.7f), ImVec2(cx + r, cy - r * 0.1f), iconCol, 1.f);
                } else if (slot == 2) {
                    draw->AddRectFilled(ImVec2(cx - r * 0.75f, cy - r * 0.8f), ImVec2(cx - r * 0.15f, cy + r * 0.9f), iconCol, 1.f);
                    draw->AddRectFilled(ImVec2(cx + r * 0.15f, cy - r * 0.8f), ImVec2(cx + r * 0.75f, cy + r * 0.9f), iconCol, 1.f);
                    draw->AddRectFilled(ImVec2(cx - r * 0.75f, cy - r * 0.8f), ImVec2(cx + r * 0.75f, cy - r * 0.4f), iconCol, 1.f);
                } else {
                    draw->AddRectFilled(ImVec2(cx - r * 0.85f, cy - r * 0.3f), ImVec2(cx - r * 0.15f, cy + r * 0.8f), iconCol, 1.5f);
                    draw->AddRectFilled(ImVec2(cx + r * 0.15f, cy - r * 0.3f), ImVec2(cx + r * 0.85f, cy + r * 0.8f), iconCol, 1.5f);
                }
            }
        }

        // 2. Отрисовка полоски прочности предмета 1-в-1 с renderItemOverlays
        if (arm.maxDamage > 0 && arm.durability < 0.999f) {
            float dur = std::clamp(arm.durability, 0.0f, 1.0f);
            float barX = ix + 1.5f * effScale;
            float barY = iy + 13.0f * effScale;
            float barW = 13.0f * effScale;
            float barH = 2.0f * effScale;
            float fillW = barW * dur;

            // Настоящая палитра прочности Minecraft (красный -> желтый -> зеленый)
            int r = static_cast<int>((1.0f - dur) * 255.0f);
            int g = static_cast<int>(dur * 255.0f);
            ImU32 durCol = IM_COL32(r, g, 0, 255);

            // Черная рамка-подложка
            draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), IM_COL32(0, 0, 0, 255));
            // Полоска текущей прочности
            draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fillW, barY + barH - 0.5f * effScale), durCol);
        }
    }
}
