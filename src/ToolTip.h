// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#pragma once

#include <common/str.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <xuocore/enumlist.h>
#include "Point.h"
#include "Sprite.h"

class CToolTip
{
public:
    struct TooltipSideCardDesc
    {
        wstr_t Data = {};
        int MaxWidth = 280;
        TEXT_ALIGN_TYPE Align = TS_CENTER;
        uint32_t BorderColorARGB = 0xFF565E66;
    };

    uint32_t Timer = 0;
    wstr_t Data = {};
    uint32_t ClilocID = 0;
    int MaxWidth = 0;
    CPoint2Di Position = CPoint2Di();
    bool Use = false;
    CTextSprite m_TextSprite;
    class CRenderObject *m_Object{ nullptr };

    CToolTip();
    ~CToolTip();

    void Reset();
    void Create(
        CTextSprite &texture,
        const wstr_t &str,
        int &width,
        int minWidth,
        TEXT_ALIGN_TYPE align = TS_CENTER);
    void Set(const wstr_t &str, int maxWidth = 0);
    void Set(int clilocID, const char *str, int maxWidth = 0, bool toCamelCase = false);
    void SetWithComparison(
        const wstr_t &primaryStr,
        const std::vector<TooltipSideCardDesc> &sideCards,
        const std::vector<uint32_t> &comparingSerials,
        uint64_t signature,
        int primaryMaxWidth = 0);
    bool IsComparingSerial(uint32_t serial) const;
    void InvalidateComparison();
    void Draw(int cursorWidth = 0, int cursorHeight = 0);

private:
    struct SideCardEntry
    {
        TooltipSideCardDesc Desc = {};
        std::unique_ptr<CTextSprite> Sprite = {};
    };

    std::vector<SideCardEntry> m_SideCards = {};
    std::vector<uint32_t> m_ComparingSerials = {};
    uint64_t m_CompareSignature = 0;
    bool m_HasCompareSignature = false;

    void ClearSideCards();
    void BuildComparisonSprites(const std::vector<TooltipSideCardDesc> &sideCards);
    void DrawCard(int x, int y, int boxW, int boxH, CTextSprite &sprite, uint32_t borderColorARGB);
};

extern CToolTip g_ToolTip;
