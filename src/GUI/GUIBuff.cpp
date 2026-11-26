// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#include "GUIBuff.h"
#include "../CrossUO.h"
#include "../Globals.h"
#include "../Gumps/GumpBuff_rules.h"
#include "../Managers/FontsManager.h"
#include "../Managers/MouseManager.h"
#include "../Renderer/RenderAPI.h"
#include "../Utility/PerfMarker.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
float4 UrgencyAccentColor(xuobuff::BuffUrgency urgency)
{
    switch (urgency)
    {
        case xuobuff::BuffUrgency::Critical:
            return float4{ 0.94f, 0.28f, 0.25f, 0.98f };
        case xuobuff::BuffUrgency::Warning:
            return float4{ 0.95f, 0.74f, 0.22f, 0.96f };
        case xuobuff::BuffUrgency::Normal:
            return float4{ 0.34f, 0.78f, 0.46f, 0.95f };
        case xuobuff::BuffUrgency::Indefinite:
        default:
            return float4{ 0.38f, 0.66f, 0.84f, 0.92f };
    }
}

uint16_t UrgencyTimerTextHue(xuobuff::BuffUrgency urgency)
{
    switch (urgency)
    {
        case xuobuff::BuffUrgency::Critical:
            return 0x0021; // bright red
        case xuobuff::BuffUrgency::Warning:
            return 0x0035; // warm gold/amber
        case xuobuff::BuffUrgency::Indefinite:
            return 0x005A; // soft ice blue
        case xuobuff::BuffUrgency::Normal:
        default:
            return 0x0481; // crisp white
    }
}
} // namespace

CGUIBuff::CGUIBuff(uint16_t graphic, uint32_t timer, uint32_t totalDurationMs, const wstr_t &text)
    : CGUIDrawObject(GOT_BUFF, 0, graphic, 0, 0, 0)
    , Timer(timer)
    , TotalDurationMs(totalDurationMs)
    , Text(text)
{
}

CGUIBuff::~CGUIBuff()
{
    m_TimerSprite.Clear();
}

CSize CGUIBuff::GetSize()
{
    int iconW = 28;
    int iconH = 28;
    if (auto *spr = g_Game.ExecuteGump(GetDrawGraphic()))
    {
        if (spr->Width > 0)
        {
            iconW = spr->Width;
        }
        if (spr->Height > 0)
        {
            iconH = spr->Height;
        }
    }
    const auto slot = xuobuff::ComputeSlotSize(iconW, iconH);
    return CSize(slot.Width, slot.Height);
}

void CGUIBuff::Draw(bool checktrans)
{
    ScopedPerfMarker(__FUNCTION__);

    auto *spr = g_Game.ExecuteGump(GetDrawGraphic());
    const int iconW = (spr != nullptr && spr->Width > 0) ? spr->Width : 28;
    const int iconH = (spr != nullptr && spr->Height > 0) ? spr->Height : 28;
    const auto slot = xuobuff::ComputeSlotSize(iconW, iconH);

    const bool hasTimer = xuobuff::HasTimer(Timer);
    const uint32_t remainingMs = xuobuff::ComputeRemainingMs(Timer, g_Ticks);
    const uint32_t remainingSec = xuobuff::RemainingSecondsCeil(remainingMs);
    const auto urgency = xuobuff::ClassifyUrgency(remainingSec, hasTimer);
    const float fraction = xuobuff::ComputeProgressFraction(remainingMs, TotalDurationMs, hasTimer);
    const float4 accent = UrgencyAccentColor(urgency);

    const int boxW = iconW + xuobuff::kBuffIconFramePad;
    const int boxH = iconH + xuobuff::kBuffIconFramePad;
    const int boxX = m_X + (slot.Width - boxW) / 2;
    const int boxY = m_Y;

    RenderAdd_SetBlend(
        g_renderCmdList,
        BlendStateCmd{ BlendFactor::BlendFactor_SrcAlpha,
                       BlendFactor::BlendFactor_OneMinusSrcAlpha });
    RenderAdd_SetDrawMode(g_renderCmdList, SetDrawModeCmd{ SDM_NO_COLOR });
    RenderAdd_SetColor(g_renderCmdList, SetColorCmd{ { 1.f, 1.f, 1.f, 1.f } });

    // Outer dark bezel + colored urgency rim + dark icon well
    RenderAdd_DrawUntexturedQuad(
        g_renderCmdList,
        DrawUntexturedQuadCmd{
            boxX, boxY, static_cast<uint32_t>(boxW), static_cast<uint32_t>(boxH),
            { 0.05f, 0.07f, 0.10f, 0.94f } });
    RenderAdd_DrawUntexturedQuad(
        g_renderCmdList,
        DrawUntexturedQuadCmd{
            boxX + 1, boxY + 1, static_cast<uint32_t>(boxW - 2), static_cast<uint32_t>(boxH - 2),
            accent });
    RenderAdd_DrawUntexturedQuad(
        g_renderCmdList,
        DrawUntexturedQuadCmd{
            boxX + 2, boxY + 2, static_cast<uint32_t>(iconW), static_cast<uint32_t>(iconH),
            { 0.04f, 0.05f, 0.07f, 0.95f } });

    if (spr != nullptr && spr->Texture)
    {
        RenderAdd_SetColor(g_renderCmdList, SetColorCmd{ { 1.f, 1.f, 1.f, Alpha / 255.f } });
        spr->Texture->Draw(boxX + 2, boxY + 2, checktrans);
        RenderAdd_SetColor(g_renderCmdList, SetColorCmd{ { 1.f, 1.f, 1.f, 1.f } });
    }

    // Sleek WoW-style duration progress bar directly under the icon
    const int barX = m_X + 1;
    const int barY = boxY + boxH + 1;
    const int barW = std::max(4, slot.Width - 2);
    RenderAdd_DrawUntexturedQuad(
        g_renderCmdList,
        DrawUntexturedQuadCmd{
            barX, barY, static_cast<uint32_t>(barW), static_cast<uint32_t>(xuobuff::kBuffProgressBarHeight),
            { 0.07f, 0.09f, 0.13f, 0.92f } });

    const int fillW = hasTimer
        ? (remainingMs > 0u ? std::max(1, static_cast<int>(std::lround(barW * fraction))) : 0)
        : barW;
    if (fillW > 0)
    {
        RenderAdd_DrawUntexturedQuad(
            g_renderCmdList,
            DrawUntexturedQuadCmd{
                barX, barY, static_cast<uint32_t>(std::min(fillW, barW)),
                static_cast<uint32_t>(xuobuff::kBuffProgressBarHeight),
                accent });
    }

    // Dark countdown pill + cached black-outlined exact duration label
    const int pillY = barY + xuobuff::kBuffProgressBarHeight + 1;
    RenderAdd_DrawUntexturedQuad(
        g_renderCmdList,
        DrawUntexturedQuadCmd{
            m_X, pillY, static_cast<uint32_t>(slot.Width), static_cast<uint32_t>(xuobuff::kBuffTimerPillHeight),
            { 0.05f, 0.07f, 0.10f, 0.86f } });

    const astr_t timerText = xuobuff::FormatBarTimer(remainingSec, hasTimer);
    const uint16_t timerHue = UrgencyTimerTextHue(urgency);
    if (m_CachedTimerText != timerText || m_CachedTimerHue != timerHue || !m_TimerSprite.Texture)
    {
        m_CachedTimerText = timerText;
        m_CachedTimerHue = timerHue;
        m_TimerSprite.Clear();
        g_FontManager.SetUseHTML(false);
        g_FontManager.GenerateW(
            1,
            m_TimerSprite,
            wstr_from_utf8(timerText),
            timerHue,
            5,
            slot.Width,
            TS_CENTER,
            UOFONT_BLACK_BORDER);
    }

    if (m_TimerSprite.Texture)
    {
        const int textY = pillY + std::max(0, (xuobuff::kBuffTimerPillHeight - m_TimerSprite.Height) / 2);
        RenderAdd_SetColor(g_renderCmdList, SetColorCmd{ { 1.f, 1.f, 1.f, 1.f } });
        m_TimerSprite.Draw(m_X, textY, false);
    }
}

bool CGUIBuff::Select()
{
    const CSize size = GetSize();
    const int localX = g_MouseManager.Position.X - m_X;
    const int localY = g_MouseManager.Position.Y - m_Y;
    return localX >= 0 && localY >= 0 && localX < size.Width && localY < size.Height;
}
