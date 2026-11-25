// SPDX-License-Identifier: AGPL-3.0-or-later

#include "../razorimport.h"

#include <cstdlib>
#include <iostream>

using namespace xuoassist;

namespace {

void check(bool condition, std::string label)
{
    if (!condition)
    {
        std::cerr << "Failed: " << label << '\n';
        std::exit(1);
    }
}

bool contains(std::string text, std::string snippet)
{
    return text.find(snippet) != std::string::npos;
}

} // namespace

int main()
{
    const auto script = ParseRazorImport(RazorImportKind::Script,
        "# Heal only if needed\r\n"
        "if hits < 40 and not poisoned\r\n"
        "  bandage 'self'\r\n"
        "  waitfortarget 800\r\n"
        "else\r\n"
        "  restock 2\r\n"
        "endif\r\n"
        "for 2\r\n"
        "  lift 0x40000000 5\r\n"
        "  drop backpack\r\n"
        "endfor\r\n");
    check(script.CanImport(), "a valid script imports");
    check(contains(script.JavaScript, "(Player.HitsValue() < 40) && !(Player.Poisoned())"),
        "conditions read the native game state");
    check(contains(script.JavaScript, "Client.BandageTarget(\"self\")"), "bandage maps to client");
    check(contains(script.JavaScript, "Client.RunInventoryAgent(\"restock\", 2)"), "agent index retained");
    check(contains(script.JavaScript, "Client.MoveItem(1073741824, 5, \"backpack\")"), "lift/drop paired");

    const auto macro = ParseRazorImport(RazorImportKind::Macro,
        "!Loop\n"
        "Assistant.Macros.DoubleClickAction|1073741824|3957\n"
        "Assistant.Macros.PauseAction|00:00:00.5000000\n"
        "Assistant.Macros.WaitForTargetAction|1.25\n"
        "Assistant.Macros.AbsoluteTargetAction|0|0|1|33|44|0|0\n"
        "Assistant.Macros.WaitForGumpAction|24|True|0.5\n"
        "Assistant.Macros.GumpResponseAction|2|1|5|1|7&test name\n"
        "Assistant.Macros.LiftAction|1073741824|2|3957\n"
        "Assistant.Macros.DropAction|1073741825|(20, 40, 0)|0\n"
        "Assistant.Macros.IfAction|0|2|42\n"
        "Assistant.Macros.UseSkillAction|21\n"
        "Assistant.Macros.EndIfAction\n");
    check(macro.CanImport(), "Razor serialized actions import");
    check(contains(macro.JavaScript, "while (true)"), "loop marked");
    check(contains(macro.JavaScript, "Client.Wait(500)"), "TimeSpan fraction");
    check(contains(macro.JavaScript, "Client.WaitForTarget(1250)"), "decimal seconds");
    check(contains(macro.JavaScript, "Client.TargetObject(1)"), "absolute object");
    check(contains(macro.JavaScript, "razorGump = waitRazorGump(24, 500)"), "exact gump wait");
    check(contains(macro.JavaScript, "hook.AddCheck(5, true)") &&
          contains(macro.JavaScript, "hook.AddEntry(7, \"test name\")"), "gump reply payload");
    check(contains(macro.JavaScript, "Client.MoveItem(1073741824, 2, 1073741825, 20, 40, 0)"),
        "coordinates and chosen stack amount retained");

    const auto profile = ParseRazorImport(RazorImportKind::Profile,
        "<?xml version=\"1.0\"?>\n<profile>\n"
        "<property name=\"ObjectDelay\" type=\"System.Int32\">600</property>\n"
        "<property name=\"CorpseRange\" type=\"System.Int32\">3</property>\n"
        "<passwords><password>private-example</password></passwords>\n"
        "<property name=\"ForceIP\" type=\"System.String\">localhost</property>\n"
        "</profile>\n");
    check(profile.CanImport(), "profile preferences convert");
    check(profile.Imported == 2 && profile.Ignored == 1, "unsupported properties skipped");
    check(profile.ProfileOptions.size() == 3 && profile.ProfileOptions[0].Value == "600" &&
          profile.ProfileOptions[1].Value == "600" && profile.ProfileOptions[2].Key == "opencorpsedistance" &&
          profile.ProfileOptions[2].Value == "3", "supported profile fields map to native settings");
    check(profile.JavaScript.empty(), "profile data is never executable");

    const auto documented = ParseRazorImport(RazorImportKind::Script,
        "setvar 'chest' 0x40000001\n"
        "createlist 'destinations'\n"
        "pushlist 'destinations' 0x40000001\n"
        "foreach dest in destinations\n"
        "  if findtype 'bandage' backpack 35 as 'supplies' and hits = 60\n"
        "    @dclick 'supplies'\n"
        "    wft\n"
        "    target last\n"
        "  endif\n"
        "endfor\n"
        "wait 2 seconds\n"
        "waitforsysmsg 'success' 2500\n"
        "waitforprompt 'any' 1000\n"
        "promptresponse 'to home'\n"
        "targetloc 33 44 1\n"
        "targetrelloc -1 2\n"
        "say 'ready' 454\n"
        "overhead 'ready' 67\n"
        "createtimer 'pet care'\n"
        "lift 0x40000002 2\n"
        "drop ground 30 40\n");
    for (const auto &issue : documented.Issues)
    {
        std::cerr << "Line " << issue.Line << ": " << issue.Message << '\n';
    }
    check(documented.CanImport(), "documented script forms import together");
    check(contains(documented.JavaScript, "Client.FindType(\"bandage\", 35, \"backpack\", \"fast\")[0]") &&
          contains(documented.JavaScript, "razorTemp[\"supplies\"]"), "scoped find assigns a named alias");
    check(contains(documented.JavaScript, "Player.HitsValue() === 60"), "Razor equality and raw health");
    check(contains(documented.JavaScript, "Client.WaitForTarget(30000)") &&
          contains(documented.JavaScript, "Client.TargetObject(\"lasttarget\")"), "wait and last target shorthands");
    check(contains(documented.JavaScript, "Client.TargetTile(\"\", 0, 0, 0, 33, 44, 1)") &&
          contains(documented.JavaScript, "Client.TargetTileRelative(\"\", 0, 0, 0, -1, 2, 0)"),
        "tile coordinates match native call signature");
    check(contains(documented.JavaScript, "Client.CharPrint(\"self\", (67).toString(16), razorValue(\"ready\"))"),
        "overhead passes hue and text in native argument positions");
    check(contains(documented.JavaScript, "Client.Drop(1073741826, 2, 30, 40, 0)"), "ground drop preserves location");

    const auto extraCommands = ParseRazorImport(RazorImportKind::Script,
        "classicuo 'enablemusic' false\n"
        "cuo 'musicvolume' 50\n"
        "waitformenu 4 250\n"
        "menuresponse 3 4 55\n"
        "gumpclose 24\n"
        "random 10\n"
        "lifttype 0x0E21 2 50\n"
        "droprelloc 1 -1\n"
        "if hp < 20\n"
        "  replay\n"
        "endif\n");
    check(extraCommands.CanImport(), "extra documented commands import");
    check(contains(extraCommands.JavaScript, "Client.SetOptionMusic(false)") &&
          contains(extraCommands.JavaScript, "Client.SetOptionMusicVolume(128)"), "volume scales to CrossUO's byte");
    check(contains(extraCommands.JavaScript, "waitRazor(() => findRazorMenu(4), 250)") &&
          contains(extraCommands.JavaScript, "menu.ItemID(i) === 3 && Number(menu.ItemColor(i)) === 55"),
          "legacy menu wait and reply match ID, choice, and hue");
    check(contains(extraCommands.JavaScript, "waitRazorGump(24, 0); if (g) g.Close()"),
          "gump close can address a specific dialog");
    check(contains(extraCommands.JavaScript, "Client.Print(\"Random: \" + Client.Random(1, 10))"),
          "random command writes the Razor system message");
    check(contains(extraCommands.JavaScript,
          "Client.Drop((Client.FindType(3617, 50, \"backpack\", \"fast\")[0] || 0), 2, ") &&
          contains(extraCommands.JavaScript, "Player.X() + (1), Player.Y() + (-1), Player.Z()"),
          "lift by type and drop relative to the player preserve amount, hue, and offsets");
    check(contains(extraCommands.JavaScript, "razorReplay: while (true)") &&
          contains(extraCommands.JavaScript, "continue razorReplay;"), "replay restarts the outer script");
    const auto outOfRangeVolume = ParseRazorImport(RazorImportKind::Script, "cuo musicvolume 200\n");
    check(!outOfRangeVolume.CanImport(), "volume is limited to Razor's percentage range");

    const auto commented = ParseRazorImport(RazorImportKind::Profile,
        "<profile><!--<property name='ObjectDelay'>600</property>-->"
        "<property name='ObjectDelay' type='System.Int32'>800</property></profile>");
    check(commented.CanImport() && commented.Imported == 1 && commented.ProfileOptions[0].Value == "800",
        "profile parser ignores comments and handles quoted attributes");
    const auto unknownXml = ParseRazorImport(RazorImportKind::Profile,
        "<!DOCTYPE profile [<!ENTITY fake SYSTEM 'file:///unreadable'>]>"
        "<profile><property name='ObjectDelay'>&fake;</property></profile>");
    check(!unknownXml.CanImport() && unknownXml.ProfileOptions.empty(), "external XML entities rejected");

    const auto namedHotkeys = ParseRazorImport(RazorImportKind::Script,
        "hotkey 'target self'\nhotkey 'Drink Cure'\nhotkey 'clear target queue'\n");
    check(namedHotkeys.CanImport() && namedHotkeys.Imported == 3 &&
          contains(namedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1059)") &&
          contains(namedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1029)") &&
          contains(namedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1195)"),
          "named hotkeys retain native action IDs");
    for (const char *name : { "hotkey 0\n", "hotkey 'Local script'\n", "hotkey 'L:1059'\n" })
    {
        check(!ParseRazorImport(RazorImportKind::Script, name).CanImport(),
              "profile-dependent hotkey references remain unsupported");
    }

    const auto hotkeys = ParseRazorImport(RazorImportKind::Profile,
        "<profile>\n<hotkeys>\n"
        "<key mod='2' key='65' send='True' command='PrivateAlias'>L:1059</key>\n"
        "<key mod='4' key='-1' send='False' name='Drink Cure' />\n"
        "<key mod='0' key='112' send='False' loc='1195' />\n"
        "<key mod='2' key='65' send='False'>L:1058</key>\n"
        "<key mod='0' key='186' send='True'>L:1028</key>\n"
        "<key mod='1' key='115' send='False'>L:1058</key>\n"
        "<key mod='0' key='66' send='False'>Unported Script</key>\n"
        "</hotkeys>\n<passwords><password>SecretPassword</password></passwords></profile>");
    check(hotkeys.CanImport() && hotkeys.ProfileHotkeys.size() == 3 && hotkeys.Ignored == 4 &&
          hotkeys.Notices.size() == 5 && hotkeys.ProfileHotkeys[0].Id == 1059 &&
          hotkeys.ProfileHotkeys[0].VirtualKey == 'A' && hotkeys.ProfileHotkeys[0].Modifiers == 2 &&
          hotkeys.ProfileHotkeys[0].PassToGame && hotkeys.ProfileHotkeys[1].Id == 1029 &&
          hotkeys.ProfileHotkeys[1].VirtualKey == -1 && !hotkeys.ProfileHotkeys[1].PassToGame &&
          hotkeys.ProfileHotkeys[2].Id == 1195 && hotkeys.ProfileHotkeys[2].VirtualKey == 112,
          "profile preserves mixed passthrough and modifier states in source order");
    check(hotkeys.Notices[0].Line == 3 && !contains(hotkeys.Notices[0].Message, "PrivateAlias"),
          "skipped chat commands are explained without exposing their text");
    check(RazorHotkeyKeyName(0x7B) == "F12" && RazorHotkeyKeyName(0x69) == "Keypad 9" &&
          RazorHotkeyKeyName(0xBA).empty() && RazorHotkeyKeyName(0).empty(),
          "only known platform-independent virtual keys are offered");

    const auto highlighting = ParseRazorImport(RazorImportKind::Profile,
        "<profile><property name='HighlightReagents'>True</property></profile>");
    check(highlighting.CanImport() && highlighting.ProfileOptions[0].File == "ItemCounters.ini" &&
          highlighting.ProfileOptions[0].Key == "highlight_reagents",
          "import uses the native reagent highlighting preference key");

    const auto targetOptions = ParseRazorImport(RazorImportKind::Profile,
        "<profile><property name='RangeCheckLT'>False</property><property name='LTRange'>18</property>"
        "<property name='QueueTargets'>True</property><property name='BlockHealPoison'>True</property>"
        "<property name='SmartLastTarget'>False</property></profile>");
    check(targetOptions.CanImport() && targetOptions.ProfileOptions.size() == 4 &&
          targetOptions.ProfileOptions[0].File == "Targeting.ini" && targetOptions.ProfileOptions[0].Key == "queue" &&
          targetOptions.ProfileOptions[1].File == "GameplayFilters.ini" &&
          targetOptions.ProfileOptions[1].Key == "guardhealing" && targetOptions.ProfileOptions[1].Value == "true" &&
          targetOptions.ProfileOptions[3].Key == "lastrange" && targetOptions.ProfileOptions[3].Value == "0",
          "profile maps boolean targeting and healing settings and preserves disabled range checks");

    const auto rejection = ParseRazorImport(RazorImportKind::Script, "say 'hello'\nunrecognized xyz\n");
    check(!rejection.CanImport() && rejection.Issues[0].Line == 2, "unknown actions cannot silently import");
    const auto malformed = ParseRazorImport(RazorImportKind::Script, "if hits < 40\nsay 'dangling'\n");
    check(!malformed.CanImport() && malformed.Issues[0].Line == 1, "unclosed block refused");
    const auto injection = ParseRazorImport(RazorImportKind::Script,
        "say 'x\"); Client.LogOut(); (\"y'\n");
    check(injection.CanImport() && contains(injection.JavaScript, "\\\""), "speech remains a quoted string");
    const auto orphan = ParseRazorImport(RazorImportKind::Macro,
        "Assistant.Macros.LiftAction|1073741824|2|3957\n");
    check(!orphan.CanImport(), "incomplete drag refused");

    // Expanded Classic Razor built-in hotkeys (spells, agents, dress, war/peace, last skill/spell).
    const auto expandedHotkeys = ParseRazorImport(RazorImportKind::Script,
        "hotkey 'Greater Heal'\n"
        "hotkey 'Flame Strike'\n"
        "hotkey 'Last Spell'\n"
        "hotkey 'Last Skill'\n"
        "hotkey 'Toggle War/Peace'\n"
        "hotkey 'Undress All'\n"
        "hotkey 'Organizer Agent'\n"
        "hotkey 'Restock Agent'\n"
        "hotkey 'Scavenger Agent'\n"
        "hotkey 'Bandage Friend'\n"
        "hotkey 'Target Closest Enemy'\n"
        "hotkey 'Clear Scavenger Cache'\n");
    check(expandedHotkeys.CanImport() && expandedHotkeys.Imported == 12 &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(3029)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(3051)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1370)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1369)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1055)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1039)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1382)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1383)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1381)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1027)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1314)") &&
          contains(expandedHotkeys.JavaScript, "Client.ExecuteRazorHotkey(1397)"),
          "expanded Classic built-in hotkeys resolve to stable IDs");

    // Additional Classic .macro actions.
    const auto extraMacro = ParseRazorImport(RazorImportKind::Macro,
        "Assistant.Macros.BandageSelfAction\n"
        "Assistant.Macros.ToggleWarModeAction\n"
        "Assistant.Macros.AttackLastTargetAction\n"
        "Assistant.Macros.SetAbilityAction|primary\n"
        "Assistant.Macros.WalkAction|north\n"
        "Assistant.Macros.TargetRelLocAction|1|-2\n"
        "Assistant.Macros.OrganizerAction|2\n"
        "Assistant.Macros.RestockAction|1\n"
        "Assistant.Macros.ScavengerAction\n"
        "Assistant.Macros.UseOnceAction\n"
        "Assistant.Macros.HotKeyAction|3029\n"
        "Assistant.Macros.WaitForStatAction|0|1|80|5\n"
        "Assistant.Macros.UnDressLayerAction|2\n"
        "Assistant.Macros.ClearJournalAction\n"
        "Assistant.Macros.ResyncAction\n");
    check(extraMacro.CanImport() &&
          contains(extraMacro.JavaScript, "Client.BandageSelf()") &&
          contains(extraMacro.JavaScript, "Client.WarMode()") &&
          contains(extraMacro.JavaScript, "Client.Attack(\"lasttarget\")") &&
          contains(extraMacro.JavaScript, "Client.UseAbility(\"primary\")") &&
          contains(extraMacro.JavaScript, "Client.Step(0)") &&
          contains(extraMacro.JavaScript, "Client.RunInventoryAgent(\"organizer\", 2)") &&
          contains(extraMacro.JavaScript, "Client.ExecuteRazorHotkey(3029)") &&
          contains(extraMacro.JavaScript, "waitRazor(() => Player.HitsValue() >= 80, 5000)") &&
          contains(extraMacro.JavaScript, "Client.Unequip(2)"),
          "additional Classic macro actions import cleanly");

    // Classic XML saved agents (organizer, restock, scavenger, useonce, dress, buy, sell, friends).
    const auto classicAgents = ParseRazorImport(RazorImportKind::Profile,
        "<profile>\n"
        "<organizer-1 hotbag='0x40000010'><item id='3821' hue='-1'/><item id='3962' hue='50'/></organizer-1>\n"
        "<restock-2 source='0x40000020' hotbag='0x40000021'><item id='3962' amount='40'/></restock-2>\n"
        "<scavenger hotbag='0x40000030'><item id='3821'/></scavenger>\n"
        "<useonce><item>0x40000040</item><item serial='0x40000041'/></useonce>\n"
        "<dresslist name='PvM Kit'><item serial='0x40000050' layer='1'/><item serial='0x40000051' layer='2'/></dresslist>\n"
        "<buy-1 name='Reagents'><item id='3962' amount='50'/></buy-1>\n"
        "<sell hotbag='0x40000060'><item id='3854'/></sell>\n"
        "<friends><friend serial='0x00001234' name='Ally'/></friends>\n"
        "</profile>\n");
    check(classicAgents.CanImport(), "Classic XML saved agents import");
    auto has_opt = [&](const char *file, const char *section, const char *key, const char *val) {
        for (const auto &opt : classicAgents.ProfileOptions)
        {
            if (opt.File == file && opt.Section == section && opt.Key == key && opt.Value == val)
            {
                return true;
            }
        }
        return false;
    };
    check(has_opt("InventoryAgents.ini", "Meta", "count", "4") &&
          has_opt("InventoryAgents.ini", "Agent0", "mode", "0") &&
          has_opt("InventoryAgents.ini", "Agent0Rule1", "matchhue", "true") &&
          has_opt("InventoryAgents.ini", "Agent1", "mode", "1") &&
          has_opt("InventoryAgents.ini", "Agent1Rule0", "quantity", "40") &&
          has_opt("InventoryAgents.ini", "Agent3", "mode", "3") &&
          has_opt("DressSets.ini", "Meta", "count", "1") &&
          has_opt("DressSets.ini", "Dress0", "name", "PvM Kit") &&
          has_opt("ShopList.ini", "Meta", "count", "2") &&
          has_opt("SellHotBag.ini", "SellHotBag", "serial", "0x40000060") &&
          has_opt("Friends.ini", "Meta", "count", "1") &&
          has_opt("Friends.ini", "Friend0", "name", "Ally"),
          "Classic XML agents map to native profile INI options");

    // UOSteam (.uos) script-to-JS translation.
    const auto uosteam = ParseRazorImport(RazorImportKind::UOSteam,
        "// UOSteam combat & utility script\n"
        "clearjournal\n"
        "setalias 'bag' 0x40000100\n"
        "if hits < 50 and not dead\n"
        "  cast 'Greater Heal' self\n"
        "  waitfortarget 1500\n"
        "  target self\n"
        "elseif injournal 'fizzles'\n"
        "  headmsg 'Retrying' 67 self\n"
        "else\n"
        "  usetype 0x0F0C any backpack\n"
        "endif\n"
        "while findtype 0x0EED any backpack\n"
        "  moveitem found 'bag' 100\n"
        "  pause 600\n"
        "endwhile\n"
        "for 1 to 2\n"
        "  useobject last\n"
        "  dress 'Battle'\n"
        "  undress\n"
        "  organizer 'Loot'\n"
        "  autoloot\n"
        "  attack enemy\n"
        "  sysmsg 'Cycle complete' 88\n"
        "endfor\n");
    check(uosteam.CanImport(), "UOSteam script translates cleanly");
    check(contains(uosteam.JavaScript, "// Converted from UOSteam.") &&
          contains(uosteam.JavaScript, "Client.ClearJournal()") &&
          contains(uosteam.JavaScript, "Client.WaitTargetObject(\"self\"); Client.Cast(\"Greater Heal\")") &&
          contains(uosteam.JavaScript, "Client.WaitForTarget(1500)") &&
          contains(uosteam.JavaScript, "Client.TargetObject(\"self\")") &&
          contains(uosteam.JavaScript, "Boolean(Client.InJournal(\"fizzles\"))") &&
          contains(uosteam.JavaScript, "Client.CharPrint(\"self\", (67).toString(16), \"Retrying\")") &&
          contains(uosteam.JavaScript, "Client.UseType(3852, 65535, \"backpack\")") &&
          contains(uosteam.JavaScript, "Client.MoveItem((razorFound || 0), 100, razorValue(\"bag\"))") &&
          contains(uosteam.JavaScript, "Client.UseObject(\"lasttarget\")") &&
          contains(uosteam.JavaScript, "Client.Dress(\"Battle\")") &&
          contains(uosteam.JavaScript, "Client.Undress()") &&
          contains(uosteam.JavaScript, "Client.RunInventoryAgent(\"Loot\")") &&
          contains(uosteam.JavaScript, "Client.AutoLootOnce(\"AutoLoot\")") &&
          contains(uosteam.JavaScript, "Client.Attack(\"enemy\")") &&
          contains(uosteam.JavaScript, "Client.Print(\"Cycle complete\")"),
          "UOSteam commands and aliases map to native CrossUO JS");

    const auto badUos = ParseRazorImport(RazorImportKind::UOSteam, "pause 100\nunknownuoscmd 123\n");
    check(!badUos.CanImport() && badUos.Issues.size() == 1 && badUos.Issues[0].Line == 2,
          "unsupported UOSteam commands report line-numbered issues");
    const auto uosInject = ParseRazorImport(RazorImportKind::UOSteam, "sysmsg 'a\"); Client.LogOut(); (\"b'\n");
    check(uosInject.CanImport() && contains(uosInject.JavaScript, "\\\""),
          "UOSteam string arguments stay safely escaped");

    if (std::getenv("PRINT_RAZOR_IMPORT") != nullptr)
    {
        std::cout << script.JavaScript << '\n' << macro.JavaScript << '\n' << injection.JavaScript << '\n'
                  << documented.JavaScript << '\n' << extraCommands.JavaScript << '\n' << namedHotkeys.JavaScript;
    }
}
