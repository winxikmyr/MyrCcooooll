#pragma once
#include <string>
#include <GL/gl.h>
#include "armor_icons.h"

class ArmorTextures {
public:
    static void Init() {
        if (s_loaded) return;

        s_textures[0]  = CreateTexture(ArmorIcons::diamond_helmet);
        s_textures[1]  = CreateTexture(ArmorIcons::diamond_chestplate);
        s_textures[2]  = CreateTexture(ArmorIcons::diamond_leggings);
        s_textures[3]  = CreateTexture(ArmorIcons::diamond_boots);

        s_textures[4]  = CreateTexture(ArmorIcons::iron_helmet);
        s_textures[5]  = CreateTexture(ArmorIcons::iron_chestplate);
        s_textures[6]  = CreateTexture(ArmorIcons::iron_leggings);
        s_textures[7]  = CreateTexture(ArmorIcons::iron_boots);

        s_textures[8]  = CreateTexture(ArmorIcons::gold_helmet);
        s_textures[9]  = CreateTexture(ArmorIcons::gold_chestplate);
        s_textures[10] = CreateTexture(ArmorIcons::gold_leggings);
        s_textures[11] = CreateTexture(ArmorIcons::gold_boots);

        s_textures[12] = CreateTexture(ArmorIcons::chainmail_helmet);
        s_textures[13] = CreateTexture(ArmorIcons::chainmail_chestplate);
        s_textures[14] = CreateTexture(ArmorIcons::chainmail_leggings);
        s_textures[15] = CreateTexture(ArmorIcons::chainmail_boots);

        s_textures[16] = CreateTexture(ArmorIcons::leather_helmet);
        s_textures[17] = CreateTexture(ArmorIcons::leather_chestplate);
        s_textures[18] = CreateTexture(ArmorIcons::leather_leggings);
        s_textures[19] = CreateTexture(ArmorIcons::leather_boots);

        s_loaded = true;
    }

    static GLuint GetTexture(int slot, const std::string& name, int itemId = 0) {
        if (!s_loaded) Init();

        // 1. Определение материала: Diamond=0, Iron=1, Gold=2, Chain=3, Leather=4
        int mat = -1;
        if (itemId >= 310 && itemId <= 313) mat = 0;
        else if (itemId >= 306 && itemId <= 309) mat = 1;
        else if (itemId >= 314 && itemId <= 317) mat = 2;
        else if (itemId >= 302 && itemId <= 305) mat = 3;
        else if (itemId >= 298 && itemId <= 301) mat = 4;

        if (mat == -1) {
            std::string lower = name;
            for (char& c : lower) c = static_cast<char>(tolower(c));

            if (lower.find("diamond") != std::string::npos || lower.find("алмаз") != std::string::npos) mat = 0;
            else if (lower.find("iron") != std::string::npos || lower.find("желез") != std::string::npos || lower.find("metal") != std::string::npos) mat = 1;
            else if (lower.find("gold") != std::string::npos || lower.find("золот") != std::string::npos) mat = 2;
            else if (lower.find("chain") != std::string::npos || lower.find("кольчуж") != std::string::npos) mat = 3;
            else if (lower.find("leather") != std::string::npos || lower.find("кожан") != std::string::npos || lower.find("мешковин") != std::string::npos || lower.find("burlap") != std::string::npos) mat = 4;
            else mat = 1; // По умолчанию железная броня
        }

        // 2. Определение части брони: Helmet=0, Chest=1, Legs=2, Boots=3
        int piece = -1;
        if (slot >= 0 && slot < 4) {
            piece = slot;
        } else {
            if (itemId == 310 || itemId == 306 || itemId == 314 || itemId == 302 || itemId == 298) piece = 0;
            else if (itemId == 311 || itemId == 307 || itemId == 315 || itemId == 303 || itemId == 299) piece = 1;
            else if (itemId == 312 || itemId == 308 || itemId == 316 || itemId == 304 || itemId == 300) piece = 2;
            else if (itemId == 313 || itemId == 309 || itemId == 317 || itemId == 305 || itemId == 301) piece = 3;

            if (piece == -1) {
                std::string lower = name;
                for (char& c : lower) c = static_cast<char>(tolower(c));

                if (lower.find("helmet") != std::string::npos || lower.find("шлем") != std::string::npos || lower.find("mask") != std::string::npos || lower.find("маск") != std::string::npos || lower.find("шапк") != std::string::npos || lower.find("каск") != std::string::npos) piece = 0;
                else if (lower.find("chestplate") != std::string::npos || lower.find("нагрудник") != std::string::npos || lower.find("куртк") != std::string::npos || lower.find("jacket") != std::string::npos || lower.find("shirt") != std::string::npos || lower.find("рубах") != std::string::npos) piece = 1;
                else if (lower.find("leggings") != std::string::npos || lower.find("понож") != std::string::npos || lower.find("штан") != std::string::npos || lower.find("pants") != std::string::npos || lower.find("trouser") != std::string::npos || lower.find("брюк") != std::string::npos) piece = 2;
                else if (lower.find("boots") != std::string::npos || lower.find("ботин") != std::string::npos || lower.find("сапог") != std::string::npos || lower.find("shoes") != std::string::npos) piece = 3;
            }
        }

        if (piece < 0 || piece > 3) return 0;
        int idx = mat * 4 + piece;
        if (idx >= 0 && idx < 20) return s_textures[idx];
        return 0;
    }

private:
    static inline GLuint CreateTexture(const uint32_t* rgba) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        return tex;
    }

    static inline bool s_loaded = false;
    static inline GLuint s_textures[20] = { 0 };
};
