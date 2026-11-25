// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#pragma once

#include "GUITilepic.h"

struct CGUITilepicScaled : public CGUITilepic
{
    CGUITilepicScaled(uint16_t graphic, uint16_t color, int x, int y, int width, int height);
    CGUITilepicScaled(
        uint16_t graphic,
        uint16_t color,
        int boxX,
        int boxY,
        int boxWidth,
        int boxHeight,
        int drawX,
        int drawY,
        int drawWidth,
        int drawHeight);
    virtual ~CGUITilepicScaled() override;

    CSize GetSize() override { return m_Size; }
    virtual void Draw(bool checktrans = false) override;
    virtual bool Select() override { return false; }

private:
    CSize m_Size;
    CPoint2Di m_DrawOffset;
    CSize m_DrawSize;
    bool m_ClipToBounds = false;
};
