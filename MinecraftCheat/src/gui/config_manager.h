// src/gui/config_manager.h
#pragma once
#include <string>
#include <vector>

class ClickGUI;

class ConfigManager {
public:
    static ConfigManager& Get() { static ConfigManager inst; return inst; }

    std::string GetConfigDirectory();
    std::vector<std::string> GetConfigList();

    bool SaveConfig(const std::string& name, const ClickGUI* gui);
    bool LoadConfig(const std::string& name, ClickGUI* gui);
    bool DeleteConfig(const std::string& name);

    void LoadDefault(ClickGUI* gui);

    std::string GetLastStatus() const { return m_lastStatus; }
    void SetLastStatus(const std::string& st) { m_lastStatus = st; }

    std::string SerializeToString(const ClickGUI* gui);
    bool DeserializeFromString(const std::string& data, ClickGUI* gui);

private:
    ConfigManager() = default;
    std::string m_lastStatus = "Готов";
};

