// src/gui/config_manager.cpp
#include "config_manager.h"
#include "clickgui.h"
#include <Windows.h>
#include <fstream>
#include <sstream>
#include <map>
#include <filesystem>

extern HMODULE g_hSelf;

std::string ConfigManager::GetConfigDirectory() {
    char tempPath[MAX_PATH] = { 0 };
    if (GetTempPathA(MAX_PATH, tempPath) > 0) {
        std::string targetDir = std::string(tempPath) + "GOOOGLECHROOME11";
        CreateDirectoryA(targetDir.c_str(), nullptr);

        // Миграция старых конфигов из myr_configs или локальной папки, чтобы настройки не пропали
        try {
            std::string prevDir = std::string(tempPath) + "myr_configs";
            if (std::filesystem::exists(prevDir)) {
                for (const auto& entry : std::filesystem::directory_iterator(prevDir)) {
                    if (entry.path().extension() == ".cfg") {
                        std::string dest = targetDir + "\\" + entry.path().filename().string();
                        if (!std::filesystem::exists(dest)) {
                            std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::skip_existing);
                        }
                    }
                }
            }

            char dllPath[MAX_PATH] = { 0 };
            if (GetModuleFileNameA(GetModuleHandleA("MyrLegit.dll"), dllPath, MAX_PATH) > 0) {
                std::string fullPath(dllPath);
                size_t pos = fullPath.find_last_of("\\/");
                if (pos != std::string::npos) {
                    std::string oldDir = fullPath.substr(0, pos) + "\\configs";
                    if (std::filesystem::exists(oldDir)) {
                        for (const auto& entry : std::filesystem::directory_iterator(oldDir)) {
                            if (entry.path().extension() == ".cfg") {
                                std::string dest = targetDir + "\\" + entry.path().filename().string();
                                if (!std::filesystem::exists(dest)) {
                                    std::filesystem::copy_file(entry.path(), dest, std::filesystem::copy_options::skip_existing);
                                }
                            }
                        }
                    }
                }
            }
        } catch (...) {}

        return targetDir;
    }

    CreateDirectoryA("configs", nullptr);
    return "configs";
}

std::vector<std::string> ConfigManager::GetConfigList() {
    std::vector<std::string> res;
    std::string dir = GetConfigDirectory();
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((dir + "\\*.cfg").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::string fname = fd.cFileName;
                size_t p = fname.rfind(".cfg");
                if (p != std::string::npos) {
                    res.push_back(fname.substr(0, p));
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
    return res;
}

std::string ConfigManager::SerializeToString(const ClickGUI* gui) {
    if (!gui) return "";
    std::ostringstream ss;

    // Сохраняем все модули: Enabled, Keybind
    for (const auto& mod : gui->Modules()) {
        if (!mod) continue;
        ss << "[" << mod->Name() << "]\n";
        ss << "enabled=" << (mod->IsEnabled() ? 1 : 0) << "\n";
        ss << "key=" << mod->GetKey() << "\n";
    }

    // Сохраняем подробные настройки модулей
    auto* aim = gui->GetAimBotModule();
    if (aim) {
        ss << "[AimBot_Settings]\n";
        ss << "fov=" << aim->fov << "\n";
        ss << "range=" << aim->range << "\n";
        ss << "smoothness=" << aim->smoothness << "\n";
        ss << "predict=" << aim->predict << "\n";
        ss << "vPredict=" << aim->vPredict << "\n";
        ss << "bodyAim=" << (aim->bodyAim ? 1 : 0) << "\n";
        ss << "drawFov=" << (aim->drawFov ? 1 : 0) << "\n";
        ss << "fovR=" << aim->fovColor[0] << "\n";
        ss << "fovG=" << aim->fovColor[1] << "\n";
        ss << "fovB=" << aim->fovColor[2] << "\n";
        ss << "fovA=" << aim->fovColor[3] << "\n";
        ss << "walls=" << (aim->walls ? 1 : 0) << "\n";
        ss << "autoShoot=" << (aim->autoShoot ? 1 : 0) << "\n";
        ss << "bots=" << (aim->bots ? 1 : 0) << "\n";
    }

    auto* wh = gui->GetWallHackModule();
    if (wh) {
        ss << "[WallHack_Settings]\n";
        ss << "player=" << (wh->playerMode ? 1 : 0) << "\n";
        ss << "mob=" << (wh->mobMode ? 1 : 0) << "\n";
        ss << "animal=" << (wh->animalMode ? 1 : 0) << "\n";
        ss << "box3D=" << (wh->box3D ? 1 : 0) << "\n";
        ss << "chams=" << (wh->chams ? 1 : 0) << "\n";
        ss << "opacity=" << wh->opacity << "\n";
        ss << "lineWidth=" << wh->lineWidth << "\n";
    }

    auto* nt = gui->GetNameTagsModule();
    if (nt) {
        ss << "[NameTags_Settings]\n";
        ss << "renderDistance=" << nt->renderDistance << "\n";
        ss << "fontScale=" << nt->fontScale << "\n";
        ss << "armorScale=" << nt->armorScale << "\n";
        ss << "bgOpacity=" << nt->bgOpacity << "\n";
        ss << "showPlayers=" << (nt->showPlayers ? 1 : 0) << "\n";
        ss << "showArmor=" << (nt->showArmor ? 1 : 0) << "\n";
        ss << "showHealth=" << (nt->showHealth ? 1 : 0) << "\n";
        ss << "showDistance=" << (nt->showDistance ? 1 : 0) << "\n";
        ss << "showMobs=" << (nt->showMobs ? 1 : 0) << "\n";
        ss << "rustexBypass=" << (nt->rustexBypass ? 1 : 0) << "\n";
    }

    auto* itemEsp = gui->GetItemESPModule();
    if (itemEsp) {
        ss << "[ItemESP_Settings]\n";
        ss << "range=" << itemEsp->range << "\n";
        ss << "showGround=" << (itemEsp->showGroundItems ? 1 : 0) << "\n";
        ss << "drawIcon=" << (itemEsp->drawIcon ? 1 : 0) << "\n";
        ss << "drawName=" << (itemEsp->drawName ? 1 : 0) << "\n";
        ss << "drawDist=" << (itemEsp->drawDistance ? 1 : 0) << "\n";
        ss << "drawBox=" << (itemEsp->drawBox ? 1 : 0) << "\n";
        ss << "drawIconOnly=" << (itemEsp->drawIconOnly ? 1 : 0) << "\n";
        ss << "iconSize=" << itemEsp->iconSize << "\n";
        ss << "labelScale=" << itemEsp->labelScaleMul << "\n";
    }

    auto* be = gui->GetBlockESPModule();
    if (be) {
        ss << "[BlockESP_Settings]\n";
        ss << "range=" << be->range << "\n";
        ss << "ores=" << (be->ores ? 1 : 0) << "\n";
        ss << "food=" << (be->food ? 1 : 0) << "\n";
        ss << "tracers=" << (be->tracers ? 1 : 0) << "\n";
    }

    auto* ah = gui->GetArmorHUDModule();
    if (ah) {
        ss << "[ArmorHUD_Settings]\n";
        ss << "scale=" << ah->scale << "\n";
        ss << "armorPosY=" << ah->armorPosY << "\n";
    }

    auto* vm = gui->GetViewModelModule();
    if (vm) {
        ss << "[ViewModel_Settings]\n";
        ss << "rightX=" << vm->rightX << "\n";
        ss << "rightY=" << vm->rightY << "\n";
        ss << "rightZ=" << vm->rightZ << "\n";
        ss << "rightScale=" << vm->rightScale << "\n";
        ss << "leftX=" << vm->leftX << "\n";
        ss << "leftY=" << vm->leftY << "\n";
        ss << "leftZ=" << vm->leftZ << "\n";
        ss << "leftScale=" << vm->leftScale << "\n";
    }

    auto* asp = gui->GetAspectModule();
    if (asp) {
        ss << "[AspectRatio_Settings]\n";
        ss << "aspect=" << asp->aspectRatio << "\n";
    }

    auto* tit = gui->GetTitlesModule();
    if (tit) {
        ss << "[Titles_Settings]\n";
        ss << "mashtab=" << tit->mashtab << "\n";
    }

    auto* hud = gui->GetHUDModule();
    if (hud) {
        ss << "[HUD_Settings]\n";
        ss << "posX=" << hud->posX << "\n";
        ss << "posY=" << hud->posY << "\n";
        ss << "showUser=" << (hud->showUser ? 1 : 0) << "\n";
        ss << "showFPS=" << (hud->showFPS ? 1 : 0) << "\n";
        ss << "showPing=" << (hud->showPing ? 1 : 0) << "\n";
    }

    auto* arr = gui->GetArrowsModule();
    if (arr) {
        ss << "[Arrows_Settings]\n";
        ss << "arrowRadius=" << arr->arrowRadius << "\n";
        ss << "textRadius=" << arr->textRadius << "\n";
        ss << "showDistance=" << (arr->showDistance ? 1 : 0) << "\n";
        ss << "onlyOffscreen=" << (arr->onlyOffscreen ? 1 : 0) << "\n";
        ss << "ignoreBot=" << (arr->ignoreBot ? 1 : 0) << "\n";
    }

    auto* np = gui->GetNameProtectModule();
    if (np) {
        ss << "[NameProtect_Settings]\n";
        ss << "nick=" << np->protectedNick << "\n";
        ss << "hideFriends=" << (np->hideFriends ? 1 : 0) << "\n";
    }

    auto* ch = gui->GetCrosshairModule();
    if (ch) {
        ss << "[Crosshair_Settings]\n";
        ss << "size=" << ch->size << "\n";
        ss << "gap=" << ch->gap << "\n";
        ss << "thickness=" << ch->thickness << "\n";
        ss << "dot=" << (ch->dot ? 1 : 0) << "\n";
    }

    ss << "[ClickGUI_Appearance]\n";
    ss << "scale=" << gui->GetGUIScale() << "\n";
    ss << "accent=" << gui->GetAccentIndex() << "\n";
    ss << "theme=" << gui->GetThemeIndex() << "\n";
    ss << "compact=" << (gui->IsCompactMode() ? 1 : 0) << "\n";
    ss << "glow=" << (gui->IsSoftGlow() ? 1 : 0) << "\n";
    ss << "keybinds=" << (gui->IsKeybindsOpen() ? 1 : 0) << "\n";
    ss << "spectators=" << (gui->IsSpectatorsOpen() ? 1 : 0) << "\n";

    return ss.str();
}

bool ConfigManager::DeserializeFromString(const std::string& data, ClickGUI* gui) {
    if (!gui || data.empty()) return false;

    std::istringstream stream(data);
    std::string line;
    std::string curSection;
    std::map<std::string, std::map<std::string, std::string>> kv;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.back() == '\r') line.pop_back();

        if (line.front() == '[' && line.back() == ']') {
            curSection = line.substr(1, line.size() - 2);
            continue;
        }

        size_t eq = line.find('=');
        if (eq != std::string::npos && !curSection.empty()) {
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            kv[curSection][k] = v;
        }
    }

    // Применяем настройки модулей и бинды
    for (const auto& mod : gui->Modules()) {
        if (!mod) continue;
        auto secIt = kv.find(mod->Name());
        if (secIt != kv.end()) {
            auto& m = secIt->second;
            if (m.find("enabled") != m.end()) {
                mod->SetEnabled(std::stoi(m["enabled"]) != 0);
            }
            if (m.find("key") != m.end()) {
                mod->SetKey(std::stoi(m["key"]));
            }
        }
    }

    // AimBot
    auto* aim = gui->GetAimBotModule();
    if (aim && kv.count("AimBot_Settings")) {
        auto& s = kv["AimBot_Settings"];
        if (s.count("fov")) aim->fov = std::stof(s["fov"]);
        if (s.count("range")) aim->range = std::stof(s["range"]);
        if (s.count("smoothness")) aim->smoothness = std::clamp(std::stof(s["smoothness"]), 1.0f, 7.0f);
        if (s.count("predict")) aim->predict = std::stof(s["predict"]);
        if (s.count("vPredict")) aim->vPredict = std::clamp(std::stof(s["vPredict"]), 0.0f, 2.0f);
        if (s.count("bodyAim")) aim->bodyAim = (std::stoi(s["bodyAim"]) != 0);
        if (s.count("drawFov")) aim->drawFov = (std::stoi(s["drawFov"]) != 0);
        if (s.count("fovR")) aim->fovColor[0] = std::stof(s["fovR"]);
        if (s.count("fovG")) aim->fovColor[1] = std::stof(s["fovG"]);
        if (s.count("fovB")) aim->fovColor[2] = std::stof(s["fovB"]);
        if (s.count("fovA")) aim->fovColor[3] = std::stof(s["fovA"]);
        if (s.count("walls")) aim->walls = (std::stoi(s["walls"]) != 0);
        if (s.count("autoShoot")) aim->autoShoot = (std::stoi(s["autoShoot"]) != 0);
        if (s.count("bots")) aim->bots = (std::stoi(s["bots"]) != 0);
    }

    // WallHack
    auto* wh = gui->GetWallHackModule();
    if (wh && kv.count("WallHack_Settings")) {
        auto& s = kv["WallHack_Settings"];
        if (s.count("player")) wh->playerMode = (std::stoi(s["player"]) != 0);
        if (s.count("mob")) wh->mobMode = (std::stoi(s["mob"]) != 0);
        if (s.count("animal")) wh->animalMode = (std::stoi(s["animal"]) != 0);
        if (s.count("box3D")) wh->box3D = (std::stoi(s["box3D"]) != 0);
        if (s.count("chams")) wh->chams = (std::stoi(s["chams"]) != 0);
        if (s.count("opacity")) wh->opacity = std::stof(s["opacity"]);
        if (s.count("lineWidth")) wh->lineWidth = std::stof(s["lineWidth"]);
    }

    // NameTags
    auto* nt = gui->GetNameTagsModule();
    if (nt && kv.count("NameTags_Settings")) {
        auto& s = kv["NameTags_Settings"];
        if (s.count("renderDistance")) nt->renderDistance = std::stof(s["renderDistance"]);
        if (s.count("fontScale")) nt->fontScale = std::stof(s["fontScale"]);
        if (s.count("armorScale")) nt->armorScale = std::stof(s["armorScale"]);
        if (s.count("bgOpacity")) nt->bgOpacity = std::stof(s["bgOpacity"]);
        if (s.count("showPlayers")) nt->showPlayers = (std::stoi(s["showPlayers"]) != 0);
        if (s.count("showArmor")) nt->showArmor = (std::stoi(s["showArmor"]) != 0);
        if (s.count("showHealth")) nt->showHealth = (std::stoi(s["showHealth"]) != 0);
        if (s.count("showDistance")) nt->showDistance = (std::stoi(s["showDistance"]) != 0);
        if (s.count("showMobs")) nt->showMobs = (std::stoi(s["showMobs"]) != 0);
        if (s.count("rustexBypass")) nt->rustexBypass = (std::stoi(s["rustexBypass"]) != 0);
    }

    // ItemESP
    auto* itemEsp = gui->GetItemESPModule();
    if (itemEsp && kv.count("ItemESP_Settings")) {
        auto& s = kv["ItemESP_Settings"];
        if (s.count("range")) itemEsp->range = std::stof(s["range"]);
        if (s.count("showGround")) itemEsp->showGroundItems = (std::stoi(s["showGround"]) != 0);
        if (s.count("drawIcon")) itemEsp->drawIcon = (std::stoi(s["drawIcon"]) != 0);
        if (s.count("drawName")) itemEsp->drawName = (std::stoi(s["drawName"]) != 0);
        if (s.count("drawDist")) itemEsp->drawDistance = (std::stoi(s["drawDist"]) != 0);
        if (s.count("drawBox")) itemEsp->drawBox = (std::stoi(s["drawBox"]) != 0);
        if (s.count("drawIconOnly")) itemEsp->drawIconOnly = (std::stoi(s["drawIconOnly"]) != 0);
        if (s.count("iconSize")) itemEsp->iconSize = std::stof(s["iconSize"]);
        if (s.count("labelScale")) itemEsp->labelScaleMul = std::stof(s["labelScale"]);
    }

    // BlockESP
    auto* be = gui->GetBlockESPModule();
    if (be && kv.count("BlockESP_Settings")) {
        auto& s = kv["BlockESP_Settings"];
        if (s.count("range")) be->range = std::stof(s["range"]);
        if (s.count("ores")) be->ores = (std::stoi(s["ores"]) != 0);
        if (s.count("food")) be->food = (std::stoi(s["food"]) != 0);
        if (s.count("tracers")) be->tracers = (std::stoi(s["tracers"]) != 0);
    }

    // ArmorHUD
    auto* ah = gui->GetArmorHUDModule();
    if (ah && kv.count("ArmorHUD_Settings")) {
        auto& s = kv["ArmorHUD_Settings"];
        if (s.count("scale")) ah->scale = std::stof(s["scale"]);
        if (s.count("armorPosY")) ah->armorPosY = std::stof(s["armorPosY"]);
    }

    // ViewModel
    auto* vm = gui->GetViewModelModule();
    if (vm && kv.count("ViewModel_Settings")) {
        auto& s = kv["ViewModel_Settings"];
        if (s.count("rightX")) vm->rightX = std::stof(s["rightX"]);
        if (s.count("rightY")) vm->rightY = std::stof(s["rightY"]);
        if (s.count("rightZ")) vm->rightZ = std::stof(s["rightZ"]);
        if (s.count("rightScale")) vm->rightScale = std::stof(s["rightScale"]);
        if (s.count("leftX")) vm->leftX = std::stof(s["leftX"]);
        if (s.count("leftY")) vm->leftY = std::stof(s["leftY"]);
        if (s.count("leftZ")) vm->leftZ = std::stof(s["leftZ"]);
        if (s.count("leftScale")) vm->leftScale = std::stof(s["leftScale"]);
    }

    // AspectRatio
    auto* asp = gui->GetAspectModule();
    if (asp && kv.count("AspectRatio_Settings")) {
        auto& s = kv["AspectRatio_Settings"];
        if (s.count("aspect")) asp->aspectRatio = std::stof(s["aspect"]);
    }

    // Titles
    auto* tit = gui->GetTitlesModule();
    if (tit && kv.count("Titles_Settings")) {
        auto& s = kv["Titles_Settings"];
        if (s.count("mashtab")) tit->mashtab = std::stof(s["mashtab"]);
    }

    // HUD
    auto* hud = gui->GetHUDModule();
    if (hud && kv.count("HUD_Settings")) {
        auto& s = kv["HUD_Settings"];
        if (s.count("posX")) hud->posX = std::stof(s["posX"]);
        if (s.count("posY")) hud->posY = std::stof(s["posY"]);
        if (s.count("showUser")) hud->showUser = (std::stoi(s["showUser"]) != 0);
        if (s.count("showFPS")) hud->showFPS = (std::stoi(s["showFPS"]) != 0);
        if (s.count("showPing")) hud->showPing = (std::stoi(s["showPing"]) != 0);
    }

    // Arrows
    auto* arr = gui->GetArrowsModule();
    if (arr && kv.count("Arrows_Settings")) {
        auto& s = kv["Arrows_Settings"];
        if (s.count("arrowRadius")) arr->arrowRadius = std::stof(s["arrowRadius"]);
        if (s.count("textRadius")) arr->textRadius = std::stof(s["textRadius"]);
        if (s.count("showDistance")) arr->showDistance = (std::stoi(s["showDistance"]) != 0);
        if (s.count("onlyOffscreen")) arr->onlyOffscreen = (std::stoi(s["onlyOffscreen"]) != 0);
        if (s.count("ignoreBot")) arr->ignoreBot = (std::stoi(s["ignoreBot"]) != 0);
    }

    // NameProtect
    auto* np = gui->GetNameProtectModule();
    if (np && kv.count("NameProtect_Settings")) {
        auto& s = kv["NameProtect_Settings"];
        if (s.count("nick")) np->protectedNick = s["nick"];
        if (s.count("hideFriends")) np->hideFriends = (std::stoi(s["hideFriends"]) != 0);
    }

    // Crosshair
    auto* ch = gui->GetCrosshairModule();
    if (ch && kv.count("Crosshair_Settings")) {
        auto& s = kv["Crosshair_Settings"];
        if (s.count("size")) ch->size = std::stof(s["size"]);
        if (s.count("gap")) ch->gap = std::stof(s["gap"]);
        if (s.count("thickness")) ch->thickness = std::stof(s["thickness"]);
        if (s.count("dot")) ch->dot = (std::stoi(s["dot"]) != 0);
    }

    // ClickGUI Appearance
    if (kv.count("ClickGUI_Appearance")) {
        auto& s = kv["ClickGUI_Appearance"];
        if (s.count("scale")) gui->SetGUIScale(std::stof(s["scale"]));
        if (s.count("accent")) gui->SetAccentIndex(std::stoi(s["accent"]));
        if (s.count("theme")) gui->SetThemeIndex(std::stoi(s["theme"]));
        if (s.count("compact")) gui->SetCompactMode(s["compact"] == "1");
        if (s.count("glow")) gui->SetSoftGlow(s["glow"] == "1");
        if (s.count("keybinds")) gui->SetKeybindsOpen(s["keybinds"] == "1");
        if (s.count("spectators")) gui->SetSpectatorsOpen(s["spectators"] == "1");
    }

    return true;
}

bool ConfigManager::SaveConfig(const std::string& name, const ClickGUI* gui) {
    if (name.empty() || !gui) return false;
    std::string path = GetConfigDirectory() + "\\" + name + ".cfg";
    std::string content = SerializeToString(gui);
    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.is_open()) {
        m_lastStatus = "Ошибка записи файла!";
        return false;
    }
    ofs << content;
    ofs.close();
    m_lastStatus = "Сохранен: " + name + ".cfg";
    return true;
}

bool ConfigManager::LoadConfig(const std::string& name, ClickGUI* gui) {
    if (name.empty() || !gui) return false;
    std::string path = GetConfigDirectory() + "\\" + name + ".cfg";
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        m_lastStatus = "Файл не найден: " + name + ".cfg";
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    bool ok = DeserializeFromString(content, gui);
    m_lastStatus = ok ? ("Загружен: " + name + ".cfg") : "Ошибка парсинга КФГ!";
    return ok;
}

bool ConfigManager::DeleteConfig(const std::string& name) {
    if (name.empty()) return false;
    std::string path = GetConfigDirectory() + "\\" + name + ".cfg";
    BOOL res = DeleteFileA(path.c_str());
    m_lastStatus = res ? ("Удален: " + name + ".cfg") : "Не удалось удалить!";
    return res != FALSE;
}

void ConfigManager::LoadDefault(ClickGUI* gui) {
    LoadConfig("default", gui);
}
