// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#pragma once

#include <common/str.h>
#include "../Sprite.h"
#include "GUIDrawObject.h"

class CGUIBuff : public CGUIDrawObject
{
public:
    uint32_t Timer = 0xFFFFFFFFu;
    uint32_t TotalDurationMs = 0u;
    wstr_t Text;
    uint32_t TooltipTimer = 0u;
    uint32_t LastDisplayedSeconds = 0xFFFFFFFFu;
    uint32_t LastTooltipSeconds = 0xFFFFFFFFu;
    bool DecAlpha = true;
    uint8_t Alpha = 0xFF;

    CGUIBuff(uint16_t graphic, uint32_t timer, uint32_t totalDurationMs, const wstr_t &text);
    virtual ~CGUIBuff() override;
    virtual CSize GetSize() override;
    virtual void Draw(bool checktrans = false) override;
    virtual bool Select() override;

private:
    CTextSprite m_TimerSprite;
    astr_t m_CachedTimerText;
    uint16_t m_CachedTimerHue = 0;
};
