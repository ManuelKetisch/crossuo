// AGPLv3 License
// Copyright (c) 2020 Jean-Martin Miljours
// Copyright (c) 2020 CrossUO Team

#pragma once

#include "Gump.h"

#if defined(XUO_ASSISTANT)
#include <array>

class CGumpToolbar final : public CGump
{
    static constexpr int SlotCount = 12;
    std::array<CGUIText *, SlotCount> m_CountText{};
    std::array<int, SlotCount> m_Counts{};
    std::array<bool, SlotCount> m_SlotActive{};
    std::array<uint8_t, 2> m_AbilityState{};
    CGUIText *m_CoverageText = nullptr;
    int m_Coverage = -1;
    int m_Columns = SlotCount;
    uint32_t m_LastRefresh = 0;
    uint64_t m_DragProfileRevision = 0;

    void RefreshCounts();
    void FitInWindow();

public:
    CGumpToolbar(int x, int y);

    static void DrawSettings();
    static void Tick();
    static void ResetSession();

    void PrepareContent() override;
    void UpdateContent() override;
    void InitToolTip() override;
    void OnLeftMouseButtonDown() override;
    void OnLeftMouseButtonUp() override;
    bool OnLeftMouseButtonDoubleClick() override;
};
#endif
