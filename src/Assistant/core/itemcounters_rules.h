// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xuoassist {

struct SupplyIconLayout
{
    int DrawX = 0;
    int DrawY = 0;
    int DrawWidth = 0;
    int DrawHeight = 0;
    int VisibleWidth = 0;
    int VisibleHeight = 0;
    bool DoubleDraw = false;
    bool Downscaled = false;
};

inline SupplyIconLayout ItemCounters_ComputeIconLayout(
    int boxX,
    int boxY,
    int boxWidth,
    int boxHeight,
    int spriteWidth,
    int spriteHeight,
    int imageOffsetX,
    int imageOffsetY,
    int imageWidth,
    int imageHeight,
    int maxScaleNum = 5,
    int maxScaleDen = 2)
{
    SupplyIconLayout layout;
    if (boxWidth <= 0 || boxHeight <= 0 || spriteWidth <= 0 || spriteHeight <= 0)
    {
        return layout;
    }

    const int visW = imageWidth > 0 ? imageWidth : spriteWidth;
    const int visH = imageHeight > 0 ? imageHeight : spriteHeight;
    const int offX = imageWidth > 0 ? imageOffsetX : 0;
    const int offY = imageHeight > 0 ? imageOffsetY : 0;

    const double scaleX = static_cast<double>(boxWidth) / static_cast<double>(visW);
    const double scaleY = static_cast<double>(boxHeight) / static_cast<double>(visH);
    const double maxScale = (maxScaleNum > 0 && maxScaleDen > 0)
        ? static_cast<double>(maxScaleNum) / static_cast<double>(maxScaleDen)
        : 1.0;
    const double scale = std::min(std::min(scaleX, scaleY), maxScale);

    const int visDrawW = std::max(1, std::min(boxWidth, static_cast<int>(std::lround(visW * scale))));
    const int visDrawH = std::max(1, std::min(boxHeight, static_cast<int>(std::lround(visH * scale))));
    const int drawW = std::max(1, static_cast<int>(std::lround(spriteWidth * scale)));
    const int drawH = std::max(1, static_cast<int>(std::lround(spriteHeight * scale)));
    const int scaledOffX = static_cast<int>(std::lround(offX * scale));
    const int scaledOffY = static_cast<int>(std::lround(offY * scale));

    layout.DrawX = boxX + (boxWidth - visDrawW) / 2 - scaledOffX;
    layout.DrawY = boxY + (boxHeight - visDrawH) / 2 - scaledOffY;
    layout.DrawWidth = drawW;
    layout.DrawHeight = drawH;
    layout.VisibleWidth = visDrawW;
    layout.VisibleHeight = visDrawH;
    layout.Downscaled = scale < 1.0;
    return layout;
}

// Compute native 1:1 backpack-style icon placement for supply tiles.
// Never upscales small sprites (avoiding blocky distortion); centers stacked
// double-draw sprites (+5, +5) when they fit at 1:1, and only downscales if a
// sprite's visible pixels exceed the icon box.
inline SupplyIconLayout ItemCounters_ComputeBackpackIconLayout(
    int boxX,
    int boxY,
    int boxWidth,
    int boxHeight,
    int spriteWidth,
    int spriteHeight,
    int imageOffsetX,
    int imageOffsetY,
    int imageWidth,
    int imageHeight,
    bool wantDoubleDraw)
{
    SupplyIconLayout layout;
    if (boxWidth <= 0 || boxHeight <= 0 || spriteWidth <= 0 || spriteHeight <= 0)
    {
        return layout;
    }

    const int visW = imageWidth > 0 ? imageWidth : spriteWidth;
    const int visH = imageHeight > 0 ? imageHeight : spriteHeight;
    const int offX = imageWidth > 0 ? imageOffsetX : 0;
    const int offY = imageHeight > 0 ? imageOffsetY : 0;

    constexpr int DoubleDrawShift = 5;
    if (wantDoubleDraw && visW + DoubleDrawShift <= boxWidth && visH + DoubleDrawShift <= boxHeight)
    {
        const int totalW = visW + DoubleDrawShift;
        const int totalH = visH + DoubleDrawShift;
        layout.DrawX = boxX + (boxWidth - totalW) / 2 - offX;
        layout.DrawY = boxY + (boxHeight - totalH) / 2 - offY;
        layout.DrawWidth = spriteWidth;
        layout.DrawHeight = spriteHeight;
        layout.VisibleWidth = totalW;
        layout.VisibleHeight = totalH;
        layout.DoubleDraw = true;
        layout.Downscaled = false;
        return layout;
    }

    if (visW <= boxWidth && visH <= boxHeight)
    {
        layout.DrawX = boxX + (boxWidth - visW) / 2 - offX;
        layout.DrawY = boxY + (boxHeight - visH) / 2 - offY;
        layout.DrawWidth = spriteWidth;
        layout.DrawHeight = spriteHeight;
        layout.VisibleWidth = visW;
        layout.VisibleHeight = visH;
        layout.DoubleDraw = false;
        layout.Downscaled = false;
        return layout;
    }

    layout = ItemCounters_ComputeIconLayout(
        boxX,
        boxY,
        boxWidth,
        boxHeight,
        spriteWidth,
        spriteHeight,
        imageOffsetX,
        imageOffsetY,
        imageWidth,
        imageHeight,
        1,
        1);
    layout.DoubleDraw = false;
    layout.Downscaled = true;
    return layout;
}

constexpr uint16_t ItemCounters_PouchGraphic = 0x0E79;

// Items whose UO tiledata carries TileFlag::Container (0x00200000) even though
// they are books, key rings, kegs, boards, or corpses rather than item bags.
// Opening them during recount pops spellbook/gump/target UI and never exposes
// backpack counter items.
inline bool ItemCounters_IsNonBagContainerGraphic(uint16_t graphic)
{
    switch (graphic)
    {
        // Spellbooks (Magery, Necromancy, Chivalry, Bushido, Ninjitsu,
        // Spellweaving, Mysticism, Mastery/Bard, Book of Arms)
        case 0x0E3B:
        case 0x0EFA:
        case 0x2252:
        case 0x2253:
        case 0x2254:
        case 0x225A:
        case 0x225B:
        case 0x238C:
        case 0x23A0:
        case 0x2D50:
        case 0x2D9D:
        // Runebooks, runic atlases, bulk order books, recipe/powerscroll books, tomes
        case 0x2259:
        case 0x22C5:
        case 0x9981:
        case 0x9982:
        case 0x9A95:
        case 0x9AA7:
        case 0x9C16:
        case 0x9C17:
        case 0xA266:
        case 0xA267:
        case 0xA5CE:
        case 0xA5CF:
        case 0xA5D0:
        case 0xA5D1:
        case 0xA92F:
        case 0xA930:
        // Key rings (including shard resource key rings)
        case 0x1011:
        case 0x1769:
        case 0x176A:
        case 0x176B:
        case 0xA0C1:
        case 0xA0C2:
        case 0xA0C3:
        case 0xA0C4:
        // Potion kegs, game boards, bulletin boards, corpses
        case 0x0E1C:
        case 0x0FA6:
        case 0x1940:
        case 0x1E5E:
        case 0x1E5F:
        case 0x2006:
        case 0x3D65:
        case 0x3D66:
        case 0x3D67:
        case 0x3D68:
        case 0x3D69:
        case 0x3D6A:
        case 0x3D6B:
            return true;
        default:
            return false;
    }
}

inline bool ItemCounters_ShouldSkipAutoOpenBag(uint16_t graphic, bool skipPouches)
{
    if (ItemCounters_IsNonBagContainerGraphic(graphic))
    {
        return true;
    }
    return skipPouches && graphic == ItemCounters_PouchGraphic;
}

inline bool ItemCounters_ShouldRunRecount(
    bool autoRecountEnabled,
    bool manualRecount,
    bool hasActiveConsumer,
    bool containerRestorePending)
{
    if (manualRecount)
    {
        return true;
    }
    return autoRecountEnabled && hasActiveConsumer && !containerRestorePending;
}

enum class ContainerChangeAction
{
    Ignore,
    RefreshCountsOnly,
    RefreshAndQueueBagOpen
};

struct ContainerChangeInput
{
    bool InsidePlayerBackpack = false;
    bool ContainerOpened = false;
    bool IsNewSubcontainer = false;
    bool MatchesTrackedCounter = false;
    bool AutoRecountEnabled = true;
    bool SkipPouches = true;
    uint16_t TargetBagGraphic = 0;
};

inline ContainerChangeAction ItemCounters_DecideContainerChange(const ContainerChangeInput &input)
{
    if (!input.InsidePlayerBackpack)
    {
        return ContainerChangeAction::Ignore;
    }

    if (input.AutoRecountEnabled &&
        !ItemCounters_ShouldSkipAutoOpenBag(input.TargetBagGraphic, input.SkipPouches) &&
        !input.ContainerOpened &&
        (input.IsNewSubcontainer || input.MatchesTrackedCounter))
    {
        return ContainerChangeAction::RefreshAndQueueBagOpen;
    }

    return ContainerChangeAction::RefreshCountsOnly;
}

enum class StatThresholdPolarity
{
    HigherIsBetter,
    LowerIsBetter
};

enum class StatThresholdBand
{
    Default,
    Good,
    Mid,
    Low
};

constexpr uint32_t ItemCounters_ThresholdLowColor = 0xFFFF5555u;
constexpr uint32_t ItemCounters_ThresholdMidColor = 0xFFFFCC44u;
constexpr uint32_t ItemCounters_ThresholdGoodColor = 0xFF7BE07Bu;

inline StatThresholdBand ItemCounters_EvaluateThreshold(
    int value,
    int maxValue,
    int minThreshold,
    int midThreshold,
    StatThresholdPolarity polarity = StatThresholdPolarity::HigherIsBetter)
{
    if (minThreshold <= 0 && midThreshold <= 0)
    {
        return StatThresholdBand::Default;
    }

    const int pct = maxValue > 0
        ? std::max(0, std::min(100, static_cast<int>((static_cast<int64_t>(value) * 100) / maxValue)))
        : value;

    if (polarity == StatThresholdPolarity::LowerIsBetter)
    {
        if (maxValue > 0 && value >= maxValue)
        {
            return StatThresholdBand::Low;
        }
        if (minThreshold > 0 && midThreshold > 0)
        {
            const int warnThreshold = std::min(minThreshold, midThreshold);
            const int critThreshold = std::max(minThreshold, midThreshold);
            if (pct >= critThreshold)
            {
                return StatThresholdBand::Low;
            }
            if (pct >= warnThreshold)
            {
                return StatThresholdBand::Mid;
            }
            return StatThresholdBand::Good;
        }
        if (midThreshold > 0 && pct >= midThreshold)
        {
            return StatThresholdBand::Low;
        }
        if (minThreshold > 0 && pct >= minThreshold)
        {
            return StatThresholdBand::Mid;
        }
        return StatThresholdBand::Good;
    }

    if (minThreshold > 0 && pct <= minThreshold)
    {
        return StatThresholdBand::Low;
    }
    if (midThreshold > 0 && pct <= midThreshold)
    {
        return StatThresholdBand::Mid;
    }
    return StatThresholdBand::Good;
}

inline uint32_t ItemCounters_ThresholdColor(StatThresholdBand band)
{
    switch (band)
    {
        case StatThresholdBand::Low:
            return ItemCounters_ThresholdLowColor;
        case StatThresholdBand::Mid:
            return ItemCounters_ThresholdMidColor;
        case StatThresholdBand::Good:
            return ItemCounters_ThresholdGoodColor;
        case StatThresholdBand::Default:
        default:
            return 0u;
    }
}

} // namespace xuoassist
