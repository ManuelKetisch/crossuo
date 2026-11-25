// SPDX-License-Identifier: MIT

#include "GumpFpsCounter.h"
#include "../GameWindow.h"
#include "../Globals.h"
#include "../PressedObject.h"
#include "../GameObjects/ObjectOnCursor.h"
#include "../Managers/MouseManager.h"
#include "../Renderer/RenderAPI.h"
#include "../Renderer/RenderTexture.h"
#include <algorithm>

namespace
{
constexpr int ResizeHandleId = 1;

int LimitSizePercent(int percent)
{
    return std::max(50, std::min(percent, 150));
}
}

class CGumpFpsCounter::Label final : public CGUIText
{
public:
    int SizePercent;

    explicit Label(int sizePercent)
        : CGUIText(0x35, 0, 0)
        , SizePercent(LimitSizePercent(sizePercent))
    {
    }

    CSize GetSize() override
    {
        return CSize(
            (m_Texture.Width * SizePercent + 50) / 100,
            (m_Texture.Height * SizePercent + 50) / 100);
    }

    void Draw(bool checktrans) override
    {
        if (m_Texture.Texture == nullptr ||
            m_Texture.Texture->Texture == RENDER_TEXTUREHANDLE_INVALID)
        {
            return;
        }

        const auto size = GetSize();
        RenderAdd_DrawQuad(g_renderCmdList, DrawQuadCmd{
            m_Texture.Texture->Texture, GetX(), GetY(),
            uint32_t(size.Width), uint32_t(size.Height) });
    }

    bool Select() override
    {
        const auto size = GetSize();
        const int x = g_MouseManager.Position.X - GetX();
        const int y = g_MouseManager.Position.Y - GetY();
        return x >= 0 && y >= 0 && x < size.Width && y < size.Height;
    }
};

CGumpFpsCounter::CGumpFpsCounter(int x, int y, int sizePercent)
    : CGump(GT_FPS_COUNTER, 0, x, y)
    , m_DesktopPosition(std::max(0, x), std::max(0, y))
{
    NoClose = true;
    // Keep both selectable objects stable as readings change during a drag.
    m_Label = static_cast<Label *>(Add(new Label(sizePercent)));
    m_ResizeHandle = static_cast<CGUIResizeButton *>(Add(
        new CGUIResizeButton(ResizeHandleId, 0x0837, 0x0838, 0x0838, 0, 0)));
    m_ResizeHandle->BoundingBoxCheck = true;
    m_ResizeHandle->Visible = false;
}

int CGumpFpsCounter::GetSizePercent() const
{
    return m_Label->SizePercent;
}

void CGumpFpsCounter::SetReading(const char *reading)
{
    if (m_Reading != reading)
    {
        m_Reading = reading;
        m_Label->CreateTextureA(3, m_Reading);
        UpdateLayout();
    }
}

bool CGumpFpsCounter::CanBeDisplayed()
{
    return g_DeveloperMode == DM_SHOW_FPS_ONLY;
}

void CGumpFpsCounter::UpdateLayout()
{
    const auto label = m_Label->GetSize();
    m_ResizeHandle->Visible = label.Width > 0 && label.Height > 0;
    m_ResizeHandle->SetX(label.Width + 4);
    m_ResizeHandle->SetY(std::max(0, label.Height - m_ResizeHandle->GetSize().Height));
    RecalculateSize();
    FrameCreated = false;
}

CPoint2Di CGumpFpsCounter::FitInWindow(const CPoint2Di &position) const
{
    const CSize window = g_GameWindow.GetSize();
    if (window.Width <= 0 || window.Height <= 0)
    {
        return position;
    }

    const int maxX = std::max(0, window.Width - GumpRect.Size.Width);
    const int maxY = std::max(0, window.Height - GumpRect.Size.Height);
    return CPoint2Di(
        std::max(0, std::min(position.X, maxX)),
        std::max(0, std::min(position.Y, maxY)));
}

void CGumpFpsCounter::UpdateVisiblePosition()
{
    const auto visiblePosition = FitInWindow(m_DesktopPosition);
    SetX(visiblePosition.X);
    SetY(visiblePosition.Y);
}

void CGumpFpsCounter::CalculateGumpState()
{
    // Login and window resizes may temporarily reduce the available space.
    // Keep the visible origin fixed while a drag uses it as its anchor.
    if (g_PressedObject.LeftGump != this)
    {
        UpdateVisiblePosition();
    }
    CGump::CalculateGumpState();
}

void CGumpFpsCounter::OnLeftMouseButtonUp()
{
    CGump::OnLeftMouseButtonUp();
    const auto offset = g_MouseManager.LeftDroppedOffset();
    if (g_PressedObject.LeftObject == m_Label && CanBeMoved() && !g_ObjectInHand.Enabled &&
        (offset.X != 0 || offset.Y != 0))
    {
        // The gump manager has applied the drag to the visible origin.
        m_DesktopPosition = FitInWindow(CPoint2Di(GetX(), GetY()));
    }
    UpdateVisiblePosition();
}

void CGumpFpsCounter::GUMP_RESIZE_START_EVENT_C
{
    m_ResizeStartPercent = GetSizePercent();
    m_ResizeTextWidth = m_Label->m_Texture.Width;
}

void CGumpFpsCounter::GUMP_RESIZE_EVENT_C
{
    if (m_ResizeTextWidth <= 0)
    {
        return;
    }

    const int delta = g_MouseManager.LeftDroppedOffset().X;
    const int percent = LimitSizePercent(m_ResizeStartPercent + delta * 100 / m_ResizeTextWidth);
    if (percent != GetSizePercent())
    {
        m_Label->SizePercent = percent;
        UpdateLayout();
    }
}

void CGumpFpsCounter::GUMP_RESIZE_END_EVENT_C
{
    OnResize(serial);
    m_ResizeTextWidth = 0;
}
