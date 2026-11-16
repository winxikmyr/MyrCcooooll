// src/gui/clickgui.h
#pragma once
#include <vector>
#include <memory>
#include <imgui.h>
#include "../modules/module_base.h"
#include "../modules/module_hud.h"
#include "../modules/module_armorhud.h"
#include "../modules/module_viewmodel.h"
#include "../modules/module_itemesp.h"
#include "../modules/module_nametags.h"
#include "../modules/module_automine.h"
#include "../modules/module_guiwalk.h"
#include "../modules/module_mcf.h"
#include "../modules/module_aimbot.h"
#include "../modules/module_arrows.h"
#include "../modules/module_aspect.h"
#include "../modules/module_screenprotect.h"
#include "../modules/module_wallhack_player.h"
#include "../modules/module_wallhack.h"
#include "../modules/module_blockesp.h"
#include "../modules/module_tracers.h"
#include "../modules/module_titles.h"
#include "../modules/module_nameprotect.h"
#include "../modules/module_crosshair.h"
#include "../modules/friend_manager.h"

class ClickGUI {
public:
    ClickGUI();
    void Draw(float animProgress = 1.0f);

    bool IsHUDEnabled() const;
    bool IsArmorHUDEnabled() const;
    bool IsViewModelEnabled() const;
    bool IsItemESPEnabled() const;
    bool IsNameTagsEnabled() const;
    bool IsAutoMineEnabled() const;
    bool IsGuiWalkEnabled() const;
    bool IsMCFEnabled() const;
    bool IsAimBotEnabled() const;
    bool IsArrowsEnabled() const;
    bool IsAspectEnabled() const;
    bool IsScreenProtectEnabled() const;
    bool IsWallHackPlayerEnabled() const;
    bool IsWallHackEnabled() const;
    bool IsBlockESPEnabled() const;
    bool IsTracersEnabled() const;
    bool IsTitlesEnabled() const;
    bool IsNameProtectEnabled() const;
    bool IsCrosshairEnabled() const;

    ModuleHUD*            GetHUDModule() const;
    ModuleArmorHUD*       GetArmorHUDModule() const;
    ModuleViewModel*      GetViewModelModule() const;
    ModuleItemESP*        GetItemESPModule() const;
    ModuleNameTags*       GetNameTagsModule() const;
    ModuleAutoMine*       GetAutoMineModule() const;
    ModuleGuiWalk*        GetGuiWalkModule() const;
    ModuleMCF*            GetMCFModule() const;
    ModuleAimBot*         GetAimBotModule() const;
    ModuleArrows*         GetArrowsModule() const;
    ModuleAspect*         GetAspectModule() const;
    ModuleScreenProtect*  GetScreenProtectModule() const;
    ModuleWallHackPlayer* GetWallHackPlayerModule() const;
    ModuleWallHack*       GetWallHackModule() const;
    ModuleBlockESP*       GetBlockESPModule() const;
    ModuleTracers*        GetTracersModule() const;
    ModuleTitles*         GetTitlesModule() const;
    ModuleNameProtect*    GetNameProtectModule() const;
    ModuleCrosshair*      GetCrosshairModule() const;

    const std::vector<std::unique_ptr<ModuleBase>>& Modules() const { return m_modules; }

    ModuleBase* GetBindingModule() const { return m_bindingModule; }
    void        SetBindingModule(ModuleBase* mod) { m_bindingModule = mod; }

    float GetGUIScale() const { return m_guiScale; }
    void  SetGUIScale(float s) { m_guiScale = s; }

    int   GetAccentIndex() const { return m_accentIndex; }
    void  SetAccentIndex(int idx) { m_accentIndex = idx; }

    int   GetThemeIndex() const { return 0; }
    void  SetThemeIndex(int) {}

    bool  IsKeybindsOpen() const { return false; }
    void  SetKeybindsOpen(bool) {}

    bool  IsSpectatorsOpen() const { return false; }
    void  SetSpectatorsOpen(bool) {}

    ImU32 GetPrimaryAccentColor() const;
    ImU32 GetSecondaryAccentColor() const;

    bool  IsCompactMode() const { return m_compactMode; }
    void  SetCompactMode(bool v) { m_compactMode = v; }

    bool  IsSoftGlow() const { return m_softGlow; }
    void  SetSoftGlow(bool v) { m_softGlow = v; }

private:
    void DrawTopBar(float s, float winW, float winH, const ImVec2& winPos);
    void DrawSidebar(float s, float winW, float winH, const ImVec2& winPos);
    void DrawContent(float s, float winW, float winH, const ImVec2& winPos);
    void DrawModuleSettings(ModuleBase* mod, float s);
    void DrawSettingsModal(float s, float winW, float winH, const ImVec2& winPos);
    void DrawFooter(float s, float winW, float winH, const ImVec2& winPos);

    std::vector<std::unique_ptr<ModuleBase>> m_modules;
    ModuleBase* m_bindingModule = nullptr;
    ModuleBase* m_selectedModule = nullptr;
    Category    m_activeCat = Category::COMBAT;
    bool        m_showSettings = false;
    float       m_guiScale = 1.0f;
    int         m_accentIndex = 0; // 0=Violet, 1=Blue, 2=Mint, 3=Rose
    bool        m_compactMode = false;
    bool        m_softGlow = true;
    char        m_searchQuery[64] = "";
};

extern bool g_hasIconFont;
