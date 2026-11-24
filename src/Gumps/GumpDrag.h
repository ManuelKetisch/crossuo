// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#pragma once

#include "Gump.h"
#include "../Platform.h"

class CGumpDrag : public CGump
{
public:
    enum class StackAction
    {
        Drag,
        Split,
        Loot
    };

private:
    StackAction m_Action;
    uint32_t m_SourceContainer;
    bool m_StartText = true;
    CGUITextEntry *m_Entry{ nullptr };
    CGUISlider *m_Slider{ nullptr };

public:
    CGumpDrag(
        uint32_t serial,
        short x,
        short y,
        StackAction action = StackAction::Drag,
        uint32_t sourceContainer = 0);
    virtual ~CGumpDrag() override;

    virtual void UpdateContent() override;

    GUMP_BUTTON_EVENT_H override;
    GUMP_SLIDER_CLICK_EVENT_H override;
    GUMP_SLIDER_MOVE_EVENT_H override;

    virtual void OnTextInput(const TextEvent &ev) override;
    virtual void OnKeyDown(const KeyEvent &ev) override;

    void OnOkayPressed();
};
