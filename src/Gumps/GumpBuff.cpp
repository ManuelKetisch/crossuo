// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#include "GumpBuff.h"
#include "GumpBuff_rules.h"
#include "../Point.h"
#include "../CrossUO.h"
#include "../ToolTip.h"
#include "../SelectedObject.h"
#include "../Managers/FontsManager.h"
#include "../Managers/ConfigManager.h"
#include "xuocore/enumlist.h"
#include <algorithm>

CGumpBuff::CGumpBuff(short x, short y)
    : CGump(GT_BUFF, 0, x, y)
{
    Graphic = 0x7580;
    m_Locker.Serial = ID_GB_LOCK_MOVING;

    Add(new CGUIGumppic(Graphic, 0, 0));
    Add(new CGUIHitBox(0, 0, 0, 0, 0));
    Add(new CGUIButton(ID_GB_NEXT_WINDOW_DIRECTION, 0x7585, 0x7589, 0x7589, 0, 0));
    Add(new CGUIAlphaBlending(true, 0.1f));
    Add(new CGUIAlphaBlending(false, 0.0f));
}

CGumpBuff::~CGumpBuff()
= default;

bool CGumpBuff::CanBeDisplayed()
{
    return g_ConfigManager.ToggleBufficonWindow;
}

void CGumpBuff::UpdateBuffIcons()
{
    for (CBaseGUI *item = (CBaseGUI *)m_Items; item != nullptr;)
    {
        CBaseGUI *next = (CBaseGUI *)item->m_Next;

        if (item->Type == GOT_BUFF)
        {
            CGUIBuff *buff = (CGUIBuff *)item;

            if (xuobuff::HasTimer(buff->Timer))
            {
                const uint32_t remainingMs = xuobuff::ComputeRemainingMs(buff->Timer, g_Ticks);
                if (remainingMs == 0u)
                {
                    if (g_ToolTip.m_Object == buff)
                    {
                        g_ToolTip.Reset();
                    }
                    Delete(buff);
                    WantUpdateContent = true;
                    item = next;
                    continue;
                }

                const uint32_t remainingSec = xuobuff::RemainingSecondsCeil(remainingMs);
                if (remainingSec != buff->LastDisplayedSeconds)
                {
                    buff->LastDisplayedSeconds = remainingSec;
                    WantRedraw = true;
                }

                if (remainingMs <= static_cast<uint32_t>(USE_ALPHA_BLENDING_WHEN_TIMER_LESS))
                {
                    WantRedraw = true;

                    int alpha = buff->Alpha;
                    const int addVal = std::max(
                        1,
                        static_cast<int>(
                            (static_cast<uint32_t>(USE_ALPHA_BLENDING_WHEN_TIMER_LESS) - remainingMs) /
                            static_cast<uint32_t>(ALPHA_CHANGE_KOEFF)));

                    if (buff->DecAlpha)
                    {
                        alpha -= addVal;

                        if (alpha <= MUNIMUM_ICON_ALPHA)
                        {
                            buff->DecAlpha = false;
                            alpha = MUNIMUM_ICON_ALPHA;
                        }
                    }
                    else
                    {
                        alpha += addVal;

                        if (alpha >= 255)
                        {
                            buff->DecAlpha = true;
                            alpha = 255;
                        }
                    }

                    buff->Alpha = static_cast<uint8_t>(alpha);
                }
                else if (buff->Alpha != 0xFF)
                {
                    buff->Alpha = 0xFF;
                    buff->DecAlpha = true;
                    WantRedraw = true;
                }
            }
        }

        item = next;
    }
}

void CGumpBuff::AddBuff(uint16_t id, uint16_t timer, const wstr_t &text)
{
    const uint32_t totalDurationMs = static_cast<uint32_t>(timer) * 1000u;
    const uint32_t ticks = timer != 0u ? (g_Ticks + totalDurationMs) : xuobuff::kBuffIndefiniteTimer;

    for (CBaseGUI *item = (CBaseGUI *)m_Items; item != nullptr; item = (CBaseGUI *)item->m_Next)
    {
        if (item->Type == GOT_BUFF && item->Graphic == id)
        {
            CGUIBuff *buff = (CGUIBuff *)item;

            buff->Timer = ticks;
            buff->TotalDurationMs = totalDurationMs;
            buff->Text = text;
            buff->DecAlpha = true;
            buff->Alpha = 0xFF;
            buff->TooltipTimer = 0;
            buff->LastDisplayedSeconds = 0xFFFFFFFFu;
            buff->LastTooltipSeconds = 0xFFFFFFFFu;

            MoveToBack(buff);
            WantUpdateContent = true;

            return;
        }
    }

    Add(new CGUIBuff(id, ticks, totalDurationMs, text));
    WantUpdateContent = true;
}

void CGumpBuff::DeleteBuff(uint16_t id)
{
    for (CBaseGUI *item = (CBaseGUI *)m_Items; item != nullptr; item = (CBaseGUI *)item->m_Next)
    {
        if (item->Type == GOT_BUFF && item->Graphic == id)
        {
            if (g_ToolTip.m_Object == item)
            {
                g_ToolTip.Reset();
            }
            Delete(item);
            WantUpdateContent = true;

            break;
        }
    }
}

void CGumpBuff::InitToolTip()
{
    if (g_SelectedObject.Serial == ID_GB_NEXT_WINDOW_DIRECTION)
    {
        g_ToolTip.Set(L"Rotate buff bar direction", 180);
    }
    else if (g_SelectedObject.Object != nullptr)
    {
        for (CBaseGUI *item = (CBaseGUI *)m_Items; item != nullptr; item = (CBaseGUI *)item->m_Next)
        {
            if (item != g_SelectedObject.Object)
            {
                continue;
            }

            if (item->Type == GOT_BUFF)
            {
                CGUIBuff *buff = (CGUIBuff *)item;
                const bool hasTimer = xuobuff::HasTimer(buff->Timer);
                const uint32_t remainingMs = xuobuff::ComputeRemainingMs(buff->Timer, g_Ticks);
                const uint32_t remainingSec = xuobuff::RemainingSecondsCeil(remainingMs);
                const wstr_t tip = xuobuff::BuildTooltipText(buff->Text, remainingSec, hasTimer);

                if (g_ToolTip.m_Object != buff)
                {
                    buff->LastTooltipSeconds = remainingSec;
                    g_ToolTip.Set(tip, 240);
                }
                else if (buff->LastTooltipSeconds != remainingSec || g_ToolTip.Data != tip)
                {
                    buff->LastTooltipSeconds = remainingSec;
                    const uint32_t prevTimer = g_ToolTip.Timer;
                    g_ToolTip.Reset();
                    g_ToolTip.Set(tip, 240);
                    g_ToolTip.Timer = prevTimer;
                    g_ToolTip.Use = !(prevTimer > g_Ticks);
                }
                else
                {
                    g_ToolTip.Set(tip, 240);
                }
            }
            else
            {
                g_ToolTip.Set(L"Buffs and Debuffs will appear here.", 140);
            }

            break;
        }
    }
}

void CGumpBuff::GetGumpStatus(
    CPoint2Di &ball,
    CPoint2Di &items,
    bool &useX,
    bool &decX,
    bool &decY,
    CPoint2Di &startGump,
    CSize &endGump)
{
    startGump.X = 0;
    startGump.Y = 0;

    endGump = g_Game.GetGumpDimension(Graphic);

    switch (Graphic)
    {
        case 0x757F: //v
        {
            ball.X = 0;
            ball.Y = 0;
            items.X = 16;
            items.Y = 25;
            decY = false;
            decX = false;
            useX = false;
            break;
        }
        case 0x7581: //^
        {
            ball.X = 34;
            ball.Y = 78;
            items.X = 4;
            items.Y = 24;
            decY = true;
            decX = false;
            useX = false;
            break;
        }
        case 0x7582: //<
        {
            ball.X = 76;
            ball.Y = 36;
            items.X = 28;
            items.Y = 5;
            decY = false;
            decX = true;
            useX = true;
            break;
        }
        case 0x7580: //>
        default:
        {
            ball.X = -2;
            ball.Y = 36;
            items.X = 20;
            items.Y = 5;
            decY = false;
            decX = false;
            useX = true;
            break;
        }
    }

    CPoint2Di itemsOfs = items;
    int maxSlotW = 0;
    int maxSlotH = 0;

    for (CBaseGUI *item = (CBaseGUI *)m_Items; item != nullptr; item = (CBaseGUI *)item->m_Next)
    {
        if (item->Type != GOT_BUFF)
        {
            continue;
        }

        const CSize slotDim = item->GetSize();
        maxSlotW = std::max(maxSlotW, slotDim.Width);
        maxSlotH = std::max(maxSlotH, slotDim.Height);

        if (useX)
        {
            if (decX)
            {
                itemsOfs.X -= slotDim.Width + BUFF_ITEM_STEP_OFFSET_X;
            }
            else
            {
                itemsOfs.X += slotDim.Width + BUFF_ITEM_STEP_OFFSET_X;
            }
        }
        else
        {
            if (decY)
            {
                itemsOfs.Y -= slotDim.Height + BUFF_ITEM_STEP_OFFSET_Y;
            }
            else
            {
                itemsOfs.Y += slotDim.Height + BUFF_ITEM_STEP_OFFSET_Y;
            }
        }
    }

    if (useX)
    {
        if (decX)
        {
            itemsOfs.X -= 12;
        }
        else
        {
            itemsOfs.X += 12;
        }
        if (maxSlotH > 0)
        {
            endGump.Height = std::max(endGump.Height, items.Y + maxSlotH + 6);
        }
    }
    else
    {
        if (decY)
        {
            itemsOfs.Y -= 12;
        }
        else
        {
            itemsOfs.Y += 12;
        }
        if (maxSlotW > 0)
        {
            endGump.Width = std::max(endGump.Width, items.X + maxSlotW + 6);
        }
    }

    if (itemsOfs.X < startGump.X)
    {
        startGump.X = itemsOfs.X;
    }

    if (itemsOfs.Y < startGump.Y)
    {
        startGump.Y = itemsOfs.Y;
    }

    if (itemsOfs.X > endGump.Width)
    {
        endGump.Width = itemsOfs.X;
    }

    if (itemsOfs.Y > endGump.Height)
    {
        endGump.Height = itemsOfs.Y;
    }
}

void CGumpBuff::PrepareContent()
{
    if (Graphic < 0x757F || Graphic > 0x7582)
    {
        Graphic = 0x7580;
        WantUpdateContent = true;
    }
}

void CGumpBuff::UpdateContent()
{
    bool decX = false;
    bool decY = false;
    bool useX = true;

    CPoint2Di ballCoordinates;
    CPoint2Di startCoordinates;
    CPoint2Di startGump;
    CSize endGump;

    GetGumpStatus(ballCoordinates, startCoordinates, useX, decX, decY, startGump, endGump);

    //Body
    CBaseGUI *gui = (CBaseGUI *)m_Items;
    gui->Graphic = Graphic;

    //Selection zone
    gui = (CBaseGUI *)gui->m_Next;
    gui->SetX(startGump.X);
    gui->SetY(startGump.Y);
    ((CGUIPolygonal *)gui)->Width = std::max(0, endGump.Width - startGump.X);
    ((CGUIPolygonal *)gui)->Height = std::max(0, endGump.Height - startGump.Y);

    //Crystall ball
    gui = (CBaseGUI *)gui->m_Next;
    gui->SetX(ballCoordinates.X);
    gui->SetY(ballCoordinates.Y);

    gui = nullptr;

    for (CBaseGUI *item = (CBaseGUI *)m_Items; item != nullptr; item = (CBaseGUI *)item->m_Next)
    {
        if (item->Type != GOT_BUFF)
        {
            if (item->Type == GOT_BLENDING && !item->Enabled)
            {
                gui = item;
            }

            continue;
        }

        CGUIBuff *buff = (CGUIBuff *)item;

        const CSize slotDim = buff->GetSize();
        buff->SetX(startCoordinates.X);
        buff->SetY(startCoordinates.Y);

        if (useX)
        {
            if (decX)
            {
                startCoordinates.X -= slotDim.Width + BUFF_ITEM_STEP_OFFSET_X;
            }
            else
            {
                startCoordinates.X += slotDim.Width + BUFF_ITEM_STEP_OFFSET_X;
            }
        }
        else
        {
            if (decY)
            {
                startCoordinates.Y -= slotDim.Height + BUFF_ITEM_STEP_OFFSET_Y;
            }
            else
            {
                startCoordinates.Y += slotDim.Height + BUFF_ITEM_STEP_OFFSET_Y;
            }
        }
    }

    // Disable transparency and move the gump to the back of the queue
    if (gui != nullptr)
    {
        MoveToBack(gui);
    }
}

void CGumpBuff::GUMP_BUTTON_EVENT_C
{
    if (serial == ID_GB_NEXT_WINDOW_DIRECTION)
    {
        switch (Graphic)
        {
            case 0x7580:
            {
                Graphic = 0x7582;
                break;
            }
            case 0x7581:
            {
                Graphic = 0x757F;
                break;
            }
            case 0x7582:
            {
                Graphic = 0x7581;
                break;
            }
            case 0x757F:
            default:
            {
                Graphic = 0x7580;
                break;
            }
        }

        WantUpdateContent = true;
    }
    else if (serial == ID_GB_LOCK_MOVING)
    {
        LockMoving = !LockMoving;
    }
}
