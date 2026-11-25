// SPDX-License-Identifier: AGPL-3.0-or-later

#include "src/Assistant/core/inventoryagent_plan.h"
#include <cassert>

using namespace xuoassist;

namespace {

InventoryAgentCorpseCandidate MakeCorpse(uint32_t serial, int distance, uint32_t body = 0x0027)
{
    InventoryAgentCorpseCandidate c;
    c.Serial = serial;
    c.Graphic = InventoryAgent_CorpseGraphic;
    c.Body = body;
    c.Container = 0xFFFFFFFF;
    c.Distance = distance;
    c.IsNpc = false;
    c.HasStaticData = false;
    c.IsContainer = false;
    c.SameMap = true;
    c.Opened = false;
    c.HasOpenGump = false;
    c.Ignored = false;
    c.Dragged = false;
    c.MultiBody = false;
    return c;
}

void TestCorePlanningAndProperties()
{
    const uint16_t bandage = 0x0E21;
    const std::vector<InventoryAgentStack> source{
        { 1, bandage, 5, 20 }, { 2, bandage, 0, 50 }, { 3, 0x0F7A, 0, 120 },
    };
    const std::vector<InventoryAgentStack> destination{
        { 4, bandage, 5, 20 }, { 5, bandage, 0, 30 }, { 6, bandage, 0, 15 },
    };

    // Exact hue: only the 45 already at the destination count toward 60.
    auto next = InventoryAgent_Plan(
        InventoryAgentMode::Restock, { { bandage, 0, true, 60 } }, source, destination);
    assert(next.Serial == 2 && next.Count == 15);

    // Wildcard hue sees all 65 of the item already at the destination.
    next = InventoryAgent_Plan(
        InventoryAgentMode::Restock, { { bandage, 0, false, 60 } }, source, destination);
    assert(next.Serial == 0);

    // When the source cannot supply the full deficit it sends only what's there.
    next = InventoryAgent_Plan(
        InventoryAgentMode::Restock, { { bandage, 5, true, 100 } }, source, destination);
    assert(next.Serial == 1 && next.Count == 20);

    // Organizer moves the matching stack even though it exceeds the restock count.
    next = InventoryAgent_Plan(
        InventoryAgentMode::Organizer, { { bandage, 0, true, 10 } }, source, destination);
    assert(next.Serial == 2 && next.Count == 50);

    // A satisfied first rule does not prevent a different rule from making progress.
    next = InventoryAgent_Plan(InventoryAgentMode::Restock,
        { { bandage, 0, false, 60 }, { 0x0F7A, 0, true, 40 } }, source, destination);
    assert(next.Serial == 3 && next.Count == 40);

    next = InventoryAgent_Plan(
        InventoryAgentMode::Restock, { { bandage, 0, true, 0 } }, source, {});
    assert(next.Serial == 0);

    // AutoLoot uses the full matching stack and obeys configured rule priority.
    next = InventoryAgent_Plan(InventoryAgentMode::AutoLoot,
        { { 0x0F7A, 0, true, 1 }, { bandage, 5, true, 1 } }, source, destination);
    assert(next.Serial == 3 && next.Count == 120);

    next = InventoryAgent_Plan(InventoryAgentMode::AutoLoot,
        { { bandage, 0, true, 1 } }, { { 42, bandage, 0, 65535 } }, {});
    assert(next.Serial == 42 && next.Count == InventoryAgent_MaxQuantity);

    next = InventoryAgent_Plan(InventoryAgentMode::AutoLoot, { { bandage, 9, true, 1 } }, source, {});
    assert(next.Serial == 0);

    next = InventoryAgent_Plan(InventoryAgentMode::AutoLoot, { { bandage, 9, false, 1 } }, source, {});
    assert(next.Serial == 1 && next.Count == 20);

    constexpr uint32_t luck = 1060436;
    constexpr uint32_t damage = 1060401;
    const InventoryAgentPropertyRule luckRange{ luck, 80, 120, true, true };
    const InventoryAgentPropertyRule damageFloor{ damage, 20, 0, true, false };
    const std::vector<InventoryAgentPropertyRule> ranges{ luckRange, damageFloor };
    assert(InventoryAgent_MatchesProperties(ranges, { { luck, L"80" }, { damage, L"20" } }));
    assert(InventoryAgent_MatchesProperties(ranges, { { luck, L"120" }, { damage, L"900" } }));
    assert(!InventoryAgent_MatchesProperties(ranges, { { luck, L"79" }, { damage, L"20" } }));
    assert(!InventoryAgent_MatchesProperties(ranges, { { luck, L"121" }, { damage, L"20" } }));
    assert(!InventoryAgent_MatchesProperties(ranges, { { luck, L"100" } }));
    assert(!InventoryAgent_MatchesProperties(ranges, { { luck, L"100" }, { damage, L"20\t20" } }));
    assert(!InventoryAgent_MatchesProperties(ranges, { { luck, L"100" }, { luck, L"100" }, { damage, L"20" } }));
    assert(!InventoryAgent_MatchesProperties(ranges, { { luck, L"100%" }, { damage, L"20" } }));

    int number = 0;
    assert(InventoryAgent_ParsePropertyInteger(std::wstring(L"-2147483648"), number) && number == INT32_MIN);
    assert(InventoryAgent_ParsePropertyInteger(std::string("+2147483647"), number) && number == INT32_MAX);
    assert(!InventoryAgent_ParsePropertyInteger(std::string("-2147483649"), number));
    assert(!InventoryAgent_ParsePropertyInteger(std::string("2147483648"), number));
    assert(!InventoryAgent_ParsePropertyInteger(std::string(""), number));
    assert(!InventoryAgent_ParsePropertyInteger(std::string("+"), number));
    assert(!InventoryAgent_ValidPropertyRule({ luck, 1, 0, true, true }));
    assert(!InventoryAgent_ValidPropertyRule({ luck, 0, 0, false, false }));

    const InventoryAgentRule propertyRule{ bandage, 0, true, 1, ranges };
    next = InventoryAgent_Plan(InventoryAgentMode::AutoLoot, { propertyRule }, source, {});
    assert(next.Serial == 0); // Unresolved properties leave the item in place.
    auto verifiedSource = source;
    InventoryAgentPropertyMatches matched;
    matched.set(1);
    verifiedSource[1].PropertyMatches = &matched;
    next = InventoryAgent_Plan(InventoryAgentMode::AutoLoot,
        { { bandage, 9, true, 1 }, propertyRule }, verifiedSource, {});
    assert(next.Serial == 2 && next.Count == 50);
    next = InventoryAgent_Plan(InventoryAgentMode::AutoLoot,
        { propertyRule, { 0x0F7A, 0, true, 1 } }, verifiedSource, {});
    assert(next.Serial == 3); // A separate ordinary rule still works.
}

void TestPerRuleDestinationOverrides()
{
    constexpr uint32_t backpack = 0x40000001;
    constexpr uint32_t profileBag = 0x40000010;
    constexpr uint32_t goldBag = 0x40000100;
    constexpr uint32_t gemBag = 0x40000200;

    // Resolution priority: per-rule override > profile destination > backpack.
    assert(InventoryAgent_ResolveDestination(goldBag, profileBag, backpack) == goldBag);
    assert(InventoryAgent_ResolveDestination(0, profileBag, backpack) == profileBag);
    assert(InventoryAgent_ResolveDestination(0, 0, backpack) == backpack);
    assert(InventoryAgent_ResolveDestination(0xFFFFFFFFu, profileBag, backpack) == profileBag);
    assert(InventoryAgent_ResolveDestination(0xFFFFFFFFu, 0xFFFFFFFFu, backpack) == backpack);

    // Planning carries the matched rule's Destination override.
    const std::vector<InventoryAgentStack> corpseLoot{
        { 101, 0x0EED, 0, 450 }, // Gold
        { 102, 0x0F16, 0, 3 },   // Amethyst
        { 103, 0x0F7A, 0, 12 },  // Black Pearl
    };

    const InventoryAgentRule goldRule{ 0x0EED, 0, true, 1, {}, goldBag };
    const InventoryAgentRule gemRule{ 0x0F16, 0, false, 1, {}, gemBag };
    const InventoryAgentRule reagentRule{ 0x0F7A, 0, true, 1, {}, 0 }; // Inherits profile default.

    auto move = InventoryAgent_Plan(InventoryAgentMode::AutoLoot, { goldRule, gemRule, reagentRule }, corpseLoot, {});
    assert(move.Serial == 101 && move.Count == 450 && move.Destination == goldBag);
    assert(InventoryAgent_ResolveDestination(move.Destination, profileBag, backpack) == goldBag);

    move = InventoryAgent_Plan(InventoryAgentMode::AutoLoot, { gemRule, reagentRule }, corpseLoot, {});
    assert(move.Serial == 102 && move.Count == 3 && move.Destination == gemBag);
    assert(InventoryAgent_ResolveDestination(move.Destination, profileBag, backpack) == gemBag);

    move = InventoryAgent_Plan(InventoryAgentMode::AutoLoot, { reagentRule }, corpseLoot, {});
    assert(move.Serial == 103 && move.Count == 12 && move.Destination == 0);
    assert(InventoryAgent_ResolveDestination(move.Destination, profileBag, backpack) == profileBag);
    assert(InventoryAgent_ResolveDestination(move.Destination, 0, backpack) == backpack);

    // Property-filtered rule with per-rule override only triggers when verified.
    constexpr uint32_t luck = 1060436;
    const InventoryAgentRule luckGemRule{ 0x0F16, 0, true, 1, { { luck, 90, 0, true, false } }, gemBag };
    auto checkedLoot = corpseLoot;
    move = InventoryAgent_Plan(InventoryAgentMode::AutoLoot, { luckGemRule, reagentRule }, checkedLoot, {});
    assert(move.Serial == 103 && move.Destination == 0);

    InventoryAgentPropertyMatches propMatch;
    propMatch.set(0);
    checkedLoot[1].PropertyMatches = &propMatch;
    move = InventoryAgent_Plan(InventoryAgentMode::AutoLoot, { luckGemRule, reagentRule }, checkedLoot, {});
    assert(move.Serial == 102 && move.Destination == gemBag);
}

void TestImportExportRoundTripAndRejection()
{
    InventoryAgentProfile original;
    original.Name = "Boss \"Loot\" \\ Run";
    original.Mode = InventoryAgentMode::AutoLoot;
    original.IncludeSubcontainers = true;
    original.AutoOpenCorpses = true;
    original.Rules = {
        { 0x0EED, 0x0000, true, 1, {}, 0x40012345 },
        { 0x0F51, 0x0482, false, 1,
          { { 1060436, 80, 120, true, true }, { 1060401, 25, 0, true, false }, { 1060413, 0, 2, false, true } },
          0 },
        { 0x0F7A, 0x0005, true, 1, { { 1060433, -5, 10, true, true } }, 0x40009999 },
    };

    // 1. Native INI export -> import round-trip.
    const astr_t iniText = InventoryAgent_ExportAutoLootIni(original);
    assert(!iniText.empty());
    const auto iniParsed = InventoryAgent_ParseAutoLootImport(iniText);
    assert(iniParsed.Success && iniParsed.Lists.size() == 1);
    const auto &iniList = iniParsed.Lists[0];
    assert(iniList.Name == original.Name);
    assert(iniList.HasIncludeSubcontainers && iniList.IncludeSubcontainers == true);
    assert(iniList.HasAutoOpenCorpses && iniList.AutoOpenCorpses == true);
    assert(iniList.Rules.size() == 3);
    assert(iniList.Rules[0].Graphic == 0x0EED && iniList.Rules[0].Hue == 0 && iniList.Rules[0].MatchHue == true &&
           iniList.Rules[0].Destination == 0x40012345 && iniList.Rules[0].Properties.empty());
    assert(iniList.Rules[1].Graphic == 0x0F51 && iniList.Rules[1].Hue == 0x0482 && iniList.Rules[1].MatchHue == false &&
           iniList.Rules[1].Destination == 0 && iniList.Rules[1].Properties.size() == 3);
    assert(iniList.Rules[1].Properties[0].Cliloc == 1060436 && iniList.Rules[1].Properties[0].HasMinimum &&
           iniList.Rules[1].Properties[0].Minimum == 80 && iniList.Rules[1].Properties[0].HasMaximum &&
           iniList.Rules[1].Properties[0].Maximum == 120);
    assert(iniList.Rules[1].Properties[1].Cliloc == 1060401 && iniList.Rules[1].Properties[1].HasMinimum &&
           iniList.Rules[1].Properties[1].Minimum == 25 && !iniList.Rules[1].Properties[1].HasMaximum);
    assert(iniList.Rules[1].Properties[2].Cliloc == 1060413 && !iniList.Rules[1].Properties[2].HasMinimum &&
           iniList.Rules[1].Properties[2].HasMaximum && iniList.Rules[1].Properties[2].Maximum == 2);
    assert(iniList.Rules[2].Properties[0].Minimum == -5 && iniList.Rules[2].Properties[0].Maximum == 10 &&
           iniList.Rules[2].Destination == 0x40009999);

    // 2. Native JSON export -> import round-trip.
    const astr_t jsonText = InventoryAgent_ExportAutoLootJson(original);
    assert(!jsonText.empty());
    const auto jsonParsed = InventoryAgent_ParseAutoLootImport(jsonText);
    assert(jsonParsed.Success && jsonParsed.Lists.size() == 1);
    const auto &jsonList = jsonParsed.Lists[0];
    assert(jsonList.Name == original.Name);
    assert(jsonList.HasIncludeSubcontainers && jsonList.IncludeSubcontainers == true);
    assert(jsonList.HasAutoOpenCorpses && jsonList.AutoOpenCorpses == true);
    assert(jsonList.Rules.size() == 3);
    assert(jsonList.Rules[0].Graphic == 0x0EED && jsonList.Rules[0].Destination == 0x40012345);
    assert(jsonList.Rules[1].Graphic == 0x0F51 && !jsonList.Rules[1].MatchHue && jsonList.Rules[1].Properties.size() == 3);
    assert(jsonList.Rules[2].Graphic == 0x0F7A && jsonList.Rules[2].Hue == 5 && jsonList.Rules[2].Destination == 0x40009999);

    // Non-AutoLoot profiles do not export as AutoLoot lists.
    InventoryAgentProfile organizer;
    organizer.Name = "Organizer";
    organizer.Mode = InventoryAgentMode::Organizer;
    assert(InventoryAgent_ExportAutoLootIni(organizer).empty());
    assert(InventoryAgent_ExportAutoLootJson(organizer).empty());

    // 3. Razor Enhanced AUTOLOOT_ITEMS JSON array import (multiple lists, BOM, disabled entries, properties, overrides).
    const astr_t razorEnhanced = "\xEF\xBB\xBF" R"JSON(
    [
      {
        "List": "Dungeon",
        "Name": "Gold Coins",
        "Graphics": 3821,
        "Color": -1,
        "Selected": true,
        "Properties": [],
        "LootBagOverride": 1073816133
      },
      {
        "List": "Dungeon",
        "Name": "Disabled Junk",
        "Graphics": 3921,
        "Color": 0,
        "Selected": false,
        "Properties": [],
        "LootBagOverride": 0
      },
      {
        "List": "Dungeon",
        "Name": "Luck Dagger",
        "Graphics": "0x0F51",
        "Color": 1153,
        "Selected": true,
        "Properties": [
          { "Name": "Luck", "Minimum": 90, "Maximum": 120 },
          { "Name": "LMC", "Minimum": 5 }
        ],
        "LootBagOverride": -1
      },
      {
        "List": "BossLoot",
        "Name": "Black Pearl",
        "ItemID": 3962,
        "Hue": 65535,
        "Selected": true,
        "Properties": [
          { "Cliloc": 1060401, "Minimum": 30 }
        ],
        "LootBagOverride": "0x4000ABCD"
      }
    ]
    )JSON";

    const auto reParsed = InventoryAgent_ParseAutoLootImport(razorEnhanced);
    assert(reParsed.Success && reParsed.Lists.size() == 2 && reParsed.SkippedDisabled == 1);
    assert(reParsed.Lists[0].Name == "Dungeon" && reParsed.Lists[0].Rules.size() == 2);
    assert(reParsed.Lists[0].Rules[0].Graphic == 3821 && !reParsed.Lists[0].Rules[0].MatchHue &&
           reParsed.Lists[0].Rules[0].Hue == 0 && reParsed.Lists[0].Rules[0].Destination == 1073816133u);
    assert(reParsed.Lists[0].Rules[1].Graphic == 0x0F51 && reParsed.Lists[0].Rules[1].MatchHue &&
           reParsed.Lists[0].Rules[1].Hue == 1153 && reParsed.Lists[0].Rules[1].Destination == 0 &&
           reParsed.Lists[0].Rules[1].Properties.size() == 2);
    assert(reParsed.Lists[0].Rules[1].Properties[0].Cliloc == 1060436 &&
           reParsed.Lists[0].Rules[1].Properties[0].Minimum == 90 &&
           reParsed.Lists[0].Rules[1].Properties[0].Maximum == 120);
    assert(reParsed.Lists[0].Rules[1].Properties[1].Cliloc == 1060433 &&
           reParsed.Lists[0].Rules[1].Properties[1].HasMinimum &&
           reParsed.Lists[0].Rules[1].Properties[1].Minimum == 5 &&
           !reParsed.Lists[0].Rules[1].Properties[1].HasMaximum);
    assert(reParsed.Lists[1].Name == "BossLoot" && reParsed.Lists[1].Rules.size() == 1);
    assert(reParsed.Lists[1].Rules[0].Graphic == 3962 && !reParsed.Lists[1].Rules[0].MatchHue &&
           reParsed.Lists[1].Rules[0].Destination == 0x4000ABCDu &&
           reParsed.Lists[1].Rules[0].Properties.size() == 1 &&
           reParsed.Lists[1].Rules[0].Properties[0].Cliloc == 1060401);

    // 4. Razor Enhanced single-list JSON object.
    const astr_t reObj = R"JSON({
      "Name": "Reagents",
      "Items": [
        { "Graphics": 3962, "Color": 0, "Selected": true }
      ]
    })JSON";
    const auto reObjParsed = InventoryAgent_ParseAutoLootImport(reObj);
    assert(reObjParsed.Success && reObjParsed.Lists.size() == 1 &&
           reObjParsed.Lists[0].Name == "Reagents" && reObjParsed.Lists[0].Rules.size() == 1 &&
           reObjParsed.Lists[0].Rules[0].MatchHue && reObjParsed.Lists[0].Rules[0].Hue == 0);

    // 5. Malformed input rejection (must fail cleanly with empty Lists and an error message).
    const std::vector<astr_t> malformedInputs = {
        "",
        "   \r\n\t ",
        astr_t(InventoryAgent_MaxExchangeBytes + 1, ' '),
        astr_t("{\"format\":\"crossuo-autoloot\"}\0trailing", 38),
        "not ini or json",
        "{",
        "[]",
        "[{\"Graphics\": 3821, \"Selected\": false}]",
        "[{\"Graphics\": 3821, \"Color\": -1},]", // trailing comma
        "{\"format\":\"crossuo-autoloot\",\"format\":\"crossuo-autoloot\"}", // duplicate key
        "{\"format\":\"wrong\",\"version\":1,\"name\":\"A\",\"rules\":[]}",
        "{\"format\":\"crossuo-autoloot\",\"version\":2,\"name\":\"A\",\"rules\":[]}",
        "{\"format\":\"crossuo-autoloot\",\"version\":1,\"name\":\"\",\"rules\":[]}",
        "{\"format\":\"crossuo-autoloot\",\"version\":1,\"name\":\"A\",\"rules\":[{\"graphic\":0}]}",
        "{\"format\":\"crossuo-autoloot\",\"version\":1,\"name\":\"A\",\"rules\":[{\"graphic\":70000}]}",
        "{\"format\":\"crossuo-autoloot\",\"version\":1,\"name\":\"A\",\"rules\":[{\"graphic\":3821,\"hue\":-1}]}",
        "{\"format\":\"crossuo-autoloot\",\"version\":1,\"name\":\"A\",\"rules\":[{\"graphic\":3821,\"destination\":\"0xFFFFFFFF\"}]}",
        "{\"format\":\"crossuo-autoloot\",\"version\":1,\"name\":\"A\",\"rules\":[{\"graphic\":3821,\"properties\":[{\"cliloc\":1060436,\"minimum\":50,\"maximum\":10}]}]}",
        "{\"format\":\"crossuo-autoloot\",\"version\":1,\"name\":\"A\",\"rules\":[{\"graphic\":3821,\"properties\":[{\"cliloc\":1060436}]}]}",
        // Unknown property name must NEVER silently strip a filter into an unfiltered rule.
        "[{\"Graphics\": 3821, \"Color\": -1, \"Properties\": [{\"Name\": \"UnknownMadeUpStat\", \"Minimum\": 10}]}]",
        // Invalid Razor Enhanced color / loot bag override / trailing tokens.
        "[{\"Graphics\": 3821, \"Color\": -2}]",
        "[{\"Graphics\": 3821, \"LootBagOverride\": 4294967295}]",
        "[{\"Graphics\": 3821}] garbage",
        // Leading zero in JSON number.
        "[{\"Graphics\": 03821}]",
        // Deeply nested JSON.
        astr_t(32, '[') + astr_t(32, ']'),
        // Malformed INI cases.
        "[WrongHeader]\nformat=crossuo-autoloot\n",
        "key_outside_section=1\n",
        "[AutoLootList]\nformat=crossuo-autoloot\nversion=1\nname=Test\nrules=1\n", // missing [Rule0]
        "[AutoLootList]\nformat=crossuo-autoloot\nversion=1\nname=Test\nrules=1\n[Rule0]\ngraphic=0x0EED\nhue=0\nmatchhue=true\n[Rule0Property0]\ncliloc=1060436\nminimum=10\n", // stray property section!
        "[AutoLootList]\nformat=crossuo-autoloot\nversion=1\nname=Test\nrules=1\n[Rule0]\ngraphic=0x0EED\nhue=0\nmatchhue=maybe\n",
        "[AutoLootList]\nformat=crossuo-autoloot\nversion=1\nname=Test\nrules=1\n[Rule0]\ngraphic=0x0EED\nhue=0\nmatchhue=true\ndestination=0xFFFFFFFF\n",
        "[AutoLootList]\nformat=crossuo-autoloot\nversion=1\nname=Test\nrules=1\n[Rule0]\ngraphic=0x0EED\nhue=0\nmatchhue=true\nproperties=1\n[Rule0Property0]\ncliloc=1060436\nminimum=50\nmaximum=10\n",
    };

    for (const auto &bad : malformedInputs)
    {
        const auto parsed = InventoryAgent_ParseAutoLootImport(bad);
        assert(!parsed.Success && parsed.Lists.empty() && !parsed.Error.empty());
    }

    // Rule count limit (> 128) rejected cleanly in JSON.
    astr_t tooMany = "[";
    for (int i = 0; i <= InventoryAgent_MaxRules; ++i)
    {
        if (i > 0)
        {
            tooMany += ",";
        }
        tooMany += "{\"List\":\"Over\",\"Graphics\":3821,\"Color\":-1}";
    }
    tooMany += "]";
    const auto tooManyParsed = InventoryAgent_ParseAutoLootImport(tooMany);
    assert(!tooManyParsed.Success && tooManyParsed.Lists.empty());
}

void TestCorpseFilteringAndTracking()
{
    // Living and dead player race IDs, ghosts, and staff bodies occur in the corpse body field.
    for (uint32_t body : { 0u, 0x0190u, 0x0191u, 0x0192u, 0x0193u, 0x025Du, 0x025Eu, 0x025Fu, 0x0260u,
                           0x029Au, 0x029Bu, 0x02B6u, 0x02B7u, 0x02E8u, 0x02E9u,
                           0x03DBu, 0x03DFu, 0x03E2u, 0x04E5u, 0x10000u })
    {
        assert(!InventoryAgent_AutoLootCorpseBody(body));
        assert(!InventoryAgent_IsLootableCorpse(MakeCorpse(0x40000010, 1, body)));
    }
    assert(InventoryAgent_AutoLootCorpseBody(0x0004)); // Gargoyle creature body
    assert(InventoryAgent_AutoLootCorpseBody(0x0027)); // Mongbat
    assert(InventoryAgent_IsLootableCorpse(MakeCorpse(0x40000010, 0, 0x0004)));
    assert(InventoryAgent_IsLootableCorpse(MakeCorpse(0x40000010, 2, 0x0027)));

    // Static tiledata container semantics: accepted when absent or container, rejected when non-container.
    auto withTileContainer = MakeCorpse(0x40000011, 1);
    withTileContainer.HasStaticData = true;
    withTileContainer.IsContainer = true;
    assert(InventoryAgent_IsLootableCorpse(withTileContainer));

    auto withTileNonContainer = MakeCorpse(0x40000012, 1);
    withTileNonContainer.HasStaticData = true;
    withTileNonContainer.IsContainer = false;
    assert(!InventoryAgent_IsLootableCorpse(withTileNonContainer));

    // Reject non-corpse graphic, NPC, non-ground container, out of reach, other map, ignored/dragged/multi.
    auto badGraphic = MakeCorpse(0x40000020, 1);
    badGraphic.Graphic = 0x0E75;
    assert(!InventoryAgent_IsLootableCorpse(badGraphic));

    auto npc = MakeCorpse(0x40000021, 1);
    npc.IsNpc = true;
    assert(!InventoryAgent_IsLootableCorpse(npc));

    auto insideBag = MakeCorpse(0x40000022, 1);
    insideBag.Container = 0x40000001;
    assert(!InventoryAgent_IsLootableCorpse(insideBag));

    assert(!InventoryAgent_IsLootableCorpse(MakeCorpse(0x40000023, 3)));
    assert(!InventoryAgent_IsLootableCorpse(MakeCorpse(0x40000024, -1)));

    auto otherMap = MakeCorpse(0x40000025, 1);
    otherMap.SameMap = false;
    assert(!InventoryAgent_IsLootableCorpse(otherMap));

    auto ignored = MakeCorpse(0x40000026, 1);
    ignored.Ignored = true;
    assert(!InventoryAgent_IsLootableCorpse(ignored));

    auto dragged = MakeCorpse(0x40000027, 1);
    dragged.Dragged = true;
    assert(!InventoryAgent_IsLootableCorpse(dragged));

    auto multi = MakeCorpse(0x40000028, 1);
    multi.MultiBody = true;
    assert(!InventoryAgent_IsLootableCorpse(multi));

    assert(!InventoryAgent_IsLootableCorpse(MakeCorpse(0, 1)));
    assert(!InventoryAgent_IsLootableCorpse(MakeCorpse(0xFFFFFFFFu, 1)));

    // Already opened corpses or corpses with an open container gump are not auto-opened again.
    std::unordered_set<uint32_t> emptyAttempted;
    auto alreadyOpened = MakeCorpse(0x40000030, 1);
    alreadyOpened.Opened = true;
    assert(InventoryAgent_IsLootableCorpse(alreadyOpened));
    assert(!InventoryAgent_CanAutoOpenCorpse(alreadyOpened, emptyAttempted));

    auto withGump = MakeCorpse(0x40000031, 1);
    withGump.HasOpenGump = true;
    assert(!InventoryAgent_CanAutoOpenCorpse(withGump, emptyAttempted));

    // Tracker selection order, deduplication, and clear/reset support.
    InventoryAgentCorpseTracker tracker;
    const std::vector<InventoryAgentCorpseCandidate> candidates = {
        MakeCorpse(0x40000105, 2, 0x0027),
        MakeCorpse(0x40000102, 1, 0x0190), // Player body: skipped!
        MakeCorpse(0x40000104, 1, 0x0027),
        MakeCorpse(0x40000103, 1, 0x0004), // Same distance 1, lower serial -> selected first.
        MakeCorpse(0x40000106, 3, 0x0027), // Out of reach -> skipped!
        alreadyOpened,                     // Already opened -> skipped!
    };

    assert(tracker.SelectNext(candidates) == 0x40000103);
    assert(tracker.RecordAttempted(0x40000103));
    assert(tracker.HasAttempted(0x40000103));
    assert(tracker.SelectNext(candidates) == 0x40000104);
    assert(tracker.RecordAttempted(0x40000104));
    assert(tracker.SelectNext(candidates) == 0x40000105);
    assert(tracker.RecordAttempted(0x40000105));
    assert(tracker.SelectNext(candidates) == 0);
    assert(tracker.Size() == 3);

    // Clearing the tracker allows eligible corpses to be selected again.
    tracker.Clear();
    assert(tracker.Size() == 0 && !tracker.HasAttempted(0x40000103));
    assert(tracker.SelectNext(candidates) == 0x40000103);

    // History cap enforcement.
    assert(!tracker.RecordAttempted(0));
    assert(!tracker.RecordAttempted(0xFFFFFFFFu));
    for (uint32_t i = 0; i < InventoryAgent_MaxCorpseHistory; ++i)
    {
        assert(tracker.RecordAttempted(0x40100000u + i));
    }
    assert(tracker.IsFull() && tracker.Size() == InventoryAgent_MaxCorpseHistory);
    assert(tracker.RecordAttempted(0x40100000u)); // Already present still succeeds.
    assert(!tracker.RecordAttempted(0x40200000u)); // New serial rejected when full.
    tracker.Clear();
    assert(!tracker.IsFull() && tracker.RecordAttempted(0x40200000u));
}

} // namespace

int main()
{
    TestCorePlanningAndProperties();
    TestPerRuleDestinationOverrides();
    TestImportExportRoundTripAndRejection();
    TestCorpseFilteringAndTracking();
    return 0;
}
