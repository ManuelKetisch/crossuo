// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#include "ToolTip.h"
#include "GameWindow.h"
#include "Globals.h"
#include "SelectedObject.h"
#include "Managers/FontsManager.h"
#include "Managers/ConfigManager.h"
#include "Managers/MouseManager.h"
#include "Managers/ClilocManager.h"
#include "Renderer/RenderAPI.h"
#include "Utility/PerfMarker.h"
#include <algorithm>
#include <cstdint>
#include <utility>

namespace
{
constexpr uint32_t kPrimaryCardBorderARGB = 0xFF7A8694;

float4 UnpackARGBColor(uint32_t argb)
{
    const float a = ((argb >> 24) & 0xFFu) / 255.0f;
    const float r = ((argb >> 16) & 0xFFu) / 255.0f;
    const float g = ((argb >> 8) & 0xFFu) / 255.0f;
    const float b = (argb & 0xFFu) / 255.0f;
    return float4{ r, g, b, (a > 0.0f) ? a : 1.0f };
}
} // namespace

CToolTip g_ToolTip;

CToolTip::CToolTip()
= default;

CToolTip::~CToolTip()
{
    Reset();
}

void CToolTip::ClearSideCards()
{
    for (auto &card : m_SideCards)
    {
        if (card.Sprite)
        {
            card.Sprite->Clear();
        }
    }
    m_SideCards.clear();
}

void CToolTip::Reset()
{
    m_TextSprite.Clear();
    ClearSideCards();
    m_ComparingSerials.clear();
    m_CompareSignature = 0;
    m_HasCompareSignature = false;
    m_Object = nullptr;
}

void CToolTip::Create(
    CTextSprite &textSprite,
    const wstr_t &str,
    int &width,
    int minWidth,
    TEXT_ALIGN_TYPE align)
{
    g_FontManager.SetUseHTML(true);
    g_FontManager.RecalculateWidthByInfo = true;
    textSprite.Clear();
    auto font = (uint8_t)g_ConfigManager.ToolTipsTextFont;

    const int maxCap = (width > 0) ? width : 600;
    int measuredWidth = g_FontManager.GetWidthW(font, str);
    if (measuredWidth > maxCap)
    {
        measuredWidth = maxCap;
    }

    measuredWidth = g_FontManager.GetWidthExW(font, str, measuredWidth, align, UOFONT_BLACK_BORDER);
    if (measuredWidth > maxCap)
    {
        measuredWidth = maxCap;
    }

    width = measuredWidth;
    if (width < minWidth)
    {
        width = minWidth;
    }

    g_FontManager.GenerateW(
        font,
        textSprite,
        str,
        g_ConfigManager.ToolTipsTextColor,
        5,
        width,
        align,
        UOFONT_BLACK_BORDER);

    g_FontManager.RecalculateWidthByInfo = false;
    g_FontManager.SetUseHTML(false);
}

void CToolTip::BuildComparisonSprites(const std::vector<TooltipSideCardDesc> &sideCards)
{
    ClearSideCards();
    if (sideCards.empty())
    {
        return;
    }

    m_SideCards.reserve(sideCards.size());
    for (const auto &desc : sideCards)
    {
        SideCardEntry entry;
        entry.Desc = desc;
        entry.Sprite = std::make_unique<CTextSprite>();

        if (!entry.Desc.Data.empty())
        {
            int cardWidth = entry.Desc.MaxWidth;
            Create(*entry.Sprite, entry.Desc.Data, cardWidth, 0, entry.Desc.Align);
        }

        m_SideCards.push_back(std::move(entry));
    }
}

void CToolTip::Set(const wstr_t &str, int maxWidth)
{
    if (str.length() == 0u)
    {
        return;
    }

    Use = !(Timer > g_Ticks);
    CRenderObject *object = g_SelectedObject.Object;
    if (object == nullptr)
    {
        return;
    }

    if (object == m_Object)
    {
        if (m_HasCompareSignature || !m_SideCards.empty())
        {
            Data = str;
            ClilocID = 0;
            MaxWidth = maxWidth;
            ClearSideCards();
            m_ComparingSerials.clear();
            m_CompareSignature = 0;
            m_HasCompareSignature = false;

            int width = MaxWidth;
            Create(m_TextSprite, Data, width, 0, TS_CENTER);
        }
        return;
    }

    Timer = g_Ticks + g_ConfigManager.ToolTipsDelay;
    m_Object = object;
    Use = false;
    Data = str;
    ClilocID = 0;
    MaxWidth = maxWidth;
    Position.X = 0;
    Position.Y = 0;
    ClearSideCards();
    m_ComparingSerials.clear();
    m_CompareSignature = 0;
    m_HasCompareSignature = false;

    int width = MaxWidth;
    Create(m_TextSprite, Data, width, 0, TS_CENTER);
}

void CToolTip::Set(int clilocID, const char *str, int maxWidth, bool toCamelCase)
{
    Set(g_ClilocManager.Cliloc(g_Language)->GetW(clilocID, toCamelCase, str), maxWidth);
    ClilocID = clilocID;
}

void CToolTip::SetWithComparison(
    const wstr_t &primaryStr,
    const std::vector<TooltipSideCardDesc> &sideCards,
    const std::vector<uint32_t> &comparingSerials,
    uint64_t signature,
    int primaryMaxWidth)
{
    if (primaryStr.length() == 0u)
    {
        return;
    }

    CRenderObject *object = g_SelectedObject.Object;
    if (object == nullptr)
    {
        return;
    }

    if (object == m_Object)
    {
        Use = !(Timer > g_Ticks);
        if (m_HasCompareSignature && signature == m_CompareSignature)
        {
            return;
        }

        Data = primaryStr;
        ClilocID = 0;
        MaxWidth = primaryMaxWidth;
        m_CompareSignature = signature;
        m_HasCompareSignature = true;
        m_ComparingSerials = comparingSerials;

        int width = MaxWidth;
        Create(m_TextSprite, Data, width, 0, TS_CENTER);
        BuildComparisonSprites(sideCards);
        return;
    }

    Timer = g_Ticks + g_ConfigManager.ToolTipsDelay;
    m_Object = object;
    Use = false;
    Data = primaryStr;
    ClilocID = 0;
    MaxWidth = primaryMaxWidth;
    Position.X = 0;
    Position.Y = 0;
    m_CompareSignature = signature;
    m_HasCompareSignature = true;
    m_ComparingSerials = comparingSerials;

    int width = MaxWidth;
    Create(m_TextSprite, Data, width, 0, TS_CENTER);
    BuildComparisonSprites(sideCards);
}

bool CToolTip::IsComparingSerial(uint32_t serial) const
{
    if (serial == 0u || m_Object == nullptr)
    {
        return false;
    }

    for (uint32_t comparedSerial : m_ComparingSerials)
    {
        if (comparedSerial == serial)
        {
            return true;
        }
    }

    return false;
}

void CToolTip::InvalidateComparison()
{
    m_CompareSignature = 0;
    m_HasCompareSignature = false;
}

void CToolTip::DrawCard(
    int x, int y, int boxW, int boxH, CTextSprite &sprite, uint32_t borderColorARGB)
{
    if (boxW <= 0 || boxH <= 0)
    {
        return;
    }

    const float4 bgColor{ 0.04f, 0.05f, 0.07f, 0.82f };
    const float4 borderColor = UnpackARGBColor(borderColorARGB);

    RenderAdd_SetBlend(
        g_renderCmdList,
        BlendStateCmd{ BlendFactor::BlendFactor_SrcAlpha,
                       BlendFactor::BlendFactor_OneMinusSrcAlpha });

    RenderAdd_DrawUntexturedQuad(
        g_renderCmdList,
        DrawUntexturedQuadCmd{ x, y, uint32_t(boxW), uint32_t(boxH), bgColor });

    // Top and bottom 1px borders
    RenderAdd_DrawUntexturedQuad(
        g_renderCmdList, DrawUntexturedQuadCmd{ x, y, uint32_t(boxW), 1u, borderColor });
    if (boxH > 1)
    {
        RenderAdd_DrawUntexturedQuad(
            g_renderCmdList,
            DrawUntexturedQuadCmd{ x, y + boxH - 1, uint32_t(boxW), 1u, borderColor });
    }

    // Left and right 1px borders (between top and bottom to avoid corner double-blending)
    if (boxH > 2)
    {
        const uint32_t sideH = uint32_t(boxH - 2);
        RenderAdd_DrawUntexturedQuad(
            g_renderCmdList, DrawUntexturedQuadCmd{ x, y + 1, 1u, sideH, borderColor });
        if (boxW > 1)
        {
            RenderAdd_DrawUntexturedQuad(
                g_renderCmdList,
                DrawUntexturedQuadCmd{ x + boxW - 1, y + 1, 1u, sideH, borderColor });
        }
    }

    RenderAdd_DisableBlend(g_renderCmdList);
    sprite.Draw(x + 6, y + 5);
}

void CToolTip::Draw(int cursorWidth, int cursorHeight)
{
    ScopedPerfMarker(__FUNCTION__);
    SCOPED_GL_DEBUG_MARKER_LABEL("CToolTip::Draw");
    if (!Use /*|| !g_ConfigManager.UseToolTips*/)
    {
        return;
    }

    if (m_TextSprite.Empty() && !Data.empty())
    {
        int width = MaxWidth;
        Create(m_TextSprite, Data, width, 0, TS_CENTER);
    }

    if (!m_TextSprite.Empty())
    {
        const CSize win = g_GameWindow.GetSize();

        if (m_SideCards.empty())
        {
            const int boxW = m_TextSprite.Width + 8;
            const int boxH = m_TextSprite.Height + 8;
            int x = Position.X;
            int y = Position.Y;
            if (x == 0)
            {
                x = g_MouseManager.Position.X - boxW;
            }

            if (y == 0)
            {
                y = g_MouseManager.Position.Y - boxH;
            }

            if (y < 0)
            {
                y = Position.Y;
            }

            if (x < 0)
            {
                x = Position.X;
            }

            const int margin = 4;
            if (win.Width > 0)
            {
                const int maxX = std::max(margin, win.Width - boxW - margin);
                x = std::max(margin, std::min(x, maxX));
            }

            if (win.Height > 0)
            {
                const int maxY = std::max(margin, win.Height - boxH - margin);
                y = std::max(margin, std::min(y, maxY));
            }

            m_TextSprite.Draw_Tooltip(x, y, boxW, boxH);
        }
        else
        {
            struct CardBox
            {
                CTextSprite *Sprite = nullptr;
                int BoxW = 0;
                int BoxH = 0;
                int RelX = 0;
                int RelY = 0;
                uint32_t BorderColorARGB = 0xFF565E66;
            };

            std::vector<CardBox> boxes;
            boxes.reserve(1 + m_SideCards.size());
            boxes.push_back(CardBox{ &m_TextSprite,
                                     m_TextSprite.Width + 12,
                                     m_TextSprite.Height + 10,
                                     0,
                                     0,
                                     kPrimaryCardBorderARGB });

            for (auto &card : m_SideCards)
            {
                if (!card.Sprite)
                {
                    card.Sprite = std::make_unique<CTextSprite>();
                }

                if (card.Sprite->Empty() && !card.Desc.Data.empty())
                {
                    int cardWidth = card.Desc.MaxWidth;
                    Create(*card.Sprite, card.Desc.Data, cardWidth, 0, card.Desc.Align);
                }

                if (!card.Sprite->Empty())
                {
                    boxes.push_back(CardBox{ card.Sprite.get(),
                                             card.Sprite->Width + 12,
                                             card.Sprite->Height + 10,
                                             0,
                                             0,
                                             card.Desc.BorderColorARGB });
                }
            }

            const int gap = 6;
            const int margin = 6;
            const int maxRowW = (win.Width > 2 * margin) ? (win.Width - 2 * margin) : 800;

            int curRowW = 0;
            int curRowH = 0;
            int curRowY = 0;
            int groupW = 0;

            for (size_t i = 0; i < boxes.size(); ++i)
            {
                if (i == 0)
                {
                    boxes[i].RelX = 0;
                    boxes[i].RelY = 0;
                    curRowW = boxes[i].BoxW;
                    curRowH = boxes[i].BoxH;
                }
                else if (curRowW + gap + boxes[i].BoxW <= maxRowW)
                {
                    boxes[i].RelX = curRowW + gap;
                    boxes[i].RelY = curRowY;
                    curRowW += gap + boxes[i].BoxW;
                    curRowH = std::max(curRowH, boxes[i].BoxH);
                }
                else
                {
                    groupW = std::max(groupW, curRowW);
                    curRowY += curRowH + gap;
                    boxes[i].RelX = 0;
                    boxes[i].RelY = curRowY;
                    curRowW = boxes[i].BoxW;
                    curRowH = boxes[i].BoxH;
                }
            }

            groupW = std::max(groupW, curRowW);
            const int groupH = curRowY + curRowH;

            int groupX = g_MouseManager.Position.X - groupW - 4;
            if (groupX < margin)
            {
                groupX = g_MouseManager.Position.X + std::max(cursorWidth, 16) + 4;
            }

            if (win.Width > 0)
            {
                const int maxX = std::max(margin, win.Width - groupW - margin);
                groupX = std::max(margin, std::min(groupX, maxX));
            }

            int groupY = g_MouseManager.Position.Y - groupH - 4;
            if (groupY < margin)
            {
                groupY = g_MouseManager.Position.Y + std::max(cursorHeight, 16) + 4;
            }

            if (win.Height > 0)
            {
                const int maxY = std::max(margin, win.Height - groupH - margin);
                groupY = std::max(margin, std::min(groupY, maxY));
            }

            for (const auto &box : boxes)
            {
                const int cardX = groupX + box.RelX;
                const int cardY = groupY + box.RelY;
                DrawCard(cardX, cardY, box.BoxW, box.BoxH, *box.Sprite, box.BorderColorARGB);
            }
        }
    }
    Use = false;
}
