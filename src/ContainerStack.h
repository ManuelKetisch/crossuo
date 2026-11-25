// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2017 Hotride

#pragma once

#include <stdint.h>
#include <deque>
#include <vector>
#include "UseItemsList_rules.h"

class CContainerStackItem
{
public:
    uint32_t Serial = 0;
    short X = 0;
    short Y = 0;
    short MinimizedX = 0;
    short MinimizedY = 0;
    bool Minimized = false;
    bool LockMoving = false;

    CContainerStackItem(
        int serial,
        short x,
        short y,
        short minimizedX,
        short minimizedY,
        bool minimized,
        bool lockMoving);
    ~CContainerStackItem() = default;
};

extern std::deque<CContainerStackItem> g_ContainerStack;
extern std::vector<xuo_container_restore::SavedContainerPosition> g_ContainerPositions;

extern uint32_t g_CheckContainerStackTimer;

void SaveContainerPosition(
    uint32_t serial,
    short x,
    short y,
    short minimizedX,
    short minimizedY,
    bool lockMoving);

const xuo_container_restore::SavedContainerPosition *FindSavedContainerPosition(uint32_t serial);

void ClearContainerPositions();
