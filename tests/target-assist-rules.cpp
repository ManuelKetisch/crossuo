// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Assistant/core/targetassist_rules.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace xuoassist;

namespace {

TargetCandidate MakeMobile(
    uint32_t serial,
    const char *name,
    uint16_t body,
    int distance,
    uint16_t hits,
    uint16_t maxHits,
    uint8_t notoriety = 4)
{
    TargetCandidate c;
    c.Serial = serial;
    c.Name = name;
    c.Body = body;
    c.Distance = distance;
    c.Hits = hits;
    c.MaxHits = maxHits;
    c.Notoriety = notoriety;
    return c;
}

void TestBodyListParsing()
{
    std::vector<uint16_t> bodies;
    assert(TargetRules_ParseBodyList("", bodies) && bodies.empty());
    assert(TargetRules_ParseBodyList("-1", bodies) && bodies.empty());
    assert(TargetRules_ParseBodyList("400, 0x0191, 400, 0x025D", bodies));
    assert(bodies.size() == 3);
    assert(bodies[0] == 400 && bodies[1] == 0x0191 && bodies[2] == 0x025D);
    assert(TargetRules_FormatBodyList(bodies) == "0x0190, 0x0191, 0x025D");

    // Reject invalid values and overflow
    assert(!TargetRules_ParseBodyList("0x10000", bodies));
    assert(!TargetRules_ParseBodyList("not_a_body", bodies));
}

void TestSelectorOrderingModes()
{
    std::vector<TargetCandidate> mobs = {
        MakeMobile(0x10, "Orc", 0x0011, 5, 80, 100),      // 80% HP, dist 5
        MakeMobile(0x20, "Orc", 0x0011, 2, 100, 100),     // 100% HP, dist 2
        MakeMobile(0x30, "Orc", 0x0011, 9, 25, 100),      // 25% HP, dist 9
        MakeMobile(0x40, "Orc", 0x0011, 6, 50, 200),      // 25% HP, dist 6 (ties 0x30 on HP%, closer)
    };

    TargetSelectionContext ctx;
    TargetFilterRule rule;
    rule.MaxDistance = 12;

    // Nearest -> 0x20 (dist 2)
    rule.Selector = TargetSelectorMode::Nearest;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 1);

    // Farthest -> 0x30 (dist 9)
    rule.Selector = TargetSelectorMode::Farthest;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 2);

    // Weakest -> 0x40 (25% HP, closer than 0x30)
    rule.Selector = TargetSelectorMode::Weakest;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 3);

    // Strongest -> 0x20 (100% HP)
    rule.Selector = TargetSelectorMode::Strongest;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 1);

    // Random -> deterministic by RandomSeed over sorted serials
    rule.Selector = TargetSelectorMode::Random;
    ctx.RandomSeed = 0;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 0);
    ctx.RandomSeed = 1;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 1);
    ctx.RandomSeed = 2;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 2);
    ctx.RandomSeed = 3;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 3);

    // Next (cycle) -> advances in serial order and wraps around
    rule.Selector = TargetSelectorMode::Next;
    ctx.LastCycledSerial = 0;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 0);
    ctx.LastCycledSerial = 0x10;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 1);
    ctx.LastCycledSerial = 0x20;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 2);
    ctx.LastCycledSerial = 0x30;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 3);
    ctx.LastCycledSerial = 0x40;
    assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 0); // wraps
}

void TestStatusAndFriendAndIgnoreFilters()
{
    auto m1 = MakeMobile(0x101, "Alpha", 0x0190, 3, 50, 100, 1);
    m1.Friend = true;
    m1.Poisoned = true;

    auto m2 = MakeMobile(0x102, "Bravo", 0x0191, 4, 90, 100, 6);
    m2.Paralyzed = true;
    m2.WarMode = true;

    auto m3 = MakeMobile(0x103, "Charlie", 0x025D, 2, 100, 100, 5);
    m3.Blessed = true;
    m3.Ignored = true;

    std::vector<TargetCandidate> mobs = { m1, m2, m3 };
    TargetSelectionContext ctx;

    // Only poisoned + Only friends -> m1 (even if GlobalSkipFriends is true!)
    {
        TargetFilterRule rule;
        rule.Poisoned = TargetFlagFilter::Only;
        rule.Friends = TargetFriendFilter::OnlyFriends;
        TargetSelectionContext c = ctx;
        c.GlobalSkipFriends = true;
        assert(TargetRules_SelectCandidate(rule, mobs, c) == 0);

        // Safety invariant: harmful cursor with GlobalProtectFriends ALWAYS blocks friends
        c.GlobalProtectFriends = true;
        assert(TargetRules_SelectCandidate(rule, mobs, c) == -1);
    }

    // Paralyzed + WarMode -> m2
    {
        TargetFilterRule rule;
        rule.Paralyzed = TargetFlagFilter::Only;
        rule.WarMode = TargetFlagFilter::Only;
        assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 1);

        rule.WarMode = TargetFlagFilter::Exclude;
        assert(TargetRules_SelectCandidate(rule, mobs, ctx) == -1);
    }

    // Blessed + IgnoreList toggle -> m3
    {
        TargetFilterRule rule;
        rule.Blessed = TargetFlagFilter::Only;
        rule.RespectIgnoreList = true;
        assert(TargetRules_SelectCandidate(rule, mobs, ctx) == -1);

        rule.RespectIgnoreList = false;
        assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 2);
    }

    // Multi-body + notoriety filter -> matches m2 (0x0191, Murderer=6 -> bit 5)
    {
        TargetFilterRule rule;
        rule.Bodies = { 0x0190, 0x0191 };
        rule.Notorieties = (1 << 5); // Murderer
        assert(TargetRules_SelectCandidate(rule, mobs, ctx) == 1);
    }
}

void TestFriendGroupImportExport()
{
    // 1. Round-trip JSON export & import
    std::vector<FriendGroupRecord> groups = {
        {
            "Guild",
            true,
            {
                { 0x00001234, "Alice", true },
                { 0x00005678, "Bob", false },
            },
            {
                { "Order of Britannia", true },
            },
        },
        {
            "PvP",
            false,
            {
                { 0x0000ABCD, "Charlie", true },
            },
            {},
        },
    };

    const std::string json = FriendRules_ExportJson(groups);
    const auto parsedJson = FriendRules_ParseImport(json);
    assert(parsedJson.Ok);
    assert(parsedJson.Groups.size() == 2);
    assert(parsedJson.Groups[0].Name == "Guild");
    assert(parsedJson.Groups[0].IncludeParty == true);
    assert(parsedJson.Groups[0].Players.size() == 2);
    assert(parsedJson.Groups[0].Players[0].Serial == 0x00001234);
    assert(parsedJson.Groups[0].Players[0].Name == "Alice");
    assert(parsedJson.Groups[0].Players[1].Enabled == false);
    assert(parsedJson.Groups[0].Guilds.size() == 1);
    assert(parsedJson.Groups[0].Guilds[0].Name == "Order of Britannia");
    assert(parsedJson.Groups[1].Name == "PvP");
    assert(parsedJson.Groups[1].Players.size() == 1);

    // 2. Round-trip INI export & import
    const std::string ini = FriendRules_ExportIni(groups);
    const auto parsedIni = FriendRules_ParseImport(ini);
    assert(parsedIni.Ok);
    assert(parsedIni.Groups.size() == 2);
    assert(parsedIni.Groups[0].Name == "Guild");
    assert(parsedIni.Groups[0].IncludeParty == true);
    assert(parsedIni.Groups[0].Players.size() == 2);
    assert(parsedIni.Groups[0].Players[1].Serial == 0x00005678);
    assert(parsedIni.Groups[0].Players[1].Enabled == false);
    assert(parsedIni.Groups[0].Guilds.size() == 1);
    assert(parsedIni.Groups[1].Players[0].Serial == 0x0000ABCD);

    // 3. Razor Enhanced FRIENDS_PLAYERS table JSON array
    const std::string rePlayers = R"([
        { "List": "Raid", "Name": "Healer", "Serial": 4660, "Enabled": true },
        { "List": "Raid", "Name": "Tank", "Serial": "0x00002222", "Enabled": false },
        { "List": "Raid", "Name": "BadZero", "Serial": 0, "Enabled": true }
    ])";
    const auto parsedRe = FriendRules_ParseImport(rePlayers);
    assert(parsedRe.Ok);
    assert(parsedRe.Groups.size() == 1);
    assert(parsedRe.Groups[0].Name == "Raid");
    assert(parsedRe.Groups[0].Players.size() == 2);
    assert(parsedRe.Groups[0].Players[0].Serial == 4660);
    assert(parsedRe.Groups[0].Players[1].Serial == 0x00002222);
    assert(parsedRe.Groups[0].Players[1].Enabled == false);
    assert(parsedRe.SkippedEntries == 1);

    // 4. Plain text / CSV exchange format
    const std::string plainText =
        "# Group: Party\n"
        "0x00001111 Diana\n"
        "Eve, 0x00002222, false\n"
        "garbage line\n";
    const auto parsedText = FriendRules_ParseImport(plainText);
    assert(parsedText.Ok);
    assert(parsedText.Groups.size() == 1);
    assert(parsedText.Groups[0].Name == "Party");
    assert(parsedText.Groups[0].Players.size() == 2);
    assert(parsedText.Groups[0].Players[0].Name == "Diana");
    assert(parsedText.Groups[0].Players[1].Name == "Eve");
    assert(parsedText.Groups[0].Players[1].Enabled == false);
    assert(parsedText.SkippedEntries == 1);

    // 5. Malformed inputs safely rejected without partial corruption
    assert(!FriendRules_ParseImport("").Ok);
    assert(!FriendRules_ParseImport("{ broken json").Ok);
    assert(!FriendRules_ParseImport("{\"unrelated\": 1}").Ok);
    assert(!FriendRules_ParseImport("[Meta]\nmalformed line without equals").Ok);
    assert(!FriendRules_ParseImport(std::string(FriendGroupMaxImportBytes + 1, 'a')).Ok);
}

} // namespace

int main()
{
    TestBodyListParsing();
    TestSelectorOrderingModes();
    TestStatusAndFriendAndIgnoreFilters();
    TestFriendGroupImportExport();
    std::cout << "target-assist-rules: all tests passed.\n";
    return 0;
}
