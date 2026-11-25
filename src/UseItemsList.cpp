// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2017 Hotride

#include "CrossUO.h"
#include "UseItemsList.h"
#include "GameObjects/GameWorld.h"
#include "Managers/GumpManager.h"
#include "Globals.h"

CUseItemActions g_UseItemActions;

void CUseItemActions::Add(int serial)
{
    m_Queue.Add(static_cast<uint32_t>(serial));
}

void CUseItemActions::NoteActionSent()
{
    m_Queue.NoteActionSent(g_Ticks);
}

void CUseItemActions::Process()
{
    if (g_World == nullptr)
    {
        return;
    }

    const auto step = m_Queue.Step(
        g_Ticks,
        [](uint32_t serial) { return g_World->FindWorldObject(serial) != nullptr; },
        [](uint32_t serial) {
            if (serial < 0x40000000u)
            {
                return g_GumpManager.GetGump(serial, 0, GT_PAPERDOLL) != nullptr;
            }
            return g_GumpManager.GetGump(serial, 0, GT_CONTAINER) != nullptr ||
                   g_GumpManager.GetGump(serial, 0, GT_SPELLBOOK) != nullptr;
        });

    if (step.Action == xuo_container_restore::StepAction::SendPaperdoll)
    {
        g_Game.PaperdollReq(step.Serial);
    }
    else if (step.Action == xuo_container_restore::StepAction::SendDoubleClick)
    {
        g_Game.DoubleClick(step.Serial);
    }
}
