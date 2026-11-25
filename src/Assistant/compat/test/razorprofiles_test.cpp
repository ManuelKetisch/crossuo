// SPDX-License-Identifier: AGPL-3.0-or-later

#include "../razorenhancedimport.h"

#include <algorithm>
#include <cassert>
#include <string>

using namespace xuoassist;

static std::string setting(const RazorImportResult &result, const char *file, const char *section, const char *key)
{
    const auto option = std::find_if(result.ProfileOptions.begin(), result.ProfileOptions.end(), [&](const auto &it) {
        return it.File == file && it.Section == section && it.Key == key;
    });
    return option != result.ProfileOptions.end() ? option->Value : std::string();
}

static void ignores_secret(const RazorImportResult &result, const std::string &needle)
{
    for (const auto &entry : result.ProfileOptions)
    {
        assert(entry.Value.find(needle) == std::string::npos);
    }
    for (const auto &entry : result.Issues)
    {
        assert(entry.Message.find(needle) == std::string::npos);
    }
    for (const auto &entry : result.Notices)
    {
        assert(entry.Message.find(needle) == std::string::npos);
    }
}

int main()
{
    const auto enhanced = ParseRazorEnhancedProfile({
        { RazorEnhancedTable::General, R"([{"ObjectDelay":650,"SmartLastTarget":true,"QueueTargets":true,
            "RangeCheckLT":true,"LTRange":9,"BandageHealhpTextBox":72,"PotionEquip":true,
            "LockToolBarCheckBox":true,"ForceIP":"fixture-secret","BandageHealAutostartCheckBox":true}])" },
        { RazorEnhancedTable::Hotkeys, R"([
            {"Group":"Target","Name":"Target Self","Key":131137,"Pass":false},
            {"Group":"Potions","Name":"Potion Cure","Key":262317,"Pass":true},
            {"Group":"Use","Name":"Right Hand","Key":66038,"Pass":true},
            {"Group":"Target","Name":"Target Last","Key":131137,"Pass":true},
            {"Group":"Scripts","Name":"Target Self","Key":131140,"Pass":false},
            {"Group":"Target","Name":"Clear Target Queue","Key":262259,"Pass":false},
            {"Group":"Target","Name":"Target Self","Key":131137.5,"Pass":false},
            {"Group":"General","Name":"Resync","Key":0,"Pass":true}])" },
        { RazorEnhancedTable::ToolbarItems, R"([
            {"Name":"Bandages","Graphics":3617,"Color":-1,"Warning":true,"WarningLimit":8},
            {"Name":"Empty","Graphics":0,"Color":0,"Warning":false,"WarningLimit":0},
            {"Name":"Line\nBreak","Graphics":3821,"Color":0,"Warning":false,"WarningLimit":0}])" },
    });
    assert(enhanced.CanImport() && enhanced.ProfileHotkeys.size() == 2 && enhanced.Ignored == 8);
    assert(enhanced.ProfileHotkeys[0].Id == 1059 && enhanced.ProfileHotkeys[0].VirtualKey == 65);
    assert(enhanced.ProfileHotkeys[0].Modifiers == 2 && !enhanced.ProfileHotkeys[0].PassToGame);
    assert(enhanced.ProfileHotkeys[1].Id == 2069 && enhanced.ProfileHotkeys[1].VirtualKey == -1);
    assert(enhanced.ProfileHotkeys[1].Modifiers == 4 && enhanced.ProfileHotkeys[1].PassToGame);
    assert(setting(enhanced,"Options.ini","main","moveitemsdelay") == "650");
    assert(setting(enhanced,"Options.ini","main","useitemsdelay") == "650");
    assert(setting(enhanced,"Targeting.ini","Targeting","lastrange") == "9");
    assert(setting(enhanced,"BandageHeal.ini","BandageHeal","hit_percent") == "72");
    assert(setting(enhanced,"BandageHeal.ini","BandageHeal","enabled").empty());
    assert(setting(enhanced,"PotionHands.ini","PotionHands","enabled") == "true");
    assert(setting(enhanced,"Toolbar.ini","Toolbar","locked") == "true");
    assert(setting(enhanced,"Toolbar.ini","Slot0","graphic") == "3617");
    assert(setting(enhanced,"Toolbar.ini","Slot0","alert") == "8");
    assert(setting(enhanced,"Toolbar.ini","Slot0","hue") == "65535");
    assert(setting(enhanced,"Toolbar.ini","Slot1","graphic") == "0");
    assert(setting(enhanced,"Toolbar.ini","Slot2","graphic").empty());
    ignores_secret(enhanced,"fixture-secret");

    const auto classic = ParseRazorImport(RazorImportKind::Profile, R"(<profile>
        <property name="PotionEquip">True</property><property name="CounterWarnAmount">7</property>
        <property name="CounterWarn">True</property><property name="ShowPartyStats">True</property>
        <passwords><password>fixture-secret</password></passwords>
        <counters><counter name="Black Pearl" enabled="True" image="False"/>
        <counter name="No matching definition" enabled="True" image="True"/>
        <counter name="Ignore" enabled="False" image="True"/></counters>
        </profile>)");
    const std::string definitions = R"(<counters>
        <counter><name>Black Pearl</name><format>bp</format><itemid>3962</itemid><hue>-1</hue></counter>
        <counter><name>Ignore</name><format>ignore</format><itemid>3963</itemid><hue>0</hue></counter>
        <counter><name>Invalid</name><format>a=b</format><itemid>3964</itemid><hue>0</hue></counter>
        </counters>)";
    const auto combined = AddClassicCounterDefinitions(classic, definitions);
    assert(classic.CanImport() && combined.CanImport() && combined.Imported == classic.Imported + 1);
    assert(setting(combined,"ItemCounters.ini","Meta","count") == "1");
    assert(setting(combined,"ItemCounters.ini","Meta","warnings") == "true");
    assert(setting(combined,"ItemCounters.ini","Counter0","icon") == "false");
    assert(setting(combined,"ItemCounters.ini","Counter0","graphic") == "3962");
    assert(setting(combined,"ItemCounters.ini","Counter0","hue") == "65535");
    assert(setting(combined,"ItemCounters.ini","Counter0","warning") == "7");
    assert(setting(combined,"ItemCounters.ini","Counter1","graphic").empty());
    assert(setting(combined,"PotionHands.ini","PotionHands","enabled") == "true");
    assert(setting(combined,"ItemCounters.ini","Display","party_stats") == "true");
    assert(combined.Ignored == classic.Ignored + 1);
    ignores_secret(combined,"fixture-secret");
    const auto standalone = AddClassicCounterDefinitions({},definitions);
    assert(standalone.CanImport() && standalone.Imported == 2 && standalone.Ignored == 1);
    assert(!AddClassicCounterDefinitions(classic, "<profile/>").CanImport());

    assert(!ParseRazorEnhancedProfile({ { RazorEnhancedTable::General, "[{}]" } }).CanImport());
    assert(!ParseRazorEnhancedProfile({ { RazorEnhancedTable::General, "{}" } }).CanImport());
    assert(!ParseRazorEnhancedProfile({ { RazorEnhancedTable::General, "[{},{}]" } }).CanImport());
    assert(!ParseRazorEnhancedProfile({ { RazorEnhancedTable::General, "[7]" } }).CanImport());
    assert(!ParseRazorEnhancedProfile({ { RazorEnhancedTable::General, "[{\"ObjectDelay\":650}]" },
                                      { RazorEnhancedTable::General, "[{}]" } }).CanImport());
    assert(!ParseRazorEnhancedProfile({ { RazorEnhancedTable::General, std::string(262145, ' ') } }).CanImport());
    assert(!ParseRazorEnhancedProfile({ { RazorEnhancedTable::General, std::string(2048, '[') } }).CanImport());
    const auto bom = ParseRazorEnhancedProfile({ { RazorEnhancedTable::General, "\xef\xbb\xbf[{\"RangeCheckLT\":false}]" } });
    assert(bom.CanImport() && setting(bom,"Targeting.ini","Targeting","lastrange") == "0");

    // Expanded Enhanced tables: AUTOLOOT, SCAVENGER, ORGANIZER, RESTOCK, DRESS, BUY, SELL,
    // BANDAGE_HEAL, TARGETS, FRIENDS, SCRIPTS, plus new hotkey groups.
    const auto fullEnhanced = ParseRazorEnhancedProfile({
        { RazorEnhancedTable::Hotkeys, R"([
            {"Group":"Spells","Name":"Greater Heal","Key":131138,"Pass":false},
            {"Group":"Agents","Name":"Organizer","Key":131139,"Pass":false},
            {"Group":"General","Name":"Toggle War/Peace","Key":131140,"Pass":false},
            {"Group":"Skills","Name":"Last Skill","Key":131141,"Pass":false}])" },
        { RazorEnhancedTable::AutoLoot, R"([
            {"Description":"Gold Loot","Bag":1073741840,"Items":[
                {"Graphics":3821,"Color":-1,"Selected":true},
                {"Graphics":3962,"Color":50,"Selected":false}]}])" },
        { RazorEnhancedTable::Scavenger, R"([
            {"Description":"Ground Gems","Bag":1073741841,"Items":[
                {"Graphics":3859,"Color":0,"Selected":true}]}])" },
        { RazorEnhancedTable::Organizer, R"([
            {"Description":"Reag Sort","Source":1073741842,"Destination":1073741843,"Items":[
                {"Graphics":3962,"Color":-1,"Amount":50,"Selected":true}]}])" },
        { RazorEnhancedTable::Restock, R"([
            {"Description":"PvM Restock","Source":1073741844,"Destination":1073741845,"Items":[
                {"Graphics":3617,"Color":-1,"Amount":100,"Selected":true}]}])" },
        { RazorEnhancedTable::Dress, R"([
            {"Description":"Mage Suit","Items":[
                {"Layer":1,"Serial":1073741850,"Selected":true},
                {"Layer":2,"Serial":1073741851,"Selected":true}]}])" },
        { RazorEnhancedTable::Buy, R"([
            {"Description":"Buy Reags","Items":[
                {"Name":"Black Pearl","Graphics":3962,"Color":-1,"Amount":50,"Selected":true}]}])" },
        { RazorEnhancedTable::Sell, R"([
            {"Description":"Sell Bottles","Bag":1073741860,"Items":[
                {"Name":"Empty Bottle","Graphics":3854,"Color":0,"Amount":20,"Selected":true}]}])" },
        { RazorEnhancedTable::BandageHeal, R"([
            {"HpLimit":80,"MaxRange":2,"Delay":4500,"UseCustomDelay":true,"TreatPoison":true,
             "PauseMortal":true,"PauseHidden":true,"AutoStart":true,"SecretField":"fixture-secret"}])" },
        { RazorEnhancedTable::Targets, R"([
            {"Name":"Nearest Red","MobileName":"","BodyIDs":[400],"Hue":-1,"RangeMin":0,"RangeMax":10,
             "Notorieties":[6],"Selector":"Weakest"}])" },
        { RazorEnhancedTable::Friends, R"([
            {"IncludeParty":true,"Players":[
                {"Serial":12345,"Name":"Healer","Enabled":true},
                {"Serial":0,"Name":"BadSerial","Enabled":true}]}])" },
        { RazorEnhancedTable::Scripts, R"([
            {"Filename":"heal.js","Loop":true,"AutoStart":true},
            {"Filename":"helper.py","Loop":false,"AutoStart":true},
            {"Filename":"../escape.js","Loop":false,"AutoStart":false}])" },
    });
    assert(fullEnhanced.CanImport());
    assert(fullEnhanced.ProfileHotkeys.size() == 4);
    assert(fullEnhanced.ProfileHotkeys[0].Id == 3029);
    assert(fullEnhanced.ProfileHotkeys[1].Id == 1382);
    assert(fullEnhanced.ProfileHotkeys[2].Id == 1055);
    assert(fullEnhanced.ProfileHotkeys[3].Id == 1369);
    assert(setting(fullEnhanced, "InventoryAgents.ini", "Meta", "count") == "4");
    assert(setting(fullEnhanced, "InventoryAgents.ini", "Agent0", "mode") == "4");
    assert(setting(fullEnhanced, "InventoryAgents.ini", "Agent0", "rules") == "1");
    assert(setting(fullEnhanced, "InventoryAgents.ini", "Agent2", "mode") == "0");
    assert(setting(fullEnhanced, "InventoryAgents.ini", "Agent3", "mode") == "1");
    assert(setting(fullEnhanced, "InventoryAgents.ini", "Agent3Rule0", "quantity") == "100");
    assert(setting(fullEnhanced, "DressSets.ini", "Meta", "count") == "1");
    assert(setting(fullEnhanced, "DressSets.ini", "Dress0", "name") == "Mage Suit");
    assert(setting(fullEnhanced, "DressSets.ini", "Dress0", "count") == "2");
    assert(setting(fullEnhanced, "ShopList.ini", "Meta", "count") == "2");
    assert(setting(fullEnhanced, "SellHotBag.ini", "SellHotBag", "enabled") == "true");
    assert(setting(fullEnhanced, "BandageHeal.ini", "BandageHeal", "hit_percent") == "80");
    assert(setting(fullEnhanced, "BandageHeal.ini", "BandageHeal", "automatic_delay") == "false");
    assert(setting(fullEnhanced, "BandageHeal.ini", "BandageHeal", "delay") == "4500");
    assert(setting(fullEnhanced, "BandageHeal.ini", "BandageHeal", "enabled").empty());
    assert(setting(fullEnhanced, "Targeting.ini", "Targeting", "presetcount") == "1");
    assert(setting(fullEnhanced, "Targeting.ini", "Preset0", "name") == "Nearest Red");
    assert(setting(fullEnhanced, "Targeting.ini", "Preset0", "body") == "400");
    assert(setting(fullEnhanced, "Targeting.ini", "Preset0", "notorieties") == "32");
    assert(setting(fullEnhanced, "Targeting.ini", "Preset0", "selector") == "1");
    assert(setting(fullEnhanced, "Friends.ini", "Meta", "feel_party_members_as_friends") == "true");
    assert(setting(fullEnhanced, "Friends.ini", "Meta", "count") == "1");
    assert(setting(fullEnhanced, "Friends.ini", "Friend0", "name") == "Healer");
    assert(setting(fullEnhanced, "Scripts.ini", "Meta", "count") == "2");
    assert(setting(fullEnhanced, "Scripts.ini", "Script0", "filename") == "heal.js");
    assert(setting(fullEnhanced, "Scripts.ini", "Script0", "autostart") == "false");
    assert(setting(fullEnhanced, "Scripts.ini", "Script1", "filename") == "helper.py");
    assert(setting(fullEnhanced, "Scripts.ini", "Script2", "filename").empty());
    ignores_secret(fullEnhanced, "fixture-secret");

    // Classic AutoSearch, NoSearchPouches, and Display/TitleBar XML properties
    // round-trip cleanly alongside counters.xml without overwriting or duplicating options.
    const auto classicDisplay = ParseRazorImport(RazorImportKind::Profile, R"(<profile>
        <property name="AutoSearch" type="System.Boolean">False</property>
        <property name="NoSearchPouches" type="System.Boolean">True</property>
        <property name="ShowImages" type="System.Boolean">True</property>
        <property name="ShowNotoHue" type="System.Boolean">True</property>
        <property name="HighlightReagents" type="System.Boolean">True</property>
        <property name="ShowHealth" type="System.Boolean">True</property>
        <property name="ShowPartyStats" type="System.Boolean">False</property>
        <property name="ShowCounters" type="System.Boolean">True</property>
        <property name="CounterWarn" type="System.Boolean">True</property>
        <property name="CounterWarnAmount" type="System.Int32">5</property>
        <property name="TitleBarDisplay" type="System.Boolean">True</property>
        <property name="TitleBarText" type="System.String">UO - {char} - {mediumstatbar} {bp}</property>
        <counters><counter name="Black Pearl" enabled="True" image="True"/></counters>
        </profile>)");
    assert(classicDisplay.CanImport());
    assert(setting(classicDisplay, "ItemCounters.ini", "Meta", "auto_recount") == "false");
    assert(setting(classicDisplay, "ItemCounters.ini", "Meta", "skip_pouches") == "true");
    assert(setting(classicDisplay, "ItemCounters.ini", "Meta", "icons") == "true");
    assert(setting(classicDisplay, "ItemCounters.ini", "Meta", "show") == "true");
    assert(setting(classicDisplay, "ItemCounters.ini", "Meta", "warnings") == "true");
    assert(setting(classicDisplay, "ItemCounters.ini", "Display", "name_notoriety") == "true");
    assert(setting(classicDisplay, "ItemCounters.ini", "Display", "highlight_reagents") == "true");
    assert(setting(classicDisplay, "ItemCounters.ini", "Display", "health_changes") == "true");
    assert(setting(classicDisplay, "ItemCounters.ini", "Display", "party_stats") == "false");
    assert(setting(classicDisplay, "Options.ini", "display", "enabled") == "true");
    assert(setting(classicDisplay, "Options.ini", "display", "titledata") == "UO - {char} - {mediumstatbar} {bp}");

    const auto classicDisplayWithCounters = AddClassicCounterDefinitions(classicDisplay, definitions);
    assert(classicDisplayWithCounters.CanImport());
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Meta", "count") == "1");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Meta", "auto_recount") == "false");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Meta", "skip_pouches") == "true");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Meta", "icons") == "true");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Meta", "show") == "true");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Meta", "warnings") == "true");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Display", "name_notoriety") == "true");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Display", "highlight_reagents") == "true");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Display", "health_changes") == "true");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Display", "party_stats") == "false");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Counter0", "key") == "bp");
    assert(setting(classicDisplayWithCounters, "ItemCounters.ini", "Counter0", "warning") == "5");
    assert(setting(classicDisplayWithCounters, "Options.ini", "display", "enabled") == "true");
    assert(setting(classicDisplayWithCounters, "Options.ini", "display", "titledata") == "UO - {char} - {mediumstatbar} {bp}");
    // Verify no duplicate (File, Section, Key) entries exist after combining with counters.xml.
    for (std::size_t i = 0; i < classicDisplayWithCounters.ProfileOptions.size(); ++i)
    {
        for (std::size_t j = i + 1; j < classicDisplayWithCounters.ProfileOptions.size(); ++j)
        {
            const auto &a = classicDisplayWithCounters.ProfileOptions[i];
            const auto &b = classicDisplayWithCounters.ProfileOptions[j];
            assert(!(a.File == b.File && a.Section == b.Section && a.Key == b.Key));
        }
    }

    // Opposite boolean polarity for AutoSearch / NoSearchPouches and invalid values rejected.
    const auto classicAutoSearchOn = ParseRazorImport(RazorImportKind::Profile, R"(<profile>
        <property name="AutoSearch" type="System.Boolean">True</property>
        <property name="NoSearchPouches" type="System.Boolean">False</property>
        </profile>)");
    assert(classicAutoSearchOn.CanImport());
    assert(setting(classicAutoSearchOn, "ItemCounters.ini", "Meta", "auto_recount") == "true");
    assert(setting(classicAutoSearchOn, "ItemCounters.ini", "Meta", "skip_pouches") == "false");
    assert(!ParseRazorImport(RazorImportKind::Profile,
        R"(<profile><property name="AutoSearch">maybe</property></profile>)").CanImport());
    assert(!ParseRazorImport(RazorImportKind::Profile,
        R"(<profile><property name="NoSearchPouches">2</property></profile>)").CanImport());
    assert(!ParseRazorImport(RazorImportKind::Profile,
        "<profile><property name=\"TitleBarText\">Line1\nLine2</property></profile>").CanImport());

    // Malformed Enhanced table inputs rejected safely.
    assert(!ParseRazorEnhancedProfile({ { RazorEnhancedTable::Scripts, R"([{"Filename":"../../etc/passwd"}])" } }).CanImport());
    assert(!ParseRazorEnhancedProfile({ { RazorEnhancedTable::Targets, R"([{"Name":"Bad","RangeMax":999}])" } }).CanImport());
    assert(!ParseRazorEnhancedProfile({ { RazorEnhancedTable::AutoLoot, "not-json" } }).CanImport());
}
