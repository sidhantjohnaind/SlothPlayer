#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "../../third_party/imgui/imgui.h"

struct SkinInfo {
    std::string name;
    std::string category;
    std::string filePath;
};

struct SkinColors {
    ImVec4 windowBg = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
    ImVec4 panelBg = ImVec4(0.11f, 0.12f, 0.15f, 1.0f);
    ImVec4 cardBg = ImVec4(0.14f, 0.16f, 0.20f, 1.0f);
    ImVec4 border = ImVec4(0.22f, 0.25f, 0.30f, 1.0f);
    ImVec4 textPrimary = ImVec4(0.95f, 0.96f, 0.98f, 1.0f);
    ImVec4 textSecondary = ImVec4(0.68f, 0.72f, 0.78f, 1.0f);
    ImVec4 textDisabled = ImVec4(0.42f, 0.45f, 0.52f, 1.0f);
    ImVec4 accent = ImVec4(0.92f, 0.66f, 0.06f, 1.0f);
    ImVec4 accentHover = ImVec4(0.98f, 0.76f, 0.20f, 1.0f);
    ImVec4 accentActive = ImVec4(0.85f, 0.58f, 0.04f, 1.0f);
    ImVec4 headerMuted = ImVec4(0.18f, 0.20f, 0.25f, 1.0f);
    ImVec4 progressBarBg = ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
};

class SkinManager {
public:
    static SkinManager& instance();

    // Scan directories for available skins (e.g. ./skins/ and local skins)
    void scanSkinDirectories();
    const std::vector<SkinInfo>& getAvailableSkins() const { return m_availableSkins; }

    // Load and apply a skin from XML file
    bool loadSkinFromFile(const std::string& filePath);
    bool applySkinByName(const std::string& name);

    // Current active skin colors
    const SkinColors& getCurrentSkinColors() const { return m_currentColors; }
    const std::string& getCurrentSkinName() const { return m_currentSkinName; }
    bool isCustomSkinActive() const { return m_hasCustomSkin; }
    void resetToDefaultTheme();

private:
    SkinManager();
    ImVec4 parseRgbString(const std::string& str, const std::unordered_map<std::string, ImVec4>& vars);

    std::vector<SkinInfo> m_availableSkins;
    SkinColors m_currentColors;
    std::string m_currentSkinName;
    bool m_hasCustomSkin = false;
};
