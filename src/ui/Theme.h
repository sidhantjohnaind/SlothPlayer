#pragma once

#include "../../third_party/imgui/imgui.h"

enum class AppTheme {
    ClassicDark = 0,    // Classic Gold & Slate (Default)
    ModernObsidian = 1, // Deep Black & Electric Cyan
    MidnightNavy = 2,   // Midnight Navy & Sky Blue
    ForestEmerald = 3,  // Forest Green & Mint
    CyberpunkNeon = 4,  // Deep Violet & Neon Pink
    NordicFrost = 5,    // Arctic Slate & Frost Ice
    AmberSunset = 6,    // Espresso & Amber Orange
    Dracula = 7,        // Gothic Slate & Radiant Purple
    TokyoNight = 8,     // Night Indigo & Tokyo Rose
    MetroLight = 9      // Clean Modern Light Mode
};

class Theme {
public:
    static void applyTheme(AppTheme theme);
    static void applyClassicTheme() { applyTheme(AppTheme::ClassicDark); }
    static AppTheme getCurrentTheme();

    // Helper colors for custom UI drawing
    static ImVec4 AccentColor();
    static ImVec4 AccentHoverColor();
    static ImVec4 AccentActiveColor();
    static ImVec4 HeaderMuted();
    static ImVec4 TextPrimary();
    static ImVec4 TextSecondary();
    static ImVec4 PanelBackground();
    static ImVec4 CardBackground();
    static ImVec4 WindowBackground();
    static ImVec4 BorderColor();

    // Expanded Album Tracklist Card styling (authentic light card or dark card)
    static bool useLightExpandedCard();
    static void setUseLightExpandedCard(bool light);
    static ImVec4 ExpandedCardBackground();
    static ImVec4 ExpandedCardBorder();
    static ImVec4 ExpandedCardTextPrimary();
    static ImVec4 ExpandedCardTextSecondary();
    static ImVec4 ExpandedCardRowHover();
    static ImVec4 ExpandedCardRowActive();
};
