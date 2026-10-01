#include "Theme.h"
#include "SkinManager.h"

static AppTheme s_currentTheme = AppTheme::ClassicDark;

AppTheme Theme::getCurrentTheme() {
    return s_currentTheme;
}

ImVec4 Theme::AccentColor() {
    if (SkinManager::instance().isCustomSkinActive()) {
        return SkinManager::instance().getCurrentSkinColors().accent;
    }
    switch (s_currentTheme) {
        case AppTheme::ModernObsidian: return ImVec4(0.00f, 0.82f, 0.88f, 1.00f);
        case AppTheme::MidnightNavy:   return ImVec4(0.26f, 0.64f, 1.00f, 1.00f);
        case AppTheme::ForestEmerald:  return ImVec4(0.18f, 0.82f, 0.44f, 1.00f);
        case AppTheme::CyberpunkNeon:  return ImVec4(1.00f, 0.16f, 0.58f, 1.00f);
        case AppTheme::NordicFrost:    return ImVec4(0.32f, 0.78f, 0.95f, 1.00f);
        case AppTheme::AmberSunset:    return ImVec4(0.98f, 0.55f, 0.14f, 1.00f);
        case AppTheme::Dracula:        return ImVec4(0.74f, 0.46f, 0.98f, 1.00f);
        case AppTheme::TokyoNight:     return ImVec4(0.97f, 0.44f, 0.58f, 1.00f);
        case AppTheme::MetroLight:     return ImVec4(0.00f, 0.47f, 0.84f, 1.00f);
        case AppTheme::ClassicDark:
        default:                       return ImVec4(0.90f, 0.63f, 0.08f, 1.00f);
    }
}

ImVec4 Theme::AccentHoverColor() {
    if (SkinManager::instance().isCustomSkinActive()) {
        return SkinManager::instance().getCurrentSkinColors().accentHover;
    }
    switch (s_currentTheme) {
        case AppTheme::ModernObsidian: return ImVec4(0.20f, 0.90f, 0.95f, 1.00f);
        case AppTheme::MidnightNavy:   return ImVec4(0.40f, 0.74f, 1.00f, 1.00f);
        case AppTheme::ForestEmerald:  return ImVec4(0.28f, 0.90f, 0.54f, 1.00f);
        case AppTheme::CyberpunkNeon:  return ImVec4(1.00f, 0.35f, 0.70f, 1.00f);
        case AppTheme::NordicFrost:    return ImVec4(0.48f, 0.86f, 0.98f, 1.00f);
        case AppTheme::AmberSunset:    return ImVec4(1.00f, 0.65f, 0.25f, 1.00f);
        case AppTheme::Dracula:        return ImVec4(0.82f, 0.58f, 1.00f, 1.00f);
        case AppTheme::TokyoNight:     return ImVec4(0.99f, 0.58f, 0.70f, 1.00f);
        case AppTheme::MetroLight:     return ImVec4(0.10f, 0.56f, 0.92f, 1.00f);
        case AppTheme::ClassicDark:
        default:                       return ImVec4(0.96f, 0.70f, 0.16f, 1.00f);
    }
}

ImVec4 Theme::AccentActiveColor() {
    if (SkinManager::instance().isCustomSkinActive()) {
        return SkinManager::instance().getCurrentSkinColors().accentActive;
    }
    switch (s_currentTheme) {
        case AppTheme::ModernObsidian: return ImVec4(0.00f, 0.68f, 0.75f, 1.00f);
        case AppTheme::MidnightNavy:   return ImVec4(0.18f, 0.52f, 0.88f, 1.00f);
        case AppTheme::ForestEmerald:  return ImVec4(0.12f, 0.68f, 0.36f, 1.00f);
        case AppTheme::CyberpunkNeon:  return ImVec4(0.85f, 0.08f, 0.48f, 1.00f);
        case AppTheme::NordicFrost:    return ImVec4(0.22f, 0.68f, 0.85f, 1.00f);
        case AppTheme::AmberSunset:    return ImVec4(0.85f, 0.45f, 0.08f, 1.00f);
        case AppTheme::Dracula:        return ImVec4(0.64f, 0.36f, 0.88f, 1.00f);
        case AppTheme::TokyoNight:     return ImVec4(0.85f, 0.35f, 0.48f, 1.00f);
        case AppTheme::MetroLight:     return ImVec4(0.00f, 0.38f, 0.72f, 1.00f);
        case AppTheme::ClassicDark:
        default:                       return ImVec4(0.78f, 0.54f, 0.06f, 1.00f);
    }
}

ImVec4 Theme::HeaderMuted() {
    if (SkinManager::instance().isCustomSkinActive()) {
        return SkinManager::instance().getCurrentSkinColors().headerMuted;
    }
    if (s_currentTheme == AppTheme::MetroLight) return ImVec4(0.45f, 0.48f, 0.52f, 1.00f);
    return ImVec4(0.58f, 0.62f, 0.68f, 1.00f);
}

ImVec4 Theme::TextPrimary() {
    if (SkinManager::instance().isCustomSkinActive()) {
        return SkinManager::instance().getCurrentSkinColors().textPrimary;
    }
    if (s_currentTheme == AppTheme::MetroLight) return ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    return ImVec4(0.94f, 0.95f, 0.97f, 1.00f);
}

ImVec4 Theme::TextSecondary() {
    if (SkinManager::instance().isCustomSkinActive()) {
        return SkinManager::instance().getCurrentSkinColors().textSecondary;
    }
    if (s_currentTheme == AppTheme::MetroLight) return ImVec4(0.48f, 0.52f, 0.56f, 1.00f);
    return ImVec4(0.55f, 0.58f, 0.63f, 1.00f);
}

ImVec4 Theme::PanelBackground() {
    if (SkinManager::instance().isCustomSkinActive()) {
        return SkinManager::instance().getCurrentSkinColors().panelBg;
    }
    switch (s_currentTheme) {
        case AppTheme::ModernObsidian: return ImVec4(0.04f, 0.04f, 0.05f, 1.00f);
        case AppTheme::MidnightNavy:   return ImVec4(0.06f, 0.08f, 0.13f, 1.00f);
        case AppTheme::ForestEmerald:  return ImVec4(0.06f, 0.09f, 0.07f, 1.00f);
        case AppTheme::CyberpunkNeon:  return ImVec4(0.05f, 0.04f, 0.07f, 1.00f);
        case AppTheme::NordicFrost:    return ImVec4(0.07f, 0.09f, 0.11f, 1.00f);
        case AppTheme::AmberSunset:    return ImVec4(0.08f, 0.07f, 0.06f, 1.00f);
        case AppTheme::Dracula:        return ImVec4(0.08f, 0.08f, 0.11f, 1.00f);
        case AppTheme::TokyoNight:     return ImVec4(0.07f, 0.08f, 0.12f, 1.00f);
        case AppTheme::MetroLight:     return ImVec4(0.93f, 0.94f, 0.96f, 1.00f);
        case AppTheme::ClassicDark:
        default:                       return ImVec4(0.082f, 0.088f, 0.098f, 1.00f);
    }
}

ImVec4 Theme::CardBackground() {
    if (SkinManager::instance().isCustomSkinActive()) {
        return SkinManager::instance().getCurrentSkinColors().cardBg;
    }
    switch (s_currentTheme) {
        case AppTheme::ModernObsidian: return ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
        case AppTheme::MidnightNavy:   return ImVec4(0.10f, 0.13f, 0.20f, 1.00f);
        case AppTheme::ForestEmerald:  return ImVec4(0.09f, 0.13f, 0.10f, 1.00f);
        case AppTheme::CyberpunkNeon:  return ImVec4(0.08f, 0.06f, 0.11f, 1.00f);
        case AppTheme::NordicFrost:    return ImVec4(0.10f, 0.13f, 0.16f, 1.00f);
        case AppTheme::AmberSunset:    return ImVec4(0.12f, 0.10f, 0.09f, 1.00f);
        case AppTheme::Dracula:        return ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
        case AppTheme::TokyoNight:     return ImVec4(0.10f, 0.11f, 0.17f, 1.00f);
        case AppTheme::MetroLight:     return ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        case AppTheme::ClassicDark:
        default:                       return ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
    }
}

ImVec4 Theme::WindowBackground() {
    if (SkinManager::instance().isCustomSkinActive()) {
        return SkinManager::instance().getCurrentSkinColors().windowBg;
    }
    switch (s_currentTheme) {
        case AppTheme::ModernObsidian: return ImVec4(0.06f, 0.06f, 0.07f, 1.00f);
        case AppTheme::MidnightNavy:   return ImVec4(0.07f, 0.09f, 0.15f, 1.00f);
        case AppTheme::ForestEmerald:  return ImVec4(0.07f, 0.10f, 0.08f, 1.00f);
        case AppTheme::CyberpunkNeon:  return ImVec4(0.07f, 0.05f, 0.09f, 1.00f);
        case AppTheme::NordicFrost:    return ImVec4(0.08f, 0.10f, 0.13f, 1.00f);
        case AppTheme::AmberSunset:    return ImVec4(0.10f, 0.08f, 0.07f, 1.00f);
        case AppTheme::Dracula:        return ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
        case AppTheme::TokyoNight:     return ImVec4(0.08f, 0.09f, 0.14f, 1.00f);
        case AppTheme::MetroLight:     return ImVec4(0.96f, 0.97f, 0.98f, 1.00f);
        case AppTheme::ClassicDark:
        default:                       return ImVec4(0.095f, 0.102f, 0.114f, 1.00f);
    }
}

ImVec4 Theme::BorderColor() {
    if (SkinManager::instance().isCustomSkinActive()) {
        return SkinManager::instance().getCurrentSkinColors().border;
    }
    switch (s_currentTheme) {
        case AppTheme::ModernObsidian: return ImVec4(0.16f, 0.17f, 0.19f, 1.00f);
        case AppTheme::MidnightNavy:   return ImVec4(0.14f, 0.18f, 0.26f, 1.00f);
        case AppTheme::ForestEmerald:  return ImVec4(0.14f, 0.19f, 0.15f, 1.00f);
        case AppTheme::CyberpunkNeon:  return ImVec4(0.24f, 0.14f, 0.32f, 1.00f);
        case AppTheme::NordicFrost:    return ImVec4(0.16f, 0.21f, 0.26f, 1.00f);
        case AppTheme::AmberSunset:    return ImVec4(0.22f, 0.18f, 0.15f, 1.00f);
        case AppTheme::Dracula:        return ImVec4(0.20f, 0.21f, 0.28f, 1.00f);
        case AppTheme::TokyoNight:     return ImVec4(0.18f, 0.20f, 0.30f, 1.00f);
        case AppTheme::MetroLight:     return ImVec4(0.82f, 0.84f, 0.87f, 1.00f);
        case AppTheme::ClassicDark:
        default:                       return ImVec4(0.18f, 0.19f, 0.22f, 1.00f);
    }
}

void Theme::applyTheme(AppTheme theme) {
    s_currentTheme = theme;
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    ImVec4 accent = AccentColor();
    ImVec4 accentHover = AccentHoverColor();
    ImVec4 accentActive = AccentActiveColor();
    ImVec4 panelBg = PanelBackground();
    ImVec4 cardBg = CardBackground();
    ImVec4 borderCol = BorderColor();
    ImVec4 textPrim = TextPrimary();
    ImVec4 textSec = TextSecondary();

    if (theme == AppTheme::MetroLight) {
        colors[ImGuiCol_Text]                  = textPrim;
        colors[ImGuiCol_TextDisabled]          = textSec;
        colors[ImGuiCol_WindowBg]              = panelBg;
        colors[ImGuiCol_ChildBg]               = cardBg;
        colors[ImGuiCol_PopupBg]               = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
        colors[ImGuiCol_Border]                = borderCol;
        colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_FrameBg]               = ImVec4(0.90f, 0.91f, 0.93f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.85f, 0.87f, 0.90f, 1.00f);
        colors[ImGuiCol_FrameBgActive]         = ImVec4(0.80f, 0.82f, 0.86f, 1.00f);

        colors[ImGuiCol_TitleBg]               = panelBg;
        colors[ImGuiCol_TitleBgActive]         = cardBg;
        colors[ImGuiCol_TitleBgCollapsed]      = panelBg;
        colors[ImGuiCol_MenuBarBg]             = panelBg;

        colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.00f, 0.00f, 0.00f, 0.18f);
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
        colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(accent.x, accent.y, accent.z, 0.80f);

        colors[ImGuiCol_CheckMark]             = accent;
        colors[ImGuiCol_SliderGrab]            = accent;
        colors[ImGuiCol_SliderGrabActive]      = accentActive;

        colors[ImGuiCol_Button]                = ImVec4(0.88f, 0.90f, 0.92f, 1.00f);
        colors[ImGuiCol_ButtonHovered]         = ImVec4(0.82f, 0.85f, 0.88f, 1.00f);
        colors[ImGuiCol_ButtonActive]          = ImVec4(0.76f, 0.79f, 0.84f, 1.00f);

        colors[ImGuiCol_Header]                = ImVec4(0.88f, 0.91f, 0.95f, 1.00f);
        colors[ImGuiCol_HeaderHovered]         = ImVec4(0.82f, 0.86f, 0.92f, 1.00f);
        colors[ImGuiCol_HeaderActive]          = ImVec4(0.76f, 0.81f, 0.88f, 1.00f);

        colors[ImGuiCol_Separator]             = borderCol;
        colors[ImGuiCol_SeparatorHovered]      = accentHover;
        colors[ImGuiCol_SeparatorActive]       = accentActive;

        colors[ImGuiCol_ResizeGrip]            = ImVec4(0.80f, 0.82f, 0.85f, 0.50f);
        colors[ImGuiCol_ResizeGripHovered]     = accentHover;
        colors[ImGuiCol_ResizeGripActive]      = accentActive;

        colors[ImGuiCol_Tab]                   = ImVec4(0.88f, 0.90f, 0.93f, 1.00f);
        colors[ImGuiCol_TabHovered]            = ImVec4(0.92f, 0.94f, 0.97f, 1.00f);
        colors[ImGuiCol_TabActive]             = cardBg;
        colors[ImGuiCol_TabUnfocused]          = ImVec4(0.86f, 0.88f, 0.91f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);

        colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.88f, 0.90f, 0.93f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]     = borderCol;
        colors[ImGuiCol_TableBorderLight]      = ImVec4(0.88f, 0.90f, 0.92f, 0.60f);
        colors[ImGuiCol_TableRowBg]            = cardBg;
        colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.96f, 0.97f, 0.98f, 1.00f);

        colors[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.25f);
        colors[ImGuiCol_NavHighlight]          = accent;
    } else {
        // Dark theme family
        colors[ImGuiCol_Text]                  = textPrim;
        colors[ImGuiCol_TextDisabled]          = textSec;
        colors[ImGuiCol_WindowBg]              = panelBg;
        colors[ImGuiCol_ChildBg]               = cardBg;
        colors[ImGuiCol_PopupBg]               = ImVec4(cardBg.x + 0.03f, cardBg.y + 0.03f, cardBg.z + 0.04f, 0.98f);
        colors[ImGuiCol_Border]                = borderCol;
        colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        colors[ImGuiCol_FrameBg]               = ImVec4(cardBg.x + 0.04f, cardBg.y + 0.04f, cardBg.z + 0.05f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]        = ImVec4(cardBg.x + 0.08f, cardBg.y + 0.08f, cardBg.z + 0.10f, 1.00f);
        colors[ImGuiCol_FrameBgActive]         = ImVec4(cardBg.x + 0.12f, cardBg.y + 0.12f, cardBg.z + 0.15f, 1.00f);

        colors[ImGuiCol_TitleBg]               = panelBg;
        colors[ImGuiCol_TitleBgActive]         = cardBg;
        colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(panelBg.x, panelBg.y, panelBg.z, 0.75f);
        colors[ImGuiCol_MenuBarBg]             = panelBg;

        colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4(1.00f, 1.00f, 1.00f, 0.16f);
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(1.00f, 1.00f, 1.00f, 0.38f);
        colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(accent.x, accent.y, accent.z, 0.80f);

        colors[ImGuiCol_CheckMark]             = accent;
        colors[ImGuiCol_SliderGrab]            = ImVec4(0.70f, 0.74f, 0.80f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]      = accentActive;

        colors[ImGuiCol_Button]                = ImVec4(cardBg.x + 0.04f, cardBg.y + 0.04f, cardBg.z + 0.05f, 1.00f);
        colors[ImGuiCol_ButtonHovered]         = ImVec4(cardBg.x + 0.10f, cardBg.y + 0.10f, cardBg.z + 0.13f, 1.00f);
        colors[ImGuiCol_ButtonActive]          = ImVec4(cardBg.x + 0.16f, cardBg.y + 0.16f, cardBg.z + 0.20f, 1.00f);

        colors[ImGuiCol_Header]                = ImVec4(cardBg.x + 0.05f, cardBg.y + 0.05f, cardBg.z + 0.07f, 1.00f);
        colors[ImGuiCol_HeaderHovered]         = ImVec4(cardBg.x + 0.11f, cardBg.y + 0.11f, cardBg.z + 0.14f, 1.00f);
        colors[ImGuiCol_HeaderActive]          = ImVec4(cardBg.x + 0.16f, cardBg.y + 0.16f, cardBg.z + 0.20f, 1.00f);

        colors[ImGuiCol_Separator]             = borderCol;
        colors[ImGuiCol_SeparatorHovered]      = accentHover;
        colors[ImGuiCol_SeparatorActive]       = accentActive;

        colors[ImGuiCol_ResizeGrip]            = ImVec4(0.22f, 0.24f, 0.27f, 0.50f);
        colors[ImGuiCol_ResizeGripHovered]     = accentHover;
        colors[ImGuiCol_ResizeGripActive]      = accentActive;

        colors[ImGuiCol_Tab]                   = cardBg;
        colors[ImGuiCol_TabHovered]            = ImVec4(cardBg.x + 0.08f, cardBg.y + 0.08f, cardBg.z + 0.10f, 1.00f);
        colors[ImGuiCol_TabActive]             = ImVec4(cardBg.x + 0.05f, cardBg.y + 0.05f, cardBg.z + 0.07f, 1.00f);
        colors[ImGuiCol_TabUnfocused]          = panelBg;
        colors[ImGuiCol_TabUnfocusedActive]   = cardBg;

        colors[ImGuiCol_TableHeaderBg]         = ImVec4(cardBg.x + 0.03f, cardBg.y + 0.03f, cardBg.z + 0.04f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]     = borderCol;
        colors[ImGuiCol_TableBorderLight]      = ImVec4(borderCol.x, borderCol.y, borderCol.z, 0.60f);
        colors[ImGuiCol_TableRowBg]            = cardBg;
        colors[ImGuiCol_TableRowBgAlt]         = ImVec4(cardBg.x + 0.02f, cardBg.y + 0.02f, cardBg.z + 0.025f, 1.00f);

        colors[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        colors[ImGuiCol_NavHighlight]          = accent;
    }

    // Geometry — Premium modern styling with generous rounding & spacing
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(8.0f, 5.0f);
    style.CellPadding       = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(7.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 5.0f);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 6.0f;
    style.GrabMinSize       = 14.0f;

    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;

    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;
}

static bool s_useLightExpandedCard = true; // Default light expanded card

bool Theme::useLightExpandedCard() {
    return s_useLightExpandedCard;
}

void Theme::setUseLightExpandedCard(bool light) {
    s_useLightExpandedCard = light;
}

ImVec4 Theme::ExpandedCardBackground() {
    if (s_useLightExpandedCard) {
        return ImVec4(0.965f, 0.968f, 0.973f, 1.0f); // Soft crisp off-white/light grey
    }
    return ImVec4(0.12f, 0.135f, 0.16f, 1.0f);
}

ImVec4 Theme::ExpandedCardBorder() {
    if (s_useLightExpandedCard) {
        return ImVec4(0.80f, 0.82f, 0.86f, 1.0f); // Crisp subtle border
    }
    return ImVec4(0.25f, 0.28f, 0.34f, 1.0f);
}

ImVec4 Theme::ExpandedCardTextPrimary() {
    if (s_useLightExpandedCard) {
        return ImVec4(0.08f, 0.09f, 0.11f, 1.0f); // Deep dark charcoal
    }
    return ImVec4(0.95f, 0.96f, 0.98f, 1.0f);
}

ImVec4 Theme::ExpandedCardTextSecondary() {
    if (s_useLightExpandedCard) {
        return ImVec4(0.38f, 0.42f, 0.48f, 1.0f); // Medium grey
    }
    return ImVec4(0.60f, 0.64f, 0.70f, 1.0f);
}

ImVec4 Theme::ExpandedCardRowHover() {
    if (s_useLightExpandedCard) {
        return ImVec4(0.88f, 0.90f, 0.93f, 1.0f); // Soft cool grey hover
    }
    return ImVec4(0.20f, 0.23f, 0.28f, 1.0f);
}

ImVec4 Theme::ExpandedCardRowActive() {
    if (s_useLightExpandedCard) {
        return ImVec4(0.82f, 0.85f, 0.89f, 1.0f);
    }
    return ImVec4(0.25f, 0.29f, 0.35f, 1.0f);
}

