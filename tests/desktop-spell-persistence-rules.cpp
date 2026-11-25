// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <common/fs.h>
#include <common/str.h>
#include <xuocore/text_parser.h>

#include "Config.h"
#include "Gumps/DesktopSpellPersistence_rules.h"
#include "Spells.h"
#include "Utility/TextFileWriter.h"

// Minimal g_Config definition so Spells.cpp can be compiled directly in this test.
Config g_Config;

namespace {

int g_Failures = 0;

#define EXPECT_TRUE(cond)                                                                      \
    do                                                                                         \
    {                                                                                          \
        if (!(cond))                                                                           \
        {                                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++g_Failures;                                                                      \
        }                                                                                      \
    } while (0)

void TestAllSpellbooksHaveGloballyUniqueSpellIds()
{
    g_Config.ClientVersion = CV_70796;
    InitSpells();

    std::set<uint16_t> seenIds;
    int totalSpells = 0;
    for (int bookType = ST_FIRST; bookType < ST_COUNT; ++bookType)
    {
        const SpellBook &book = GetSpellbook(bookType);
        EXPECT_TRUE(book.SpellCount > 0);
        for (int i = 0; i < book.SpellCount; ++i)
        {
            const Spell *spell = book.Spells[i];
            EXPECT_TRUE(spell != nullptr);
            if (spell == nullptr)
            {
                continue;
            }
            EXPECT_TRUE(spell->Id > 0);
            EXPECT_TRUE(seenIds.insert(spell->Id).second);
            const Spell *found = FindSpellByGraphicAndType(spell->SpellIconGraphic, spell->Type);
            EXPECT_TRUE(found == spell);
            ++totalSpells;
        }
    }
    EXPECT_TRUE(totalSpells >= 180);

    // Explicitly verify previously colliding book-index-0 / book-index-1 spells across books.
    const Spell *mageryClumsy = FindSpellByGraphicAndType(0x08C0, ST_MAGERY);
    const Spell *chivalryCleanse = FindSpellByGraphicAndType(0x5100, ST_CHIVALRY);
    const Spell *necroAnimate = FindSpellByGraphicAndType(0x5000, ST_NECROMANCY);
    const Spell *bushidoHonor = FindSpellByGraphicAndType(0x5420, ST_BUSHIDO);
    EXPECT_TRUE(mageryClumsy != nullptr && chivalryCleanse != nullptr &&
        necroAnimate != nullptr && bushidoHonor != nullptr);
    EXPECT_TRUE(mageryClumsy->Id != chivalryCleanse->Id);
    EXPECT_TRUE(mageryClumsy->Id != necroAnimate->Id);
    EXPECT_TRUE(chivalryCleanse->Id != bushidoHonor->Id);

    // Invalid / corrupt spellbook types and graphics return nullptr cleanly (no crash/assert).
    EXPECT_TRUE(FindSpellByGraphicAndType(0x08C0, ST_INVALID) == nullptr);
    EXPECT_TRUE(FindSpellByGraphicAndType(0x08C0, ST_COUNT) == nullptr);
    EXPECT_TRUE(FindSpellByGraphicAndType(0x08C0, static_cast<SPELLBOOK_TYPE>(99)) == nullptr);
    EXPECT_TRUE(FindSpellByGraphicAndType(0x0000, ST_MAGERY) == nullptr);
    EXPECT_TRUE(FindSpellByGraphicAndType(0x5100, ST_MAGERY) == nullptr);
}

void TestDesktopSaveDebouncerTimingAndWraparound()
{
    DesktopSaveDebouncer db;
    EXPECT_TRUE(!db.ShouldSave(1000, true, true));

    // Initial login marks saved: periodic save fires after 60s, not before.
    db.MarkSaved(1000);
    EXPECT_TRUE(!db.ShouldSave(60999, true, true));
    EXPECT_TRUE(db.ShouldSave(61000, true, true));
    EXPECT_TRUE(!db.ShouldSave(61000, false, true));
    EXPECT_TRUE(!db.ShouldSave(61000, true, false));

    // Dragging a spell icon marks dirty and triggers save 1s later.
    db.MarkDirty(5000);
    EXPECT_TRUE(!db.ShouldSave(5999, true, true));
    EXPECT_TRUE(db.ShouldSave(6000, true, true));

    // Rapid drags coalesce up to MaxDirtyDelayMs (5s) from the first dirty event.
    db.MarkSaved(6000);
    db.MarkDirty(10000);
    db.MarkDirty(10700);
    EXPECT_TRUE(!db.ShouldSave(11500, true, true));
    EXPECT_TRUE(db.ShouldSave(11700, true, true));

    // Continuous edits every 500ms still force a flush at firstDirty + 5000ms.
    for (uint32_t t = 11200; t <= 14700; t += 500)
    {
        db.MarkDirty(t);
    }
    EXPECT_TRUE(!db.ShouldSave(14999, true, true));
    EXPECT_TRUE(db.ShouldSave(15000, true, true));

    // Wrap-safe uint32_t tick rollover.
    db.Reset();
    const uint32_t nearWrap = std::numeric_limits<uint32_t>::max() - 300u;
    db.MarkDirty(nearWrap);
    EXPECT_TRUE(!db.ShouldSave(nearWrap + 500u, true, true));
    EXPECT_TRUE(db.ShouldSave(nearWrap + 1000u, true, true));

    // Reset clears dirty and periodic state on logout/ClearWorld.
    db.Reset();
    EXPECT_TRUE(!db.ShouldSave(nearWrap + 100000u, true, true));
}

void TestPersistableGumpFilterAndGroupCoordNormalization()
{
    EXPECT_TRUE(ShouldTriggerDesktopSaveForGump(GT_SPELL));
    EXPECT_TRUE(ShouldTriggerDesktopSaveForGump(GT_SKILL));
    EXPECT_TRUE(ShouldTriggerDesktopSaveForGump(GT_ABILITY));
    EXPECT_TRUE(ShouldTriggerDesktopSaveForGump(GT_RACIAL_ABILITY));
    EXPECT_TRUE(ShouldTriggerDesktopSaveForGump(GT_CONTAINER));
    EXPECT_TRUE(ShouldTriggerDesktopSaveForGump(GT_SPELLBOOK));
    EXPECT_TRUE(!ShouldTriggerDesktopSaveForGump(GT_GENERIC));
    EXPECT_TRUE(!ShouldTriggerDesktopSaveForGump(GT_POPUP_MENU));
    EXPECT_TRUE(!ShouldTriggerDesktopSaveForGump(GT_DRAG));

    // Negative group origin shifts the whole group together, preserving 44px spacing.
    std::vector<DesktopCoordPoint> group = { { -10, -4 }, { 34, -4 }, { 78, 40 } };
    NormalizeDesktopGroupCoords(group);
    EXPECT_TRUE(group[0].X == 0 && group[0].Y == 0);
    EXPECT_TRUE(group[1].X == 44 && group[1].Y == 0);
    EXPECT_TRUE(group[2].X == 88 && group[2].Y == 44);

    // Non-negative group stays untouched.
    std::vector<DesktopCoordPoint> visible = { { 120, 240 }, { 164, 240 } };
    NormalizeDesktopGroupCoords(visible);
    EXPECT_TRUE(visible[0].X == 120 && visible[0].Y == 240);
    EXPECT_TRUE(visible[1].X == 164 && visible[1].Y == 240);
}

void TestDesktopTextRoundTripForUngroupedAndGroupedSpells()
{
    const fs_path path = fs_path_from("/tmp/test_desktop_spell_roundtrip.txt");
    {
        TextFileWriter writer(path);
        EXPECT_TRUE(writer.Opened());
        writer.WriteRaw("[gumps]");
        // Ungrouped Magery Heal (legacy serial 259 -> normalizes to Id 4)
        writer.WriteRaw("Gump=16,320,410,0,0,0,1,259,2243,0");
        // Ungrouped Skill icon (skill serial 46 = Meditation)
        writer.WriteRaw("Gump=39,500,600,0,0,0,0,46");
        // Grouped cross-book spells (Magery Clumsy + Chivalry Cleanse by Fire,
        // both with legacy serial 256 -> must normalize to 1 and 201 without colliding!)
        writer.WriteRaw("SpellGroup=3");
        writer.WriteRaw("Spell=16,-4,500,0,0,0,0,256,2240,0");
        writer.WriteRaw("Spell=16,40,500,0,0,0,1,256,20736,2");
        // Corrupt entry inside group should be skipped cleanly.
        writer.WriteRaw("Spell=16,84,500,0,0,0,0,999,65000,99");
    }

    TextFileParser file(path, " =", "#;", "\"");
    int ungroupedSpells = 0;
    int ungroupedSkillSerial = -1;
    int spellGroupMembers = 0;
    std::set<uint32_t> loadedSpellIds;
    std::vector<DesktopCoordPoint> groupPts;

    while (!file.IsEOF())
    {
        auto v = file.ReadTokens();
        if (v.empty())
        {
            continue;
        }
        const astr_t &key = v[0];
        if (key == "Gump" && v.size() >= 2)
        {
            const auto fields = TextFields(v);
            const auto type = static_cast<GUMP_TYPE>(str_to_int(fields[0]));
            if (type == GT_SPELL)
            {
                EXPECT_TRUE(fields.size() == 10);
                const uint16_t graphic = static_cast<uint16_t>(str_to_int(fields[8]));
                const SPELLBOOK_TYPE st = static_cast<SPELLBOOK_TYPE>(str_to_int(fields[9]));
                const Spell *info = FindSpellByGraphicAndType(graphic, st);
                EXPECT_TRUE(info != nullptr && info->Id == 4);
                loadedSpellIds.insert(info->Id);
                ++ungroupedSpells;
            }
            else if (type == GT_SKILL)
            {
                EXPECT_TRUE(fields.size() == 8);
                ungroupedSkillSerial = static_cast<int>(TextToUInt(fields[7]));
            }
        }
        else if (key == "SpellGroup" && v.size() >= 2)
        {
            spellGroupMembers = str_to_int(v[1]);
        }
        else if (key == "Spell" && spellGroupMembers > 0 && v.size() >= 2)
        {
            spellGroupMembers--;
            const auto fields = TextFields(v);
            EXPECT_TRUE(fields.size() == 10);
            const int sx = str_to_int(fields[1]);
            const int sy = str_to_int(fields[2]);
            const uint16_t graphic = static_cast<uint16_t>(str_to_int(fields[8]));
            const SPELLBOOK_TYPE st = static_cast<SPELLBOOK_TYPE>(str_to_int(fields[9]));
            const Spell *info = FindSpellByGraphicAndType(graphic, st);
            if (info != nullptr && loadedSpellIds.insert(info->Id).second)
            {
                groupPts.push_back({ sx, sy });
            }
        }
    }

    NormalizeDesktopGroupCoords(groupPts);
    EXPECT_TRUE(ungroupedSpells == 1);
    EXPECT_TRUE(ungroupedSkillSerial == 46);
    EXPECT_TRUE(loadedSpellIds.size() == 3);
    EXPECT_TRUE(loadedSpellIds.count(4) == 1);
    EXPECT_TRUE(loadedSpellIds.count(1) == 1);
    EXPECT_TRUE(loadedSpellIds.count(201) == 1);
    EXPECT_TRUE(groupPts.size() == 2);
    EXPECT_TRUE(groupPts[0].X == 0 && groupPts[0].Y == 500);
    EXPECT_TRUE(groupPts[1].X == 44 && groupPts[1].Y == 500);

    fs_del(path);
}

} // namespace

int main()
{
    TestAllSpellbooksHaveGloballyUniqueSpellIds();
    TestDesktopSaveDebouncerTimingAndWraparound();
    TestPersistableGumpFilterAndGroupCoordNormalization();
    TestDesktopTextRoundTripForUngroupedAndGroupedSpells();
    if (g_Failures != 0)
    {
        std::fprintf(stderr, "desktop-spell-persistence-rules: %d failure(s)\n", g_Failures);
        return 1;
    }
    std::printf("desktop-spell-persistence-rules: all tests passed\n");
    return 0;
}
