// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#include "GUITilepicScaled.h"
#include "GUIScissor.h"
#include "../CrossUO.h"
#include "../Sprite.h"
#include "../Renderer/RenderAPI.h"
#include "../Utility/PerfMarker.h"
#include <cstdint>

CGUITilepicScaled::CGUITilepicScaled(
    uint16_t graphic, uint16_t color, int x, int y, int width, int height)
    : CGUITilepic(graphic, color, x, y)
    , m_Size(width, height)
    , m_DrawOffset(0, 0)
    , m_DrawSize(width, height)
    , m_ClipToBounds(false)
{
}

CGUITilepicScaled::CGUITilepicScaled(
    uint16_t graphic,
    uint16_t color,
    int boxX,
    int boxY,
    int boxWidth,
    int boxHeight,
    int drawX,
    int drawY,
    int drawWidth,
    int drawHeight)
    : CGUITilepic(graphic, color, boxX, boxY)
    , m_Size(boxWidth, boxHeight)
    , m_DrawOffset(drawX - boxX, drawY - boxY)
    , m_DrawSize(drawWidth, drawHeight)
    , m_ClipToBounds(true)
{
}

CGUITilepicScaled::~CGUITilepicScaled()
= default;

void CGUITilepicScaled::Draw(bool checktrans)
{
    ScopedPerfMarker(__FUNCTION__);
    (void)checktrans;

    auto *spr = g_Game.ExecuteStaticArt(Graphic);
    if (spr != nullptr && spr->Texture != nullptr && m_DrawSize.Width > 0 && m_DrawSize.Height > 0 &&
        (!m_ClipToBounds || (m_Size.Width > 0 && m_Size.Height > 0)))
    {
        if (m_ClipToBounds)
        {
            CGUIScissor::PushGumpLocal(m_X, m_Y, m_Size.Width, m_Size.Height);
        }
        SetShaderMode();
        auto textureCmd = DrawQuadCmd{ spr->Texture->Texture,
                                       m_X + m_DrawOffset.X,
                                       m_Y + m_DrawOffset.Y,
                                       uint32_t(m_DrawSize.Width),
                                       uint32_t(m_DrawSize.Height) };
        RenderAdd_DrawQuad(g_renderCmdList, textureCmd);
        if (m_ClipToBounds)
        {
            Render_PopScissor();
        }
    }
}
