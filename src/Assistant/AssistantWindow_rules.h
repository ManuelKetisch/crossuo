// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>

namespace xuoassist {

constexpr int AssistantUiScaleMin = 85;
constexpr int AssistantUiScaleMax = 150;
constexpr int AssistantUiScaleDefault = 100;

inline int AssistantOverlay_ClampUiScale(int percent)
{
    if (percent <= 0)
    {
        return AssistantUiScaleDefault;
    }
    if (percent < AssistantUiScaleMin)
    {
        return AssistantUiScaleMin;
    }
    if (percent > AssistantUiScaleMax)
    {
        return AssistantUiScaleMax;
    }
    return percent;
}

inline int AssistantOverlay_SanitizeOpacity(int percent)
{
    if (percent <= 0)
    {
        return 100;
    }
    if (percent < 30)
    {
        return 30;
    }
    if (percent > 100)
    {
        return 100;
    }
    return percent;
}

// Ensures packed IM_COL32 (0xAABBGGRR) text has legible contrast on the dark
// charcoal (#303030) ImGui background by lifting overly dark hues toward light
// gray while preserving their tint.
inline uint32_t AssistantOverlay_ReadableTextOnDark(uint32_t rgba)
{
    const uint32_t r = rgba & 0xFFu;
    const uint32_t g = (rgba >> 8) & 0xFFu;
    const uint32_t b = (rgba >> 16) & 0xFFu;
    const uint32_t a = (rgba >> 24) & 0xFFu;
    if (a == 0u || (r == 0u && g == 0u && b == 0u))
    {
        return 0xFFE6E6E6u;
    }

    // Rec. 709 relative luminance in 0..255000 integer scale.
    const uint32_t luma = 213u * r + 715u * g + 72u * b;
    constexpr uint32_t MinLuma = 128000u; // ~50% luminance on #303030 (~19% bg)
    if (luma >= MinLuma)
    {
        return (rgba & 0x00FFFFFFu) | 0xFF000000u;
    }

    // Blend toward #EBEBEB (235) just enough to cross the readability floor.
    constexpr uint32_t Target = 235u;
    constexpr uint32_t TargetLuma = 235000u;
    const uint32_t denom = TargetLuma - luma;
    const uint32_t numer = MinLuma - luma;
    const uint32_t nr = r + ((Target - r) * numer + denom / 2u) / denom;
    const uint32_t ng = g + ((Target - g) * numer + denom / 2u) / denom;
    const uint32_t nb = b + ((Target - b) * numer + denom / 2u) / denom;
    return (nr & 0xFFu) | ((ng & 0xFFu) << 8) | ((nb & 0xFFu) << 16) | 0xFF000000u;
}

struct AssistantOverlayInputState
{
    bool ShellVisible = false;
    bool HudVisible = false;
    bool AssetViewerOpen = false;
    bool WantCaptureMouse = false;
    bool WantCaptureKeyboard = false;
    bool WantTextInput = false;
};

inline bool AssistantOverlay_AnySurfaceVisible(const AssistantOverlayInputState &state)
{
    return state.ShellVisible || state.HudVisible || state.AssetViewerOpen;
}

inline bool AssistantOverlay_ShouldShowSystemCursor(const AssistantOverlayInputState &state)
{
    return AssistantOverlay_AnySurfaceVisible(state) && state.WantCaptureMouse;
}

inline bool AssistantOverlay_ShouldCaptureMouse(const AssistantOverlayInputState &state)
{
    return AssistantOverlay_AnySurfaceVisible(state) && state.WantCaptureMouse;
}

inline bool AssistantOverlay_ShouldCaptureKeyboard(const AssistantOverlayInputState &state)
{
    if (state.WantTextInput)
    {
        return true;
    }

    // The standalone HUD has no text or keyboard widgets, so dragging or clicking it
    // must not swallow game chat or hotkeys while the shell and viewer are closed.
    const bool keyboardSurface = state.ShellVisible || state.AssetViewerOpen;
    return keyboardSurface && state.WantCaptureKeyboard;
}

} // namespace xuoassist
