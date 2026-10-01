#include "SkinManager.h"
#include "Theme.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

SkinManager::SkinManager() {
    scanSkinDirectories();
}

SkinManager& SkinManager::instance() {
    static SkinManager s_inst;
    return s_inst;
}

void SkinManager::scanSkinDirectories() {
    m_availableSkins.clear();

    std::vector<std::string> searchPaths = {
        "./skins",
        "../skins"
    };

    for (const auto& basePath : searchPaths) {
        if (!fs::exists(basePath)) continue;

        try {
            for (const auto& entry : fs::recursive_directory_iterator(basePath)) {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() == ".xml") {
                    SkinInfo info;
                    info.filePath = entry.path().string();
                    info.name = entry.path().stem().string();
                    
                    // Category from parent folder name
                    std::string parentName = entry.path().parent_path().filename().string();
                    if (parentName != "Skins" && !parentName.empty()) {
                        info.category = parentName;
                    } else {
                        info.category = "General";
                    }
                    m_availableSkins.push_back(info);
                }
            }
        } catch (...) {}
    }

    // Sort alphabetically by category then name
    std::sort(m_availableSkins.begin(), m_availableSkins.end(), [](const SkinInfo& a, const SkinInfo& b) {
        if (a.category != b.category) return a.category < b.category;
        return a.name < b.name;
    });
}

ImVec4 SkinManager::parseRgbString(const std::string& str, const std::unordered_map<std::string, ImVec4>& vars) {
    std::string s = str;
    // Trim whitespace and quotes
    s.erase(0, s.find_first_not_of(" \t\r\n\""));
    s.erase(s.find_last_not_of(" \t\r\n\"") + 1);

    if (s.empty()) return ImVec4(0, 0, 0, 0);

    // Check if it's a variable
    auto it = vars.find(s);
    if (it != vars.end()) {
        return it->second;
    }

    // Parse comma-separated R,G,B or A,R,G,B
    std::stringstream ss(s);
    std::string token;
    std::vector<int> nums;
    while (std::getline(ss, token, ',')) {
        try {
            // trim token
            token.erase(0, token.find_first_not_of(" \t\r\n"));
            token.erase(token.find_last_not_of(" \t\r\n") + 1);
            if (!token.empty()) {
                nums.push_back(std::stoi(token));
            }
        } catch (...) {}
    }

    if (nums.size() == 3) {
        return ImVec4(
            std::clamp(nums[0] / 255.0f, 0.0f, 1.0f),
            std::clamp(nums[1] / 255.0f, 0.0f, 1.0f),
            std::clamp(nums[2] / 255.0f, 0.0f, 1.0f),
            1.0f
        );
    } else if (nums.size() == 4) {
        // A,R,G,B in Skin XML
        return ImVec4(
            std::clamp(nums[1] / 255.0f, 0.0f, 1.0f),
            std::clamp(nums[2] / 255.0f, 0.0f, 1.0f),
            std::clamp(nums[3] / 255.0f, 0.0f, 1.0f),
            std::clamp(nums[0] / 255.0f, 0.0f, 1.0f)
        );
    }

    return ImVec4(0, 0, 0, 0);
}

bool SkinManager::loadSkinFromFile(const std::string& filePath) {
    if (!fs::exists(filePath)) return false;

    std::ifstream file(filePath);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string xml = buffer.str();

    std::unordered_map<std::string, ImVec4> vars;

    // 1. Extract variables from <variables> ... </variables>
    size_t varStart = xml.find("<variables>");
    size_t varEnd = xml.find("</variables>");
    if (varStart != std::string::npos && varEnd != std::string::npos && varEnd > varStart) {
        std::string varBlock = xml.substr(varStart + 11, varEnd - varStart - 11);
        std::regex varRegex(R"(([a-zA-Z0-9_\-]+)\s*=\s*\"([^\"]+)\")");
        auto words_begin = std::sregex_iterator(varBlock.begin(), varBlock.end(), varRegex);
        auto words_end = std::sregex_iterator();

        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch match = *i;
            std::string key = match[1].str();
            std::string val = match[2].str();
            ImVec4 col = parseRgbString(val, vars);
            vars[key] = col;
        }
    }

    SkinColors colors;

    // Direct variable checks first
    if (vars.count("Background")) colors.windowBg = vars["Background"];
    if (vars.count("theme")) colors.accent = vars["theme"];
    if (vars.count("Highlight")) colors.accent = vars["Highlight"];
    if (vars.count("Highlight2")) colors.accentHover = vars["Highlight2"];
    if (vars.count("light_30")) colors.accentHover = vars["light_30"];
    if (vars.count("light_50")) colors.accentActive = vars["light_50"];
    if (vars.count("Primary-Text")) colors.textPrimary = vars["Primary-Text"];
    if (vars.count("font_1")) colors.textPrimary = vars["font_1"];
    if (vars.count("Secondary-Text")) colors.textSecondary = vars["Secondary-Text"];
    if (vars.count("font_2")) colors.textSecondary = vars["font_2"];
    if (vars.count("Tertiary-Text")) colors.textDisabled = vars["Tertiary-Text"];
    if (vars.count("font_3")) colors.textDisabled = vars["font_3"];

    // 2. Parse <element id="..." bg="..." fg="..." bdr="..." />
    std::regex elemRegex(R"(<element\s+id=\"([^\"]+)\"([^>]*)\/?>)");
    auto elem_begin = std::sregex_iterator(xml.begin(), xml.end(), elemRegex);
    auto elem_end = std::sregex_iterator();

    std::regex bgRegex(R"(bg=\"([^\"]+)\")");
    std::regex fgRegex(R"(fg=\"([^\"]+)\")");
    std::regex bdrRegex(R"(bdr=\"([^\"]+)\")");

    for (std::sregex_iterator i = elem_begin; i != elem_end; ++i) {
        std::smatch match = *i;
        std::string id = match[1].str();
        std::string attrs = match[2].str();

        std::string bgVal, fgVal, bdrVal;
        std::smatch attrMatch;
        if (std::regex_search(attrs, attrMatch, bgRegex)) bgVal = attrMatch[1].str();
        if (std::regex_search(attrs, attrMatch, fgRegex)) fgVal = attrMatch[1].str();
        if (std::regex_search(attrs, attrMatch, bdrRegex)) bdrVal = attrMatch[1].str();

        ImVec4 bg = parseRgbString(bgVal, vars);
        ImVec4 fg = parseRgbString(fgVal, vars);
        ImVec4 bdr = parseRgbString(bdrVal, vars);

        if (id == "ApplicationBackColour" || id == "WindowBackColour") {
            if (bg.w > 0.0f) colors.windowBg = bg;
        } else if (id == "Panel.Body.Default" || id == "Panel.ChildBody.Default") {
            if (bg.w > 0.0f) colors.panelBg = bg;
            if (fg.w > 0.0f) colors.textPrimary = fg;
            if (bdr.w > 0.0f) colors.border = bdr;
        } else if (id == "Panel.Body.Highlight" || id == "Panel.ChildBody.Highlight") {
            if (bg.w > 0.0f) colors.accent = bg;
        } else if (id == "PlayerFlat.ProgressBar") {
            if (bg.w > 0.0f) colors.progressBarBg = bg;
            if (fg.w > 0.0f) colors.accent = fg;
        } else if (id == "Player.Wavebar.Inner") {
            if (fg.w > 0.0f) colors.accent = fg;
        } else if (id == "MenuBar.Default") {
            if (fg.w > 0.0f) colors.textPrimary = fg;
        }
    }

    // Ensure derivations if some were missing
    if (colors.panelBg.w == 0.0f || (colors.panelBg.x == colors.windowBg.x && colors.panelBg.y == colors.windowBg.y)) {
        colors.panelBg = ImVec4(colors.windowBg.x + 0.03f, colors.windowBg.y + 0.03f, colors.windowBg.z + 0.04f, 1.0f);
    }
    colors.cardBg = ImVec4(colors.panelBg.x + 0.04f, colors.panelBg.y + 0.04f, colors.panelBg.z + 0.05f, 1.0f);
    if (colors.accentHover.w == 0.0f || colors.accentHover.x == colors.accent.x) {
        colors.accentHover = ImVec4(
            std::min(1.0f, colors.accent.x * 1.15f),
            std::min(1.0f, colors.accent.y * 1.15f),
            std::min(1.0f, colors.accent.z * 1.15f),
            1.0f
        );
    }
    if (colors.accentActive.w == 0.0f || colors.accentActive.x == colors.accent.x) {
        colors.accentActive = ImVec4(
            colors.accent.x * 0.85f,
            colors.accent.y * 0.85f,
            colors.accent.z * 0.85f,
            1.0f
        );
    }
    colors.headerMuted = ImVec4(colors.panelBg.x + 0.06f, colors.panelBg.y + 0.06f, colors.panelBg.z + 0.07f, 1.0f);

    m_currentColors = colors;
    m_currentSkinName = fs::path(filePath).stem().string();
    m_hasCustomSkin = true;

    // Apply directly to Dear ImGui style
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = colors.windowBg;
    style.Colors[ImGuiCol_ChildBg] = colors.panelBg;
    style.Colors[ImGuiCol_PopupBg] = colors.cardBg;
    style.Colors[ImGuiCol_Border] = colors.border;
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Text] = colors.textPrimary;
    style.Colors[ImGuiCol_TextDisabled] = colors.textDisabled;
    style.Colors[ImGuiCol_Header] = ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.35f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.60f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(colors.accent.x, colors.accent.y, colors.accent.z, 0.80f);
    style.Colors[ImGuiCol_Button] = ImVec4(colors.cardBg.x + 0.04f, colors.cardBg.y + 0.04f, colors.cardBg.z + 0.05f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = colors.accent;
    style.Colors[ImGuiCol_ButtonActive] = colors.accentActive;
    style.Colors[ImGuiCol_FrameBg] = ImVec4(colors.windowBg.x + 0.02f, colors.windowBg.y + 0.02f, colors.windowBg.z + 0.03f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(colors.cardBg.x, colors.cardBg.y, colors.cardBg.z, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = colors.accent;
    style.Colors[ImGuiCol_SliderGrab] = colors.accent;
    style.Colors[ImGuiCol_SliderGrabActive] = colors.accentActive;
    style.Colors[ImGuiCol_CheckMark] = colors.accent;
    style.Colors[ImGuiCol_TableRowBg] = colors.windowBg;
    style.Colors[ImGuiCol_TableRowBgAlt] = colors.panelBg;

    return true;
}

bool SkinManager::applySkinByName(const std::string& name) {
    for (const auto& s : m_availableSkins) {
        if (s.name == name) {
            return loadSkinFromFile(s.filePath);
        }
    }
    return false;
}

void SkinManager::resetToDefaultTheme() {
    m_hasCustomSkin = false;
    m_currentSkinName.clear();
    Theme::applyTheme(AppTheme::ClassicDark);
}
