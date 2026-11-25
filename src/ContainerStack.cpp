// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2017 Hotride

#include "ContainerStack.h"
#include <deque>

std::deque<CContainerStackItem> g_ContainerStack;
std::vector<xuo_container_restore::SavedContainerPosition> g_ContainerPositions;
uint32_t g_CheckContainerStackTimer = 0;

CContainerStackItem::CContainerStackItem(
    int serial,
    short x,
    short y,
    short minimizedX,
    short minimizedY,
    bool minimized,
    bool lockMoving)
    : Serial(serial)
    , X(x)
    , Y(y)
    , MinimizedX(minimizedX)
    , MinimizedY(minimizedY)
    , Minimized(minimized)
    , LockMoving(lockMoving)
{
}

void SaveContainerPosition(
    uint32_t serial,
    short x,
    short y,
    short minimizedX,
    short minimizedY,
    bool lockMoving)
{
    xuo_container_restore::SavedContainerPosition entry;
    entry.Serial = serial;
    entry.X = x;
    entry.Y = y;
    entry.MinimizedX = minimizedX;
    entry.MinimizedY = minimizedY;
    entry.LockMoving = lockMoving;
    xuo_container_restore::UpsertSavedContainerPosition(g_ContainerPositions, entry);
}

const xuo_container_restore::SavedContainerPosition *FindSavedContainerPosition(uint32_t serial)
{
    return xuo_container_restore::FindSavedContainerPositionIn(g_ContainerPositions, serial);
}

void ClearContainerPositions()
{
    g_ContainerPositions.clear();
}
