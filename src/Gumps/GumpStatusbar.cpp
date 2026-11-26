// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#include "../Misc.h"
#include "GumpStatusbar.h"
#include "../Renderer/RenderShader.h" // g_ColorizerShader and friends
#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <common/utils.h> // countof
#include <cstdint>
#include <utility>
#include <xuocore/uodata.h>
#include <xuocore/vfs.h>
#include "../Config.h"
#include "../CrossUO.h"
#include "../Target.h"
#include "../ToolTip.h"
#include "../TargetGump.h"
#include "../Party.h"
#include "../PressedObject.h"
#include "../SelectedObject.h"
#include "../TextEngine/GameConsole.h"
#include "../Managers/ConfigManager.h"
#include "../Managers/ConnectionManager.h"
#include "../Managers/FontsManager.h"
#include "../Managers/GumpManager.h"
#include "../Managers/MouseManager.h"
#include "../GameObjects/GameWorld.h"
#include "../GameObjects/GamePlayer.h"
#include "../Network/Packets.h"
#include "Platform.h"
#if defined(XUO_ASSISTANT)
#include "../Assistant/core/itemcounters_rules.h"
#include "../Assistant/ui/assistantui.h"
#endif

int CGumpStatusbar::m_StatusbarDefaultWidth = 154;
int CGumpStatusbar::m_StatusbarDefaultHeight = 59;

enum
{
    ID_GSB_NONE,

    ID_GSB_MINIMIZE,
    ID_GSB_TEXT_FIELD,
    ID_GSB_BUTTON_HEAL_1,
    ID_GSB_BUTTON_HEAL_2,
    ID_GSB_BUTTON_REMOVE_FROM_GROUP,
    ID_GSB_BUFF_GUMP,
    ID_GSB_BUFF_LOCKER_STR,
    ID_GSB_BUFF_LOCKER_DEX,
    ID_GSB_BUFF_LOCKER_INT,
    ID_GSB_TEXT_STR,
    ID_GSB_TEXT_DEX,
    ID_GSB_TEXT_INT,
    ID_GSB_TEXT_SEX,
    ID_GSB_TEXT_ARMOR,
    ID_GSB_TEXT_HITS,
    ID_GSB_TEXT_STAM,
    ID_GSB_TEXT_MANA,
    ID_GSB_TEXT_MAX_STATS,
    ID_GSB_TEXT_LUCK,
    ID_GSB_TEXT_WEIGHT,
    ID_GSB_TEXT_DAMAGE,
    ID_GSB_TEXT_GOLD,
    ID_GSB_TEXT_FOLLOWERS,
    ID_GSB_TEXT_RESISTANCE_PHYSICAL,
    ID_GSB_TEXT_RESISTANCE_FIRE,
    ID_GSB_TEXT_RESISTANCE_COLD,
    ID_GSB_TEXT_RESISTANCE_POISON,
    ID_GSB_TEXT_RESISTANCE_ENERGY,
    ID_GSB_TEXT_TITHING_POINTS,
    ID_GSB_TEXT_HIT_CHANCE,
    ID_GSB_TEXT_DEFENCE_CHANCE,
    ID_GSB_TEXT_LOWER_MANA,
    ID_GSB_TEXT_SWING_SPEED,
    ID_GSB_TEXT_WEAPON_DMG,
    ID_GSB_TEXT_LOWER_REG_COST,
    ID_GSB_TEXT_SPELL_DMG,
    ID_GSB_TEXT_FASTER_CASTING,
    ID_GSB_TEXT_CAST_RECOVERY,

    ID_GSB_COUNT,
};

static const wstr_t tooltip[] = {
    L"(none)",                      //ID_GSB_NONE
    L"Minimize the statusbar gump", //ID_GSB_MINIMIZE
    L"",                            //ID_GSB_TEXT_FIELD
    L"",                            //ID_GSB_BUTTON_HEAL_1
    L"",                            //ID_GSB_BUTTON_HEAL_2
    L"Remove bar from group",       //ID_GSB_BUTTON_REMOVE_FROM_GROUP
    L"Open buff window",            //ID_GSB_BUFF_GUMP
    L"Change strength state",       //ID_GSB_BUFF_LOCKER_STR
    L"Change dexterity state",      //ID_GSB_BUFF_LOCKER_DEX
    L"Change intelligence state",   //ID_GSB_BUFF_LOCKER_INT
    L"Strength",                    //ID_GSB_TEXT_STR
    L"Dexterity",                   //ID_GSB_TEXT_DEX
    L"Intelligence",                //ID_GSB_TEXT_INT
    L"Sex",                         //ID_GSB_TEXT_SEX
    L"Armor",                       //ID_GSB_TEXT_ARMOR
    L"Hit Points",                  //ID_GSB_TEXT_HITS
    L"Stamina",                     //ID_GSB_TEXT_STAM
    L"Mana",                        //ID_GSB_TEXT_MANA
    L"Maximum Stats",               //ID_GSB_TEXT_MAX_STATS
    L"Luck",                        //ID_GSB_TEXT_LUCK
    L"Weight",                      //ID_GSB_TEXT_WEIGHT
    L"Damage",                      //ID_GSB_TEXT_DAMAGE
    L"Gold",                        //ID_GSB_TEXT_GOLD
    L"Followers",                   //ID_GSB_TEXT_FOLLOWERS
    L"Physical Resistance",         //ID_GSB_TEXT_RESISTANCE_PHYSICAL
    L"Fire Resistance",             //ID_GSB_TEXT_RESISTANCE_FIRE
    L"Cold Resistance",             //ID_GSB_TEXT_RESISTANCE_COLD
    L"Poison Resistance",           //ID_GSB_TEXT_RESISTANCE_POISON
    L"Energy Resistance",           //ID_GSB_TEXT_RESISTANCE_ENERGY
    L"Tithing Points",              //ID_GSB_TEXT_TITHING_POINTS
    L"Hit Chance Increase",         //ID_GSB_TEXT_HIT_CHANCE
    L"Defense Chance Increase",     //ID_GSB_TEXT_DEFENCE_CHANCE
    L"Lower Mana Cost",             //ID_GSB_TEXT_LOWER_MANA
    L"Swing Speed Increase",        //ID_GSB_TEXT_SWING_SPEED
    L"Weapon Damage Increase",      //ID_GSB_TEXT_WEAPON_DMG
    L"Lower Reagent Cost",          //ID_GSB_TEXT_LOWER_REG_COST
    L"Spell Damage Increase",       //ID_GSB_TEXT_SPELL_DMG
    L"Faster Casting",              //ID_GSB_TEXT_FASTER_CASTING
    L"Faster Cast Recovery"         //ID_GSB_TEXT_CAST_RECOVERY
};

static_assert(countof(tooltip) == ID_GSB_COUNT, "Wrong amount of entries in tooltip table");

namespace
{
constexpr uint16_t kStatusWeightNormalHue = 0x0386;
constexpr uint16_t kStatusWeightWarnHue = 0x002B;
constexpr uint16_t kStatusWeightOverloadHue = 0x0021;

uint16_t ResolvePlayerWeightHue()
{
    if (g_Player == nullptr)
    {
        return kStatusWeightNormalHue;
    }
#if defined(XUO_ASSISTANT)
    return xuoassist::ItemCounters_WeightStatusHue(
        g_Player->Weight,
        g_Player->MaxWeight,
        kStatusWeightNormalHue,
        kStatusWeightWarnHue,
        kStatusWeightOverloadHue);
#else
    if (g_Player->MaxWeight == 0)
    {
        return kStatusWeightNormalHue;
    }
    const int pct = static_cast<int>((static_cast<int64_t>(g_Player->Weight) * 100) / g_Player->MaxWeight);
    if (g_Player->Weight >= g_Player->MaxWeight || pct >= 90)
    {
        return kStatusWeightOverloadHue;
    }
    if (pct >= 75)
    {
        return kStatusWeightWarnHue;
    }
    return kStatusWeightNormalHue;
#endif
}
} // namespace

#if defined(XUO_ASSISTANT)
enum
{
    ID_GSB_SUPPLIES_TOGGLE = 1000,
    ID_GSB_SUPPLIES_COUNTERS,
    ID_GSB_SUPPLIES_RECOUNT,
    ID_GSB_SUPPLIES_PREVIOUS,
    ID_GSB_SUPPLIES_NEXT,
    ID_GSB_SUPPLIES_COVERAGE,
    ID_GSB_SUPPLIES_STATUS,
    ID_GSB_SUPPLIES_HIDE_ZERO,
    ID_GSB_SUPPLIES_TAB_ALL,
    ID_GSB_SUPPLIES_TAB_FIRST,
    ID_GSB_SUPPLIES_TAB_LAST = ID_GSB_SUPPLIES_TAB_FIRST + 15,
    ID_GSB_SUPPLIES_GROUP_FIRST,
    ID_GSB_SUPPLIES_GROUP_LAST = ID_GSB_SUPPLIES_GROUP_FIRST + 15,
    ID_GSB_SUPPLY_FIRST,
};

namespace
{
bool IsSuppliesInteractiveControl(uint32_t serial)
{
    return serial == ID_GSB_SUPPLIES_TOGGLE ||
           serial == ID_GSB_SUPPLIES_COUNTERS ||
           serial == ID_GSB_SUPPLIES_RECOUNT ||
           serial == ID_GSB_SUPPLIES_PREVIOUS ||
           serial == ID_GSB_SUPPLIES_NEXT ||
           serial == ID_GSB_SUPPLIES_HIDE_ZERO ||
           serial == ID_GSB_SUPPLIES_TAB_ALL ||
           (serial >= ID_GSB_SUPPLIES_TAB_FIRST && serial <= ID_GSB_SUPPLIES_TAB_LAST) ||
           (serial >= ID_GSB_SUPPLIES_GROUP_FIRST && serial <= ID_GSB_SUPPLIES_GROUP_LAST);
}
void AppendStatusSegment(
    astr_t &out,
    const char *separator,
    const char *prefix,
    const astr_t &value,
    const char *altMatch = nullptr)
{
    if (value.empty())
    {
        return;
    }
    if (!out.empty())
    {
        out += separator;
    }
    if (prefix != nullptr && prefix[0] != '\0')
    {
        const astr_t lowerValue = str_lower(value);
        const astr_t lowerPrefix = str_lower(prefix);
        const bool hasPrefix = lowerValue.find(lowerPrefix) != astr_t::npos ||
            (altMatch != nullptr && altMatch[0] != '\0' && lowerValue.find(altMatch) != astr_t::npos);
        if (!hasPrefix)
        {
            out += prefix;
            out += " ";
        }
    }
    out += value;
}

astr_t FormatSuppliesStatusText(const xuoassist::SuppliesSnapshot &supplies, const char *separator)
{
    astr_t text;
    if (!supplies.CastSpellName.empty())
    {
        AppendStatusSegment(text, separator, "Cast:", supplies.CastSpellName);
    }
    AppendStatusSegment(text, separator, "Bandage", supplies.BandageText);
    AppendStatusSegment(text, separator, "Gate", supplies.GateText);
    AppendStatusSegment(text, separator, "Criminal", supplies.CriminalText, "crim");
    AppendStatusSegment(text, separator, "Gold", supplies.GoldText, "gp");
    return text;
}
} // namespace
#endif

CGumpStatusbar::CGumpStatusbar(uint32_t serial, short x, short y, bool minimized)
    : CGump(GT_STATUSBAR, serial, x, y)
{
    if (minimized)
    {
        Minimized = true;
        MinimizedX = x;
        MinimizedY = y;
    }

    if (!g_ConfigManager.DisableNewTargetSystem && Serial == g_NewTargetSystem.Serial)
    {
        g_GumpManager.UpdateGump(Serial, 0, GT_TARGET_SYSTEM);
    }

    m_Locker.Serial = ID_GSB_LOCK_MOVING;
}

CGumpStatusbar::~CGumpStatusbar()
{
    if (g_ConnectionManager.Connected() && g_Config.ClientVersion >= CV_200 && g_World != nullptr &&
        g_World->FindWorldObject(Serial) != nullptr)
    {
        CPacketCloseStatusbarGump(Serial).Send();
    }

    RemoveFromGroup();
}

void CGumpStatusbar::InitToolTip()
{
    const uint32_t id = g_SelectedObject.Serial;
#if defined(XUO_ASSISTANT)
    if (id >= ID_GSB_SUPPLY_FIRST && id - ID_GSB_SUPPLY_FIRST < m_Supplies.Entries.size())
    {
        const auto &counter = m_Supplies.Entries[id - ID_GSB_SUPPLY_FIRST];
        astr_t tip = counter.Name + " (" + xuoassist::ItemCounters_CategoryLabel(counter.Category) + ")";
        if (counter.Count >= 0)
        {
            tip += ": " + str_from(counter.Count);
        }
        else
        {
            tip += ": --";
        }
        const std::string unitWeight = xuoassist::ItemCounters_FormatUnitWeight(counter.UnitWeightMilliStones);
        if (counter.Count > 0 && counter.WeightStones >= 0)
        {
            tip += "\nWeight: ";
            if (counter.WeightEstimated)
            {
                tip += "~";
            }
            tip += str_from(counter.WeightStones);
            tip += (counter.WeightStones == 1 ? " stone" : " stones");
            if (!unitWeight.empty())
            {
                tip += " (" + unitWeight + ")";
            }
        }
        else if (!unitWeight.empty())
        {
            tip += "\nUnit weight: " + unitWeight;
        }
        if (counter.Warning > 0)
        {
            tip += "\nLow-stock alert below " + str_from(counter.Warning) + ".";
        }
        if (counter.Highlighted)
        {
            tip += "\nUsed by active spell.";
        }
        tip += "\nDouble-click to use.";
        g_ToolTip.Set(wstr_from_utf8(tip), 190);
        return;
    }
    if (id == ID_GSB_SUPPLIES_HIDE_ZERO)
    {
        g_ToolTip.Set(
            m_Supplies.HideZero
                ? L"Showing carried items only. Click to show zero-count items."
                : L"Hide zero-count items and show only carried supplies.",
            190);
        return;
    }
    if (id == ID_GSB_SUPPLIES_TAB_ALL)
    {
        g_ToolTip.Set(L"Show all supply groups.", 180);
        return;
    }
    if (id >= ID_GSB_SUPPLIES_TAB_FIRST && id <= ID_GSB_SUPPLIES_TAB_LAST)
    {
        const int cat = static_cast<int>(id - ID_GSB_SUPPLIES_TAB_FIRST);
        if (xuoassist::ItemCounters_IsValidCategory(cat))
        {
            const astr_t tip = astr_t("Show only ") +
                xuoassist::ItemCounters_CategoryLabel(static_cast<xuoassist::SupplyCategory>(cat)) + ".";
            g_ToolTip.Set(wstr_from_utf8(tip), 180);
        }
        return;
    }
    if (id >= ID_GSB_SUPPLIES_GROUP_FIRST && id <= ID_GSB_SUPPLIES_GROUP_LAST)
    {
        const int cat = static_cast<int>(id - ID_GSB_SUPPLIES_GROUP_FIRST);
        if (xuoassist::ItemCounters_IsValidCategory(cat))
        {
            const auto category = static_cast<xuoassist::SupplyCategory>(cat);
            const bool collapsed =
                xuoassist::ItemCounters_IsCategoryCollapsed(m_Supplies.CollapsedCategories, category);
            astr_t tip = astr_t(collapsed ? "Expand " : "Collapse ") +
                xuoassist::ItemCounters_CategoryLabel(category) + ".";
            const size_t catIdx = static_cast<size_t>(cat);
            if (m_Supplies.HasSupplyWeight && catIdx < xuoassist::ItemCounters_CategoryCount &&
                m_Supplies.CategoryWeightStones[catIdx] > 0)
            {
                const int stones = m_Supplies.CategoryWeightStones[catIdx];
                tip += "\nCarried weight: ";
                if (m_Supplies.CategoryWeightEstimated[catIdx])
                {
                    tip += "~";
                }
                tip += str_from(stones) + (stones == 1 ? " stone." : " stones.");
            }
            g_ToolTip.Set(wstr_from_utf8(tip), 190);
        }
        return;
    }
    if (id == ID_GSB_SUPPLIES_COUNTERS)
    {
        g_ToolTip.Set(L"Open Assistant Display > Counters to edit tracked items and timers.", 190);
        return;
    }
    if (id == ID_GSB_SUPPLIES_RECOUNT)
    {
        g_ToolTip.Set(
            m_Supplies.Recounting
                ? L"Stop opening backpack bags."
                : (m_Supplies.SkipPouches
                      ? L"Open unopened backpack bags to sync counts. Pouches are skipped for safety."
                      : L"Open unopened backpack bags to sync counts."),
            190);
        return;
    }
    if (id == ID_GSB_SUPPLIES_COVERAGE)
    {
        if (m_Supplies.Recounting)
        {
            g_ToolTip.Set(
                m_Supplies.SkipPouches
                    ? L"Opening unopened backpack bags to sync counts. Pouches are skipped for safety."
                    : L"Opening unopened backpack bags to sync counts.",
                190);
        }
        else if (m_Supplies.Coverage == xuoassist::SuppliesCoverage::Complete)
        {
            g_ToolTip.Set(
                m_Supplies.AutoRecount
                    ? L"Counts stay in sync automatically across your backpack and opened bags."
                    : L"Counts include your backpack and opened bags.",
                190);
        }
        else if (m_Supplies.Coverage == xuoassist::SuppliesCoverage::Partial)
        {
            g_ToolTip.Set(
                m_Supplies.SkipPouches
                    ? L"Some unopened bags are not counted yet (pouches are skipped for safety). Use Recount to check safe bags."
                    : L"Some unopened bags are not counted yet. Use Recount to check them.",
                190);
        }
        else
        {
            g_ToolTip.Set(L"Open your backpack or use Recount to sync supply counts.", 190);
        }
        return;
    }
    if (id == ID_GSB_SUPPLIES_STATUS)
    {
        const astr_t tip = FormatSuppliesStatusText(m_Supplies, "\n");
        if (!tip.empty())
        {
            g_ToolTip.Set(wstr_from_utf8(tip), 190);
        }
        return;
    }
#endif
    if (id == ID_GSB_NONE || id >= ID_GSB_COUNT)
    {
        return;
    }

    if (id == ID_GSB_TEXT_WEIGHT && Serial == g_PlayerSerial && g_Player != nullptr)
    {
        const int curWeight = static_cast<int>(g_Player->Weight);
        const int maxWeight = static_cast<int>(g_Player->MaxWeight);
        astr_t tip = "Weight: " + str_from(curWeight);
        if (maxWeight > 0)
        {
            const int pct = static_cast<int>((static_cast<int64_t>(curWeight) * 100) / maxWeight);
            tip += " / " + str_from(maxWeight) + " stones (" + str_from(pct) + "%)";
            const int diff = maxWeight - curWeight;
            if (diff >= 0)
            {
                tip += "\n" + str_from(diff) + (diff == 1 ? " stone free" : " stones free");
            }
            else
            {
                const int excess = -diff;
                tip += "\nOverloaded by " + str_from(excess) +
                    (excess == 1 ? " stone!" : " stones!");
            }
        }
        else
        {
            tip += (curWeight == 1 ? " stone" : " stones");
        }
#if defined(XUO_ASSISTANT)
        if (g_Config.AssistantEnabled)
        {
            const auto summary = xuoassist::ItemCounters_GetSupplyWeightSummary();
            if (summary.HasKnownWeight && summary.TotalWeightStones > 0)
            {
                tip += "\nTracked supplies: ";
                if (summary.AnyEstimated)
                {
                    tip += "~";
                }
                tip += str_from(summary.TotalWeightStones) +
                    (summary.TotalWeightStones == 1 ? " stone" : " stones");
                if (!summary.HeavyCategories.empty())
                {
                    astr_t breakdown;
                    const size_t limit = std::min<size_t>(summary.HeavyCategories.size(), 4u);
                    for (size_t i = 0; i < limit; ++i)
                    {
                        const auto &cat = summary.HeavyCategories[i];
                        if (i > 0)
                        {
                            breakdown += ", ";
                        }
                        breakdown += xuoassist::ItemCounters_CategoryShortLabel(cat.Category);
                        breakdown += " ";
                        if (cat.Estimated)
                        {
                            breakdown += "~";
                        }
                        breakdown += str_from(cat.WeightStones) + "st";
                    }
                    tip += "\n" + breakdown;
                }
            }
        }
#endif
        g_ToolTip.Set(wstr_from_utf8(tip), 230);
        return;
    }

    //if (Minimized && Serial == g_PlayerSerial)
    //    g_ToolTip.Set(L"Double click to maximize the statusbar gump");
    const auto &text = tooltip[id];
    if (!text.empty())
    {
        g_ToolTip.Set(text, 80);
    }
}

CGumpStatusbar *CGumpStatusbar::GetTopStatusbar()
{
    if (!InGroup())
    {
        return nullptr;
    }

    if (m_GroupPrev == nullptr)
    {
        return this;
    }

    CGumpStatusbar *gump = m_GroupPrev;

    while (gump != nullptr && gump->m_GroupPrev != nullptr)
    {
        gump = gump->m_GroupPrev;
    }

    return gump;
}

CGumpStatusbar *CGumpStatusbar::GetNearStatusbar(int &x, int &y)
{
    if (InGroup() || !Minimized)
    {
        return nullptr;
    }

    //154x59 mini-gump

    /*
    static const int gumpWidth = 154;
    static const int gumpHeight = 59;

    const int rangeX = 77;
    const int rangeY = 29;
    */

    int gumpWidth = m_StatusbarDefaultWidth;
    int gumpHeight = m_StatusbarDefaultHeight;

    int rangeX = gumpWidth / 2;
    int rangeY = gumpHeight / 2;

    const int rangeOffsetX = 60;
    const int rangeOffsetY = 24;

    CGump *gump = (CGump *)g_GumpManager.m_Items;

    while (gump != nullptr)
    {
        if (gump != this && gump->GumpType == GT_STATUSBAR && gump->Minimized)
        {
            int gumpX = gump->MinimizedX;
            int offsetX = abs(x - gumpX);
            int passed = 0;

            if (x >= gumpX && x <= (gumpX + gumpWidth))
            {
                passed = 2;
            }
            else if (offsetX < rangeOffsetX)
            { //left part of gump
                passed = 1;
            }
            else
            {
                offsetX = abs(x - (gumpX + gumpWidth));

                if (offsetX < rangeOffsetX)
                { //right part of gump
                    passed = -1;
                }
                else if (x >= (gumpX - rangeX) && x <= (gumpX + gumpWidth + rangeX))
                {
                    passed = 2;
                }
            }

            int gumpY = gump->MinimizedY;

            if (abs(passed) == 1)
            {
                if (y < (gumpY - rangeY) || y > (gumpY + gumpHeight + rangeY))
                {
                    passed = 0;
                }
            }
            else if (passed == 2) //in gump range X
            {
                int offsetY = abs(y - gumpY);

                if (offsetY < rangeOffsetY)
                { //top part of gump
                    passed = 2;
                }
                else
                {
                    offsetY = abs(y - (gumpY + gumpHeight));

                    if (offsetY < rangeOffsetY)
                    { //bottom part of gump
                        passed = -2;
                    }
                    else
                    {
                        passed = 0;
                    }
                }
            }

            if (passed != 0)
            {
                int testX = gumpX;
                int testY = gumpY;

                switch (passed)
                {
                    case -2: //gump bottom
                    {
                        testY += gumpHeight;
                        break;
                    }
                    case -1: //gump right
                    {
                        testX += gumpWidth;
                        break;
                    }
                    case 1: //gump left
                    {
                        testX -= gumpWidth;
                        break;
                    }
                    case 2: //gump top
                    {
                        testY -= gumpHeight;
                        break;
                    }
                    default:
                        break;
                }

                CGump *testGump = (CGump *)g_GumpManager.m_Items;

                while (testGump != nullptr)
                {
                    if (testGump != this && testGump->GumpType == GT_STATUSBAR &&
                        testGump->Minimized)
                    {
                        if (testGump->MinimizedX == testX && testGump->MinimizedY == testY)
                        {
                            break;
                        }
                    }

                    testGump = (CGump *)testGump->m_Next;
                }

                if (testGump == nullptr)
                {
                    x = testX;
                    y = testY;

                    break;
                }
            }
        }

        gump = (CGump *)gump->m_Next;
    }

    return (CGumpStatusbar *)gump;
}

bool CGumpStatusbar::GetStatusbarGroupOffset(int &x, int &y)
{
    if (InGroup() && Minimized && g_MouseManager.LeftButtonPressed &&
        g_PressedObject.LeftGump != nullptr &&
        (g_PressedObject.LeftObject == nullptr ||
         (g_PressedObject.LeftObject->IsGUI() &&
          ((CBaseGUI *)g_PressedObject.LeftObject)->MoveOnDrag)))
    {
        CGumpStatusbar *gump = GetTopStatusbar();

        while (gump != nullptr)
        {
            if (gump != this && g_PressedObject.LeftGump == gump && gump->CanBeMoved())
            {
                CPoint2Di offset = g_MouseManager.LeftDroppedOffset();

                x += offset.X;
                y += offset.Y;

                return true;
            }

            gump = gump->m_GroupNext;
        }
    }

    return false;
}

void CGumpStatusbar::UpdateGroup(int x, int y)
{
    if (!InGroup())
    {
        return;
    }

    CGumpStatusbar *gump = GetTopStatusbar();

    while (gump != nullptr)
    {
        if (gump != this)
        {
            gump->MinimizedX += x;
            gump->MinimizedY += y;

            g_GumpManager.MoveToBack(gump);
            //gump->WantRedraw = true;
        }

        gump = gump->m_GroupNext;
    }

    g_GumpManager.MoveToBack(this);
}

void CGumpStatusbar::AddStatusbar(CGumpStatusbar *bar)
{
    if (m_GroupNext == nullptr)
    {
        m_GroupNext = bar;
        bar->m_GroupPrev = this;
        bar->m_GroupNext = nullptr;
    }
    else
    {
        CGumpStatusbar *gump = m_GroupNext;

        while (gump != nullptr && gump->m_GroupNext != nullptr)
        {
            gump = gump->m_GroupNext;
        }

        gump->m_GroupNext = bar;
        bar->m_GroupPrev = gump;
        bar->m_GroupNext = nullptr;
    }

    if (bar->m_StatusbarUnlocker != nullptr)
    {
        bar->m_StatusbarUnlocker->Visible = bar->InGroup();
        bar->WantRedraw = true;
    }

    if (m_StatusbarUnlocker != nullptr)
    {
        m_StatusbarUnlocker->Visible = InGroup();

        WantRedraw = true;
    }
}

void CGumpStatusbar::RemoveFromGroup()
{
    if (m_GroupNext != nullptr)
    {
        m_GroupNext->WantRedraw = true;
        m_GroupNext->m_GroupPrev = m_GroupPrev;

        if (m_GroupNext->m_StatusbarUnlocker != nullptr)
        {
            m_GroupNext->m_StatusbarUnlocker->Visible = m_GroupNext->InGroup();
        }
    }

    if (m_GroupPrev != nullptr)
    {
        m_GroupPrev->WantRedraw = true;
        m_GroupPrev->m_GroupNext = m_GroupNext;

        if (m_GroupPrev->m_StatusbarUnlocker != nullptr)
        {
            m_GroupPrev->m_StatusbarUnlocker->Visible = m_GroupPrev->InGroup();
        }
    }

    m_GroupNext = nullptr;
    m_GroupPrev = nullptr;

    if (m_StatusbarUnlocker != nullptr)
    {
        m_StatusbarUnlocker->Visible = InGroup();
        WantRedraw = true;
    }
}

void CGumpStatusbar::CalculateGumpState()
{
    CGump::CalculateGumpState();
    if ((g_GumpMovingOffset.X != 0) || (g_GumpMovingOffset.Y != 0))
    {
        if (g_Target.IsTargeting())
        {
            g_GeneratedMouseDown = true;
        }

        if (!InGroup())
        {
            int testX = g_MouseManager.Position.X;
            int testY = g_MouseManager.Position.Y;

            if (GetNearStatusbar(testX, testY) != nullptr)
            {
                g_GumpTranslate.X = (float)testX;
                g_GumpTranslate.Y = (float)testY;
            }
        }
    }
    else if (InGroup())
    {
        int x = (int)g_GumpTranslate.X;
        int y = (int)g_GumpTranslate.Y;

        GetStatusbarGroupOffset(x, y);

        g_GumpTranslate.X = (float)x;
        g_GumpTranslate.Y = (float)y;
    }
}

void CGumpStatusbar::PrepareContent()
{
#if defined(XUO_ASSISTANT)
    // A counter refresh must not replace a hitbox midway through a click or drag.
    if (g_Config.AssistantEnabled && Serial == g_PlayerSerial && !Minimized && m_SuppliesExpanded &&
        g_PressedObject.LeftGump != this)
    {
        const uint64_t revision = xuoassist::ItemCounters_Revision();
        if (revision != m_SuppliesRevision || g_Ticks - m_SuppliesSampleAt >= 250)
        {
            m_SuppliesSampleAt = g_Ticks;
            auto snapshot = xuoassist::ItemCounters_GetSupplies();
            m_SuppliesRevision = xuoassist::ItemCounters_Revision();
            if (!(m_Supplies == snapshot))
            {
                m_Supplies = std::move(snapshot);
                WantUpdateContent = true;
            }
        }
    }
#endif
    if (m_HitsBody != nullptr)
    {
        CGameCharacter *obj = g_World->FindWorldCharacter(Serial);

        if (obj == nullptr && m_HitsBody->Color != 0x0386)
        {
            m_Body->Color = 0x0386;
            m_HitsBody->Color = 0x0386;
            m_Entry->Color = 0x0386;
            m_Entry->ReadOnly = true;
        }
        else if (obj != nullptr && m_HitsBody->Color == 0x0386)
        {
            m_Body->Color = g_ConfigManager.GetColorByNotoriety(obj->Notoriety);
            m_HitsBody->Color = 0;

            if (obj->CanChangeName)
            {
                m_Entry->ReadOnly = false;
                m_Entry->Color = 0x000E;
            }
            else
            {
                m_Entry->ReadOnly = true;
                m_Entry->Color = 0x0386;
            }
        }
    }
}

#if defined(XUO_ASSISTANT)
void CGumpStatusbar::AddSupplies(int x, int y, int width)
{
    constexpr int Padding = 10;
    constexpr int HeaderHeight = 31;
    constexpr int StatusHeight = 20;
    constexpr int TabHeight = 17;
    constexpr int TabGapX = 3;
    constexpr int TabGapY = 3;
    constexpr int TabBarBottomGap = 5;
    constexpr int GroupHeaderHeight = 17;
    constexpr int GroupGap = 4;
    constexpr int CellWidth = 48;
    constexpr int CellHeight = 44;
    constexpr int IconBoxWidth = 40;
    constexpr int IconBoxHeight = 28;
    constexpr int MaxBodyHeight = 460;
    constexpr int ToggleWidth = 74;
    constexpr uint16_t Ink = 0x0386;
    constexpr uint16_t Muted = 0x03B2;
    constexpr uint16_t LowHue = 0x0021;
    constexpr uint16_t HighlightTextHue = 0x0481;
    constexpr uint16_t HighlightLowHue = 0x0035;
    constexpr uint32_t HighlightBorder = 0xFF1D1E87;
    constexpr uint32_t HighlightFill = 0xFF3336A6;
    constexpr uint32_t ActiveTabBorder = 0xFF284857;
    constexpr uint32_t ActiveTabFill = 0xFF3D687A;
    constexpr uint32_t GroupBorder = 0xFF7B939D;
    constexpr uint32_t GroupFill = 0xFFB5C4CA;

    const auto addFill = [this, x](int left, int top, int w, int h, uint32_t color) {
        Add(new CGUIColoredPolygone(0, 0, x + left, top, w, h, color));
    };
    const auto addText = [this, x](const astr_t &label, int left, int top, uint16_t hue,
                             int textWidth = 0, TEXT_ALIGN_TYPE align = TS_LEFT) {
        auto *text = static_cast<CGUIText *>(Add(new CGUIText(hue, x + left, top)));
        text->CreateTextureA(9, label, textWidth, align, textWidth > 0 ? UOFONT_CROPPED : 0);
    };
    const auto addControl = [this, x, &addFill, &addText](
                                int id,
                                const char *label,
                                int left,
                                int top,
                                int w,
                                bool active = false) {
        addFill(left, top, w, 17, active ? ActiveTabBorder : 0xFF627B89);
        addFill(left + 1, top + 1, w - 2, 15, active ? ActiveTabFill : 0xFFD5E1E6);
        addText(label, left, top + 2, active ? HighlightTextHue : Ink, w, TS_CENTER);
        Add(new CGUIHitBox(id, x + left, top, w, 17, true));
    };

    if (!m_SuppliesInitialized)
    {
        m_SuppliesInitialized = true;
        m_SuppliesExpanded = xuoassist::ItemCounters_StatusExpanded();
    }

    if (m_SuppliesExpanded)
    {
        m_Supplies = xuoassist::ItemCounters_GetSupplies();
        m_SuppliesRevision = xuoassist::ItemCounters_Revision();
        m_SuppliesSampleAt = g_Ticks;
    }

    const int innerWidth = std::max(1, width - 2 * Padding);
    const astr_t statusLine = m_SuppliesExpanded ? FormatSuppliesStatusText(m_Supplies, " | ") : astr_t{};
    const int statusStripHeight = statusLine.empty() ? 0 : StatusHeight;
    const int columns = std::max(1, innerWidth / CellWidth);
    const int step = innerWidth / columns;

    struct TabButtonSpec
    {
        int Id = 0;
        const char *Label = "";
        bool Active = false;
        int Left = 0;
        int TopOffset = 0;
        int Width = 0;
    };

    std::vector<TabButtonSpec> tabs;
    int tabBarHeight = 0;
    int effectiveCategory = m_Supplies.ActiveCategory;
    xuoassist::SupplyLayoutResult layout;

    if (m_SuppliesExpanded)
    {
        std::vector<xuoassist::SupplyLayoutEntryInput> layoutInputs;
        layoutInputs.reserve(m_Supplies.Entries.size());
        bool hasCustom = false;
        for (size_t i = 0; i < m_Supplies.Entries.size(); ++i)
        {
            const auto &entry = m_Supplies.Entries[i];
            if (entry.Category == xuoassist::SupplyCategory::Custom)
            {
                hasCustom = true;
            }
            layoutInputs.push_back({
                i,
                entry.Category,
                entry.Count,
                entry.Highlighted,
                entry.WeightStones,
                entry.WeightEstimated,
            });
        }
        if (effectiveCategory == static_cast<int>(xuoassist::SupplyCategory::Custom) && !hasCustom)
        {
            effectiveCategory = -1;
        }
        layout = xuoassist::ItemCounters_BuildGroupedSupplyLayout(
            layoutInputs,
            effectiveCategory,
            m_Supplies.HideZero,
            m_Supplies.CollapsedCategories,
            columns,
            CellHeight,
            GroupHeaderHeight,
            GroupGap,
            MaxBodyHeight);

        tabs.push_back({ ID_GSB_SUPPLIES_TAB_ALL, "All", !xuoassist::ItemCounters_IsValidCategory(effectiveCategory) });
        for (size_t c = 0; c < xuoassist::ItemCounters_CategoryCount; ++c)
        {
            const auto cat = static_cast<xuoassist::SupplyCategory>(c);
            if (cat == xuoassist::SupplyCategory::Custom && layout.CategoryCounts[c] == 0)
            {
                continue;
            }
            tabs.push_back({
                ID_GSB_SUPPLIES_TAB_FIRST + static_cast<int>(c),
                xuoassist::ItemCounters_CategoryShortLabel(cat),
                effectiveCategory == static_cast<int>(c),
            });
        }
        tabs.push_back({ ID_GSB_SUPPLIES_HIDE_ZERO, "In bag", m_Supplies.HideZero });

        int cursorX = 0;
        int cursorY = 0;
        for (auto &tab : tabs)
        {
            const int measured = g_FontManager.GetWidthA(9, tab.Label);
            const int pillW = std::min(innerWidth, std::max(28, measured + 10));
            if (cursorX > 0 && cursorX + pillW > innerWidth)
            {
                cursorX = 0;
                cursorY += TabHeight + TabGapY;
            }
            tab.Left = Padding + cursorX;
            tab.TopOffset = cursorY;
            tab.Width = pillW;
            cursorX += pillW + TabGapX;
        }
        tabBarHeight = cursorY + TabHeight + TabBarBottomGap;
    }

    const size_t pages = std::max(size_t{ 1 }, layout.Pages.size());
    m_SuppliesPage = std::min(m_SuppliesPage, pages - 1);
    const bool hasPage = !layout.Pages.empty();
    const int bodyHeight = hasPage ? layout.Pages[m_SuppliesPage].Height : 22;
    const int footer = pages > 1 ? 24 : 0;
    const int height = !m_SuppliesExpanded
        ? HeaderHeight
        : HeaderHeight + statusStripHeight + tabBarHeight + bodyHeight + footer + 8;

    addFill(0, y, width, height, 0xFF323E44);
    addFill(1, y + 1, width - 2, height - 2, 0xFF5D8698);
    addFill(3, y + 3, width - 6, height - 6, 0xFFC5CDCF);
    addControl(ID_GSB_SUPPLIES_TOGGLE, m_SuppliesExpanded ? "- Supplies" : "+ Supplies", Padding, y + 7, ToggleWidth);
    if (!m_SuppliesExpanded)
    {
        return;
    }

    const bool compactHeader = width < 310;
    const int recountWidth = compactHeader ? 52 : 58;
    const int countersWidth = compactHeader ? 34 : 66;
    const int recountLeft = width - Padding - recountWidth;
    const int countersLeft = recountLeft - 4 - countersWidth;
    const int coverageLeft = Padding + ToggleWidth + 6;
    const int coverageWidth = countersLeft - coverageLeft - 4;

    const char *coverage = m_Supplies.AutoRecount ? "Backpack (auto)" : "Backpack";
    if (m_Supplies.Recounting)
    {
        coverage = m_Supplies.AutoRecount ? "Auto-syncing..." : "Recounting...";
    }
    else if (m_Supplies.Coverage == xuoassist::SuppliesCoverage::Unknown)
    {
        coverage = "Counts unknown";
    }
    else if (m_Supplies.Coverage == xuoassist::SuppliesCoverage::Partial)
    {
        coverage = "Partial counts";
    }
    if (coverageWidth > 12)
    {
        addText(coverage, coverageLeft, y + 9, Ink, coverageWidth, TS_LEFT);
        Add(new CGUIHitBox(ID_GSB_SUPPLIES_COVERAGE, x + coverageLeft, y + 7, coverageWidth, 17));
    }

    addControl(
        ID_GSB_SUPPLIES_COUNTERS,
        compactHeader ? "Edit" : "Counters",
        countersLeft,
        y + 7,
        countersWidth);

    const char *recount = m_Supplies.Recounting ? "Cancel" : "Recount";
    if (m_Supplies.CanRecount || m_Supplies.Recounting)
    {
        addControl(ID_GSB_SUPPLIES_RECOUNT, recount, recountLeft, y + 7, recountWidth);
    }
    else
    {
        addText(recount, recountLeft, y + 9, Muted, recountWidth, TS_CENTER);
    }
    Add(new CGUILine(x + Padding, y + HeaderHeight - 3, x + width - Padding,
        y + HeaderHeight - 3, 0xFF516771));

    if (!statusLine.empty())
    {
        const int statusTop = y + HeaderHeight;
        addFill(Padding, statusTop, innerWidth, StatusHeight - 2, 0xFF7B939D);
        addFill(Padding + 1, statusTop + 1, innerWidth - 2, StatusHeight - 4, 0xFFD5E1E6);
        const uint16_t statusHue = !m_Supplies.CriminalText.empty() ? LowHue : Ink;
        addText(statusLine, Padding + 6, statusTop + 2, statusHue, innerWidth - 12, TS_LEFT);
        Add(new CGUIHitBox(ID_GSB_SUPPLIES_STATUS, x + Padding, statusTop, innerWidth, StatusHeight - 2));
    }

    const int tabBarTop = y + HeaderHeight + statusStripHeight + 1;
    for (const auto &tab : tabs)
    {
        addControl(tab.Id, tab.Label, tab.Left, tabBarTop + tab.TopOffset, tab.Width, tab.Active);
    }

    const int bodyTop = y + HeaderHeight + statusStripHeight + tabBarHeight;
    if (!hasPage)
    {
        const char *emptyMsg = m_Supplies.Entries.empty()
            ? "Click Counters to choose tracked items."
            : (m_Supplies.HideZero
                  ? "No carried items in this group. Click In bag to show all."
                  : "No tracked items in this group.");
        addText(emptyMsg, Padding, bodyTop + 4, Ink, innerWidth, TS_LEFT);
        return;
    }

    const auto &page = layout.Pages[m_SuppliesPage];
    int cursorY = bodyTop;
    for (size_t s = 0; s < page.Sections.size(); ++s)
    {
        if (s > 0)
        {
            cursorY += GroupGap;
        }
        const auto &section = page.Sections[s];
        if (section.ShowHeader)
        {
            addFill(Padding, cursorY, innerWidth, GroupHeaderHeight, GroupBorder);
            addFill(Padding + 1, cursorY + 1, innerWidth - 2, GroupHeaderHeight - 2, GroupFill);
            const astr_t title = astr_t(section.Collapsed ? "+ " : "- ") +
                xuoassist::ItemCounters_CategoryLabel(section.Category);
            astr_t summary = section.HasKnownCounts
                ? (str_from(section.StockedEntries) + "/" + str_from(section.TotalEntries))
                : str_from(section.TotalEntries);
            if (section.HasKnownWeight && section.WeightStones > 0)
            {
                summary += " | ";
                if (section.WeightEstimated)
                {
                    summary += "~";
                }
                summary += str_from(section.WeightStones) + "st";
            }
            const int summaryW = 84;
            addText(title, Padding + 5, cursorY + 1, Ink, std::max(1, innerWidth - summaryW - 10), TS_LEFT);
            addText(
                summary,
                Padding + innerWidth - summaryW - 4,
                cursorY + 1,
                section.HasKnownCounts && section.StockedEntries > 0 ? Ink : Muted,
                summaryW,
                TS_RIGHT);
            Add(new CGUIHitBox(
                ID_GSB_SUPPLIES_GROUP_FIRST + static_cast<int>(section.Category),
                x + Padding,
                cursorY,
                innerWidth,
                GroupHeaderHeight,
                true));
            cursorY += GroupHeaderHeight;
        }

        if (section.Collapsed)
        {
            continue;
        }

        const size_t visible = section.SourceIndices.size();
        for (size_t i = 0; i < visible; ++i)
        {
            const size_t entryIndex = section.SourceIndices[i];
            const auto &entry = m_Supplies.Entries[entryIndex];
            const int cellLeft = Padding + static_cast<int>(i % static_cast<size_t>(columns)) * step;
            const int cellTop = cursorY + static_cast<int>(i / static_cast<size_t>(columns)) * CellHeight;
            if (entry.Highlighted)
            {
                addFill(cellLeft + 1, cellTop + 1, step - 2, CellHeight - 2, HighlightBorder);
                addFill(cellLeft + 2, cellTop + 2, step - 4, CellHeight - 4, HighlightFill);
            }
            bool doubleDraw = false;
            const uint16_t drawGraphic = entry.Icon
                ? get_stack_graphic(entry.Graphic, entry.Count > 1 ? entry.Count : 1, doubleDraw)
                : 0;
            const auto *sprite = entry.Icon ? g_Game.ExecuteStaticArt(drawGraphic) : nullptr;
            if (sprite != nullptr && sprite->Width > 0 && sprite->Height > 0)
            {
                const int iconBoxX = x + cellLeft + (step - IconBoxWidth) / 2;
                const int iconBoxY = cellTop + 2;
                const auto layoutIcon = xuoassist::ItemCounters_ComputeBackpackIconLayout(
                    iconBoxX,
                    iconBoxY,
                    IconBoxWidth,
                    IconBoxHeight,
                    sprite->Width,
                    sprite->Height,
                    sprite->ImageOffsetX,
                    sprite->ImageOffsetY,
                    sprite->ImageWidth,
                    sprite->ImageHeight,
                    doubleDraw);
                const uint16_t hue = entry.Hue == 0xFFFF ? 0 : static_cast<uint16_t>(entry.Hue & 0x3FFF);
                const bool partialHue = IsPartialHue(g_Game.GetStaticFlags(drawGraphic));
                Add(new CGUIShader(&g_ColorizerShader, true));
                if (layoutIcon.Downscaled)
                {
                    auto *tile = static_cast<CGUITilepicScaled *>(Add(new CGUITilepicScaled(
                        drawGraphic,
                        hue,
                        iconBoxX,
                        iconBoxY,
                        IconBoxWidth,
                        IconBoxHeight,
                        layoutIcon.DrawX,
                        layoutIcon.DrawY,
                        layoutIcon.DrawWidth,
                        layoutIcon.DrawHeight)));
                    tile->PartialHue = partialHue;
                }
                else
                {
                    auto *tile = static_cast<CGUITilepic *>(Add(new CGUITilepic(
                        drawGraphic,
                        hue,
                        layoutIcon.DrawX,
                        layoutIcon.DrawY,
                        layoutIcon.DoubleDraw)));
                    tile->PartialHue = partialHue;
                }
                Add(new CGUIShader(&g_ColorizerShader, false));
            }
            else
            {
                addText(
                    entry.Key,
                    cellLeft,
                    cellTop + 10,
                    entry.Highlighted ? HighlightTextHue : Ink,
                    step,
                    TS_CENTER);
            }
            const uint16_t countHue = entry.Highlighted
                ? (entry.Low ? HighlightLowHue : HighlightTextHue)
                : (entry.Low ? LowHue : (entry.Count == 0 ? Muted : Ink));
            addText(
                entry.Count < 0 ? astr_t("--") : str_from(entry.Count),
                cellLeft,
                cellTop + 30,
                countHue,
                step,
                TS_CENTER);
            Add(new CGUIHitBox(
                ID_GSB_SUPPLY_FIRST + static_cast<int>(entryIndex),
                x + cellLeft,
                cellTop,
                step,
                CellHeight));
        }

        const int rowsTaken = static_cast<int>((visible + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));
        cursorY += rowsTaken * CellHeight;
    }

    if (pages > 1)
    {
        const int footerTop = bodyTop + bodyHeight + 3;
        if (m_SuppliesPage > 0)
        {
            addControl(ID_GSB_SUPPLIES_PREVIOUS, "<", width / 2 - 59, footerTop, 30);
        }
        addText(
            str_from(m_SuppliesPage + 1) + "/" + str_from(pages),
            width / 2 - 28,
            footerTop + 2,
            Ink,
            56,
            TS_CENTER);
        if (m_SuppliesPage + 1 < pages)
        {
            addControl(ID_GSB_SUPPLIES_NEXT, ">", width / 2 + 29, footerTop, 30);
        }
    }
}
#endif

void CGumpStatusbar::UpdateContent()
{
    Clear();
    m_StatusbarUnlocker = nullptr;
    m_Body = nullptr;
    m_HitsBody = nullptr;
    m_Entry = nullptr;
    bool useUOPGumps = g_FileManager.UseUOPGumps;
    CGUIText *text = nullptr;

    if (Serial == g_PlayerSerial)
    {
        if (!Minimized)
        {
            SDL_Point p = { 0, 0 };

            uint16_t gumpId = 0x2A6C;
            if (g_Config.ClientVersion < CV_308D || g_ConfigManager.GetOldStyleStatusbar())
            {
                p.x = 244;
                p.y = 112;
                gumpId = 0x0802;
            }
            Add(new CGUIGumppic(gumpId, 0, 0));
            auto &io = g_Index.m_Gump[gumpId];
            vfs_gump_dims_resolve(&io); // streamed hole-baked gumps resolve dims on first use
            int xOffset = 0;

            if (io.DataSize == 169884 && io.Height == 171 && io.Width == 409)
            {
                static const int ROW_0_Y = 26;
                static const int ROW_1_Y = 56;
                static const int ROW_2_Y = 86;
                static const int ROW_3_Y = 116;
                static const int ROW_HEIGHT = 24;
                static const int ROW_PADDING = 2;

                static const int LOCKER_COLUMN_X = 10;
                //static const int LOCKER_COLUMN_WIDTH = 10;

                static const int COLUMN_1_X = 20;
                static const int COLUMN_1_WIDTH = 80;
                static const int COLUMN_1_ICON_WIDTH = 35;

                static const int COLUMN_2_X = 100;
                static const int COLUMN_2_WIDTH = 60;
                static const int COLUMN_2_ICON_WIDTH = 20;

                static const int COLUMN_3_X = 160;
                static const int COLUMN_3_WIDTH = 60;
                static const int COLUMN_3_ICON_WIDTH = 30;

                static const int COLUMN_4_X = 220;
                static const int COLUMN_4_WIDTH = 80;
                static const int COLUMN_4_ICON_WIDTH = 35;

                static const int COLUMN_5_X = 300;
                static const int COLUMN_5_WIDTH = 80;
                static const int COLUMN_5_ICON_WIDTH = 55;

                Add(new CGUIGumppic(0x0805, 34, 12));
                int percent = CalculatePercents(g_Player->MaxHits, g_Player->Hits, 109);
                if (percent > 0)
                {
                    short gumpid = 0x0806;
                    if (g_Player->IsPoisoned())
                    {
                        gumpid = 0x0808;
                    }
                    else if (g_Player->YellowHits())
                    {
                        gumpid = 0x0809;
                    }

                    Add(new CGUIGumppicTiled(gumpid, 34, 12, percent, 0));
                }

                Add(new CGUIGumppic(0x0805, 34, 25));
                percent = CalculatePercents(g_Player->MaxMana, g_Player->Mana, 109);
                if (percent > 0)
                {
                    Add(new CGUIGumppicTiled(0x0806, 34, 25, percent, 0));
                }

                Add(new CGUIGumppic(0x0805, 34, 38));
                percent = CalculatePercents(g_Player->MaxStam, g_Player->Stam, 109);
                if (percent > 0)
                {
                    Add(new CGUIGumppicTiled(0x0806, 34, 38, percent, 0));
                }

                if (g_Player->GetName().length())
                {
                    text = (CGUIText *)Add(new CGUIText(0x0386, 100, 10));
                    text->CreateTextureA(1, g_Player->GetName(), 320, TS_CENTER);
                }

                Add(new CGUIButton(ID_GSB_BUFF_GUMP, 0x7538, 0x7538, 0x7538, 40, 50));

                char status = g_Player->LockStr;
                short gumpID = 0x0984;
                if (status == 1)
                {
                    gumpID = 0x0986;
                }
                else if (status == 2)
                {
                    gumpID = 0x082C;
                }
                Add(new CGUIButton(
                    ID_GSB_BUFF_LOCKER_STR,
                    gumpID,
                    gumpID,
                    gumpID,
                    LOCKER_COLUMN_X,
                    ROW_1_Y + ROW_HEIGHT - ROW_PADDING));
                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_1_X + COLUMN_1_ICON_WIDTH,
                    ROW_1_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->Str));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_STR, COLUMN_1_X, ROW_1_Y, COLUMN_1_WIDTH, ROW_HEIGHT));

                status = g_Player->LockDex;
                gumpID = 0x0984;
                if (status == 1)
                {
                    gumpID = 0x0986;
                }
                else if (status == 2)
                {
                    gumpID = 0x082C;
                }
                Add(new CGUIButton(
                    ID_GSB_BUFF_LOCKER_DEX,
                    gumpID,
                    gumpID,
                    gumpID,
                    LOCKER_COLUMN_X,
                    ROW_2_Y + ROW_HEIGHT - ROW_PADDING));
                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_1_X + COLUMN_1_ICON_WIDTH,
                    ROW_2_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->Dex));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_DEX, COLUMN_1_X, ROW_2_Y, COLUMN_1_WIDTH, ROW_HEIGHT));

                status = g_Player->LockInt;
                gumpID = 0x0984;
                if (status == 1)
                {
                    gumpID = 0x0986;
                }
                else if (status == 2)
                {
                    gumpID = 0x082C;
                }
                Add(new CGUIButton(
                    ID_GSB_BUFF_LOCKER_INT,
                    gumpID,
                    gumpID,
                    gumpID,
                    LOCKER_COLUMN_X,
                    ROW_3_Y + ROW_HEIGHT - ROW_PADDING));
                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_1_X + COLUMN_1_ICON_WIDTH,
                    ROW_3_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->Int));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_INT, COLUMN_1_X, ROW_3_Y, COLUMN_1_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_2_X + COLUMN_2_ICON_WIDTH,
                    ROW_1_Y + (ROW_HEIGHT / 2) - ROW_PADDING));
                text->CreateTextureA(1, str_from(g_Player->Hits), 40, TS_CENTER);
                Add(new CGUILine(
                    COLUMN_2_X + COLUMN_2_ICON_WIDTH,
                    ROW_1_Y + 23,
                    COLUMN_2_X + COLUMN_2_ICON_WIDTH + 30,
                    ROW_1_Y + 23,
                    0xFF383838));
                text = (CGUIText *)Add(new CGUIText(
                    0x0386, COLUMN_2_X + COLUMN_2_ICON_WIDTH, ROW_1_Y + ROW_HEIGHT - ROW_PADDING));
                text->CreateTextureA(1, str_from(g_Player->MaxHits), 40, TS_CENTER);
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_HITS, COLUMN_2_X, ROW_1_Y, COLUMN_2_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_2_X + COLUMN_2_ICON_WIDTH,
                    ROW_2_Y + (ROW_HEIGHT / 2) - ROW_PADDING));
                text->CreateTextureA(1, str_from(g_Player->Stam), 40, TS_CENTER);
                Add(new CGUILine(
                    COLUMN_2_X + COLUMN_2_ICON_WIDTH,
                    ROW_2_Y + 23,
                    COLUMN_2_X + COLUMN_2_ICON_WIDTH + 30,
                    ROW_2_Y + 23,
                    0xFF383838));
                text = (CGUIText *)Add(new CGUIText(
                    0x0386, COLUMN_2_X + COLUMN_2_ICON_WIDTH, ROW_2_Y + ROW_HEIGHT - ROW_PADDING));
                text->CreateTextureA(1, str_from(g_Player->MaxStam), 40, TS_CENTER);
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_STAM, COLUMN_2_X, ROW_2_Y, COLUMN_2_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_2_X + COLUMN_2_ICON_WIDTH,
                    ROW_3_Y + (ROW_HEIGHT / 2) - ROW_PADDING));
                text->CreateTextureA(1, str_from(g_Player->Mana), 40, TS_CENTER);
                Add(new CGUILine(
                    COLUMN_2_X + COLUMN_2_ICON_WIDTH,
                    ROW_3_Y + 23,
                    COLUMN_2_X + COLUMN_2_ICON_WIDTH + 30,
                    ROW_3_Y + 23,
                    0xFF383838));
                text = (CGUIText *)Add(new CGUIText(
                    0x0386, COLUMN_2_X + COLUMN_2_ICON_WIDTH, ROW_3_Y + ROW_HEIGHT - ROW_PADDING));
                text->CreateTextureA(1, str_from(g_Player->MaxMana), 40, TS_CENTER);
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_MANA, COLUMN_2_X, ROW_3_Y, COLUMN_2_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_3_X + COLUMN_3_ICON_WIDTH,
                    ROW_1_Y + (ROW_HEIGHT / 2) - ROW_PADDING));
                text->CreateTextureA(1, str_from(g_Player->Followers), 40, TS_CENTER);
                Add(new CGUILine(
                    COLUMN_3_X + COLUMN_3_ICON_WIDTH,
                    ROW_1_Y + 23,
                    COLUMN_3_X + COLUMN_3_ICON_WIDTH + 25,
                    ROW_1_Y + 23,
                    0xFF383838));
                text = (CGUIText *)Add(new CGUIText(
                    0x0386, COLUMN_3_X + COLUMN_3_ICON_WIDTH, ROW_1_Y + ROW_HEIGHT - ROW_PADDING));
                text->CreateTextureA(1, str_from(g_Player->MaxFollowers), 40, TS_CENTER);
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_FOLLOWERS, COLUMN_3_X, ROW_1_Y, COLUMN_3_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_3_X + COLUMN_3_ICON_WIDTH,
                    ROW_2_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->Armor));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_ARMOR, COLUMN_3_X, ROW_2_Y, COLUMN_3_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    ResolvePlayerWeightHue(),
                    COLUMN_3_X + COLUMN_3_ICON_WIDTH,
                    ROW_3_Y + (ROW_HEIGHT / 2) - ROW_PADDING));
                text->CreateTextureA(1, str_from(g_Player->Weight), 40, TS_CENTER);
                Add(new CGUILine(
                    COLUMN_3_X + COLUMN_3_ICON_WIDTH,
                    ROW_3_Y + 23,
                    COLUMN_3_X + COLUMN_3_ICON_WIDTH + 25,
                    ROW_3_Y + 23,
                    0xFF383838));
                text = (CGUIText *)Add(new CGUIText(
                    0x0386, COLUMN_3_X + COLUMN_3_ICON_WIDTH, ROW_3_Y + ROW_HEIGHT - ROW_PADDING));
                text->CreateTextureA(1, str_from(g_Player->MaxWeight), 40, TS_CENTER);
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_WEIGHT, COLUMN_3_X, ROW_3_Y, COLUMN_3_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_4_X + COLUMN_4_ICON_WIDTH + 20,
                    ROW_0_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->Luck));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_RESISTANCE_FIRE, COLUMN_4_X, ROW_0_Y, COLUMN_4_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_4_X + COLUMN_4_ICON_WIDTH,
                    ROW_1_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->StatsCap));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_DAMAGE, COLUMN_4_X, ROW_1_Y, COLUMN_4_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_4_X + COLUMN_4_ICON_WIDTH,
                    ROW_2_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(
                    1, str_from(g_Player->MinDamage) + "-" + str_from(g_Player->MaxDamage));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_DAMAGE, COLUMN_4_X, ROW_2_Y, COLUMN_4_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_4_X + COLUMN_4_ICON_WIDTH,
                    ROW_3_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->Gold));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_GOLD, COLUMN_4_X, ROW_3_Y, COLUMN_4_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_5_X + COLUMN_5_ICON_WIDTH,
                    ROW_0_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->ColdResistance));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_LUCK, COLUMN_5_X, ROW_0_Y, COLUMN_5_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_5_X + COLUMN_5_ICON_WIDTH,
                    ROW_1_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->FireResistance));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_LUCK, COLUMN_5_X, ROW_1_Y, COLUMN_5_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_5_X + COLUMN_5_ICON_WIDTH,
                    ROW_2_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->PoisonResistance));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_LUCK, COLUMN_5_X, ROW_2_Y, COLUMN_5_WIDTH, ROW_HEIGHT));

                text = (CGUIText *)Add(new CGUIText(
                    0x0386,
                    COLUMN_5_X + COLUMN_5_ICON_WIDTH,
                    ROW_3_Y + ROW_HEIGHT - (3 * ROW_PADDING)));
                text->CreateTextureA(1, str_from(g_Player->EnergyResistance));
                Add(new CGUIHitBox(
                    ID_GSB_TEXT_LUCK, COLUMN_5_X, ROW_3_Y, COLUMN_5_WIDTH, ROW_HEIGHT));

                Add(new CGUIHitBox(
                    ID_GSB_MINIMIZE,
                    COLUMN_5_X + COLUMN_5_WIDTH + 5,
                    ROW_3_Y + ROW_HEIGHT + 5,
                    20,
                    20,
                    true));
            }
            else if (g_Config.ClientVersion >= CV_308Z && !g_ConfigManager.GetOldStyleStatusbar())
            {
                p.x = 389;
                p.y = 152;

                // Player name
                if (g_Player->GetName().length() != 0u)
                {
                    text = (CGUIText *)Add(new CGUIText(0x0386, useUOPGumps ? 90 : 58, 50));
                    text->CreateTextureA(1, g_Player->GetName(), 320, TS_CENTER);
                }

                if (g_Config.ClientVersion >= CV_5020)
                {
                    Add(new CGUIButton(ID_GSB_BUFF_GUMP, 0x7538, 0x7538, 0x7538, 40, 50));
                }

                if (g_DrawStatLockers)
                {
                    //Str
                    uint8_t status = g_Player->LockStr; // Status (down / up / lock)
                    xOffset = useUOPGumps ? 28 : 40;
                    uint16_t gumpID = 0x0984; //Up
                    if (status == 1)
                    {
                        gumpID = 0x0986; //Down
                    }
                    else if (status == 2)
                    {
                        gumpID = 0x082C; //Lock
                    }

                    Add(new CGUIButton(
                        ID_GSB_BUFF_LOCKER_STR, gumpID, gumpID, gumpID, xOffset, 76));

                    //Dex
                    status = g_Player->LockDex; // Status (down / up / lock)

                    gumpID = 0x0984; //Up
                    if (status == 1)
                    {
                        gumpID = 0x0986; //Down
                    }
                    else if (status == 2)
                    {
                        gumpID = 0x082C; //Lock
                    }

                    Add(new CGUIButton(
                        ID_GSB_BUFF_LOCKER_DEX, gumpID, gumpID, gumpID, xOffset, 102));

                    //Int
                    status = g_Player->LockInt; // Status (down / up / lock)

                    gumpID = 0x0984; //Up
                    if (status == 1)
                    {
                        gumpID = 0x0986; //Down
                    }
                    else if (status == 2)
                    {
                        gumpID = 0x082C; //Lock
                    }

                    Add(new CGUIButton(
                        ID_GSB_BUFF_LOCKER_INT, gumpID, gumpID, gumpID, xOffset, 132));
                }

                if (useUOPGumps)
                {
                    xOffset = 80;
                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 161));
                    text->CreateTextureA(1, str_from(g_Player->AttackChance));
                    Add(new CGUIHitBox(ID_GSB_TEXT_HIT_CHANCE, 58, 154, 59, 24));
                }
                else
                {
                    xOffset = 88;
                }

                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 77));
                text->CreateTextureA(1, str_from(g_Player->Str));

                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 105));
                text->CreateTextureA(1, str_from(g_Player->Dex));

                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 133));
                text->CreateTextureA(1, str_from(g_Player->Int));

                Add(new CGUIHitBox(ID_GSB_TEXT_STR, 58, 70, 59, 24));
                Add(new CGUIHitBox(ID_GSB_TEXT_DEX, 58, 98, 59, 24));
                Add(new CGUIHitBox(ID_GSB_TEXT_INT, 58, 126, 59, 24));

                int textWidth = 40;
                if (useUOPGumps)
                {
                    xOffset = 150;
                    //Defence chance increase
                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 161));
                    text->CreateTextureA(
                        1,
                        str_from(g_Player->DefenceChance) + "/" +
                            str_from(g_Player->MaxDefenceChance));
                    Add(new CGUIHitBox(ID_GSB_TEXT_DEFENCE_CHANCE, 124, 154, 59, 24));
                }
                else
                {
                    xOffset = 146;
                }
                //Hits
                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 70));
                text->CreateTextureA(1, str_from(g_Player->Hits), textWidth, TS_CENTER);

                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 83));
                text->CreateTextureA(1, str_from(g_Player->MaxHits), textWidth, TS_CENTER);

                //Stam
                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 98));
                text->CreateTextureA(1, str_from(g_Player->Stam), textWidth, TS_CENTER);

                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 111));
                text->CreateTextureA(1, str_from(g_Player->MaxStam), textWidth, TS_CENTER);

                //Mana
                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 126));
                text->CreateTextureA(1, str_from(g_Player->Mana), textWidth, TS_CENTER);

                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 139));
                text->CreateTextureA(1, str_from(g_Player->MaxMana), textWidth, TS_CENTER);

                Add(new CGUILine(xOffset, 138, 185, 138, 0xFF383838));
                Add(new CGUILine(xOffset, 110, 185, 110, 0xFF383838));
                Add(new CGUILine(xOffset, 82, 185, 82, 0xFF383838));

                Add(new CGUIHitBox(ID_GSB_TEXT_HITS, 124, 70, 59, 24));
                Add(new CGUIHitBox(ID_GSB_TEXT_STAM, 124, 98, 59, 24));
                Add(new CGUIHitBox(ID_GSB_TEXT_MANA, 124, 126, 59, 24));

                if (useUOPGumps)
                {
                    xOffset = 240;
                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 161));
                    text->CreateTextureA(1, str_from(g_Player->LowerManaCost));
                    Add(new CGUIHitBox(ID_GSB_TEXT_LOWER_MANA, 205, 154, 65, 24));
                }
                else
                {
                    xOffset = 220;
                }
                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 77));
                text->CreateTextureA(1, str_from(g_Player->StatsCap));

                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 105));
                text->CreateTextureA(1, str_from(g_Player->Luck));

                //Weights
                text = (CGUIText *)Add(new CGUIText(ResolvePlayerWeightHue(), xOffset, 126));
                text->CreateTextureA(1, str_from(g_Player->Weight), textWidth, TS_CENTER);

                Add(new CGUILine(
                    useUOPGumps ? 236 : 216, 138, useUOPGumps ? 270 : 250, 138, 0xFF383838));

                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 139));
                text->CreateTextureA(1, str_from(g_Player->MaxWeight), textWidth, TS_CENTER);

                xOffset = useUOPGumps ? 205 : 188;
                Add(new CGUIHitBox(ID_GSB_TEXT_MAX_STATS, xOffset, 70, 65, 24));
                //Add(new CGUIHitBox(ID_GSB_TEXT_TITHING_POINTS, xOffset, 70, 65, 24)); // Find which client version had Tithing Points
                Add(new CGUIHitBox(ID_GSB_TEXT_LUCK, xOffset, 98, 65, 24));
                Add(new CGUIHitBox(ID_GSB_TEXT_WEIGHT, xOffset, 126, 65, 24));

                if (useUOPGumps)
                {
                    xOffset = 320;
                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 105));
                    text->CreateTextureA(1, str_from(g_Player->WeaponDamage));
                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 161));
                    text->CreateTextureA(1, str_from(g_Player->WeaponSpeed));

                    Add(new CGUIHitBox(ID_GSB_TEXT_WEAPON_DMG, 285, 98, 69, 24));
                    Add(new CGUIHitBox(ID_GSB_TEXT_SWING_SPEED, 285, 154, 69, 24));
                }
                else
                {
                    xOffset = 280;
                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 105));
                    text->CreateTextureA(1, str_from(g_Player->Gold));
                    Add(new CGUIHitBox(ID_GSB_TEXT_GOLD, 260, 98, 69, 24));
                }

                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 77));
                text->CreateTextureA(
                    1, str_from(g_Player->MinDamage) + "-" + str_from(g_Player->MaxDamage));

                text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 133));
                text->CreateTextureA(
                    1, str_from(g_Player->Followers) + "/" + str_from(g_Player->MaxFollowers));

                xOffset = useUOPGumps ? 285 : 260;
                Add(new CGUIHitBox(ID_GSB_TEXT_DAMAGE, xOffset, 70, 69, 24));
                Add(new CGUIHitBox(ID_GSB_TEXT_FOLLOWERS, xOffset, 126, 69, 24));

                if (useUOPGumps)
                {
                    xOffset = 400;
                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 77));
                    text->CreateTextureA(1, str_from(g_Player->LowerRegCost));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 105));
                    text->CreateTextureA(1, str_from(g_Player->SpellDamage));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 133));
                    text->CreateTextureA(1, str_from(g_Player->CastSpeed));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 161));
                    text->CreateTextureA(1, str_from(g_Player->CastRecovery));

                    xOffset = 365;
                    Add(new CGUIHitBox(ID_GSB_TEXT_LOWER_REG_COST, xOffset, 70, 55, 24));
                    Add(new CGUIHitBox(ID_GSB_TEXT_SPELL_DMG, xOffset, 98, 55, 24));
                    Add(new CGUIHitBox(ID_GSB_TEXT_FASTER_CASTING, xOffset, 126, 55, 24));
                    Add(new CGUIHitBox(ID_GSB_TEXT_CAST_RECOVERY, xOffset, 154, 55, 24));

                    xOffset = 480;
                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 161));
                    text->CreateTextureA(1, str_from(g_Player->Gold));
                    Add(new CGUIHitBox(ID_GSB_TEXT_GOLD, 445, 154, 55, 24));

                    xOffset = 475;
                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 74));
                    text->CreateTextureA(
                        1,
                        str_from(g_Player->Armor) + "/" +
                            str_from(g_Player->MaxPhysicalResistance));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 92));
                    text->CreateTextureA(
                        1,
                        str_from(g_Player->FireResistance) + "/" +
                            str_from(g_Player->MaxFireResistance));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 106));
                    text->CreateTextureA(
                        1,
                        str_from(g_Player->ColdResistance) + "/" +
                            str_from(g_Player->MaxColdResistance));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 120));
                    text->CreateTextureA(
                        1,
                        str_from(g_Player->PoisonResistance) + "/" +
                            str_from(g_Player->MaxPoisonResistance));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 134));
                    text->CreateTextureA(
                        1,
                        str_from(g_Player->EnergyResistance) + "/" +
                            str_from(g_Player->MaxEnergyResistance));
                }
                else
                {
                    xOffset = 354;
                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 76));
                    text->CreateTextureA(1, str_from(g_Player->Armor));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 92));
                    text->CreateTextureA(1, str_from(g_Player->FireResistance));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 106));
                    text->CreateTextureA(1, str_from(g_Player->ColdResistance));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 120));
                    text->CreateTextureA(1, str_from(g_Player->PoisonResistance));

                    text = (CGUIText *)Add(new CGUIText(0x0386, xOffset, 134));
                    text->CreateTextureA(1, str_from(g_Player->EnergyResistance));
                }

                xOffset = useUOPGumps ? 445 : 334;
                Add(new CGUIHitBox(ID_GSB_TEXT_RESISTANCE_PHYSICAL, xOffset, 76, 40, 14));
                Add(new CGUIHitBox(ID_GSB_TEXT_RESISTANCE_FIRE, xOffset, 92, 40, 14));
                Add(new CGUIHitBox(ID_GSB_TEXT_RESISTANCE_COLD, xOffset, 106, 40, 14));
                Add(new CGUIHitBox(ID_GSB_TEXT_RESISTANCE_POISON, xOffset, 120, 40, 14));
                Add(new CGUIHitBox(ID_GSB_TEXT_RESISTANCE_ENERGY, xOffset, 134, 40, 14));
            }
            else
            {
                if (p.x == 0)
                {
                    p.x = 243;
                    p.y = 150;
                }

                if (g_Player->GetName().length() != 0u)
                {
                    text = (CGUIText *)Add(new CGUIText(0x0386, 86, 42));
                    text->CreateTextureA(1, g_Player->GetName());
                }

                text = (CGUIText *)Add(new CGUIText(0x0386, 86, 61));
                text->CreateTextureA(1, str_from(g_Player->Str));

                text = (CGUIText *)Add(new CGUIText(0x0386, 86, 73));
                text->CreateTextureA(1, str_from(g_Player->Dex));

                text = (CGUIText *)Add(new CGUIText(0x0386, 86, 85));
                text->CreateTextureA(1, str_from(g_Player->Int));

                text = (CGUIText *)Add(new CGUIText(0x0386, 86, 97));
                text->CreateTextureA(1, (g_Player->Gender == GENDER_FEMALE ? "F" : "M"));

                text = (CGUIText *)Add(new CGUIText(0x0386, 86, 109));
                text->CreateTextureA(1, str_from(g_Player->Armor));

                text = (CGUIText *)Add(new CGUIText(0x0386, 171, 61));
                text->CreateTextureA(
                    1, str_from(g_Player->Hits) + "/" + str_from(g_Player->MaxHits));

                text = (CGUIText *)Add(new CGUIText(0x0386, 171, 73));
                text->CreateTextureA(
                    1, str_from(g_Player->Mana) + "/" + str_from(g_Player->MaxMana));

                text = (CGUIText *)Add(new CGUIText(0x0386, 171, 85));
                text->CreateTextureA(
                    1, str_from(g_Player->Stam) + "/" + str_from(g_Player->MaxStam));

                text = (CGUIText *)Add(new CGUIText(0x0386, 171, 97));
                text->CreateTextureA(1, str_from(g_Player->Gold));

                text = (CGUIText *)Add(new CGUIText(ResolvePlayerWeightHue(), 171, 109));
                text->CreateTextureA(1, str_from(g_Player->Weight));

                Add(new CGUIHitBox(ID_GSB_TEXT_STR, 86, 61, 34, 12));
                Add(new CGUIHitBox(ID_GSB_TEXT_DEX, 86, 73, 34, 12));
                Add(new CGUIHitBox(ID_GSB_TEXT_INT, 86, 85, 34, 12));
                Add(new CGUIHitBox(ID_GSB_TEXT_SEX, 86, 97, 34, 12));
                Add(new CGUIHitBox(ID_GSB_TEXT_ARMOR, 86, 109, 34, 12));

                Add(new CGUIHitBox(ID_GSB_TEXT_HITS, 171, 61, 66, 12));
                Add(new CGUIHitBox(ID_GSB_TEXT_MANA, 171, 73, 66, 12));
                Add(new CGUIHitBox(ID_GSB_TEXT_STAM, 171, 85, 66, 12));
                Add(new CGUIHitBox(ID_GSB_TEXT_GOLD, 171, 97, 66, 12));
                Add(new CGUIHitBox(ID_GSB_TEXT_WEIGHT, 171, 109, 66, 12));

                if (!g_ConfigManager.GetOldStyleStatusbar())
                {
                    if (g_Config.ClientVersion == CV_308D)
                    {
                        text = (CGUIText *)Add(new CGUIText(0x0386, 171, 124));
                        text->CreateTextureA(1, str_from(g_Player->StatsCap));

                        Add(new CGUIHitBox(ID_GSB_TEXT_MAX_STATS, 171, 124, 34, 12));
                    }
                    else if (g_Config.ClientVersion == CV_308J)
                    {
                        text = (CGUIText *)Add(new CGUIText(0x0386, 180, 131));
                        text->CreateTextureA(1, str_from(g_Player->StatsCap));

                        text = (CGUIText *)Add(new CGUIText(0x0386, 180, 144));
                        text->CreateTextureA(
                            1,
                            str_from(g_Player->Followers) + "/" + str_from(g_Player->MaxFollowers));

                        Add(new CGUIHitBox(ID_GSB_TEXT_MAX_STATS, 180, 131, 34, 12));
                        Add(new CGUIHitBox(ID_GSB_TEXT_FOLLOWERS, 171, 144, 34, 12));
                    }
                }
            }

            if (!useUOPGumps)
            {
                Add(new CGUIHitBox(ID_GSB_MINIMIZE, p.x, p.y, 16, 16, true));
            }
            else
            {
                p.x = 540;
                p.y = 180;
                Add(new CGUIHitBox(ID_GSB_MINIMIZE, p.x, p.y, 16, 16, true));
            }
#if defined(XUO_ASSISTANT)
            if (g_Config.AssistantEnabled && io.Width >= 240 && io.Height > 0)
            {
                int left = 0;
                int right = io.Width - 1;
                int bottom = io.Height - 1;
                // Some status art includes transparent gutters around the visible frame.
                if (auto *body = g_Game.ExecuteGump(gumpId))
                {
                    const int row = body->Height / 2;
                    while (left < right && !body->TestHit(left, row, true))
                    {
                        ++left;
                    }
                    while (right > left && !body->TestHit(right, row, true))
                    {
                        --right;
                    }
                    const int middle = (left + right) / 2;
                    while (bottom > row && !body->TestHit(middle, bottom, true))
                    {
                        --bottom;
                    }
                }
                if (right - left + 1 < 240)
                {
                    left = 0;
                    right = io.Width - 1;
                    bottom = io.Height - 1;
                }
                AddSupplies(left, std::max(bottom + 1, p.y + 16) + 2, right - left + 1);
            }
#endif
        }
        else // Reduced status bar version
        {
            if (g_Party.Leader != 0 && !g_ConfigManager.GetOriginalPartyStatusbar()) //inParty
            {
                CGUIGumppic *bodyGump = (CGUIGumppic *)Add(new CGUIGumppic(0x0803, 0, 0));
                bodyGump->SelectOnly = true;

                text = (CGUIText *)Add(new CGUIText(0x0386, 16, -2));
                text->CreateTextureA(3, "[* SELF *]");

                Add(new CGUIButton(ID_GSB_BUTTON_HEAL_1, 0x0938, 0x093A, 0x093A, 16, 20));
                Add(new CGUIButton(ID_GSB_BUTTON_HEAL_2, 0x0939, 0x093A, 0x093A, 16, 33));

                int color = 0;
                if (g_Player->IsPoisoned())
                {
                    color = 63; //Character status line (green)
                }
                else if (g_Player->YellowHits())
                {
                    color = 353; //Character status line (yellow)
                }

                //Hits
                Add(new CGUIGumppic(0x0028, 34, 20));

                int per = CalculatePercents(g_Player->MaxHits, g_Player->Hits, 96);
                Add(new CGUIShader(&g_ColorizerShader, true));
                if (per > 0)
                {
                    CGUIGumppic *gumppic =
                        (CGUIGumppic *)Add(new CGUIGumppicTiled(0x0029, 34, 20, per, 0));
                    gumppic->Color = color;
                }

                //Mana
                Add(new CGUIGumppic(0x0028, 34, 33));

                per = CalculatePercents(g_Player->MaxMana, g_Player->Mana, 96);

                if (per > 0)
                {
                    CGUIGumppic *gumppic = (CGUIGumppic *)Add(new CGUIGumppicTiled(
                        0x0029, 34, 33, per, 0)); //0x0170 green //0x0035 yellow
                    gumppic->Color = 0x0482;
                }

                //Stam
                Add(new CGUIGumppic(0x0028, 34, 45));

                per = CalculatePercents(g_Player->MaxStam, g_Player->Stam, 96);

                if (per > 0)
                {
                    CGUIGumppic *gumppic = (CGUIGumppic *)Add(new CGUIGumppicTiled(
                        0x0029, 34, 45, per, 0)); //0x0170 green //0x0035 yellow
                    gumppic->Color = 0x0075;
                }

                Add(new CGUIShader(&g_ColorizerShader, false));
            }
            else
            {
                if (g_Player->Warmode)
                {
                    Add(new CGUIGumppic(0x0807, 0, 0)); // warmode enabled
                }
                else
                {
                    Add(new CGUIGumppic(0x0803, 0, 0));
                }

                //Hits
                Add(new CGUIGumppic(0x0805, 34, 12));

                int per = CalculatePercents(g_Player->MaxHits, g_Player->Hits, 109);

                if (per > 0)
                {
                    uint16_t gumpid = 0x0806; //Character status line (blue)
                    if (g_Player->IsPoisoned())
                    {
                        gumpid = 0x0808; //Character status line (green)
                    }
                    else if (g_Player->YellowHits())
                    {
                        gumpid = 0x0809; //Character status line (yellow)
                    }

                    Add(new CGUIGumppicTiled(gumpid, 34, 12, per, 0));
                }

                //Mana
                Add(new CGUIGumppic(0x0805, 34, 25));

                per = CalculatePercents(g_Player->MaxMana, g_Player->Mana, 109);

                if (per > 0)
                {
                    Add(new CGUIGumppicTiled(0x0806, 34, 25, per, 0));
                }

                //Stam
                Add(new CGUIGumppic(0x0805, 34, 38));

                per = CalculatePercents(g_Player->MaxStam, g_Player->Stam, 109);

                if (per > 0)
                {
                    Add(new CGUIGumppicTiled(0x0806, 34, 38, per, 0));
                }
            }

            m_StatusbarUnlocker = (CGUIButton *)Add(
                new CGUIButton(ID_GSB_BUTTON_REMOVE_FROM_GROUP, 0x082C, 0x082C, 0x082C, 136, 24));
            m_StatusbarUnlocker->BoundingBoxCheck = true;
            m_StatusbarUnlocker->Visible = InGroup();
        }
    }
    else
    {
        if (g_Party.Contains(Serial) && !g_ConfigManager.GetOriginalPartyStatusbar())
        {
            for (int i = 0; i < 10; i++)
            {
                if (g_Party.Member[i].Serial == Serial)
                {
                    CPartyObject &member = g_Party.Member[i];
                    if (member.Character == nullptr)
                    {
                        member.Character = g_World->FindWorldCharacter(member.Serial);

                        if (member.Character == nullptr)
                        {
                            break;
                        }
                    }

                    CGUIGumppic *bodyGump = (CGUIGumppic *)Add(new CGUIGumppic(0x0803, 0, 0));
                    bodyGump->SelectOnly = true;

                    auto memberName = member.GetName((int)i);
                    bool outofRange = false;
                    if (member.Character->RemovedFromRender())
                    {
                        outofRange = true;
                    }
                    uint16_t textColor =
                        outofRange ?
                            912 :
                            g_ConfigManager.GetColorByNotoriety(member.Character->Notoriety);

                    Add(new CGUIHitBox(ID_GSB_TEXT_FIELD, 16, -2, 109, 16));
                    m_Entry = (CGUITextEntry *)Add(new CGUITextEntry(
                        ID_GSB_TEXT_FIELD,
                        textColor,
                        textColor,
                        textColor,
                        16,
                        -2,
                        125,
                        false,
                        1,
                        TS_LEFT,
                        UOFONT_FIXED));
                    m_Entry->m_Entry.SetTextA(memberName);
                    m_Entry->CheckOnSerial = true;

                    if (member.Character->CanChangeName)
                    {
                        m_Entry->ReadOnly = false;
                    }
                    else
                    {
                        m_Entry->ReadOnly = true;
                    }

                    if (!outofRange)
                    {
                        Add(new CGUIButton(ID_GSB_BUTTON_HEAL_1, 0x0938, 0x093A, 0x093A, 16, 20));
                        Add(new CGUIButton(ID_GSB_BUTTON_HEAL_2, 0x0939, 0x093A, 0x093A, 16, 33));
                    }
                    int color = 0;
                    if (member.Character->IsPoisoned())
                    {
                        color = 63; //Character status line (green)
                    }
                    else if (member.Character->YellowHits())
                    {
                        color = 353; //Character status line (yellow)
                    }

                    if (outofRange)
                    {
                        color = 912;
                    }
                    Add(new CGUIShader(&g_ColorizerShader, true));
                    //Hits
                    CGUIGumppic *g = new CGUIGumppic(0x0028, 34, 20);
                    g->Color = outofRange ? color : 0;
                    Add(g);

                    int per =
                        CalculatePercents(member.Character->MaxHits, member.Character->Hits, 96);

                    if (per > 0)
                    {
                        CGUIGumppic *gumppic =
                            (CGUIGumppic *)Add(new CGUIGumppicTiled(0x0029, 34, 20, per, 0));
                        gumppic->Color = color;
                    }

                    //Mana
                    g = new CGUIGumppic(0x0028, 34, 33);
                    g->Color = outofRange ? color : 0;
                    Add(g);

                    per = CalculatePercents(member.Character->MaxMana, member.Character->Mana, 96);

                    if (per > 0)
                    {
                        CGUIGumppic *gumppic = (CGUIGumppic *)Add(new CGUIGumppicTiled(
                            0x0029, 34, 33, per, 0)); //0x0170 green //0x0035 yellow
                        gumppic->Color = outofRange ? color : 0x0482;
                    }

                    //Stam
                    g = new CGUIGumppic(0x0028, 34, 45);
                    g->Color = outofRange ? color : 0;
                    Add(g);

                    per = CalculatePercents(member.Character->MaxStam, member.Character->Stam, 96);

                    if (per > 0)
                    {
                        CGUIGumppic *gumppic = (CGUIGumppic *)Add(new CGUIGumppicTiled(
                            0x0029, 34, 45, per, 0)); //0x0170 green //0x0035 yellow
                        gumppic->Color = outofRange ? color : 0x0075;
                    }

                    Add(new CGUIShader(&g_ColorizerShader, false));

                    break;
                }
            }
        }
        else
        {
            Add(new CGUIShader(&g_ColorizerShader, true));

            uint16_t color = 0;
            uint16_t hitsColor = 0x0386;
            uint16_t textColor = 0x0386;
            CGameCharacter *obj = g_World->FindWorldCharacter(Serial);
            auto objName = m_Name;
            bool canChangeName = false;

            if (obj != nullptr)
            {
                hitsColor = 0;
                color = g_ConfigManager.GetColorByNotoriety(obj->Notoriety);

                if (obj->Notoriety == NT_CRIMINAL || obj->Notoriety == NT_SOMEONE_GRAY)
                {
                    color = 0;
                }

                objName = obj->GetName();
                m_Name = objName;

                if (obj->CanChangeName)
                {
                    textColor = 0x000E;
                    canChangeName = true;
                }
            }

            m_Body = (CGUIGumppic *)Add(new CGUIGumppic(0x0804, 0, 0));
            m_Body->Color = color;

            m_HitsBody = (CGUIGumppic *)Add(new CGUIGumppic(0x0805, 34, 38));
            m_HitsBody->Color = hitsColor;

            Add(new CGUIShader(&g_ColorizerShader, false));

            if (obj != nullptr && obj->MaxHits > 0)
            {
                int per = CalculatePercents(obj->MaxHits, obj->Hits, 109);

                if (per > 0)
                {
                    uint16_t gumpid = 0x0806; //Character status line (blue)
                    if (obj->IsPoisoned())
                    {
                        gumpid = 0x0808; //Character status line (green)
                    }
                    else if (obj->YellowHits())
                    {
                        gumpid = 0x0809; //Character status line (yellow)
                    }

                    Add(new CGUIGumppicTiled(gumpid, 34, 38, per, 0));
                }
            }

            Add(new CGUIHitBox(ID_GSB_TEXT_FIELD, 16, 14, 109, 16));
            m_Entry = (CGUITextEntry *)Add(new CGUITextEntry(
                ID_GSB_TEXT_FIELD,
                textColor,
                textColor,
                textColor,
                16,
                14,
                125,
                false,
                1,
                TS_LEFT,
                UOFONT_FIXED));
            m_Entry->m_Entry.SetTextA(objName);
            m_Entry->CheckOnSerial = true;

            if (canChangeName)
            {
                m_Entry->ReadOnly = false;
            }
            else
            {
                m_Entry->ReadOnly = true;
            }
        }

        m_StatusbarUnlocker = (CGUIButton *)Add(
            new CGUIButton(ID_GSB_BUTTON_REMOVE_FROM_GROUP, 0x082C, 0x082C, 0x082C, 136, 24));
        m_StatusbarUnlocker->BoundingBoxCheck = true;
        m_StatusbarUnlocker->Visible = InGroup();
    }

    for (CBaseGUI *item = (CBaseGUI *)m_Items; item != nullptr; item = (CBaseGUI *)item->m_Next)
    {
        if (item->Serial != ID_GSB_MINIMIZE
#if defined(XUO_ASSISTANT)
            && !IsSuppliesInteractiveControl(item->Serial)
#endif
        )
        {
            item->MoveOnDrag = true;
        }
    }
}

void CGumpStatusbar::OnLeftMouseButtonDown()
{
    if (g_GeneratedMouseDown)
    {
        return;
    }

    if (g_Target.IsTargeting())
    {
        g_Target.SendTargetObject(Serial);
        g_MouseManager.CancelDoubleClick = true;
    }
    else
    {
        CGump::OnLeftMouseButtonDown();
    }
}

void CGumpStatusbar::GUMP_BUTTON_EVENT_C
{
    if (g_GeneratedMouseDown)
    {
        return;
    }

#if defined(XUO_ASSISTANT)
    if (Serial == g_PlayerSerial && !Minimized && g_Config.AssistantEnabled &&
        IsSuppliesInteractiveControl(serial))
    {
        if (serial == ID_GSB_SUPPLIES_TOGGLE)
        {
            m_SuppliesInitialized = true;
            m_SuppliesExpanded = !m_SuppliesExpanded;
            xuoassist::ItemCounters_SetStatusExpanded(m_SuppliesExpanded);
        }
        else if (serial == ID_GSB_SUPPLIES_COUNTERS)
        {
            xuoassist::AssistantUI_OpenDisplayCounters();
            return;
        }
        else if (serial == ID_GSB_SUPPLIES_RECOUNT)
        {
            if (m_Supplies.Recounting)
            {
                xuoassist::ItemCounters_CancelRecount();
            }
            else
            {
                xuoassist::ItemCounters_StartRecount();
            }
        }
        else if (serial == ID_GSB_SUPPLIES_PREVIOUS)
        {
            if (m_SuppliesPage > 0)
            {
                --m_SuppliesPage;
            }
        }
        else if (serial == ID_GSB_SUPPLIES_NEXT)
        {
            ++m_SuppliesPage;
        }
        else if (serial == ID_GSB_SUPPLIES_HIDE_ZERO)
        {
            xuoassist::ItemCounters_SetStatusHideZero(!m_Supplies.HideZero);
            m_SuppliesPage = 0;
        }
        else if (serial == ID_GSB_SUPPLIES_TAB_ALL)
        {
            xuoassist::ItemCounters_SetStatusCategory(-1);
            m_SuppliesPage = 0;
        }
        else if (serial >= ID_GSB_SUPPLIES_TAB_FIRST && serial <= ID_GSB_SUPPLIES_TAB_LAST)
        {
            const int cat = static_cast<int>(serial - ID_GSB_SUPPLIES_TAB_FIRST);
            if (xuoassist::ItemCounters_IsValidCategory(cat))
            {
                xuoassist::ItemCounters_SetStatusCategory(cat);
                m_SuppliesPage = 0;
            }
        }
        else if (serial >= ID_GSB_SUPPLIES_GROUP_FIRST && serial <= ID_GSB_SUPPLIES_GROUP_LAST)
        {
            const int cat = static_cast<int>(serial - ID_GSB_SUPPLIES_GROUP_FIRST);
            if (xuoassist::ItemCounters_IsValidCategory(cat))
            {
                xuoassist::ItemCounters_ToggleStatusCategoryCollapsed(static_cast<xuoassist::SupplyCategory>(cat));
                m_SuppliesPage = 0;
            }
        }
        WantUpdateContent = true;
        return;
    }
#endif
    if (serial == ID_GSB_MINIMIZE && Serial == g_PlayerSerial)
    {
        Minimized = true;
        WantUpdateContent = true;
    }
    else if (serial == ID_GSB_LOCK_MOVING)
    {
        LockMoving = !LockMoving;
    }
    else if (serial == ID_GSB_BUTTON_HEAL_1)
    {
        g_Game.CastSpell(29);
        g_PartyHelperTimer = g_Ticks + 500;
        g_PartyHelperTarget = Serial;
    }
    else if (serial == ID_GSB_BUTTON_HEAL_2)
    {
        g_Game.CastSpell(11);
        g_PartyHelperTimer = g_Ticks + 500;
        g_PartyHelperTarget = Serial;
    }
    else if (serial == ID_GSB_BUTTON_REMOVE_FROM_GROUP)
    {
        CGumpStatusbar *oldGroup = m_GroupNext;

        if (oldGroup == nullptr)
        {
            oldGroup = m_GroupPrev;
        }

        RemoveFromGroup();

        if (oldGroup != nullptr)
        {
            oldGroup->UpdateGroup(0, 0);
            oldGroup->WantRedraw = true;
        }
    }
    else if (serial == ID_GSB_BUFF_GUMP)
    {
        g_ConfigManager.ToggleBufficonWindow = true;
    }
    else if (serial == ID_GSB_BUFF_LOCKER_STR)
    {
        g_Player->LockStr = (g_Player->LockStr + 1) % 3;
        WantUpdateContent = true;

        CPacketChangeStatLockStateRequest(0, g_Player->LockStr).Send();
    }
    else if (serial == ID_GSB_BUFF_LOCKER_DEX)
    {
        g_Player->LockDex = (g_Player->LockDex + 1) % 3;
        WantUpdateContent = true;

        CPacketChangeStatLockStateRequest(1, g_Player->LockDex).Send();
    }
    else if (serial == ID_GSB_BUFF_LOCKER_INT)
    {
        g_Player->LockInt = (g_Player->LockInt + 1) % 3;
        WantUpdateContent = true;

        CPacketChangeStatLockStateRequest(2, g_Player->LockInt).Send();
    }
}

bool CGumpStatusbar::OnLeftMouseButtonDoubleClick()
{
    if (g_GeneratedMouseDown)
    {
        return false;
    }

#if defined(XUO_ASSISTANT)
    if (Serial == g_PlayerSerial && !Minimized && g_Config.AssistantEnabled)
    {
        const uint32_t pressed = g_PressedObject.LeftSerial;
        if (IsSuppliesInteractiveControl(pressed))
        {
            return true;
        }
        if (pressed >= ID_GSB_SUPPLY_FIRST)
        {
            const size_t index = static_cast<size_t>(pressed - ID_GSB_SUPPLY_FIRST);
            if (index < m_Supplies.Entries.size())
            {
                const auto &entry = m_Supplies.Entries[index];
                xuoassist::ItemCounters_UseCounter(entry.Graphic, entry.Hue);
                return true;
            }
        }
        if (pressed == ID_GSB_SUPPLIES_COVERAGE || pressed == ID_GSB_SUPPLIES_STATUS)
        {
            return true;
        }
    }
#endif
    if ((g_PressedObject.LeftSerial == 0u) && Serial == g_PlayerSerial && Minimized)
    {
        Minimized = false;

        if (InGroup())
        {
            CGumpStatusbar *oldGroup = m_GroupNext;

            if (oldGroup == nullptr)
            {
                oldGroup = m_GroupPrev;
            }

            RemoveFromGroup();

            if (oldGroup != nullptr)
            {
                oldGroup->UpdateGroup(0, 0);
                oldGroup->WantRedraw = true;
            }
        }

        WantUpdateContent = true;

        return true;
    }
    if (Serial != g_PlayerSerial)
    {
        if (g_Player->Warmode)
        {
            g_Game.Attack(Serial);
        }
        else
        {
            g_Game.DoubleClick(Serial);
        }
        return true;
    }
    if (!Minimized)
    {
        g_Game.PaperdollReq(Serial);
        return true;
    }

    return false;
}

void CGumpStatusbar::OnTextInput(const TextEvent &ev)
{
    if (Serial != g_PlayerSerial)
    {
        astr_t str = g_EntryPointer->GetTextA();
        if (g_EntryPointer->Pos() > 0)
        {
            str.resize(g_EntryPointer->Pos());
        }
        else
        {
            str = "";
        }

        const auto ch = EvChar(ev);
        if ((g_EntryPointer->Length() <= 15) && g_FontManager.GetWidthA(1, str) <= 100 &&
            ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')))
        {
            g_EntryPointer->Insert(ch);
            WantRedraw = true;
        }
    }
}

void CGumpStatusbar::OnKeyDown(const KeyEvent &ev)
{
    const auto key = EvKey(ev);
    switch (key)
    {
        case KEY_RETURN:
        case KEY_RETURN2:
        {
            if (g_EntryPointer->Length() != 0u)
            {
                SendRenameRequest();
            }
            else
            {
                CGameObject *obj = g_World->FindWorldObject(Serial);
                if (obj != nullptr)
                {
                    g_EntryPointer->SetTextA(obj->GetName());
                }
            }

            if (g_ConfigManager.GetConsoleNeedEnter())
            {
                g_EntryPointer = nullptr;
            }
            else
            {
                g_EntryPointer = &g_GameConsole;
            }

            WantRedraw = true;
            break;
        }
        case KEY_HOME:
        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_BACK:
        case KEY_DELETE:
        case KEY_END:
        {
            g_EntryPointer->OnKey(this, key);
            WantRedraw = true;
            break;
        }
        case KEY_ESCAPE:
        {
            CGameObject *obj = g_World->FindWorldObject(Serial);
            if (obj != nullptr)
            {
                g_EntryPointer->SetTextA(obj->GetName());
            }

            if (g_ConfigManager.GetConsoleNeedEnter())
            {
                g_EntryPointer = nullptr;
            }
            else
            {
                g_EntryPointer = &g_GameConsole;
            }

            WantRedraw = true;
            break;
        }
        default:
            break;
    }
}

void CGumpStatusbar::SendRenameRequest()
{
    for (CBaseGUI *item = (CBaseGUI *)m_Items; item != nullptr; item = (CBaseGUI *)item->m_Next)
    {
        if (item->Type != GOT_TEXTENTRY)
        {
            continue;
        }

        CEntryText *entry = &((CGUITextEntry *)item)->m_Entry;
        if (entry->Length() != 0u)
        {
            CPacketRenameRequest(Serial, entry->GetTextA()).Send();
        }
    }
}
