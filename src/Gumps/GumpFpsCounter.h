// SPDX-License-Identifier: MIT

#pragma once

#include "Gump.h"

class CGumpFpsCounter final : public CGump
{
    class Label;
    Label *m_Label = nullptr;
    CGUIResizeButton *m_ResizeHandle = nullptr;
    astr_t m_Reading;
    CPoint2Di m_DesktopPosition;
    int m_ResizeStartPercent = 0;
    int m_ResizeTextWidth = 0;

    CPoint2Di FitInWindow(const CPoint2Di &position) const;
    void UpdateVisiblePosition();
    void CalculateGumpState() override;
    void UpdateLayout();

public:
    CGumpFpsCounter(int x, int y, int sizePercent = 100);

    int GetSizePercent() const;
    const CPoint2Di &GetDesktopPosition() const { return m_DesktopPosition; }
    void SetReading(const char *reading);
    bool CanBeDisplayed() override;
    void OnLeftMouseButtonUp() override;
    GUMP_RESIZE_START_EVENT_H override;
    GUMP_RESIZE_EVENT_H override;
    GUMP_RESIZE_END_EVENT_H override;
};
