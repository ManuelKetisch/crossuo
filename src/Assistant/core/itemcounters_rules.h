// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace xuoassist {

constexpr int ItemCounters_CurrentCatalogVersion = 2;
constexpr uint16_t ItemCounters_AnyHue = 0xFFFFu;

enum class SupplyCategory : uint8_t
{
    Magery = 0,
    Necromancy = 1,
    Mysticism = 2,
    Potions = 3,
    Combat = 4,
    Utility = 5,
    Custom = 6,
    Count = 7
};

constexpr size_t ItemCounters_CategoryCount = static_cast<size_t>(SupplyCategory::Count);

inline bool ItemCounters_IsValidCategory(int value)
{
    return value >= 0 && value < static_cast<int>(SupplyCategory::Count);
}

inline const char *ItemCounters_CategoryId(SupplyCategory category)
{
    switch (category)
    {
        case SupplyCategory::Magery:
            return "magery";
        case SupplyCategory::Necromancy:
            return "necromancy";
        case SupplyCategory::Mysticism:
            return "mysticism";
        case SupplyCategory::Potions:
            return "potions";
        case SupplyCategory::Combat:
            return "combat";
        case SupplyCategory::Utility:
            return "utility";
        case SupplyCategory::Custom:
        default:
            return "custom";
    }
}

inline const char *ItemCounters_CategoryLabel(SupplyCategory category)
{
    switch (category)
    {
        case SupplyCategory::Magery:
            return "Magery";
        case SupplyCategory::Necromancy:
            return "Necromancy";
        case SupplyCategory::Mysticism:
            return "Mysticism & Rare";
        case SupplyCategory::Potions:
            return "Potions";
        case SupplyCategory::Combat:
            return "Combat";
        case SupplyCategory::Utility:
            return "Tools & Utility";
        case SupplyCategory::Custom:
        default:
            return "Custom";
    }
}

inline const char *ItemCounters_CategoryShortLabel(SupplyCategory category)
{
    switch (category)
    {
        case SupplyCategory::Magery:
            return "Magery";
        case SupplyCategory::Necromancy:
            return "Necro";
        case SupplyCategory::Mysticism:
            return "Mystic";
        case SupplyCategory::Potions:
            return "Potions";
        case SupplyCategory::Combat:
            return "Combat";
        case SupplyCategory::Utility:
            return "Tools";
        case SupplyCategory::Custom:
        default:
            return "Custom";
    }
}

inline bool ItemCounters_EqualsIgnoreCaseAscii(const char *a, const char *b)
{
    if (a == nullptr || b == nullptr)
    {
        return false;
    }
    while (*a != '\0' && *b != '\0')
    {
        const auto ca = static_cast<unsigned char>(*a);
        const auto cb = static_cast<unsigned char>(*b);
        if (std::tolower(ca) != std::tolower(cb))
        {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

inline bool ItemCounters_ParseCategory(const char *text, SupplyCategory &out)
{
    if (text == nullptr || text[0] == '\0')
    {
        return false;
    }
    if (ItemCounters_EqualsIgnoreCaseAscii(text, "magery"))
    {
        out = SupplyCategory::Magery;
        return true;
    }
    if (ItemCounters_EqualsIgnoreCaseAscii(text, "necromancy") ||
        ItemCounters_EqualsIgnoreCaseAscii(text, "necro"))
    {
        out = SupplyCategory::Necromancy;
        return true;
    }
    if (ItemCounters_EqualsIgnoreCaseAscii(text, "mysticism") ||
        ItemCounters_EqualsIgnoreCaseAscii(text, "mystic"))
    {
        out = SupplyCategory::Mysticism;
        return true;
    }
    if (ItemCounters_EqualsIgnoreCaseAscii(text, "potions") ||
        ItemCounters_EqualsIgnoreCaseAscii(text, "potion"))
    {
        out = SupplyCategory::Potions;
        return true;
    }
    if (ItemCounters_EqualsIgnoreCaseAscii(text, "combat"))
    {
        out = SupplyCategory::Combat;
        return true;
    }
    if (ItemCounters_EqualsIgnoreCaseAscii(text, "utility") ||
        ItemCounters_EqualsIgnoreCaseAscii(text, "tools"))
    {
        out = SupplyCategory::Utility;
        return true;
    }
    if (ItemCounters_EqualsIgnoreCaseAscii(text, "custom"))
    {
        out = SupplyCategory::Custom;
        return true;
    }
    return false;
}

inline int ItemCounters_ParseCategoryFilter(const char *text)
{
    if (text == nullptr || text[0] == '\0' || ItemCounters_EqualsIgnoreCaseAscii(text, "all"))
    {
        return -1;
    }
    SupplyCategory parsed = SupplyCategory::Custom;
    if (ItemCounters_ParseCategory(text, parsed))
    {
        return static_cast<int>(parsed);
    }
    return -1;
}

inline const char *ItemCounters_CategoryFilterId(int filter)
{
    if (!ItemCounters_IsValidCategory(filter))
    {
        return "all";
    }
    return ItemCounters_CategoryId(static_cast<SupplyCategory>(filter));
}

inline bool ItemCounters_HueMatches(uint16_t filterHue, uint16_t itemColor)
{
    return filterHue == ItemCounters_AnyHue ||
           itemColor == filterHue ||
           static_cast<uint16_t>(itemColor & 0x3FFFu) == filterHue;
}

inline bool ItemCounters_IsCategoryCollapsed(uint32_t collapsedMask, SupplyCategory category)
{
    const uint8_t index = static_cast<uint8_t>(category);
    if (index >= ItemCounters_CategoryCount)
    {
        return false;
    }
    return (collapsedMask & (1u << index)) != 0u;
}

inline uint32_t ItemCounters_ToggleCategoryCollapsed(uint32_t collapsedMask, SupplyCategory category)
{
    const uint8_t index = static_cast<uint8_t>(category);
    if (index >= ItemCounters_CategoryCount)
    {
        return collapsedMask;
    }
    return collapsedMask ^ (1u << index);
}

inline bool ItemCounters_ShouldDisplaySupply(int count, bool highlighted, bool hideZero)
{
    return !hideZero || count != 0 || highlighted;
}

struct DefaultSupplySpec
{
    const char *Name;
    const char *Key;
    uint16_t Graphic;
    uint16_t Hue;
    int Warning;
    SupplyCategory Category;
};

// Complete built-in supply catalog matched to ModernUO reagent, potion, and
// consumable item IDs and hues, organized into logical gameplay groups.
inline const std::array<DefaultSupplySpec, 47> &ItemCounters_GetDefaultCatalog()
{
    static constexpr std::array<DefaultSupplySpec, 47> catalog = {{
    // Magery reagents (8)
    { "Black pearl", "bp", 0x0F7A, ItemCounters_AnyHue, 5, SupplyCategory::Magery },
    { "Blood moss", "bm", 0x0F7B, ItemCounters_AnyHue, 5, SupplyCategory::Magery },
    { "Garlic", "gl", 0x0F84, ItemCounters_AnyHue, 5, SupplyCategory::Magery },
    { "Ginseng", "gs", 0x0F85, ItemCounters_AnyHue, 5, SupplyCategory::Magery },
    { "Mandrake root", "mr", 0x0F86, ItemCounters_AnyHue, 5, SupplyCategory::Magery },
    { "Nightshade", "ns", 0x0F88, ItemCounters_AnyHue, 5, SupplyCategory::Magery },
    { "Spider's silk", "ss", 0x0F8D, ItemCounters_AnyHue, 5, SupplyCategory::Magery },
    { "Sulfurous ash", "sa", 0x0F8C, ItemCounters_AnyHue, 5, SupplyCategory::Magery },

    // Necromancy reagents (5)
    { "Bat wing", "bw", 0x0F78, ItemCounters_AnyHue, 5, SupplyCategory::Necromancy },
    { "Daemon blood", "db", 0x0F7D, ItemCounters_AnyHue, 5, SupplyCategory::Necromancy },
    { "Grave dust", "gd", 0x0F8F, ItemCounters_AnyHue, 5, SupplyCategory::Necromancy },
    { "Nox crystal", "nc", 0x0F8E, ItemCounters_AnyHue, 5, SupplyCategory::Necromancy },
    { "Pig iron", "pi", 0x0F8A, ItemCounters_AnyHue, 5, SupplyCategory::Necromancy },

    // Mysticism & rare reagents (5)
    { "Bone", "bone", 0x0F7E, 0x0000, 0, SupplyCategory::Mysticism },
    { "Daemon bone", "dbone", 0x0F80, ItemCounters_AnyHue, 0, SupplyCategory::Mysticism },
    { "Dragon's blood", "drb", 0x4077, ItemCounters_AnyHue, 0, SupplyCategory::Mysticism },
    { "Fertile dirt", "fd", 0x0F81, ItemCounters_AnyHue, 0, SupplyCategory::Mysticism },
    { "Dead wood", "dw", 0x0F90, ItemCounters_AnyHue, 0, SupplyCategory::Mysticism },

    // Potions: core combat (8) + specialty alchemy (5) = 13
    // Exact hues are required for 0x0F06 (Night Sight / Conflagration / Confusion Blast)
    // and 0x0F0A (Poison / Invisibility / Darkglow / Parasitic).
    { "Heal potion", "heal_pot", 0x0F0C, 0x0000, 2, SupplyCategory::Potions },
    { "Cure potion", "cure_pot", 0x0F07, 0x0000, 2, SupplyCategory::Potions },
    { "Refresh potion", "refresh_pot", 0x0F0B, 0x0000, 2, SupplyCategory::Potions },
    { "Agility potion", "agil_pot", 0x0F08, 0x0000, 0, SupplyCategory::Potions },
    { "Strength potion", "str_pot", 0x0F09, 0x0000, 0, SupplyCategory::Potions },
    { "Explosion potion", "explo_pot", 0x0F0D, 0x0000, 0, SupplyCategory::Potions },
    { "Poison potion", "poison_pot", 0x0F0A, 0x0000, 0, SupplyCategory::Potions },
    { "Night sight potion", "nsight_pot", 0x0F06, 0x0000, 0, SupplyCategory::Potions },
    { "Invisibility potion", "invis_pot", 0x0F0A, 0x048D, 0, SupplyCategory::Potions },
    { "Conflagration potion", "conflag_pot", 0x0F06, 0x0489, 0, SupplyCategory::Potions },
    { "Confusion blast", "confuse_pot", 0x0F06, 0x048D, 0, SupplyCategory::Potions },
    { "Darkglow poison", "darkglow_pot", 0x0F0A, 0x0096, 0, SupplyCategory::Potions },
    { "Parasitic poison", "parasitic_pot", 0x0F0A, 0x017C, 0, SupplyCategory::Potions },

    // Combat supplies & ammo (8)
    { "Bandages", "bandages", 0x0E21, ItemCounters_AnyHue, 10, SupplyCategory::Combat },
    { "Arrows", "arrows", 0x0F3F, ItemCounters_AnyHue, 0, SupplyCategory::Combat },
    { "Crossbow bolts", "bolts", 0x1BFB, ItemCounters_AnyHue, 0, SupplyCategory::Combat },
    { "Orange petals", "opetals", 0x1021, 0x002B, 0, SupplyCategory::Combat },
    { "Smoke bombs", "smoke", 0x2808, 0x0000, 0, SupplyCategory::Combat },
    { "Shuriken", "shuriken", 0x27AC, ItemCounters_AnyHue, 0, SupplyCategory::Combat },
    { "Fukiya darts", "darts", 0x2806, ItemCounters_AnyHue, 0, SupplyCategory::Combat },
    { "Bolas", "bolas", 0x26AC, 0x0000, 0, SupplyCategory::Combat },

    // Tools & utility supplies (8)
    { "Empty bottles", "bottles", 0x0F0E, 0x0000, 0, SupplyCategory::Utility },
    { "Blank scrolls", "bscrolls", 0x0EF3, 0x0000, 0, SupplyCategory::Utility },
    { "Recall scrolls", "recalls", 0x1F4C, 0x0000, 0, SupplyCategory::Utility },
    { "Lockpicks", "lockpicks", 0x14FC, 0x0000, 0, SupplyCategory::Utility },
    { "Oil cloth", "oilcloth", 0x175D, 0x07D1, 0, SupplyCategory::Utility },
    { "Pet ointment", "ointment", 0x0E24, 0x048E, 0, SupplyCategory::Utility },
    { "Powder of translocation", "transloc", 0x26B8, 0x0000, 0, SupplyCategory::Utility },
    { "Zoogi fungus", "zoogi", 0x26B7, 0x0000, 0, SupplyCategory::Utility },
    }};
    return catalog;
}

inline SupplyCategory ItemCounters_InferCategory(uint16_t graphic, uint16_t hue)
{
    const uint16_t maskedHue = hue == ItemCounters_AnyHue ? ItemCounters_AnyHue : static_cast<uint16_t>(hue & 0x3FFFu);
    for (const auto &spec : ItemCounters_GetDefaultCatalog())
    {
        if (spec.Graphic != graphic)
        {
            continue;
        }
        if (spec.Hue == ItemCounters_AnyHue || spec.Hue == maskedHue ||
            (maskedHue == ItemCounters_AnyHue && spec.Hue == 0))
        {
            return spec.Category;
        }
    }
    // Any potion bottle graphic (including custom shard hues) groups under Potions.
    if (graphic >= 0x0F06 && graphic <= 0x0F0D)
    {
        return SupplyCategory::Potions;
    }
    return SupplyCategory::Custom;
}

inline bool ItemCounters_DefaultMatchesExisting(
    const char *existingKey,
    uint16_t existingGraphic,
    uint16_t existingHue,
    const DefaultSupplySpec &spec)
{
    if (existingKey != nullptr && ItemCounters_EqualsIgnoreCaseAscii(existingKey, spec.Key))
    {
        return true;
    }
    if (existingGraphic != spec.Graphic)
    {
        return false;
    }
    const uint16_t maskedExisting = existingHue == ItemCounters_AnyHue
        ? ItemCounters_AnyHue
        : static_cast<uint16_t>(existingHue & 0x3FFFu);
    if (maskedExisting == spec.Hue)
    {
        return true;
    }
    // Treat an existing hue-0 counter as already covering an AnyHue built-in default
    // (for example a user-added Bat Wing targeted with hue 0), while keeping
    // distinct hued potions sharing 0x0F06 or 0x0F0A separate.
    if (spec.Hue == ItemCounters_AnyHue && maskedExisting == 0)
    {
        return true;
    }
    return false;
}

struct SupplyLayoutEntryInput
{
    size_t SourceIndex = 0;
    SupplyCategory Category = SupplyCategory::Custom;
    int Count = 0;
    bool Highlighted = false;
};

struct SupplyLayoutGroup
{
    SupplyCategory Category = SupplyCategory::Custom;
    int TotalEntries = 0;
    int StockedEntries = 0;
    bool HasKnownCounts = false;
    bool Collapsed = false;
    std::vector<size_t> SourceIndices;
};

struct SupplyLayoutSectionSlice
{
    SupplyCategory Category = SupplyCategory::Custom;
    int TotalEntries = 0;
    int StockedEntries = 0;
    bool HasKnownCounts = false;
    bool Collapsed = false;
    bool ShowHeader = false;
    std::vector<size_t> SourceIndices;
};

struct SupplyLayoutPage
{
    std::vector<SupplyLayoutSectionSlice> Sections;
    int Height = 0;
};

struct SupplyLayoutResult
{
    std::array<int, ItemCounters_CategoryCount> CategoryCounts{};
    std::vector<SupplyLayoutPage> Pages;
};

inline SupplyLayoutResult ItemCounters_BuildGroupedSupplyLayout(
    const std::vector<SupplyLayoutEntryInput> &entries,
    int activeCategory,
    bool hideZero,
    uint32_t collapsedMask,
    int columns,
    int cellHeight,
    int headerHeight,
    int groupGap,
    int maxPageHeight)
{
    SupplyLayoutResult result;
    const int cols = std::max(1, columns);
    const int safeCellH = std::max(1, cellHeight);
    const int safeHeaderH = std::max(0, headerHeight);
    const int safeGap = std::max(0, groupGap);
    const int safeMaxPageH = std::max(safeHeaderH + safeCellH, maxPageHeight);
    const bool singleCategory = ItemCounters_IsValidCategory(activeCategory);

    std::array<SupplyLayoutGroup, ItemCounters_CategoryCount> buckets{};
    for (size_t c = 0; c < ItemCounters_CategoryCount; ++c)
    {
        buckets[c].Category = static_cast<SupplyCategory>(c);
        buckets[c].Collapsed = !singleCategory &&
            ItemCounters_IsCategoryCollapsed(collapsedMask, buckets[c].Category);
    }

    for (const auto &entry : entries)
    {
        const size_t catIndex = static_cast<size_t>(entry.Category) < ItemCounters_CategoryCount
            ? static_cast<size_t>(entry.Category)
            : static_cast<size_t>(SupplyCategory::Custom);
        result.CategoryCounts[catIndex] += 1;

        if (singleCategory && static_cast<int>(catIndex) != activeCategory)
        {
            continue;
        }
        auto &group = buckets[catIndex];
        group.TotalEntries += 1;
        if (entry.Count >= 0)
        {
            group.HasKnownCounts = true;
            if (entry.Count > 0)
            {
                group.StockedEntries += 1;
            }
        }
        if (ItemCounters_ShouldDisplaySupply(entry.Count, entry.Highlighted, hideZero))
        {
            group.SourceIndices.push_back(entry.SourceIndex);
        }
    }

    SupplyLayoutPage current;
    const auto flushPage = [&]() {
        if (!current.Sections.empty())
        {
            result.Pages.push_back(std::move(current));
            current = {};
        }
    };

    for (size_t c = 0; c < ItemCounters_CategoryCount; ++c)
    {
        const auto &group = buckets[c];
        if (group.TotalEntries == 0)
        {
            continue;
        }
        if (hideZero && group.SourceIndices.empty())
        {
            continue;
        }

        const bool showHeader = !singleCategory;
        if (group.Collapsed)
        {
            const int gapBefore = current.Sections.empty() ? 0 : safeGap;
            const int needed = gapBefore + safeHeaderH;
            if (!current.Sections.empty() && current.Height + needed > safeMaxPageH)
            {
                flushPage();
            }
            SupplyLayoutSectionSlice slice;
            slice.Category = group.Category;
            slice.TotalEntries = group.TotalEntries;
            slice.StockedEntries = group.StockedEntries;
            slice.HasKnownCounts = group.HasKnownCounts;
            slice.Collapsed = true;
            slice.ShowHeader = showHeader;
            current.Height += (current.Sections.empty() ? 0 : safeGap) + (showHeader ? safeHeaderH : 0);
            current.Sections.push_back(std::move(slice));
            continue;
        }

        size_t offset = 0;
        const size_t totalVisible = group.SourceIndices.size();
        while (offset < totalVisible)
        {
            const int gapBefore = current.Sections.empty() ? 0 : safeGap;
            const int hdr = showHeader ? safeHeaderH : 0;
            const int minNeeded = gapBefore + hdr + safeCellH;
            if (!current.Sections.empty() && current.Height + minNeeded > safeMaxPageH)
            {
                flushPage();
            }
            const int effectiveGap = current.Sections.empty() ? 0 : safeGap;
            const int avail = std::max(safeCellH, safeMaxPageH - current.Height - effectiveGap - hdr);
            const int maxRows = std::max(1, avail / safeCellH);
            const size_t maxItems = static_cast<size_t>(maxRows) * static_cast<size_t>(cols);
            const size_t take = std::min(totalVisible - offset, maxItems);
            const int rowsTaken = static_cast<int>((take + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));

            SupplyLayoutSectionSlice slice;
            slice.Category = group.Category;
            slice.TotalEntries = group.TotalEntries;
            slice.StockedEntries = group.StockedEntries;
            slice.HasKnownCounts = group.HasKnownCounts;
            slice.Collapsed = false;
            slice.ShowHeader = showHeader;
            slice.SourceIndices.assign(
                group.SourceIndices.begin() + static_cast<ptrdiff_t>(offset),
                group.SourceIndices.begin() + static_cast<ptrdiff_t>(offset + take));
            current.Height += effectiveGap + hdr + rowsTaken * safeCellH;
            current.Sections.push_back(std::move(slice));
            offset += take;
        }
    }

    flushPage();
    return result;
}

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
