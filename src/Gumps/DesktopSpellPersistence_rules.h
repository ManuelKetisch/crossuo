// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <vector>
#include <xuocore/enumlist.h>

// Pure, header-only rules for desktop layout debounced persistence and
// cross-book spell icon normalization / group clamping.
struct DesktopSaveDebouncer
{
    static constexpr uint32_t DebounceDelayMs = 1000u;
    static constexpr uint32_t MaxDirtyDelayMs = 5000u;
    static constexpr uint32_t PeriodicIntervalMs = 60000u;

    bool Dirty = false;
    bool HasSaved = false;
    uint32_t FirstDirtyTick = 0u;
    uint32_t DirtyDeadline = 0u;
    uint32_t NextPeriodicSave = 0u;

    static bool TickReached(uint32_t now, uint32_t target)
    {
        return static_cast<int32_t>(now - target) >= 0;
    }

    void Reset()
    {
        Dirty = false;
        HasSaved = false;
        FirstDirtyTick = 0u;
        DirtyDeadline = 0u;
        NextPeriodicSave = 0u;
    }

    void MarkDirty(uint32_t now)
    {
        if (!Dirty)
        {
            Dirty = true;
            FirstDirtyTick = now;
            DirtyDeadline = now + DebounceDelayMs;
            return;
        }

        const uint32_t candidate = now + DebounceDelayMs;
        const uint32_t maxDeadline = FirstDirtyTick + MaxDirtyDelayMs;
        DirtyDeadline = TickReached(candidate, maxDeadline) ? maxDeadline : candidate;
    }

    void MarkSaved(uint32_t now)
    {
        Dirty = false;
        HasSaved = true;
        FirstDirtyTick = 0u;
        DirtyDeadline = 0u;
        NextPeriodicSave = now + PeriodicIntervalMs;
    }

    bool ShouldSave(uint32_t now, bool inGame, bool configLoaded) const
    {
        if (!inGame || !configLoaded)
        {
            return false;
        }
        if (Dirty && TickReached(now, DirtyDeadline))
        {
            return true;
        }
        if (HasSaved && TickReached(now, NextPeriodicSave))
        {
            return true;
        }
        return false;
    }
};

inline bool ShouldTriggerDesktopSaveForGump(GUMP_TYPE type)
{
    switch (type)
    {
        case GT_PAPERDOLL:
        case GT_STATUSBAR:
        case GT_MINIMAP:
        case GT_WORLD_MAP:
        case GT_SKILLS:
        case GT_JOURNAL:
        case GT_CONTAINER:
        case GT_SPELLBOOK:
        case GT_SPELL:
        case GT_MENUBAR:
        case GT_BUFF:
        case GT_CONSOLE_TYPE:
        case GT_SKILL:
        case GT_COMBAT_BOOK:
        case GT_ABILITY:
        case GT_RACIAL_ABILITIES_BOOK:
        case GT_RACIAL_ABILITY:
        case GT_PROPERTY_ICON:
        case GT_RESOURCETRACKER:
        case GT_FPS_COUNTER:
            return true;
        default:
            return false;
    }
}

struct DesktopCoordPoint
{
    int X = 0;
    int Y = 0;
};

// Shifts a group of icons together if any member starts at a negative X or Y,
// keeping the group on-screen without collapsing relative icon alignment.
inline void NormalizeDesktopGroupCoords(std::vector<DesktopCoordPoint> &points)
{
    if (points.empty())
    {
        return;
    }
    int minX = points[0].X;
    int minY = points[0].Y;
    for (const auto &pt : points)
    {
        if (pt.X < minX)
        {
            minX = pt.X;
        }
        if (pt.Y < minY)
        {
            minY = pt.Y;
        }
    }
    const int shiftX = minX < 0 ? -minX : 0;
    const int shiftY = minY < 0 ? -minY : 0;
    if (shiftX == 0 && shiftY == 0)
    {
        return;
    }
    for (auto &pt : points)
    {
        pt.X += shiftX;
        pt.Y += shiftY;
    }
}
