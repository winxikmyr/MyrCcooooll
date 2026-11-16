// src/modules/module_base.h
#pragma once
#include <string>
#include <Windows.h>

enum class Category { VISUAL, MOVEMENT, COMBAT, MISC, CONFIGS };

class ModuleBase {
public:
    explicit ModuleBase(std::string name, Category cat, bool defaultOn = false, int defaultKey = 0)
        : m_name(std::move(name)), m_cat(cat), m_enabled(defaultOn), m_key(defaultKey) {}

    virtual ~ModuleBase() = default;

    virtual void OnEnable()  {}
    virtual void OnDisable() {}
    virtual void OnTick()    {}

    void Toggle() {
        m_enabled = !m_enabled;
        m_enabled ? OnEnable() : OnDisable();
    }

    void SetEnabled(bool state) {
        if (m_enabled != state) {
            m_enabled = state;
            m_enabled ? OnEnable() : OnDisable();
        }
    }

    bool               IsEnabled()   const { return m_enabled; }
    const std::string& Name()        const { return m_name; }
    Category           GetCategory() const { return m_cat; }

    int  GetKey() const { return m_key; }
    void SetKey(int key) { m_key = key; }

    std::string GetKeyName() const {
        if (m_key <= 0) return "None";
        if (m_key >= 'A' && m_key <= 'Z') return std::string(1, static_cast<char>(m_key));
        if (m_key >= '0' && m_key <= '9') return std::string(1, static_cast<char>(m_key));
        switch (m_key) {
            case VK_SPACE:   return "SPACE";
            case VK_RETURN:  return "ENTER";
            case VK_SHIFT:
            case VK_LSHIFT:  return "LSHIFT";
            case VK_RSHIFT:  return "RSHIFT";
            case VK_CONTROL:
            case VK_LCONTROL:return "LCTRL";
            case VK_RCONTROL:return "RCTRL";
            case VK_MENU:
            case VK_LMENU:   return "LALT";
            case VK_RMENU:   return "RALT";
            case VK_TAB:     return "TAB";
            case VK_CAPITAL: return "CAPS";
            case VK_ESCAPE:  return "ESC";
            case VK_BACK:    return "BACK";
            case VK_INSERT:  return "INS";
            case VK_DELETE:  return "DEL";
            case VK_HOME:    return "HOME";
            case VK_END:     return "END";
            case VK_PRIOR:   return "PGUP";
            case VK_NEXT:    return "PGDN";
            case VK_UP:      return "UP";
            case VK_DOWN:    return "DOWN";
            case VK_LEFT:    return "LEFT";
            case VK_RIGHT:   return "RIGHT";
            default: break;
        }
        char name[32] = {0};
        UINT scanCode = MapVirtualKeyA(m_key, MAPVK_VK_TO_VSC);
        if (scanCode != 0) {
            int len = GetKeyNameTextA(scanCode << 16, name, sizeof(name));
            if (len > 0) return std::string(name);
        }
        return "K" + std::to_string(m_key);
    }

protected:
    std::string m_name;
    Category    m_cat;
    bool        m_enabled;
    int         m_key;
};
