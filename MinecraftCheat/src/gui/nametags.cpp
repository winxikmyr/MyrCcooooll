// src/gui/nametags.cpp
#include "nametags.h"
#include "../modules/module_nametags.h"
#include "../modules/friend_manager.h"
#include "../jvm/jvm_bridge.h"
#include "armor_textures.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <string>
#include <vector>

static const std::unordered_map<int, const char*> CUSTOM_ITEM_IDS = {
    { 1125, "Stone" },
    { 1396, "Bow" },
    { 1451, "Stone Pickaxe" },
    { 190,  "Machete" },
    { 367,  "Stone Spear" },
    { 711,  "Stone Axe" },
    { 191,  "Bone Knife" },
    { 245,  "Iron Axe" },
    { 249,  "Crossbow" },
    { 57,   "Flint" },
    { 232,  "Wooden Spear" },
    { 10,   "Pawn" }
};

static ImU32 GetArmorColor(int itemId, const std::string& name) {
    if ((itemId >= 310 && itemId <= 313) || itemId == 276 ||
        name.find("Алмаз") != std::string::npos || name.find("Diamond") != std::string::npos) {
        return IM_COL32(46, 230, 230, 255);
    }
    if ((itemId >= 306 && itemId <= 309) || itemId == 267 ||
        name.find("Желез") != std::string::npos || name.find("Iron") != std::string::npos) {
        return IM_COL32(235, 235, 240, 255);
    }
    if ((itemId >= 314 && itemId <= 317) || itemId == 283 ||
        name.find("Золот") != std::string::npos || name.find("Gold") != std::string::npos) {
        return IM_COL32(255, 215, 0, 255);
    }
    if ((itemId >= 298 && itemId <= 301) || name.find("Кожан") != std::string::npos || name.find("Leather") != std::string::npos) {
        return IM_COL32(160, 82, 45, 255);
    }
    return IM_COL32(50, 150, 255, 255);
}

static std::string CleanMinecraftCodes(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ) {
        unsigned char c1 = static_cast<unsigned char>(input[i]);
        // 1. Код § в UTF-8: 0xC2 0xA7
        if (c1 == 0xC2 && i + 1 < input.size() && static_cast<unsigned char>(input[i + 1]) == 0xA7) {
            i += 2;
            // Пропускаем символ кода форматирования (цвет, r, l, o и т.д.)
            if (i < input.size()) {
                unsigned char c2 = static_cast<unsigned char>(input[i]);
                if ((c2 >= '0' && c2 <= '9') || (c2 >= 'a' && c2 <= 'z') || (c2 >= 'A' && c2 <= 'Z')) {
                    i += 1;
                }
            }
            continue;
        }
        // 2. Одиночный байт §: 0xA7 (CP1251 или ANSI)
        if (c1 == 0xA7) {
            i += 1;
            if (i < input.size()) {
                unsigned char c2 = static_cast<unsigned char>(input[i]);
                if ((c2 >= '0' && c2 <= '9') || (c2 >= 'a' && c2 <= 'z') || (c2 >= 'A' && c2 <= 'Z')) {
                    i += 1;
                }
            }
            continue;
        }
        // 3. Код через амперсанд (&c, &r, &f и т.д.)
        if (c1 == '&' && i + 1 < input.size()) {
            unsigned char c2 = static_cast<unsigned char>(input[i + 1]);
            if ((c2 >= '0' && c2 <= '9') || (c2 >= 'a' && c2 <= 'f') || (c2 >= 'k' && c2 <= 'o') || c2 == 'r' || (c2 >= 'A' && c2 <= 'F') || c2 == 'R') {
                i += 2;
                continue;
            }
        }
        // Обычный символ
        out.push_back(input[i]);
        i += 1;
    }
    // Удаляем управляющие символы и нормализуем пробелы
    std::string trimmed;
    bool prevSpace = false;
    for (char c : out) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!prevSpace && !trimmed.empty()) trimmed.push_back(' ');
            prevSpace = true;
        } else if (static_cast<unsigned char>(c) >= 32) {
            trimmed.push_back(c);
            prevSpace = false;
        }
    }
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();

    // Финальная очистка от любых остаточных символов параграфа
    std::string res;
    res.reserve(trimmed.size());
    for (size_t i = 0; i < trimmed.size(); ) {
        if (i + 1 < trimmed.size() && static_cast<unsigned char>(trimmed[i]) == 0xC2 && static_cast<unsigned char>(trimmed[i+1]) == 0xA7) {
            i += 2;
            if (i < trimmed.size()) i += 1;
            continue;
        }
        if (static_cast<unsigned char>(trimmed[i]) == 0xA7) {
            i += 1;
            if (i < trimmed.size()) i += 1;
            continue;
        }
        res.push_back(trimmed[i]);
        i += 1;
    }
    // Удаляем любые остаточные "§r" или "&r" в конце
    while (res.size() >= 2 && (res.substr(res.size() - 2) == "§r" || res.substr(res.size() - 2) == "&r")) {
        res.resize(res.size() - 2);
        while (!res.empty() && (res.back() == ' ' || res.back() == '\t')) res.pop_back();
    }
    return res;
}

static std::string GetUtf8SafePrefix(const std::string& str, size_t maxChars) {
    std::string res;
    size_t count = 0;
    for (size_t i = 0; i < str.size() && count < maxChars; ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        size_t len = 1;
        if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > str.size()) break;
        res.append(str, i, len);
        i += len;
        count++;
    }
    return res;
}

void NameTags::Draw(const ModuleNameTags* settings) {
    if (!settings || !settings->IsEnabled() || !settings->showPlayers) return;

    const auto& data = JVMBridge::Get().Data();
    if (data.thirdPersonView == 2) return;
    if (data.players.empty()) return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImFont* font = ImGui::GetFont();
    if (!draw || !font) return;

    for (const auto& player : data.players) {
        if (player.distance < 0.05f) continue;
        if (player.distance > settings->renderDistance) continue;

        // Прямая проекция точки над головой игрока (для спящих/сидящих игроков учитываем полную высоту)
        double effHeight = (player.height >= 1.0) ? player.height : 1.8;
        // Поднимаем точку проекции ещё выше ванильного никнейма игрока (+1.45), чтобы NameTags всегда висел чисто НАД ником и не загораживал его
        double pHeight = effHeight + 1.45 - (player.isSneaking ? 0.2 : 0.0);
        Vec3 headPos = { player.pos.x, player.pos.y + pHeight, player.pos.z };
        Vec2 scHead;
        bool projected = JVMBridge::Get().WorldToScreen(headPos, scHead);
        if (!projected) {
            // Верхняя точка может пересечь near plane при приближении или
            // camera bob. Проецируем точки от головы до корпуса, сохраняя тег.
            const double fallbackHeights[] = { effHeight + 0.95, effHeight + 0.55, effHeight * 0.95, effHeight * 0.8, effHeight * 0.6, 0.2 };
            for (double h : fallbackHeights) {
                if (JVMBridge::Get().WorldToScreen({ player.pos.x, player.pos.y + h, player.pos.z }, scHead)) {
                    projected = true;
                    break;
                }
            }
        }
        if (!projected) continue;

        float center = scHead.x; // Центр игрока на экране строго по оси X
        float posY   = scHead.y; // Верхняя точка над головой на экране
        // Не прижимаем тег искусственно к верхней кромке экрана (posY = 28.0f), так как при взгляде под углом или на дистанции это смещает неймтег высоко в небо
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        if (center < -200.0f || center > disp.x + 200.0f || posY < -200.0f || posY > disp.y + 200.0f) continue;

        // Масштабирование шрифта
        float distFactor = std::max(0.7f, std::min(1.8f, (1.0f + player.distance * 0.015f)));
        float scale = settings->fontScale / distFactor;
        if (scale < 0.6f) scale = 0.6f;
        if (scale > 2.0f) scale = 2.0f;

        // Формирование строки тега: полное удаление §r и кодов форматирования
        std::string tagName;
        if (player.isBot) {
            tagName += "[BOT] ";
        }
        if (!player.rankPrefix.empty()) {
            tagName += CleanMinecraftCodes(player.rankPrefix) + " ";
        }
        if (player.isZoomed) {
            tagName += "[ZOOMED] ";
        }
        std::string realName = CleanMinecraftCodes((settings->rustexBypass && !player.realName.empty()) ? player.realName : player.name);
        // Защита от дублирования собственного ника: чужой игрок никогда не должен показываться под ником локального пользователя!
        if (!data.username.empty() && data.username != "Player" && data.username != "Unknown" && player.distance >= 0.8f) {
            std::string lowReal = realName;
            std::string lowUser = data.username;
            for (char& c : lowReal) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            for (char& c : lowUser) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (lowReal == lowUser) {
                realName = (!player.name.empty() && player.name != data.username && player.name != "?" && player.name != "Unknown") ? CleanMinecraftCodes(player.name) : "Player";
            }
        }
        if (realName.empty() || realName == "Player" || realName == "?" || realName == "Unknown") {
            if (!player.realName.empty() && player.realName != "Player" && player.realName != "?" && player.realName != "Unknown") realName = CleanMinecraftCodes(player.realName);
            else if (!player.name.empty() && player.name != "Player" && player.name != "?" && player.name != "Unknown") realName = CleanMinecraftCodes(player.name);
        }
        // Сначала используем распознанный RustEx ник, затем обычное имя
        // сущности. Не скрываем сам тег, если сервер временно отдаёт заглушку.
        if (realName.empty() || realName == "?" || realName == "Unknown") {
            realName = CleanMinecraftCodes(player.name);
        }
        if (realName.empty()) realName = "Player";
        tagName += realName;
        if (!player.clanSuffix.empty()) {
            tagName += " " + CleanMinecraftCodes(player.clanSuffix);
        }

        if (settings->showDistance) {
            char distBuf[32];
            snprintf(distBuf, sizeof(distBuf), "  %.0fm", player.distance);
            tagName += distBuf;
        }

        // Финальная очистка строки тега от любых оставшихся кодов форматирования
        tagName = CleanMinecraftCodes(tagName);

        float fontSize = font->FontSize * scale;
        ImVec2 nameTextSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, tagName.c_str());

        // Размеры подложки (строго по размеру очищенного ника, без мусорного "0" и лишних полос)
        float bgWidth = nameTextSize.x + 8.0f * scale;
        float bgHeight = nameTextSize.y + 5.0f * scale;

        // Позиция подложки: строго центрировано над головой игрока
        float bgX = center - bgWidth * 0.5f;
        float bgY = posY - (13.0f * scale);
        float endPosX = bgX + bgWidth;

        int alpha = static_cast<int>(settings->bgOpacity * 240.f);

        // 1. Мягкая внешняя тень
        draw->AddRect(
            ImVec2(bgX - 1.5f, bgY - 1.5f),
            ImVec2(bgX + bgWidth + 1.5f, bgY + bgHeight + 1.5f),
            IM_COL32(10, 12, 18, alpha / 2),
            6.0f * scale,
            0,
            2.0f
        );

        // 2. Полупрозрачный темный фон (bgColor = new Color(30, 30, 30, 230))
        draw->AddRectFilled(
            ImVec2(bgX, bgY),
            ImVec2(bgX + bgWidth, bgY + bgHeight),
            IM_COL32(30, 30, 30, alpha),
            6.0f * scale
        );

        bool isFriend = FriendManager::Get().IsFriend(realName);
        ImU32 borderColor = isFriend ? IM_COL32(75, 230, 110, static_cast<int>(alpha * 0.90f)) : IM_COL32(50, 110, 240, static_cast<int>(alpha * 0.85f));
        ImU32 nickColor = isFriend ? IM_COL32(85, 255, 120, 255) : IM_COL32(245, 245, 250, 255);

        // 3. Аккуратная рамка
        draw->AddRect(
            ImVec2(bgX, bgY),
            ImVec2(bgX + bgWidth, bgY + bgHeight),
            borderColor,
            6.0f * scale,
            0,
            1.2f
        );

        // 4. Текст ника: 1-в-1 Fonts.nunitoBold14.drawString(tagName, center - bgWidth / 2 + 2, posY - 11F, textColor)
        float textX = bgX + 4.0f * scale;
        float textY = bgY + 2.5f * scale;
        draw->AddText(font, fontSize, ImVec2(textX, textY), nickColor, tagName.c_str());

        // 5. Отрисовка предметов и зачарований над ником (1-в-1 как в Java NameTags)
        if (settings->showArmor) {
            struct DisplaySlotInfo {
                const ArmorItem* item;
                int armorSlot; // 0=шлем, 1=нагрудник, 2=поножи, 3=ботинки, -1=в руке/другое
            };
            std::vector<DisplaySlotInfo> stacks;

            // Приоритетно отображаем экипированную броню (Шлем, Нагрудник, Поножи, Ботинки) и предмет в руке
            for (int i = 0; i < 4; ++i) {
                if (!player.armor[i].isEmpty && (player.armor[i].hasIcon || player.armor[i].itemId > 0 || !player.armor[i].name.empty())) {
                    stacks.push_back({ &player.armor[i], i });
                }
            }
            if (!player.heldItem.isEmpty && (player.heldItem.hasIcon || player.heldItem.itemId > 0 || !player.heldItem.name.empty())) {
                stacks.push_back({ &player.heldItem, -1 });
            }
            if (stacks.empty()) {
                for (const auto& it : player.items) {
                    if (!it.isEmpty && (it.hasIcon || it.itemId > 0 || !it.name.empty())) {
                        stacks.push_back({ &it, -1 });
                    }
                }
            }

            if (!stacks.empty()) {
                float aScale = (settings->armorScale > 0.1f) ? settings->armorScale : 1.0f;
                float effScale = scale * aScale;
                // Предмет в руке важнее брони: увеличенный слот делает
                // серверную иконку различимой даже на обычном масштабе тега.
                const float heldBoost = 1.35f;
                float baseSlotSize = 20.0f * effScale;
                float baseSlotStep = 24.0f * effScale;
                float slotSize = baseSlotSize;
                float slotStep = baseSlotStep;
                if (std::any_of(stacks.begin(), stacks.end(), [](const DisplaySlotInfo& s) { return s.armorSlot < 0; })) {
                    slotSize *= heldBoost;
                    slotStep *= heldBoost;
                }
                float totalW = static_cast<float>(stacks.size() - 1) * slotStep + slotSize;

                for (size_t iterable = 0; iterable < stacks.size(); ++iterable) {
                    const auto& dSlot = stacks[iterable];
                    const auto* stack = dSlot.item;
                    const float itemSlotSize = (dSlot.armorSlot < 0) ? slotSize : baseSlotSize;
                    const float itemX = center - (totalW * 0.5f) + (static_cast<float>(iterable) * slotStep) + (slotSize - itemSlotSize) * 0.5f;
                    float itemY = posY - (35.0f * effScale);

                    // 1. Всегда рисуем аккуратную закругленную подложку слота
                    draw->AddRectFilled(
                        ImVec2(itemX, itemY),
                        ImVec2(itemX + itemSlotSize, itemY + itemSlotSize),
                        IM_COL32(18, 22, 32, alpha),
                        3.0f * effScale
                    );

                    // 2. Всегда приоритетно отрисовываем иконку текстуры из атласа / ресурс-пака
                    bool drawnIcon = false;
                    if (data.atlasTextureId > 0 && stack->hasIcon && (stack->maxU > stack->minU) && (stack->maxV > stack->minV)) {
                        draw->AddImage(
                            reinterpret_cast<ImTextureID>(static_cast<intptr_t>(data.atlasTextureId)),
                            ImVec2(itemX + 1.0f * effScale, itemY + 1.0f * effScale),
                            ImVec2(itemX + itemSlotSize - 1.0f * effScale, itemY + itemSlotSize - 1.0f * effScale),
                            ImVec2(stack->minU, stack->minV),
                            ImVec2(stack->maxU, stack->maxV)
                        );
                        drawnIcon = true;
                    } else {
                        GLuint aTex = ArmorTextures::GetTexture(dSlot.armorSlot, stack->name, stack->itemId);
                        if (aTex != 0) {
                            draw->AddImage(
                                reinterpret_cast<ImTextureID>(static_cast<intptr_t>(aTex)),
                                ImVec2(itemX + 1.0f * effScale, itemY + 1.0f * effScale),
                                ImVec2(itemX + itemSlotSize - 1.0f * effScale, itemY + itemSlotSize - 1.0f * effScale)
                            );
                            drawnIcon = true;
                        }
                    }

                    if (!drawnIcon) {
                        // Чистое имя без цветовых кодов
                        std::string cleanName = CleanMinecraftCodes(stack->name);
                        std::string cleanNoColor = "";
                        for (size_t ci = 0; ci < cleanName.size(); ++ci) {
                            unsigned char uch = static_cast<unsigned char>(cleanName[ci]);
                            if (uch == 0xC2 && ci + 1 < cleanName.size() && static_cast<unsigned char>(cleanName[ci + 1]) == 0xA7) {
                                if (ci + 2 < cleanName.size()) ci += 2; else ci += 1;
                                continue;
                            }
                            if (uch == 0xA7 || uch == '&') {
                                if (ci + 1 < cleanName.size()) ci += 1;
                                continue;
                            }
                            if (uch >= 32) cleanNoColor += cleanName[ci];
                        }
                        cleanName = cleanNoColor;

                        auto itCustom = CUSTOM_ITEM_IDS.find(stack->itemId);
                        if (itCustom != CUSTOM_ITEM_IDS.end() && (cleanName.empty() || cleanName.find("sosi") != std::string::npos)) {
                            cleanName = itCustom->second;
                        }
                        if (cleanName.empty() || cleanName == "f" || cleanName == "r" || cleanName == "§f") {
                            if (dSlot.armorSlot == 0) cleanName = "Шлем";
                            else if (dSlot.armorSlot == 1) cleanName = "Куртка";
                            else if (dSlot.armorSlot == 2) cleanName = "Штаны";
                            else if (dSlot.armorSlot == 3) cleanName = "Ботинки";
                            else {
                                switch (stack->itemId) {
                                    case 298: case 302: case 306: case 310: case 314: cleanName = "Шлем"; break;
                                    case 299: case 303: case 307: case 311: case 315: cleanName = "Куртка"; break;
                                    case 300: case 304: case 308: case 312: case 316: cleanName = "Штаны"; break;
                                    case 301: case 305: case 309: case 313: case 317: cleanName = "Ботинки"; break;
                                    case 276: case 267: case 272: case 268: case 283: cleanName = "Меч"; break;
                                    case 278: case 257: case 274: case 270: case 285: cleanName = "Кирка"; break;
                                    case 279: case 258: case 275: case 271: case 286: cleanName = "Топор"; break;
                                    case 261: cleanName = "Лук"; break;
                                    case 392: cleanName = "Картофель"; break;
                                    case 297: cleanName = "Хлеб"; break;
                                }
                            }
                        }

                        // Умное короткое обозначение для слота 16x16 без вылезания текста и без битого UTF-8
                        std::string label = "";
                        std::string lowerN = "";
                        for (char c : cleanName) lowerN += static_cast<char>(tolower(static_cast<unsigned char>(c)));

                        if (cleanName.find("Штан") != std::string::npos || cleanName.find("штан") != std::string::npos || cleanName.find("понож") != std::string::npos || lowerN.find("pant") != std::string::npos || lowerN.find("trouser") != std::string::npos || dSlot.armorSlot == 2) {
                            label = "Шт";
                        } else if (cleanName.find("Курт") != std::string::npos || cleanName.find("курт") != std::string::npos || cleanName.find("Рубах") != std::string::npos || cleanName.find("рубах") != std::string::npos || cleanName.find("нагруд") != std::string::npos || lowerN.find("shirt") != std::string::npos || lowerN.find("jacket") != std::string::npos || lowerN.find("torso") != std::string::npos || dSlot.armorSlot == 1) {
                            label = "Курт";
                        } else if (cleanName.find("Шлем") != std::string::npos || cleanName.find("шлем") != std::string::npos || cleanName.find("Маск") != std::string::npos || cleanName.find("маск") != std::string::npos || cleanName.find("Шапк") != std::string::npos || lowerN.find("mask") != std::string::npos || lowerN.find("helmet") != std::string::npos || dSlot.armorSlot == 0) {
                            label = "Шлем";
                        } else if (cleanName.find("Ботин") != std::string::npos || cleanName.find("ботин") != std::string::npos || cleanName.find("Обув") != std::string::npos || lowerN.find("boot") != std::string::npos || lowerN.find("shoe") != std::string::npos || dSlot.armorSlot == 3) {
                            label = "Бот";
                        } else if (cleanName.find("Копь") != std::string::npos || cleanName.find("копь") != std::string::npos || lowerN.find("spear") != std::string::npos) {
                            label = "Копь";
                        } else if (cleanName.find("Кирк") != std::string::npos || cleanName.find("кирк") != std::string::npos || lowerN.find("pickaxe") != std::string::npos) {
                            label = "Кирк";
                        } else if (cleanName.find("Топор") != std::string::npos || cleanName.find("топор") != std::string::npos || lowerN.find("axe") != std::string::npos || lowerN.find("hatchet") != std::string::npos) {
                            label = "Топ";
                        } else if (cleanName.find("Лук") != std::string::npos || cleanName.find("лук") != std::string::npos || lowerN.find("bow") != std::string::npos) {
                            label = "Лук";
                        } else if (cleanName.find("Меч") != std::string::npos || cleanName.find("меч") != std::string::npos || lowerN.find("sword") != std::string::npos || lowerN.find("machete") != std::string::npos) {
                            label = "Меч";
                        } else if (cleanName.find("Хазмат") != std::string::npos || lowerN.find("hazmat") != std::string::npos) {
                            label = "Хазм";
                        } else if (!cleanName.empty()) {
                            label = GetUtf8SafePrefix(cleanName, 3);
                        }

                        if (!label.empty() && label.find("§") == std::string::npos && label.find("\xA7") == std::string::npos && label != "&f" && label != "f" && label != "r") {
                            ImVec2 tSz = font->CalcTextSizeA(fontSize * 0.65f * aScale, FLT_MAX, 0.0f, label.c_str());
                            float tx = itemX + (slotSize - tSz.x) * 0.5f;
                            float ty = itemY + (slotSize - tSz.y) * 0.5f;
                            draw->AddText(font, fontSize * 0.65f * aScale, ImVec2(tx, ty), IM_COL32(230, 235, 245, 255), label.c_str());
                        }
                    }

                    // 3. Полоска прочности предмета 1-в-1 с renderItemOverlays
                    if (stack->maxDamage > 0 && stack->durability < 0.999f) {
                        float dur = std::clamp(stack->durability, 0.0f, 1.0f);
                        float barX = itemX + 1.5f * effScale;
                        float barY = itemY + slotSize - 2.5f * effScale;
                        float barW = slotSize - 3.0f * effScale;
                        float barH = 2.0f * effScale;
                        float fillW = barW * dur;

                        int r = static_cast<int>((1.0f - dur) * 255.0f);
                        int g = static_cast<int>(dur * 255.0f);
                        ImU32 durCol = IM_COL32(r, g, 0, 255);

                        draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH), IM_COL32(0, 0, 0, 255));
                        draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fillW, barY + barH - 0.5f * effScale), durCol);
                    }

                    // 4. Количество предметов в стаке (если > 1)
                    if (stack->count > 1) {
                        char cntStr[16];
                        snprintf(cntStr, sizeof(cntStr), "%d", stack->count);
                        float cntFontSize = fontSize * 0.65f * aScale;
                        ImVec2 cntSz = font->CalcTextSizeA(cntFontSize, FLT_MAX, 0.0f, cntStr);
                        float cntX = itemX + slotSize - cntSz.x;
                        float cntY = itemY + slotSize - cntSz.y - 1.0f * effScale;
                        draw->AddText(font, cntFontSize, ImVec2(cntX + 1.0f, cntY + 1.0f), IM_COL32(0, 0, 0, 255), cntStr);
                        draw->AddText(font, cntFontSize, ImVec2(cntX, cntY), IM_COL32(255, 255, 255, 255), cntStr);
                    }

                    // Зачарования над предметом:
                    float enchCenterX = itemX + slotSize * 0.5f;
                    float enchBaseY = posY - (37.5f * effScale) - (6.0f * effScale);
                    float enchFontSize = fontSize * 0.75f * aScale;

                    for (size_t i = 0; i < stack->enchantments.size(); ++i) {
                        const auto& ench = stack->enchantments[i];
                        ImVec2 eSize = font->CalcTextSizeA(enchFontSize, FLT_MAX, 0.0f, ench.c_str());
                        float eX = enchCenterX - eSize.x * 0.5f;
                        float eY = enchBaseY - (static_cast<float>(i) * 8.5f * scale);

                        // Тень
                        draw->AddText(font, enchFontSize, ImVec2(eX + 1.0f, eY + 1.0f), IM_COL32(0, 0, 0, 220), ench.c_str());
                        draw->AddText(font, enchFontSize, ImVec2(eX, eY), IM_COL32(255, 255, 255, 255), ench.c_str());
                    }
                }
            }
        }

        // 7. Отрисовка активных зелий справа от игрока:
        // Fonts.nunitoBold14.drawString(power, endPosX + 5.0F, posY + 10.0F + startY, -1);
        if (!player.potions.empty()) {
            float potX = endPosX + 5.0f * scale;
            float potY = posY + 10.0f * scale;
            float potFontSize = fontSize * 0.82f;

            for (const auto& potion : player.potions) {
                // Тень
                draw->AddText(font, potFontSize, ImVec2(potX + 1.0f, potY + 1.0f), IM_COL32(0, 0, 0, 200), potion.name.c_str());
                draw->AddText(font, potFontSize, ImVec2(potX, potY), potion.color, potion.name.c_str());
                potY += 10.5f * scale;
            }
        }
    }
}
