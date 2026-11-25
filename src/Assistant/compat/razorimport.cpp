// SPDX-License-Identifier: AGPL-3.0-or-later

#include "razorimport.h"

#include <external/tinyxml2.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

namespace xuoassist {
namespace {

using Tokens = std::vector<std::string>;

constexpr size_t MAX_IMPORT_BYTES = 256 * 1024;
constexpr size_t MAX_IMPORT_LINES = 5000;
constexpr size_t MAX_NESTING = 64;

bool space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

std::string trim(const std::string &value)
{
    size_t first = 0;
    size_t last = value.size();
    while (first < last && space(value[first]))
    {
        ++first;
    }
    while (last > first && space(value[last - 1]))
    {
        --last;
    }
    return value.substr(first, last - first);
}

std::string lower(std::string value)
{
    std::string output(value);
    for (auto &ch : output)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch += 'a' - 'A';
        }
    }
    return output;
}

std::string command_name(const std::string &value)
{
    return lower(!value.empty() && value.front() == '@' ? value.substr(1) : value);
}

std::string quote(std::string value)
{
    std::string output = "\"";
    const std::string HEX = "0123456789abcdef";
    for (const unsigned char ch : value)
    {
        if (ch == '"' || ch == '\\')
        {
            output += '\\';
            output += char(ch);
        }
        else if (ch < 32)
        {
            output += "\\u00";
            output += HEX[ch >> 4];
            output += HEX[ch & 15];
        }
        else
        {
            output += char(ch);
        }
    }
    return output + '"';
}

bool integer(const std::string &input, int64_t &value, int64_t minimum, int64_t maximum)
{
    const auto text = trim(input);
    if (text.empty())
    {
        return false;
    }
    const int base = text.size() > 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X') ? 16 : 10;
    char *end = nullptr;
    errno = 0;
    value = std::strtoll(text.c_str(), &end, base);
    return errno == 0 && end != text.c_str() && *end == '\0' &&
        value >= minimum && value <= maximum;
}

std::string number(std::string input, int64_t minimum = 0, int64_t maximum = UINT32_MAX)
{
    int64_t value = 0;
    return integer(input, value, minimum, maximum) ? std::to_string(value) : std::string();
}

bool boolean(std::string input, bool &value)
{
    const auto text = lower(trim(input));
    if (text == "true" || text == "1")
    {
        value = true;
        return true;
    }
    if (text == "false" || text == "0")
    {
        value = false;
        return true;
    }
    return false;
}

Tokens split_fields(const std::string &value, char separator)
{
    Tokens output;
    size_t start = 0;
    while (true)
    {
        const auto stop = value.find(separator, start);
        output.emplace_back(value.substr(start, stop == std::string::npos ? stop : stop - start));
        if (stop == std::string::npos)
        {
            return output;
        }
        start = stop + 1;
    }
}

bool tokenize(const std::string &line, Tokens &tokens)
{
    size_t pos = 0;
    while (pos < line.size())
    {
        while (pos < line.size() && space(line[pos]))
        {
            ++pos;
        }
        if (pos == line.size() || line[pos] == '#' || line.compare(pos, 2, "//") == 0)
        {
            break;
        }
        std::string word;
        if (line[pos] == '\'' || line[pos] == '"')
        {
            const char delimiter = line[pos++];
            while (pos < line.size() && line[pos] != delimiter)
            {
                if (line[pos] == '\\' && pos + 1 < line.size() &&
                    (line[pos + 1] == delimiter || line[pos + 1] == '\\'))
                {
                    ++pos;
                }
                word += line[pos++];
            }
            if (pos >= line.size())
            {
                return false;
            }
            ++pos;
            if (pos < line.size() && !space(line[pos]))
            {
                return false;
            }
        }
        else
        {
            const auto start = pos;
            while (pos < line.size() && !space(line[pos]))
            {
                ++pos;
            }
            word = line.substr(start, pos - start);
        }
        tokens.push_back(std::move(word));
    }
    return true;
}

std::string milliseconds(std::string value)
{
    // The .NET TimeSpan used by PauseAction is [days.]hh:mm:ss[.fraction].
    const auto parts = split_fields(value, ':');
    if (parts.size() != 3)
    {
        return {};
    }
    auto hours = std::string(parts[0]);
    const auto daysEnd = hours.find('.');
    int64_t days = 0;
    if (daysEnd != std::string::npos)
    {
        if (!integer(hours.substr(0, daysEnd), days, 0, 24))
        {
            return {};
        }
        hours.erase(0, daysEnd + 1);
    }
    int64_t h = 0, m = 0, s = 0, ms = 0;
    const auto seconds = split_fields(parts[2], '.');
    if (seconds.size() > 2 || !integer(hours, h, 0, 23) ||
        !integer(parts[1], m, 0, 59) || !integer(seconds[0], s, 0, 59))
    {
        return {};
    }
    if (seconds.size() == 2)
    {
        if (seconds[1].empty() || seconds[1].size() > 7 ||
            seconds[1].find_first_not_of("0123456789") != std::string::npos)
        {
            return {};
        }
        auto fraction = seconds[1].substr(0, 3);
        fraction.resize(3, '0');
        if (!integer(fraction, ms, 0, 999))
        {
            return {};
        }
    }
    return number(std::to_string((((days * 24 + h) * 60 + m) * 60 + s) * 1000 + ms), 0, INT32_MAX);
}

std::string seconds_to_milliseconds(std::string value)
{
    const auto parts = split_fields(value, '.');
    int64_t seconds = 0, fraction = 0;
    if (parts.size() > 2 || !integer(parts[0], seconds, 0, INT32_MAX / 1000))
    {
        return {};
    }
    if (parts.size() == 2)
    {
        if (parts[1].empty() || parts[1].find_first_not_of("0123456789") != std::string::npos)
        {
            return {};
        }
        auto digits = parts[1].substr(0, 3);
        digits.resize(3, '0');
        if (!integer(digits, fraction, 0, 999))
        {
            return {};
        }
    }
    return number(std::to_string(seconds * 1000 + fraction), 0, INT32_MAX);
}

std::string call(const char *method, const std::string &arguments = {})
{
    return std::string("Client.") + method + "(" + arguments + ")";
}

std::string serial(std::string value)
{
    const auto alias = lower(value);
    if (alias == "last")
    {
        return quote("lasttarget");
    }
    if (alias == "self" || alias == "backpack" || alias == "lasttarget" ||
        alias == "lastattack" || alias == "lastobject" || alias == "lastcontainer" ||
        alias == "enemy" || alias == "friend")
    {
        return quote(alias);
    }
    const auto numeric = number(value, 1);
    if (!numeric.empty())
    {
        return numeric;
    }
    if (alias == "found")
    {
        return "(razorFound || 0)";
    }
    if (value.empty() || value.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _$-.") != std::string::npos)
    {
        return {};
    }
    return "razorValue(" + quote(value) + ")";
}

std::string name_or_number(std::string value)
{
    const auto numeric = number(value);
    return numeric.empty() ? quote(value) : numeric;
}

std::string direction(std::string value)
{
    const auto input = lower(value);
    const std::string DIRECTIONS[] = { "north", "northeast", "east", "southeast",
        "south", "southwest", "west", "northwest" };
    const std::string SHORT[] = { "n", "ne", "e", "se", "s", "sw", "w", "nw" };
    const std::string DIAGONAL[] = { "", "right", "", "down", "", "left", "", "up" };
    for (size_t i = 0; i < 8; ++i)
    {
        if (input == DIRECTIONS[i] || input == SHORT[i] || (!input.empty() && input == DIAGONAL[i]))
        {
            return std::to_string(i);
        }
    }
    return number(input, 0, 7);
}

// Razor's optional true/false searches include the pack and nearby world objects.
// The native finder handles each container separately.
std::string find_item(const std::string &item, const std::string &scope, const std::string &hue)
{
    const auto location = lower(scope);
    const auto args = name_or_number(item) + ", " + hue;
    const auto backpack = call("FindType", args + ", \"backpack\", \"fast\"") + "[0]";
    if (location == "backpack")
    {
        return backpack;
    }
    if (location == "true" || location == "false" || location == "ground")
    {
        const auto ground = call("FindType", args + ", \"ground\", \"nearest\", " +
            (location == "true" ? "2" : "31")) + "[0]";
        return location == "ground" ? ground : "(" + backpack + " || " + ground + ")";
    }
    const auto container = serial(scope);
    return container.empty() ? std::string() :
        call("FindType", args + ", " + container + ", \"fast\"") + "[0]";
}

bool comparison(std::string input)
{
    return input == "=" || input == "==" || input == "!=" || input == "<=" || input == ">=" ||
        input == "<" || input == ">";
}

// Source IDs and localized names are in Classic Razor's English language
// file. Native action numbers follow the fixed lists in core/actions.cpp.
constexpr RazorHotkeyDefinition RAZOR_HOTKEYS[] = {
    { 1004, "Stop Current Macro", RazorHotkeyGroup::StopMacro, 0 },
    { 1025, "Bandage Self", RazorHotkeyGroup::BandageHealing, 0 },
    { 1026, "Bandage LastTarget", RazorHotkeyGroup::BandageHealing, 2 },
    { 1027, "Bandage Friend", RazorHotkeyGroup::BandageHealing, 1 },
    { 1028, "Drink Heal", RazorHotkeyGroup::UseObject, 3 },
    { 1029, "Drink Cure", RazorHotkeyGroup::UseObject, 4 },
    { 1030, "Drink Refresh", RazorHotkeyGroup::UseObject, 5 },
    { 1031, "Drink Night Sight", RazorHotkeyGroup::UseObject, 8 },
    { 1032, "Drink Explosion", RazorHotkeyGroup::UseObject, 9 },
    { 1033, "Drink Strength", RazorHotkeyGroup::UseObject, 6 },
    { 1034, "Drink Agility", RazorHotkeyGroup::UseObject, 7 },
    { 1039, "Undress All", RazorHotkeyGroup::ClientAction, 1 },
    { 1040, "Undress Both Hands", RazorHotkeyGroup::ClientAction, 2 },
    { 1041, "Undress Left Hand", RazorHotkeyGroup::ClientAction, 3 },
    { 1042, "Undress Right Hand", RazorHotkeyGroup::ClientAction, 4 },
    { 1043, "Arm/Disarm Right Hand", RazorHotkeyGroup::ClientAction, 5 },
    { 1044, "Toggle Hotkeys", RazorHotkeyGroup::ToggleHotkeys, 0 },
    { 1045, "Arm/Disarm Left Hand", RazorHotkeyGroup::ClientAction, 6 },
    { 1046, "Toggle Map", RazorHotkeyGroup::ToggleMap, 0 },
    { 1055, "Toggle War/Peace", RazorHotkeyGroup::ClientAction, 0 },
    { 1058, "Last Target", RazorHotkeyGroup::Targeting, 1 },
    { 1059, "Target Self", RazorHotkeyGroup::Targeting, 0 },
    { 1195, "Clear Target Queue", RazorHotkeyGroup::Targeting, 6 },
    { 1196, "Attack Last Combatant", RazorHotkeyGroup::Attack, 0 },
    { 1311, "Target Closest Grey", RazorHotkeyGroup::NewTargetSystem, 7 },
    { 1312, "Target Closest Criminal", RazorHotkeyGroup::NewTargetSystem, 7 },
    { 1313, "Target Closest Murderer", RazorHotkeyGroup::NewTargetSystem, 7 },
    { 1314, "Target Closest Enemy", RazorHotkeyGroup::NewTargetSystem, 7 },
    { 1315, "Target Closest Friend", RazorHotkeyGroup::NewTargetSystem, 13 },
    { 1316, "Target Closest Non-Friendly", RazorHotkeyGroup::NewTargetSystem, 7 },
    { 1317, "Target Next Grey", RazorHotkeyGroup::NewTargetSystem, 5 },
    { 1318, "Target Next Criminal", RazorHotkeyGroup::NewTargetSystem, 5 },
    { 1319, "Target Next Murderer", RazorHotkeyGroup::NewTargetSystem, 5 },
    { 1320, "Target Next Enemy", RazorHotkeyGroup::NewTargetSystem, 5 },
    { 1321, "Target Next Friend", RazorHotkeyGroup::NewTargetSystem, 11 },
    { 1322, "Target Next Non-Friendly", RazorHotkeyGroup::NewTargetSystem, 5 },
    { 1325, "Target Closest Innocent", RazorHotkeyGroup::NewTargetSystem, 10 },
    { 1326, "Target Next Innocent", RazorHotkeyGroup::NewTargetSystem, 8 },
    { 1369, "Last Skill", RazorHotkeyGroup::UseSkill, 0 },
    { 1370, "Last Spell", RazorHotkeyGroup::CastSpell, 0 },
    { 1371, "Last Object", RazorHotkeyGroup::UseObject, 0 },
    { 1372, "All Names", RazorHotkeyGroup::AllNames, 0 },
    { 1380, "UseOnce Agent", RazorHotkeyGroup::InventoryAgent, 2 },
    { 1381, "Scavenger Agent", RazorHotkeyGroup::InventoryAgent, 3 },
    { 1382, "Organizer Agent", RazorHotkeyGroup::InventoryAgent, 4 },
    { 1383, "Restock Agent", RazorHotkeyGroup::InventoryAgent, 5 },
    { 1394, "Cancel Current Target", RazorHotkeyGroup::Targeting, 6 },
    { 1395, "Attack Last Target", RazorHotkeyGroup::Attack, 1 },
    { 1396, "Set Last Target", RazorHotkeyGroup::NewTargetSystem, 1 },
    { 1397, "Clear Scavenger Cache", RazorHotkeyGroup::InventoryAgent, 1 },
    { 1398, "Stop Inventory Agents", RazorHotkeyGroup::InventoryAgent, 0 },
    { 1969, "All Corpses", RazorHotkeyGroup::AllNames, 3 },
    { 1970, "All Mobiles", RazorHotkeyGroup::AllNames, 1 },
    { 1971, "All Humanoids", RazorHotkeyGroup::AllNames, 2 },
    { 2069, "Use Item in Right Hand", RazorHotkeyGroup::UseObject, 1 },
    { 2070, "Use Item in Left Hand", RazorHotkeyGroup::UseObject, 2 },
    { 3001, "Clumsy", RazorHotkeyGroup::CastSpell, 1 },
    { 3002, "Create Food", RazorHotkeyGroup::CastSpell, 2 },
    { 3003, "Feeblemind", RazorHotkeyGroup::CastSpell, 3 },
    { 3004, "Heal", RazorHotkeyGroup::CastSpell, 4 },
    { 3005, "Magic Arrow", RazorHotkeyGroup::CastSpell, 5 },
    { 3006, "Night Sight", RazorHotkeyGroup::CastSpell, 6 },
    { 3007, "Reactive Armor", RazorHotkeyGroup::CastSpell, 7 },
    { 3008, "Weaken", RazorHotkeyGroup::CastSpell, 8 },
    { 3009, "Agility", RazorHotkeyGroup::CastSpell, 9 },
    { 3010, "Cunning", RazorHotkeyGroup::CastSpell, 10 },
    { 3011, "Cure", RazorHotkeyGroup::CastSpell, 11 },
    { 3012, "Harm", RazorHotkeyGroup::CastSpell, 12 },
    { 3013, "Magic Trap", RazorHotkeyGroup::CastSpell, 13 },
    { 3014, "Magic Untrap", RazorHotkeyGroup::CastSpell, 14 },
    { 3015, "Protection", RazorHotkeyGroup::CastSpell, 15 },
    { 3016, "Strength", RazorHotkeyGroup::CastSpell, 16 },
    { 3017, "Bless", RazorHotkeyGroup::CastSpell, 17 },
    { 3018, "Fireball", RazorHotkeyGroup::CastSpell, 18 },
    { 3019, "Magic Lock", RazorHotkeyGroup::CastSpell, 19 },
    { 3020, "Poison", RazorHotkeyGroup::CastSpell, 20 },
    { 3021, "Telekinesis", RazorHotkeyGroup::CastSpell, 21 },
    { 3022, "Teleport", RazorHotkeyGroup::CastSpell, 22 },
    { 3023, "Unlock", RazorHotkeyGroup::CastSpell, 23 },
    { 3024, "Wall of Stone", RazorHotkeyGroup::CastSpell, 24 },
    { 3025, "Arch Cure", RazorHotkeyGroup::CastSpell, 25 },
    { 3026, "Arch Protection", RazorHotkeyGroup::CastSpell, 26 },
    { 3027, "Curse", RazorHotkeyGroup::CastSpell, 27 },
    { 3028, "Fire Field", RazorHotkeyGroup::CastSpell, 28 },
    { 3029, "Greater Heal", RazorHotkeyGroup::CastSpell, 29 },
    { 3030, "Lightning", RazorHotkeyGroup::CastSpell, 30 },
    { 3031, "Mana Drain", RazorHotkeyGroup::CastSpell, 31 },
    { 3032, "Recall", RazorHotkeyGroup::CastSpell, 32 },
    { 3033, "Blade Spirits", RazorHotkeyGroup::CastSpell, 33 },
    { 3034, "Dispel Field", RazorHotkeyGroup::CastSpell, 34 },
    { 3035, "Incognito", RazorHotkeyGroup::CastSpell, 35 },
    { 3036, "Magic Reflection", RazorHotkeyGroup::CastSpell, 36 },
    { 3037, "Mind Blast", RazorHotkeyGroup::CastSpell, 37 },
    { 3038, "Paralyze", RazorHotkeyGroup::CastSpell, 38 },
    { 3039, "Poison Field", RazorHotkeyGroup::CastSpell, 39 },
    { 3040, "Summon Creature", RazorHotkeyGroup::CastSpell, 40 },
    { 3041, "Dispel", RazorHotkeyGroup::CastSpell, 41 },
    { 3042, "Energy Bolt", RazorHotkeyGroup::CastSpell, 42 },
    { 3043, "Explosion", RazorHotkeyGroup::CastSpell, 43 },
    { 3044, "Invisibility", RazorHotkeyGroup::CastSpell, 44 },
    { 3045, "Mark", RazorHotkeyGroup::CastSpell, 45 },
    { 3046, "Mass Curse", RazorHotkeyGroup::CastSpell, 46 },
    { 3047, "Paralyze Field", RazorHotkeyGroup::CastSpell, 47 },
    { 3048, "Reveal", RazorHotkeyGroup::CastSpell, 48 },
    { 3049, "Chain Lightning", RazorHotkeyGroup::CastSpell, 49 },
    { 3050, "Energy Field", RazorHotkeyGroup::CastSpell, 50 },
    { 3051, "Flame Strike", RazorHotkeyGroup::CastSpell, 51 },
    { 3052, "Gate Travel", RazorHotkeyGroup::CastSpell, 52 },
    { 3053, "Mana Vampire", RazorHotkeyGroup::CastSpell, 53 },
    { 3054, "Mass Dispel", RazorHotkeyGroup::CastSpell, 54 },
    { 3055, "Meteor Swarm", RazorHotkeyGroup::CastSpell, 55 },
    { 3056, "Polymorph", RazorHotkeyGroup::CastSpell, 56 },
    { 3057, "Earthquake", RazorHotkeyGroup::CastSpell, 57 },
    { 3058, "Energy Vortex", RazorHotkeyGroup::CastSpell, 58 },
    { 3059, "Resurrection", RazorHotkeyGroup::CastSpell, 59 },
    { 3060, "Summon Air Elemental", RazorHotkeyGroup::CastSpell, 60 },
    { 3061, "Summon Daemon", RazorHotkeyGroup::CastSpell, 61 },
    { 3062, "Summon Earth Elemental", RazorHotkeyGroup::CastSpell, 62 },
    { 3063, "Summon Fire Elemental", RazorHotkeyGroup::CastSpell, 63 },
    { 3064, "Summon Water Elemental", RazorHotkeyGroup::CastSpell, 64 },
};

class Translator
{
public:
    explicit Translator(RazorImportResult &result)
        : m_result(result)
    {
    }

    void set_line(size_t line) { m_line = line; }

    void issue(std::string message) { m_result.Issues.push_back({ m_line, std::move(message) }); }

    void write(const std::string &line)
    {
        m_body.append(m_blocks.size() * 4, ' ');
        m_body += line + '\n';
    }

    void code(const std::string &line)
    {
        write(line);
        ++m_result.Imported;
    }

    bool open(std::string kind, const std::string &expression)
    {
        if (m_blocks.size() >= MAX_NESTING)
        {
            return false;
        }
        code(expression + " {");
        m_blocks.push_back({ std::move(kind), m_line, false });
        return true;
    }

    bool close(const char *kind)
    {
        if (m_blocks.empty() || m_blocks.back().Kind != kind)
        {
            return false;
        }
        m_blocks.pop_back();
        write("}");
        return true;
    }

    void loop()
    {
        if (m_result.Imported || m_loop)
        {
            issue("The !Loop marker must appear once, before the macro actions.");
            return;
        }
        m_loop = open("macro", "while (true)");
        // The marker controls playback. It is not itself a playable action.
        --m_result.Imported;
    }

    void finish(const char *sourceLabel = "Razor")
    {
        if (!m_liftSource.empty())
        {
            m_result.Issues.push_back({ m_liftLine, "A lift needs an immediately following drop." });
        }
        if (m_loop && m_blocks.size() == 1)
        {
            write("Client.Wait(20);");
            close("macro");
        }
        for (const auto &block : m_blocks)
        {
            m_result.Issues.push_back({ block.Line, "Unclosed " + block.Kind + " block." });
        }
        if (m_result.Imported != 0)
        {
            m_result.JavaScript =
                std::string("// Converted from ") + sourceLabel + ". Review item bindings before playing.\n"
                "(() => {\n"
                "let razorFound = null;\n"
                "const razorTemp = Object.create(null);\n"
                "function razorValue(key) {\n"
                "  if (key === \"found\") return razorFound || 0;\n"
                "  return razorTemp[key] ?? (Client.GetGlobal(\"razor.var.\" + key) || key);\n"
                "}\n";
            if (m_usesLists)
            {
                m_result.JavaScript +=
                    "const razorLists = Object.create(null);\n"
                    "function razorPopList(key, value) {\n"
                    "  const list = razorLists[key];\n"
                    "  if (!list || list.length === 0) return false;\n"
                    "  const index = value === \"front\" ? 0 : value === \"back\" ? list.length - 1 : list.indexOf(value);\n"
                    "  if (index < 0) return false;\n"
                    "  list.splice(index, 1);\n"
                    "  return true;\n"
                    "}\n";
            }
            if (m_usesWait)
            {
                m_result.JavaScript +=
                    "function waitRazor(check, timeout) {\n"
                    "  const end = Date.now() + timeout;\n"
                    "  do {\n"
                    "    if (check()) return true;\n"
                    "    if (Date.now() >= end) break;\n"
                    "    Client.Wait(20);\n"
                    "  } while (Date.now() <= end);\n"
                    "  return false;\n"
                    "}\n";
            }
            if (m_usesMenu)
            {
                m_result.JavaScript +=
                    "function findRazorMenu(id) {\n"
                    "  for (let i = Client.MenuCount() - 1; i >= 0; --i) {\n"
                    "    const menu = Client.GetMenu(i);\n"
                    "    if (Number(menu.Serial()) && Number(menu.ID()) === id) return menu;\n"
                    "  }\n"
                    "  return null;\n"
                    "}\n";
            }
            if (m_usesGump)
            {
                m_result.JavaScript +=
                    "let razorGump;\n"
                    "function waitRazorGump(id, timeout) {\n"
                    "  const end = Date.now() + timeout;\n"
                    "  do {\n"
                    "    if (!id) { const g = Client.GetLastGump(); if (g) return g; }\n"
                    "    else for (let i = Client.GumpCount() - 1; i >= 0; --i) {\n"
                    "      const g = Client.GetGump(i); if (g && (g.ID() >>> 0) === id) return g;\n"
                    "    }\n"
                    "    if (Date.now() >= end) break;\n"
                    "    Client.Wait(20);\n"
                    "  } while (Date.now() <= end);\n"
                    "  return null;\n"
                    "}\n";
            }
            if (m_usesReplay)
            {
                m_result.JavaScript += "razorReplay: while (true) {\n" + m_body + "break;\n}\n";
            }
            else
            {
                m_result.JavaScript += m_body;
            }
            m_result.JavaScript += "})();\n";
        }
    }

    void script(const Tokens &tokens);
    void macro(const Tokens &parts);
    void uosteam(const Tokens &tokens);

private:
    struct Block
    {
        std::string Kind;
        size_t Line;
        bool Else;
    };
    RazorImportResult &m_result;
    std::vector<Block> m_blocks;
    std::string m_body;
    size_t m_line = 0;
    bool m_loop = false;
    bool m_usesGump = false;
    bool m_usesWait = false;
    bool m_usesMenu = false;
    bool m_usesReplay = false;
    bool m_usesLists = false;
    size_t m_liftLine = 0;
    std::string m_liftSource;
    std::string m_liftAmount;

    std::string operand(const Tokens &tokens, size_t &pos);
    std::string predicate(const Tokens &tokens, size_t start);
    bool structure(const std::string &command, const Tokens &tokens);
    std::string action(const Tokens &tokens);
    std::string uosteam_hue(const std::string &token);
    std::string uosteam_findtype(const Tokens &tokens, size_t &pos);
    std::string uosteam_operand(const Tokens &tokens, size_t &pos);
    std::string uosteam_predicate(const Tokens &tokens, size_t start);
    bool uosteam_structure(const std::string &command, const Tokens &tokens);
    std::string uosteam_action(const Tokens &tokens);
};

std::string Translator::operand(const Tokens &tokens, size_t &pos)
{
    if (pos >= tokens.size())
    {
        return {};
    }
    const auto &raw = tokens[pos++];
    const auto token = lower(raw);
    struct Stat
    {
        const char *Name;
        const char *JavaScript;
    };
    constexpr Stat STATS[] = {
        { "hits", "Player.HitsValue()" }, { "maxhits", "Player.MaxHits()" },
        { "hp", "Player.HitsValue()" }, { "maxhp", "Player.MaxHits()" },
        { "mana", "Player.ManaValue()" }, { "maxmana", "Player.MaxMana()" },
        { "stam", "Player.StamValue()" }, { "stamina", "Player.StamValue()" },
        { "maxstam", "Player.MaxStam()" }, { "weight", "Player.WeightValue()" },
        { "maxweight", "Player.MaxWeight()" }, { "gold", "Player.Gold()" },
        { "str", "Player.Str()" }, { "dex", "Player.Dex()" }, { "int", "Player.Int()" },
        { "poisoned", "Player.Poisoned()" }, { "dead", "Player.Dead()" },
        { "hidden", "Player.Hidden()" }, { "frozen", "Player.Frozen()" },
        { "paralyzed", "Player.Frozen()" }, { "warmode", "Player.WarMode()" },
        { "followers", "Player.Followers()" }, { "maxfollowers", "Player.MaxFollowers()" },
        { "diffhits", "Player.MaxHits() - Player.HitsValue()" },
        { "diffhp", "Player.MaxHits() - Player.HitsValue()" },
        { "diffweight", "Player.MaxWeight() - Player.WeightValue()" },
        { "lhandempty", "!Client.ObjAtLayer(2)" }, { "rhandempty", "!Client.ObjAtLayer(1)" },
        { "diffmana", "Player.MaxMana() - Player.ManaValue()" },
        { "diffstam", "Player.MaxStam() - Player.StamValue()" },
        { "targetexists", "Client.HaveTarget()" }, { "connected", "Client.Connected()" },
        { "queued", "Client.InventoryAgentBusy()" },
    };
    for (const auto &field : STATS)
    {
        if (field.Name == token)
        {
            return field.JavaScript;
        }
    }
    if (token == "list" || token == "listexists" || token == "inlist" || token == "poplist")
    {
        if (pos >= tokens.size())
        {
            return {};
        }
        m_usesLists = true;
        const auto key = quote(tokens[pos++]);
        const auto list = "razorLists[" + key + "]";
        if (token == "list") return "(" + list + "?.length ?? 0)";
        if (token == "listexists") return "(" + list + " !== undefined)";
        if (pos >= tokens.size())
        {
            return {};
        }
        const auto value = quote(tokens[pos++]);
        return token == "inlist" ? "(" + list + "?.includes(" + value + ") ?? false)" :
            "razorPopList(" + key + ", " + value + ")";
    }
    if (token == "varexist" && pos < tokens.size())
    {
        const auto name = quote(tokens[pos++]);
        return "(razorTemp[" + name + "] !== undefined || "
            + call("GetGlobal", "\"razor.var.\" + " + name) + " !== \"\")";
    }
    if (token == "findtype")
    {
        if (pos >= tokens.size()) return {};
        const auto &item = tokens[pos++];
        std::string scope = "false";
        std::string hue = "65535";
        if (pos < tokens.size())
        {
            const auto next = lower(tokens[pos]);
            if (next == "true" || next == "false" || next == "backpack" || next == "ground")
            {
                scope = next;
                ++pos;
                if (pos < tokens.size())
                {
                    const auto parsedHue = number(tokens[pos], 0, UINT16_MAX);
                    if (!parsedHue.empty())
                    {
                        hue = parsedHue;
                        ++pos;
                    }
                }
            }
        }
        const auto found = find_item(item, scope, hue);
        std::string result = "(razorFound = " + found + " || null)";
        if (pos + 1 < tokens.size() && lower(tokens[pos]) == "as")
        {
            const auto name = quote(tokens[pos + 1]);
            pos += 2;
            result = "(razorTemp[" + name + "] = " + result + ")";
        }
        return "(" + result + " !== null)";
    }
    struct ValueCall
    {
        const char *Name;
        const char *Method;
        bool Numeric;
    };
    constexpr ValueCall CALLS[] = {
        { "count", "Count", true }, { "skill", "SkillValue", false },
        { "insysmsg", "InJournal", false }, { "insysmessage", "InJournal", false },
        { "injournal", "InJournal", false },
        { "timer", "Timer", false }, { "timerexists", "TimerExists", false },
        { "buffexists", "BuffExists", false }, { "findbuff", "BuffExists", false },
        { "findobject", "FindObject", true },
    };
    for (const auto &entry : CALLS)
    {
        if (entry.Name == token)
        {
            if (pos >= tokens.size())
            {
                return {};
            }
            const auto &argument = tokens[pos++];
            auto value = entry.Numeric ? number(argument) : quote(argument);
            if (token == "count" && value.empty())
            {
                value = quote(argument);
            }
            if (token == "findobject" && value.empty())
            {
                value = serial(argument);
            }
            if (value.empty())
            {
                return {};
            }
            if (token == "count")
            {
                value += ", 0xffff, \"backpack\"";
            }
            if (token == "insysmsg" || token == "insysmessage")
            {
                value += ", \"sys\"";
            }
            const auto expression = call(entry.Method, value);
            if (token == "findobject")
            {
                return "((razorFound = " + expression + "?.Serial() || null) !== null)";
            }
            if (token == "insysmsg" || token == "insysmessage" || token == "injournal")
            {
                return "Boolean(" + expression + ")";
            }
            return expression;
        }
    }
    bool truth = false;
    if ((token == "true" || token == "false") && boolean(token, truth))
    {
        return truth ? "true" : "false";
    }
    return number(raw, INT32_MIN);
}

std::string Translator::predicate(const Tokens &tokens, size_t pos)
{
    std::string output;
    while (pos < tokens.size())
    {
        const bool negate = lower(tokens[pos]) == "not";
        if (negate)
        {
            ++pos;
        }
        auto left = operand(tokens, pos);
        if (left.empty())
        {
            return {};
        }
        if (pos < tokens.size() && comparison(tokens[pos]))
        {
            const auto op = tokens[pos++];
            const auto right = operand(tokens, pos);
            if (right.empty())
            {
                return {};
            }
            const auto jsOp = op == "=" || op == "==" ? "===" : op == "!=" ? "!==" : op;
            left += " " + jsOp + " " + right;
        }
        output += (negate ? "!(" : "(") + left + ")";
        if (pos == tokens.size())
        {
            return output;
        }
        const auto logical = lower(tokens[pos++]);
        if (pos == tokens.size() || (logical != "and" && logical != "or"))
        {
            return {};
        }
        output += logical == "and" ? " && " : " || ";
    }
    return {};
}

bool Translator::structure(const std::string &command, const Tokens &tokens)
{
    if (command == "for" && tokens.size() == 2)
    {
        const auto count = number(tokens[1], 0, INT32_MAX);
        const auto var = "razorLoop" + std::to_string(m_blocks.size());
        return !count.empty() && open("for", "for (let " + var + " = 0; " + var + " < " +
            count + "; ++" + var + ")");
    }
    if (command == "foreach" && tokens.size() == 4 && lower(tokens[2]) == "in")
    {
        m_usesLists = true;
        const auto item = "razorEach" + std::to_string(m_blocks.size());
        if (!open("for", "for (const " + item + " of razorLists[" + quote(tokens[3]) + "] || [])"))
        {
            return false;
        }
        write("razorTemp[" + quote(tokens[1]) + "] = " + item + ";");
        return true;
    }
    if (command == "if" || command == "while" || command == "elseif")
    {
        const auto value = predicate(tokens, 1);
        if (value.empty())
        {
            return false;
        }
        if (command == "elseif")
        {
            if (m_blocks.empty() || m_blocks.back().Kind != "if" || m_blocks.back().Else)
            {
                return false;
            }
            auto block = std::move(m_blocks.back());
            m_blocks.pop_back();
            write("} else if (" + value + ") {");
            m_blocks.push_back(std::move(block));
            return true;
        }
        if (!open(command, command + " (" + value + ")"))
        {
            return false;
        }
        if (command == "while")
        {
            write("Client.Wait(20);");
        }
        return true;
    }
    if (command == "else" && tokens.size() == 1 && !m_blocks.empty() &&
        m_blocks.back().Kind == "if" && !m_blocks.back().Else)
    {
        auto block = std::move(m_blocks.back());
        m_blocks.pop_back();
        write("} else {");
        block.Else = true;
        m_blocks.push_back(std::move(block));
        return true;
    }
    if (tokens.size() == 1 && command == "endif")
    {
        return close("if");
    }
    if (tokens.size() == 1 && command == "endfor")
    {
        return close("for");
    }
    if (tokens.size() == 1 && command == "endwhile")
    {
        return close("while");
    }
    if (tokens.size() == 1 && (command == "break" || command == "continue"))
    {
        if (std::any_of(m_blocks.begin(), m_blocks.end(), [](const Block &block) {
                return block.Kind == "for" || block.Kind == "while" || block.Kind == "macro";
            }))
        {
            code(command + ";");
            return true;
        }
    }
    return false;
}

std::string Translator::action(const Tokens &tokens)
{
    const auto command = command_name(tokens[0]);
    enum class Argument { None, Text, Number, Serial, NameOrNumber };
    struct Mapping { const char *Razor; const char *Client; Argument Type; };
    constexpr Mapping SIMPLE[] = {
        { "clearsysmsg", "ClearJournal", Argument::None },
        { "clearjournal", "ClearJournal", Argument::None },
        { "canceltarget", "CancelTarget", Argument::None },
        { "cancelwaittarget", "CancelWaitTarget", Argument::None },
        { "snapshot", "Screenshot", Argument::None },
        { "disarm", "Disarm", Argument::None },
        { "bandageself", "BandageSelf", Argument::None },
        { "resync", "Resend", Argument::None },
        { "removetimer", "RemoveTimer", Argument::Text },
        { "dress", "Dress", Argument::Text },
        { "undress", "Undress", Argument::Text },
        { "arm", "Arm", Argument::Text },
        { "waitfortarget", "WaitForTarget", Argument::Number },
        { "wft", "WaitForTarget", Argument::Number },
        { "waitforshop", "WaitForShop", Argument::Number },
        { "pause", "Wait", Argument::Number },
        { "wait", "Wait", Argument::Number },
        { "click", "Click", Argument::Serial },
        { "attack", "Attack", Argument::Serial },
        { "bandage", "BandageTarget", Argument::Serial },
        { "equip", "Equip", Argument::Serial },
        { "contextmenu", "RequestContextMenu", Argument::Serial },
        { "skill", "UseSkill", Argument::NameOrNumber },
        { "useskill", "UseSkill", Argument::NameOrNumber },
        { "cast", "Cast", Argument::NameOrNumber },
        { "virtue", "InvokeVirtue", Argument::Text },
        { "createtimer", "SetTimer", Argument::Text },
        { "promptresponse", "SendPrompt", Argument::Text },
        { "clearignore", "IgnoreReset", Argument::None },
        { "ignore", "Ignore", Argument::Serial },
    };
    for (const auto &entry : SIMPLE)
    {
        if (entry.Razor != command ||
            ((command == "wait" || command == "pause") && tokens.size() == 3))
        {
            continue;
        }
        if (entry.Type == Argument::None)
        {
            return tokens.size() == 1 ? call(entry.Client) : std::string();
        }
        if (tokens.size() == 1 && (command == "waitfortarget" || command == "wft"))
        {
            return call(entry.Client, "30000");
        }
        if (tokens.size() == 1 && (command == "waitforshop" || command == "undress"))
        {
            return call(entry.Client);
        }
        if (tokens.size() != 2)
        {
            return {};
        }
        std::string value;
        switch (entry.Type)
        {
            case Argument::Text: value = quote(tokens[1]); break;
            case Argument::Number: value = number(tokens[1], 0, INT32_MAX); break;
            case Argument::Serial: value = serial(tokens[1]); break;
            case Argument::NameOrNumber: value = name_or_number(tokens[1]); break;
            case Argument::None: break;
        }
        return value.empty() ? std::string() : call(entry.Client, value);
    }

    if (command == "hotkey" && tokens.size() == 2)
    {
        const auto *binding = FindRazorHotkey(tokens[1]);
        return binding != nullptr ? call("ExecuteRazorHotkey", std::to_string(binding->Id)) : std::string();
    }
    if (command == "clearhands" && tokens.size() == 2)
    {
        const auto hand = lower(tokens[1]);
        if (hand == "left") return call("Unequip", "2");
        if (hand == "right") return call("Unequip", "1");
        if (hand == "both") return call("Unequip", "1") + "; " + call("Unequip", "2");
        return {};
    }
    if (command == "dclick" && tokens.size() == 2)
    {
        const auto location = lower(tokens[1]);
        if (location == "right" || location == "left" || location == "hands")
        {
            const std::string item = location == "right" ? "Client.ObjAtLayer(1)" :
                location == "left" ? "Client.ObjAtLayer(2)" :
                "Client.ObjAtLayer(1) || Client.ObjAtLayer(2)";
            return "{ const item = " + item + "; if (item) item.UseObject(); }";
        }
        const auto target = serial(tokens[1]);
        return target.empty() ? std::string() : call("UseObject", target);
    }
    if (command == "lasttarget" && tokens.size() == 1)
    {
        return call("TargetObject", quote("lasttarget"));
    }
    if ((command == "target" || command == "setlasttarget") && tokens.size() == 2)
    {
        const auto target = lower(tokens[1]);
        if (command == "target" && target == "cancel") return call("CancelTarget");
        if (command == "target" && target == "clear") return call("CancelWaitTarget");
        if (command == "target" && (target == "closest" || target == "next" || target == "random"))
        {
            return {};
        }
        const auto item = serial(tokens[1]);
        return item.empty() ? std::string() :
            call(command == "target" ? "TargetObject" : "SetLastTarget", item);
    }
    if ((command == "wait" || command == "pause") && tokens.size() == 3)
    {
        const auto unit = lower(tokens[2]);
        int64_t duration = 0;
        const int64_t factor = unit == "s" || unit == "sec" || unit == "second" || unit == "seconds" ?
            1000 : unit == "m" || unit == "min" || unit == "minute" || unit == "minutes" ? 60000 : 0;
        return factor != 0 && integer(tokens[1], duration, 0, INT32_MAX / factor) ?
            call("Wait", std::to_string(duration * factor)) : std::string();
    }
    if ((command == "waitforprompt" || command == "waitformenu") && tokens.size() <= 3)
    {
        const auto type = tokens.size() >= 2 ? lower(tokens[1]) : "any";
        const auto timeout = tokens.size() == 3 ? number(tokens[2], 0, INT32_MAX) : "30000";
        if (timeout.empty()) return {};
        if (type == "any") return call(command == "waitforprompt" ? "WaitForPrompt" : "WaitForMenu", timeout);
        const auto id = number(type);
        if (id.empty()) return {};
        m_usesWait = true;
        if (command == "waitformenu")
        {
            m_usesMenu = true;
            return "waitRazor(() => findRazorMenu(" + id + "), " + timeout + ")";
        }
        return "waitRazor(() => Client.PromptExists() && Number(Client.PromptID()) === " + id + ", " +
            timeout + ")";
    }
    if ((command == "waitforsysmsg" || command == "wfsysmsg" || command == "waitjournal") &&
        tokens.size() >= 2 && tokens.size() <= 3)
    {
        const auto duration = tokens.size() == 3 ? number(tokens[2], 0, INT32_MAX) : "30000";
        if (duration.empty()) return {};
        m_usesWait = true;
        return "waitRazor(() => Client.InJournal(" + quote(tokens[1]) +
            (command == "waitjournal" ? "" : ", \"sys\"") + "), " + duration + ")";
    }
    if (command == "targetrelloc" && tokens.size() == 3)
    {
        const auto x = number(tokens[1], INT16_MIN, INT16_MAX);
        const auto y = number(tokens[2], INT16_MIN, INT16_MAX);
        return x.empty() || y.empty() ? std::string() :
            call("TargetTileRelative", quote("") + ", 0, 0, 0, " + x + ", " + y + ", 0");
    }
    if (command == "rename" && tokens.size() == 3)
    {
        const auto target = serial(tokens[1]);
        return target.empty() ? std::string() : call("RenameMount", target + ", " + quote(tokens[2]));
    }
    if (command == "sound" && tokens.size() == 2)
    {
        const auto id = number(tokens[1], 0, UINT16_MAX);
        if (!id.empty()) return call("Sound", id);
    }
    if ((command == "music" || command == "sound") && tokens.size() == 2 &&
        (lower(tokens[1]) == "on" || lower(tokens[1]) == "off"))
    {
        return call(command == "music" ? "SetOptionMusic" : "SetOptionSound",
            lower(tokens[1]) == "on" ? "true" : "false");
    }
    if (command == "menu" && tokens.size() == 3)
    {
        const auto target = tokens[1] == "0" ? quote("self") : serial(tokens[1]);
        const auto index = number(tokens[2], 0, UINT16_MAX);
        return target.empty() || index.empty() ? std::string() :
            call("WaitContextMenu", target + ", " + index) + "; " + call("RequestContextMenu", target);
    }
    if (command == "menuresponse" && (tokens.size() == 3 || tokens.size() == 4))
    {
        const auto index = number(tokens[1], 0, UINT16_MAX);
        const auto id = number(tokens[2], 0, UINT16_MAX);
        const auto hue = tokens.size() == 4 ? number(tokens[3], 0, UINT16_MAX) : std::string();
        if (index.empty() || id.empty() || (tokens.size() == 4 && hue.empty()))
        {
            return {};
        }
        m_usesMenu = true;
        if (index == "0")
        {
            return "{ const menu = findRazorMenu(" + id + "); if (menu) menu.Close(); }";
        }
        return "{ const menu = findRazorMenu(" + id + "); "
            "if (menu) for (let i = 0; i < menu.ItemsCount(); ++i) { "
            "if (menu.ItemID(i) === " + index +
            (hue.empty() ? "" : " && Number(menu.ItemColor(i)) === " + hue) +
            ") { menu.Select(i); break; } } }";
    }
    if (command == "random" && tokens.size() == 2)
    {
        const auto maximum = number(tokens[1], 1, INT32_MAX);
        return maximum.empty() ? std::string() :
            call("Print", quote("Random: ") + " + " + call("Random", "1, " + maximum));
    }
    if ((command == "classicuo" || command == "cuo") && tokens.size() == 3)
    {
        struct OptionSetting { const char *Razor; const char *Client; bool IsPercent; };
        constexpr OptionSetting OPTIONS[] = {
            { "enablemusic", "SetOptionMusic", false },
            { "enablesound", "SetOptionSound", false },
            { "musicvolume", "SetOptionMusicVolume", true },
            { "soundvolume", "SetOptionSoundVolume", true },
            { "alwaysrun", "SetOptionAlwaysRun", false },
        };
        const auto setting = lower(tokens[1]);
        for (const auto &option : OPTIONS)
        {
            if (setting != option.Razor) continue;
            if (option.IsPercent)
            {
                int64_t percent = 0;
                return integer(tokens[2], percent, 0, 100) ?
                    call(option.Client, std::to_string((percent * 255 + 50) / 100)) : std::string();
            }
            bool enabled = false;
            return boolean(tokens[2], enabled) ?
                call(option.Client, enabled ? "true" : "false") : std::string();
        }
        return {};
    }
    struct SpeechMapping { const char *Razor; const char *Client; };
    constexpr SpeechMapping SPEECH[] = {
        { "say", "Say" }, { "msg", "Say" }, { "yell", "SayYell" }, { "whisper", "SayWhisper" },
        { "emote", "SayEmote" }, { "guildmsg", "SayGuild" }, { "guild", "SayGuild" },
        { "partymsg", "SayParty" }, { "party", "SayParty" }, { "alliance", "SayAlliance" },
    };
    if (tokens.size() == 2 || tokens.size() == 3)
    {
        const auto text = "razorValue(" + quote(tokens[1]) + ")";
        const auto hue = tokens.size() == 3 ? number(tokens[2], 0, UINT16_MAX) : std::string();
        if (command == "sysmsg" && tokens.size() == 2) return call("Print", text);
        if (command == "overhead" && (tokens.size() == 2 || !hue.empty()))
        {
            return call("CharPrint", quote("self") + ", " +
                (hue.empty() ? quote("0xFFFF") : "(" + hue + ").toString(16)") + ", " + text);
        }
        for (const auto &speech : SPEECH)
        {
            if (command == speech.Razor && (tokens.size() == 2 || !hue.empty()))
            {
                return call(speech.Client, hue.empty() ? text : text + ", " + hue);
            }
        }
    }
    if ((command == "dclicktype" || command == "targettype") && tokens.size() >= 2 && tokens.size() <= 4)
    {
        const auto location = tokens.size() >= 3 ? tokens[2] : "false";
        const auto hue = tokens.size() == 4 ? number(tokens[3], 0, UINT16_MAX) : "65535";
        if (hue.empty())
        {
            return {};
        }
        const auto item = find_item(tokens[1], location, hue);
        const auto method = command == "dclicktype" ? "UseObject" : "TargetObject";
        return item.empty() ? std::string() :
            "{ const razorItem = " + item + "; if (razorItem) " + call(method, "razorItem") + "; }";
    }
    if ((command == "walk" || command == "run") && tokens.size() == 2)
    {
        auto facing = direction(tokens[1]);
        if (facing.empty())
        {
            return {};
        }
        return call("Step", facing + (command == "run" ? ", true" : ""));
    }
    if (command == "warmode" && tokens.size() <= 2)
    {
        if (tokens.size() == 1)
        {
            return call("WarMode");
        }
        const auto mode = lower(tokens[1]);
        return mode == "on" || mode == "off" ? call("WarMode", quote(mode)) : std::string();
    }
    if (command == "setability" && (tokens.size() == 2 ||
        (tokens.size() == 3 && lower(tokens[2]) == "on")))
    {
        const auto slot = lower(tokens[1]);
        return slot == "primary" || slot == "secondary" ?
            call("UseAbility", quote(slot)) : std::string();
    }
    if (command == "settimer" && tokens.size() == 3)
    {
        const auto elapsed = number(tokens[2], 0, INT32_MAX);
        return elapsed.empty() ? std::string() : call("SetTimer", quote(tokens[1]) + ", " + elapsed);
    }
    if ((command == "targettile" || command == "targetloc") && tokens.size() == 4)
    {
        const auto x = number(tokens[1], 0, UINT16_MAX);
        const auto y = number(tokens[2], 0, UINT16_MAX);
        const auto z = number(tokens[3], -128, 127);
        return x.empty() || y.empty() || z.empty() ? std::string() : call("TargetTile", quote("") + ", 0, 0, 0, " + x + ", " + y + ", " + z);
    }
    if ((command == "setvar" || command == "setvariable" || command == "setvar!") && tokens.size() == 3)
    {
        const auto value = serial(tokens[2]);
        if (value.empty())
        {
            return {};
        }
        if (command == "setvar!")
        {
            return "razorTemp[" + quote(tokens[1]) + "] = " + value;
        }
        return call("SetGlobal", quote("razor.var." + tokens[1]) + ", " + value);
    }
    if ((command == "unsetvar" || command == "unsetvar!") && tokens.size() == 2)
    {
        if (command == "unsetvar!")
        {
            return "delete razorTemp[" + quote(tokens[1]) + "]";
        }
        return call("SetGlobal", quote("razor.var." + tokens[1]) + ", \"\"");
    }
    if ((command == "createlist" || command == "clearlist" || command == "removelist") && tokens.size() == 2)
    {
        m_usesLists = true;
        const auto list = "razorLists[" + quote(tokens[1]) + "]";
        if (command == "createlist") return list + " ?" "?= []";
        if (command == "clearlist") return list + " = []";
        return "delete " + list;
    }
    if (command == "pushlist" && (tokens.size() == 3 || tokens.size() == 4))
    {
        const auto direction = tokens.size() == 4 ? lower(tokens[3]) : "back";
        if (direction != "front" && direction != "back")
        {
            return {};
        }
        m_usesLists = true;
        return "razorLists[" + quote(tokens[1]) + "]?." +
            (direction == "front" ? "unshift(" : "push(") + quote(tokens[2]) + ")";
    }
    if (command == "poplist" && tokens.size() == 3)
    {
        m_usesLists = true;
        return "razorPopList(" + quote(tokens[1]) + ", " + quote(tokens[2]) + ")";
    }
    if (command == "waitforgump" && tokens.size() >= 1 && tokens.size() <= 3)
    {
        const auto id = tokens.size() == 1 || lower(tokens[1]) == "any" ? "0" : number(tokens[1]);
        const auto timeout = tokens.size() == 3 ? number(tokens[2], 0, INT32_MAX) : "30000";
        if (id.empty() || timeout.empty())
        {
            return {};
        }
        m_usesGump = true;
        return "razorGump = waitRazorGump(" + id + ", " + timeout + ")";
    }
    if (command == "gumpclose" && (tokens.size() == 1 || tokens.size() == 2))
    {
        const auto id = tokens.size() == 2 ? number(tokens[1]) : "0";
        if (id.empty()) return {};
        m_usesGump = true;
        return "{ const g = waitRazorGump(" + id + ", 0); if (g) g.Close(); razorGump = undefined; }";
    }
    if (command == "gumpresponse" && tokens.size() == 2)
    {
        const auto button = number(tokens[1], 0, INT32_MAX);
        if (button.empty())
        {
            return {};
        }
        m_usesGump = true;
        return "{ const g = razorGump === undefined ? Client.GetLastGump() : razorGump; "
            "if (g) g.Select(Client.CreateGumpHook(" + button + ")); razorGump = undefined; }";
    }
    if ((command == "restock" || command == "organizer" || command == "scavenger") && tokens.size() <= 2)
    {
        const auto index = tokens.size() == 2 ? number(tokens[1], 1, INT32_MAX) : "1";
        return index.empty() ? std::string() : call("RunInventoryAgent", quote(command) + ", " + index);
    }
    if (command == "useonce" && tokens.size() == 1)
    {
        return call("RunInventoryAgent", "\"useonce\", 1");
    }
    return {};
}

void Translator::script(const Tokens &tokens)
{
    if (tokens.empty())
    {
        return;
    }
    const auto command = command_name(tokens[0]);
    if (!m_liftSource.empty())
    {
        auto item = std::move(m_liftSource);
        m_liftSource.clear();
        const auto args = item + ", " + m_liftAmount;
        if (command == "drop" && (tokens.size() == 2 || tokens.size() == 4 || tokens.size() == 5))
        {
            const auto container = serial(tokens[1]);
            if (!container.empty())
            {
                if (tokens.size() == 2)
                {
                    code(call("MoveItem", args + ", " + container) + ";");
                    return;
                }
                const auto x = number(tokens[2], -1, UINT16_MAX);
                const auto y = number(tokens[3], -1, UINT16_MAX);
                const auto z = tokens.size() == 5 ? number(tokens[4], -128, 127) : "0";
                if (!x.empty() && !y.empty() && !z.empty())
                {
                    if (lower(tokens[1]) == "ground")
                    {
                        code(call("Drop", args + ", " + x + ", " + y + ", " + z) + ";");
                    }
                    else
                    {
                        code(call("MoveItem", args + ", " + container + ", " + x + ", " + y + ", " + z) + ";");
                    }
                    return;
                }
            }
        }
        if (command == "droprelloc" && tokens.size() == 3)
        {
            const auto x = number(tokens[1], INT16_MIN, INT16_MAX);
            const auto y = number(tokens[2], INT16_MIN, INT16_MAX);
            if (!x.empty() && !y.empty())
            {
                code(call("Drop", args + ", Player.X() + (" + x + "), Player.Y() + (" + y +
                    "), Player.Z()") + ";");
                return;
            }
        }
        if (command == "droploc" && tokens.size() == 4)
        {
            const auto x = number(tokens[1], 0, UINT16_MAX);
            const auto y = number(tokens[2], 0, UINT16_MAX);
            const auto z = number(tokens[3], -128, 127);
            if (!x.empty() && !y.empty() && !z.empty())
            {
                code(call("Drop", args + ", " + x + ", " + y + ", " + z) + ";");
                return;
            }
        }
        issue("The previous lift must be followed by a supported drop.");
        return;
    }
    if ((command == "lift" && tokens.size() >= 2 && tokens.size() <= 3) ||
        (command == "lifttype" && tokens.size() >= 2 && tokens.size() <= 4))
    {
        if (command == "lift")
        {
            m_liftSource = serial(tokens[1]);
        }
        else
        {
            const auto hue = tokens.size() == 4 ? number(tokens[3], 0, UINT16_MAX) : "65535";
            if (!hue.empty())
            {
                m_liftSource = "(" + find_item(tokens[1], "backpack", hue) + " || 0)";
            }
        }
        m_liftAmount = tokens.size() >= 3 ? number(tokens[2], 1, UINT16_MAX) : "1";
        m_liftLine = m_line;
        if (m_liftSource.empty() || m_liftAmount.empty())
        {
            m_liftSource.clear();
            issue("Lift needs an item serial and a valid amount.");
        }
        return;
    }
    if (structure(command, tokens))
    {
        return;
    }
    if (command == "stop" && tokens.size() == 1)
    {
        code("return;");
        return;
    }
    if ((command == "loop" || command == "replay") && tokens.size() == 1)
    {
        m_usesReplay = true;
        code("Client.Wait(20); continue razorReplay;");
        return;
    }
    const auto statement = action(tokens);
    if (statement.empty())
    {
        issue("Unsupported or invalid Razor command: " + tokens[0]);
        return;
    }
    code(statement + ";");
}

void Translator::macro(const Tokens &parts)
{
    const std::string PREFIX = "Assistant.Macros.";
    if (parts[0].compare(0, PREFIX.size(), PREFIX) != 0)
    {
        issue("Expected a Razor macro action.");
        return;
    }
    const auto kind = parts[0].substr(PREFIX.size());
    const auto field = [&](size_t index) -> std::string {
        return index < parts.size() ? parts[index] : std::string();
    };
    auto invalid = [&] { issue("Unsupported or invalid macro action: " + kind); };
    auto convert = [&](const char *command) { script({ command, field(1) }); };

    if (kind == "DoubleClickAction" && parts.size() == 3)
    {
        convert("dclick");
    }
    else if (kind == "DoubleClickTypeAction" && parts.size() >= 2 && parts.size() <= 3)
    {
        bool item = true;
        if (parts.size() == 3 && (!boolean(parts[2], item) || !item))
        {
            invalid();
            return;
        }
        convert("dclicktype");
    }
    else if (kind == "UseSkillAction" && parts.size() == 2)
    {
        convert("skill");
    }
    else if ((kind == "MacroCastSpellAction" || kind == "ExtCastSpellAction") && parts.size() == 2)
    {
        convert("cast");
    }
    else if (kind == "PauseAction" && parts.size() == 2)
    {
        const auto duration = milliseconds(parts[1]);
        if (duration.empty())
        {
            invalid();
            return;
        }
        code(call("Wait", duration) + ";");
    }
    else if (kind == "WaitForGumpAction" && parts.size() >= 2 && parts.size() <= 4)
    {
        bool strict = false;
        const auto timeout = parts.size() == 4 ? seconds_to_milliseconds(parts[3]) : "300000";
        if ((parts.size() >= 3 && !boolean(parts[2], strict)) || timeout.empty())
        {
            invalid();
            return;
        }
        script({ "waitforgump", strict ? parts[1] : "any", timeout });
    }
    else if (kind == "GumpResponseAction" && parts.size() >= 4)
    {
        int64_t switches = 0;
        if (!integer(parts[2], switches, 0, 256) || parts.size() < 4 + static_cast<size_t>(switches))
        {
            invalid();
            return;
        }
        const auto button = number(parts[1], 0, INT32_MAX);
        const auto start = static_cast<size_t>(3 + switches);
        int64_t entries = 0;
        if (button.empty() || !integer(parts[start], entries, 0, 256) ||
            parts.size() != start + 1 + static_cast<size_t>(entries))
        {
            invalid();
            return;
        }
        std::string hook = "const hook = Client.CreateGumpHook(" + button + "); ";
        for (size_t i = 3; i < start; ++i)
        {
            const auto check = number(parts[i], 0, INT32_MAX);
            if (check.empty())
            {
                invalid();
                return;
            }
            hook += "hook.AddCheck(" + check + ", true); ";
        }
        for (size_t i = start + 1; i < parts.size(); ++i)
        {
            const auto divider = parts[i].find('&');
            const auto id = number(std::string(parts[i]).substr(0, divider), 0, UINT16_MAX);
            if (divider == std::string::npos || id.empty())
            {
                invalid();
                return;
            }
            hook += "hook.AddEntry(" + id + ", " + quote(parts[i].substr(divider + 1)) + "); ";
        }
        m_usesGump = true;
        code("{ const g = razorGump === undefined ? Client.GetLastGump() : razorGump; "
            "if (g) { " + hook + "g.Select(hook); } razorGump = undefined; }");
    }
    else if (kind == "LiftAction" && parts.size() == 4)
    {
        script({ "lift", parts[1], parts[2] });
    }
    else if (kind == "DropAction" && parts.size() == 4 && parts[3] == "0")
    {
        auto at = trim(parts[2]);
        if (at.size() > 1 && at.front() == '(' && at.back() == ')')
        {
            at = at.substr(1, at.size() - 2);
        }
        const auto point = split_fields(at, ',');
        if (point.size() != 3)
        {
            invalid();
            return;
        }
        if (parts[1] == "0" || parts[1] == "4294967295")
        {
            script({ "droploc", point[0], point[1], point[2] });
        }
        else if (trim(point[0]) == "-1" || trim(point[0]) == "65535")
        {
            script({ "drop", parts[1] });
        }
        else
        {
            script({ "drop", parts[1], point[0], point[1], point[2] });
        }
    }
    else if (kind == "WaitForTargetAction" && parts.size() <= 2)
    {
        const auto duration = parts.size() == 2 ? seconds_to_milliseconds(parts[1]) : "30000";
        if (duration.empty())
        {
            invalid();
            return;
        }
        code(call("WaitForTarget", duration) + ";");
    }
    else if (kind == "AbsoluteTargetAction" && parts.size() == 8)
    {
        if (parts[1] == "0")
        {
            script({ "target", parts[3] });
        }
        else if (parts[1] == "1" && parts[7] == "0")
        {
            script({ "targettile", parts[4], parts[5], parts[6] });
        }
        else
        {
            // A static target includes its graphic. The plain tile API has no graphic.
            invalid();
        }
    }
    else if (kind == "TargetSelfAction" && parts.size() == 1)
    {
        script({ "target", "self" });
    }
    else if (kind == "LastTargetAction" && parts.size() == 1)
    {
        script({ "target", "lasttarget" });
    }
    else if (kind == "SpeechAction" && parts.size() >= 7)
    {
        int64_t keywords = 0;
        if (!integer(parts[5], keywords, 0, 256) || parts.size() != 7 + static_cast<size_t>(keywords))
        {
            invalid();
            return;
        }
        const auto message = quote(parts.back());
        if (parts.back().size() > 1 && parts.back().front() == '-')
        {
            issue("Razor-local speech command needs a native command mapping.");
            return;
        }
        const auto &type = parts[1];
        const char *method = type == "0" ? "Say" : type == "2" ? "SayEmote" :
            type == "8" ? "SayWhisper" : type == "9" ? "SayYell" : nullptr;
        if (!method || keywords != 0)
        {
            invalid();
            return;
        }
        code(call(method, message) + ";");
    }
    else if (kind == "DressAction" && parts.size() == 2)
    {
        convert("dress");
    }
    else if (kind == "UnDressAction" && parts.size() == 2)
    {
        convert("undress");
    }
    else if (kind == "ClearSysMessages" && parts.size() == 1)
    {
        script({ "clearsysmsg" });
    }
    else if (kind == "ForAction" && parts.size() == 2)
    {
        convert("for");
    }
    else if (kind == "EndForAction" && parts.size() == 1)
    {
        script({ "endfor" });
    }
    else if (kind == "ElseAction" && parts.size() == 1)
    {
        script({ "else" });
    }
    else if (kind == "EndIfAction" && parts.size() == 1)
    {
        script({ "endif" });
    }
    else if (kind == "EndWhileAction" && parts.size() == 1)
    {
        script({ "endwhile" });
    }
    else if ((kind == "IfAction" || kind == "WhileAction" || kind == "ElseIfAction") && parts.size() >= 4)
    {
        constexpr const char *STAT[] = { "hits", "mana", "stam", nullptr, nullptr, "weight" };
        constexpr const char *OP[] = { "<=", ">=", "<", ">" };
        const char *branch = kind == "IfAction" ? "if" : kind == "WhileAction" ? "while" : "elseif";
        int64_t index = 0, op = 0;
        if (!integer(parts[1], index, 0, 5) || STAT[index] == nullptr ||
            !integer(parts[2], op, 0, 3))
        {
            invalid();
            return;
        }
        script({ branch, STAT[index], OP[op], parts[3] });
    }
    else if (kind == "WaitForStatAction" && parts.size() >= 4 && parts.size() <= 5)
    {
        constexpr const char *STAT[] = { "hits", "mana", "stam", nullptr, nullptr, "weight" };
        constexpr const char *OP[] = { "<=", ">=", "<", ">" };
        int64_t index = 0, op = 0;
        const auto threshold = number(parts[3], 0, INT32_MAX);
        const auto timeout = parts.size() == 5 ? seconds_to_milliseconds(parts[4]) : "30000";
        if (!integer(parts[1], index, 0, 5) || STAT[index] == nullptr ||
            !integer(parts[2], op, 0, 3) || threshold.empty() || timeout.empty())
        {
            invalid();
            return;
        }
        const std::string statExpr = std::string(STAT[index]) == "hits" ? "Player.HitsValue()" :
            std::string(STAT[index]) == "mana" ? "Player.ManaValue()" :
            std::string(STAT[index]) == "stam" ? "Player.StamValue()" : "Player.WeightValue()";
        m_usesWait = true;
        code("waitRazor(() => " + statExpr + " " + OP[op] + " " + threshold + ", " + timeout + ");");
    }
    else if (kind == "UseOnceAction" && parts.size() == 1)
    {
        script({ "useonce" });
    }
    else if ((kind == "OrganizerAction" || kind == "OrganizerAgentAction") && parts.size() <= 2)
    {
        if (parts.size() == 2) script({ "organizer", parts[1] });
        else script({ "organizer" });
    }
    else if ((kind == "RestockAction" || kind == "RestockAgentAction") && parts.size() <= 2)
    {
        if (parts.size() == 2) script({ "restock", parts[1] });
        else script({ "restock" });
    }
    else if ((kind == "ScavengerAction" || kind == "ScavengerAgentAction") && parts.size() <= 2)
    {
        if (parts.size() == 2) script({ "scavenger", parts[1] });
        else script({ "scavenger" });
    }
    else if (kind == "SetAbilityAction" && parts.size() == 2)
    {
        const auto ability = lower(parts[1]);
        if (ability == "0" || ability == "primary") script({ "setability", "primary" });
        else if (ability == "1" || ability == "secondary") script({ "setability", "secondary" });
        else invalid();
    }
    else if (kind == "WalkAction" && parts.size() == 2)
    {
        script({ "walk", parts[1] });
    }
    else if (kind == "TargetRelLocAction" && parts.size() == 3)
    {
        script({ "targetrelloc", parts[1], parts[2] });
    }
    else if (kind == "WaitForMenuAction" && parts.size() >= 2 && parts.size() <= 4)
    {
        bool strict = false;
        const auto timeout = parts.size() == 4 ? seconds_to_milliseconds(parts[3]) : "30000";
        if ((parts.size() >= 3 && !boolean(parts[2], strict)) || timeout.empty())
        {
            invalid();
            return;
        }
        script({ "waitformenu", strict ? parts[1] : "any", timeout });
    }
    else if (kind == "MenuResponseAction" && (parts.size() == 3 || parts.size() == 4))
    {
        if (parts.size() == 4) script({ "menuresponse", parts[1], parts[2], parts[3] });
        else script({ "menuresponse", parts[1], parts[2] });
    }
    else if (kind == "WaitForPromptAction" && parts.size() <= 2)
    {
        const auto timeout = parts.size() == 2 ? seconds_to_milliseconds(parts[1]) : "30000";
        if (timeout.empty())
        {
            invalid();
            return;
        }
        script({ "waitforprompt", "any", timeout });
    }
    else if ((kind == "PromptAction" || kind == "ResponsePromptAction") && parts.size() == 2)
    {
        script({ "promptresponse", parts[1] });
    }
    else if (kind == "ContextMenuAction" && parts.size() == 3)
    {
        script({ "menu", parts[1], parts[2] });
    }
    else if (kind == "HotKeyAction" && parts.size() == 2)
    {
        const auto locNum = number(parts[1], 1, 65535);
        const RazorHotkeyDefinition *hk = !locNum.empty() ?
            FindRazorHotkey(static_cast<unsigned>(std::strtoul(locNum.c_str(), nullptr, 10))) :
            FindRazorHotkey(parts[1]);
        if (hk == nullptr)
        {
            invalid();
            return;
        }
        code(call("ExecuteRazorHotkey", std::to_string(hk->Id)) + ";");
    }
    else if (kind == "BandageSelfAction" && parts.size() == 1)
    {
        script({ "bandageself" });
    }
    else if (kind == "ToggleWarModeAction" && parts.size() == 1)
    {
        script({ "warmode" });
    }
    else if (kind == "AttackLastTargetAction" && parts.size() == 1)
    {
        script({ "attack", "lasttarget" });
    }
    else if (kind == "AttackLastCombatantAction" && parts.size() == 1)
    {
        script({ "attack", "lastattack" });
    }
    else if (kind == "ClearTargetQueueAction" && parts.size() == 1)
    {
        script({ "cancelwaittarget" });
    }
    else if (kind == "CancelTargetAction" && parts.size() == 1)
    {
        script({ "canceltarget" });
    }
    else if (kind == "LiftTypeAction" && parts.size() >= 3 && parts.size() <= 4)
    {
        if (parts.size() == 4) script({ "lifttype", parts[1], parts[2], parts[3] });
        else script({ "lifttype", parts[1], parts[2] });
    }
    else if (kind == "UnDressLayerAction" && parts.size() == 2)
    {
        const auto layer = number(parts[1], 1, 25);
        if (layer.empty())
        {
            invalid();
            return;
        }
        code(call("Unequip", layer) + ";");
    }
    else if (kind == "InvokeVirtueAction" && parts.size() == 2)
    {
        script({ "virtue", parts[1] });
    }
    else if (kind == "ResyncAction" && parts.size() == 1)
    {
        script({ "resync" });
    }
    else if (kind == "ClearJournalAction" && parts.size() == 1)
    {
        script({ "clearjournal" });
    }
    else
    {
        invalid();
    }
}

std::string Translator::uosteam_hue(const std::string &token)
{
    const auto folded = lower(token);
    if (folded == "any" || folded == "-1")
    {
        return "65535";
    }
    return number(token, 0, UINT16_MAX);
}

std::string Translator::uosteam_findtype(const Tokens &tokens, size_t &pos)
{
    if (pos >= tokens.size())
    {
        return {};
    }
    const auto graphic = name_or_number(tokens[pos++]);
    std::string hue = "65535";
    std::string scope = "\"backpack\"";
    std::string range;
    if (pos < tokens.size())
    {
        const auto next = lower(tokens[pos]);
        if (next != "and" && next != "or" && !comparison(tokens[pos]))
        {
            const auto parsedHue = uosteam_hue(tokens[pos]);
            if (!parsedHue.empty())
            {
                hue = parsedHue;
                ++pos;
            }
        }
    }
    if (pos < tokens.size())
    {
        const auto next = lower(tokens[pos]);
        if (next != "and" && next != "or" && !comparison(tokens[pos]))
        {
            if (next == "ground" || next == "world" || next == "any")
            {
                scope = "\"ground\"";
                ++pos;
            }
            else if (next == "backpack" || next == "self")
            {
                scope = quote(next);
                ++pos;
            }
            else
            {
                const auto cont = serial(tokens[pos]);
                if (!cont.empty())
                {
                    scope = cont;
                    ++pos;
                }
            }
        }
    }
    // Optional amount (ignored by FindType list) and range
    if (pos < tokens.size() && lower(tokens[pos]) != "and" && lower(tokens[pos]) != "or" &&
        !comparison(tokens[pos]))
    {
        const auto maybeNum = number(tokens[pos], 0, INT32_MAX);
        if (!maybeNum.empty())
        {
            ++pos;
            if (pos < tokens.size() && lower(tokens[pos]) != "and" && lower(tokens[pos]) != "or" &&
                !comparison(tokens[pos]))
            {
                const auto parsedRange = number(tokens[pos], 1, 31);
                if (!parsedRange.empty())
                {
                    range = parsedRange;
                    ++pos;
                }
            }
        }
    }
    const std::string mode = scope == "\"ground\"" ? "\"nearest\"" : "\"fast\"";
    const std::string args = graphic + ", " + hue + ", " + scope + ", " + mode +
        (range.empty() ? std::string() : ", " + range);
    return "(razorFound = " + call("FindType", args) + "[0] || null)";
}

std::string Translator::uosteam_operand(const Tokens &tokens, size_t &pos)
{
    if (pos >= tokens.size())
    {
        return {};
    }
    const auto &raw = tokens[pos];
    const auto token = lower(raw);
    if (token == "findtype")
    {
        ++pos;
        const auto found = uosteam_findtype(tokens, pos);
        return found.empty() ? std::string() : "(" + found + " !== null)";
    }
    if (token == "findalias")
    {
        ++pos;
        if (pos >= tokens.size()) return {};
        const auto name = quote(tokens[pos++]);
        return "(razorTemp[" + name + "] !== undefined || " +
            call("GetGlobal", "\"razor.var.\" + " + name) + " !== \"\")";
    }
    if (token == "injournal")
    {
        ++pos;
        if (pos >= tokens.size()) return {};
        const auto needle = quote(tokens[pos++]);
        std::string author;
        if (pos < tokens.size())
        {
            const auto next = lower(tokens[pos]);
            if (next != "and" && next != "or" && !comparison(tokens[pos]))
            {
                author = quote(tokens[pos++]);
            }
        }
        return "Boolean(" + call("InJournal", author.empty() ? needle : needle + ", " + author) + ")";
    }
    if (token == "counttype")
    {
        ++pos;
        if (pos >= tokens.size()) return {};
        const auto graphic = name_or_number(tokens[pos++]);
        std::string hue = "65535";
        std::string container = "\"backpack\"";
        if (pos < tokens.size() && lower(tokens[pos]) != "and" && lower(tokens[pos]) != "or" &&
            !comparison(tokens[pos]))
        {
            const auto parsedHue = uosteam_hue(tokens[pos]);
            if (!parsedHue.empty())
            {
                hue = parsedHue;
                ++pos;
            }
        }
        if (pos < tokens.size() && lower(tokens[pos]) != "and" && lower(tokens[pos]) != "or" &&
            !comparison(tokens[pos]))
        {
            const auto cont = serial(tokens[pos]);
            if (!cont.empty())
            {
                container = cont;
                ++pos;
            }
        }
        return call("Count", graphic + ", " + hue + ", " + container);
    }
    return operand(tokens, pos);
}

std::string Translator::uosteam_predicate(const Tokens &tokens, size_t pos)
{
    std::string output;
    while (pos < tokens.size())
    {
        bool negate = false;
        if (lower(tokens[pos]) == "not" || tokens[pos] == "!")
        {
            negate = true;
            ++pos;
        }
        auto left = uosteam_operand(tokens, pos);
        if (left.empty())
        {
            return {};
        }
        if (pos < tokens.size() && comparison(tokens[pos]))
        {
            const auto op = tokens[pos++];
            const auto right = uosteam_operand(tokens, pos);
            if (right.empty())
            {
                return {};
            }
            const auto jsOp = op == "=" || op == "==" ? "===" : op == "!=" ? "!==" : op;
            left += " " + jsOp + " " + right;
        }
        output += (negate ? "!(" : "(") + left + ")";
        if (pos == tokens.size())
        {
            return output;
        }
        const auto logical = lower(tokens[pos++]);
        if (pos == tokens.size() || (logical != "and" && logical != "or"))
        {
            return {};
        }
        output += logical == "and" ? " && " : " || ";
    }
    return {};
}

bool Translator::uosteam_structure(const std::string &command, const Tokens &tokens)
{
    if (command == "for" && tokens.size() == 4 && lower(tokens[2]) == "to")
    {
        const auto start = number(tokens[1], INT32_MIN, INT32_MAX);
        const auto stop = number(tokens[3], INT32_MIN, INT32_MAX);
        if (start.empty() || stop.empty())
        {
            return false;
        }
        const auto var = "razorLoop" + std::to_string(m_blocks.size());
        return open("for", "for (let " + var + " = " + start + "; " + var + " <= " + stop + "; ++" + var + ")");
    }
    if (command == "if" || command == "while" || command == "elseif")
    {
        const auto value = uosteam_predicate(tokens, 1);
        if (value.empty())
        {
            return false;
        }
        if (command == "elseif")
        {
            if (m_blocks.empty() || m_blocks.back().Kind != "if" || m_blocks.back().Else)
            {
                return false;
            }
            auto block = std::move(m_blocks.back());
            m_blocks.pop_back();
            write("} else if (" + value + ") {");
            m_blocks.push_back(std::move(block));
            return true;
        }
        if (!open(command, command + " (" + value + ")"))
        {
            return false;
        }
        if (command == "while")
        {
            write("Client.Wait(20);");
        }
        return true;
    }
    return structure(command, tokens);
}

std::string Translator::uosteam_action(const Tokens &tokens)
{
    const auto command = command_name(tokens[0]);
    if (command == "useobject" && tokens.size() == 2)
    {
        const auto target = serial(tokens[1]);
        return target.empty() ? std::string() : call("UseObject", target);
    }
    if (command == "usetype" && tokens.size() >= 2 && tokens.size() <= 5)
    {
        const auto graphic = name_or_number(tokens[1]);
        const auto hue = tokens.size() >= 3 ? uosteam_hue(tokens[2]) : "65535";
        if (hue.empty())
        {
            return {};
        }
        if (tokens.size() <= 3)
        {
            return call("UseType", graphic + ", " + hue);
        }
        const auto source = lower(tokens[3]) == "ground" ? quote("ground") : serial(tokens[3]);
        if (source.empty())
        {
            return {};
        }
        if (tokens.size() == 4)
        {
            return call("UseType", graphic + ", " + hue + ", " + source);
        }
        const auto range = number(tokens[4], 1, 31);
        if (range.empty())
        {
            return {};
        }
        return "{ const razorItem = " + call("FindType", graphic + ", " + hue + ", " + source + ", \"nearest\", " + range) +
            "[0]; if (razorItem) " + call("UseObject", "razorItem") + "; }";
    }
    if (command == "cast" && (tokens.size() == 2 || tokens.size() == 3))
    {
        const auto spell = name_or_number(tokens[1]);
        if (tokens.size() == 2)
        {
            return call("Cast", spell);
        }
        const auto target = serial(tokens[2]);
        return target.empty() ? std::string() :
            call("WaitTargetObject", target) + "; " + call("Cast", spell);
    }
    if ((command == "target" || command == "target!") && tokens.size() == 2)
    {
        const auto folded = lower(tokens[1]);
        if (folded == "cancel") return call("CancelTarget");
        if (folded == "clear") return call("CancelWaitTarget");
        const auto target = serial(tokens[1]);
        return target.empty() ? std::string() : call("TargetObject", target);
    }
    if (command == "waitfortarget" && tokens.size() <= 2)
    {
        const auto timeout = tokens.size() == 2 ? number(tokens[1], 0, INT32_MAX) : "15000";
        return timeout.empty() ? std::string() : call("WaitForTarget", timeout);
    }
    if (command == "sysmsg" && (tokens.size() == 2 || tokens.size() == 3))
    {
        if (tokens.size() == 3 && number(tokens[2], 0, UINT16_MAX).empty())
        {
            return {};
        }
        return call("Print", quote(tokens[1]));
    }
    if (command == "headmsg" && tokens.size() >= 2 && tokens.size() <= 4)
    {
        const auto hue = tokens.size() >= 3 ? number(tokens[2], 0, UINT16_MAX) : std::string();
        if (tokens.size() >= 3 && hue.empty())
        {
            return {};
        }
        const auto target = tokens.size() == 4 ? serial(tokens[3]) : quote("self");
        if (target.empty())
        {
            return {};
        }
        return call("CharPrint", target + ", " +
            (hue.empty() ? quote("0xFFFF") : "(" + hue + ").toString(16)") + ", " + quote(tokens[1]));
    }
    if (command == "moveitem" && tokens.size() >= 3 && tokens.size() <= 7)
    {
        const auto item = serial(tokens[1]);
        const auto dest = serial(tokens[2]);
        if (item.empty() || dest.empty())
        {
            return {};
        }
        if (tokens.size() == 3)
        {
            return call("MoveItem", item + ", 0, " + dest);
        }
        if (tokens.size() == 4)
        {
            const auto amount = number(tokens[3], 0, UINT16_MAX);
            return amount.empty() ? std::string() : call("MoveItem", item + ", " + amount + ", " + dest);
        }
        if (tokens.size() == 6 || tokens.size() == 7)
        {
            const auto x = number(tokens[3], -1, UINT16_MAX);
            const auto y = number(tokens[4], -1, UINT16_MAX);
            const auto z = number(tokens[5], -128, 127);
            const auto amount = tokens.size() == 7 ? number(tokens[6], 0, UINT16_MAX) : "0";
            if (x.empty() || y.empty() || z.empty() || amount.empty())
            {
                return {};
            }
            return call("MoveItem", item + ", " + amount + ", " + dest + ", " + x + ", " + y + ", " + z);
        }
        return {};
    }
    if (command == "moveitemoffset" && (tokens.size() == 6 || tokens.size() == 7))
    {
        const auto item = serial(tokens[1]);
        const auto dx = number(tokens[3], INT16_MIN, INT16_MAX);
        const auto dy = number(tokens[4], INT16_MIN, INT16_MAX);
        const auto dz = number(tokens[5], -128, 127);
        const auto amount = tokens.size() == 7 ? number(tokens[6], 0, UINT16_MAX) : "0";
        if (item.empty() || dx.empty() || dy.empty() || dz.empty() || amount.empty())
        {
            return {};
        }
        return call("Drop", item + ", " + amount + ", Player.X() + (" + dx + "), Player.Y() + (" + dy +
            "), Player.Z() + (" + dz + ")");
    }
    if (command == "findtype" && tokens.size() >= 2 && tokens.size() <= 6)
    {
        size_t pos = 1;
        const auto expr = uosteam_findtype(tokens, pos);
        return !expr.empty() && pos == tokens.size() ? expr : std::string();
    }
    if (command == "autoloot" && tokens.size() <= 2)
    {
        const auto name = tokens.size() == 2 ? quote(tokens[1]) : quote("AutoLoot");
        return call("AutoLootOnce", name);
    }
    if ((command == "organizer" || command == "restock" || command == "scavenger") && tokens.size() == 2)
    {
        const auto index = number(tokens[1], 1, INT32_MAX);
        return index.empty() ? call("RunInventoryAgent", quote(tokens[1])) :
            call("RunInventoryAgent", quote(command) + ", " + index);
    }
    if (command == "setalias" && tokens.size() == 3)
    {
        const auto target = serial(tokens[2]);
        return target.empty() ? std::string() :
            "razorTemp[" + quote(tokens[1]) + "] = " + target;
    }
    if (command == "unsetalias" && tokens.size() == 2)
    {
        return "delete razorTemp[" + quote(tokens[1]) + "]";
    }
    if (command == "ignoreobject" && tokens.size() == 2)
    {
        const auto target = serial(tokens[1]);
        return target.empty() ? std::string() : call("Ignore", target);
    }
    return action(tokens);
}

void Translator::uosteam(const Tokens &tokens)
{
    if (tokens.empty())
    {
        return;
    }
    const auto command = command_name(tokens[0]);
    if (uosteam_structure(command, tokens))
    {
        return;
    }
    if (command == "stop" && tokens.size() == 1)
    {
        code("return;");
        return;
    }
    if ((command == "loop" || command == "replay") && tokens.size() == 1)
    {
        m_usesReplay = true;
        code("Client.Wait(20); continue razorReplay;");
        return;
    }
    const auto statement = uosteam_action(tokens);
    if (statement.empty())
    {
        issue("Unsupported or invalid UOSteam command: " + tokens[0]);
        return;
    }
    code(statement + ";");
}

void add_profile_option(RazorImportResult &result, const char *file, const char *section,
    const char *key, const std::string &value)
{
    const auto previous = std::find_if(result.ProfileOptions.begin(), result.ProfileOptions.end(),
        [&](const auto &option) { return option.Key == key && option.File == file && option.Section == section; });
    if (previous != result.ProfileOptions.end())
    {
        previous->Value = value;
    }
    else
    {
        result.ProfileOptions.push_back({ key, value, file, section });
    }
}

std::string boolean(std::string value)
{
    value = lower(std::move(value));
    return value == "true" || value == "false" ? value : std::string();
}

constexpr std::size_t MaxClassicAgents = 32;
constexpr std::size_t MaxClassicAgentRules = 256;
constexpr std::size_t MaxClassicFriends = 256;

std::string format_hex(uint32_t value, int digits)
{
    static const char HEX[] = "0123456789ABCDEF";
    std::string out = "0x";
    for (int i = digits - 1; i >= 0; --i)
    {
        out += HEX[(value >> (i * 4)) & 0xF];
    }
    return out;
}

bool safe_label(const std::string &value)
{
    return !value.empty() && value.size() <= 63 && std::all_of(value.begin(), value.end(),
        [](unsigned char c) { return c >= 32 && c != 127; });
}

void parse_profile_agents(const tinyxml2::XMLElement &root, RazorImportResult &result)
{
    std::size_t agentCount = 0;
    std::size_t dressCount = 0;
    std::size_t shopCount = 0;
    std::size_t friendCount = 0;
    bool hasSellHotBag = false;

    for (const auto *element = root.FirstChildElement(); element != nullptr;
         element = element->NextSiblingElement())
    {
        const std::string tag = lower(element->Name());
        auto starts_with = [&](const char *prefix) {
            const std::size_t len = std::char_traits<char>::length(prefix);
            return tag == prefix || (tag.size() > len && tag.compare(0, len, prefix) == 0 && tag[len] == '-');
        };

        if (starts_with("organizer") || starts_with("restock") || starts_with("scavenger"))
        {
            if (agentCount >= MaxClassicAgents)
            {
                ++result.Ignored;
                continue;
            }
            const int mode = starts_with("organizer") ? 0 : starts_with("restock") ? 1 : 2;
            const auto *attrName = element->Attribute("name");
            std::string agentName = (attrName != nullptr && safe_label(trim(attrName))) ? trim(attrName) : tag;
            if (!agentName.empty() && agentName[0] >= 'a' && agentName[0] <= 'z')
            {
                agentName[0] = static_cast<char>(agentName[0] - 'a' + 'A');
            }
            const auto *hotbagAttr = element->Attribute("hotbag");
            const auto *destAttr = element->Attribute("destination");
            const auto *sourceAttr = element->Attribute("source");
            int64_t destVal = 0;
            int64_t srcVal = 0;
            if (hotbagAttr != nullptr) integer(hotbagAttr, destVal, 1, UINT32_MAX);
            else if (destAttr != nullptr) integer(destAttr, destVal, 1, UINT32_MAX);
            if (sourceAttr != nullptr) integer(sourceAttr, srcVal, 1, UINT32_MAX);

            const std::string section = "Agent" + std::to_string(agentCount);
            std::size_t ruleCount = 0;
            for (const auto *item = element->FirstChildElement("item"); item != nullptr;
                 item = item->NextSiblingElement("item"))
            {
                if (ruleCount >= MaxClassicAgentRules)
                {
                    ++result.Ignored;
                    continue;
                }
                const auto *idAttr = item->Attribute("id");
                const auto *itemText = item->FirstChildElement() == nullptr ? item->GetText() : nullptr;
                int64_t graphic = 0;
                if (!integer(idAttr != nullptr ? idAttr : (itemText != nullptr ? itemText : ""), graphic, 1, 65534))
                {
                    ++result.Ignored;
                    continue;
                }
                int64_t hue = -1;
                const auto *hueAttr = item->Attribute("hue");
                if (hueAttr != nullptr && !integer(hueAttr, hue, -1, 65535))
                {
                    hue = -1;
                }
                int64_t amount = 1;
                const auto *amountAttr = item->Attribute("amount");
                if (amountAttr != nullptr && !integer(amountAttr, amount, 1, 65535))
                {
                    amount = 1;
                }
                const std::string ruleSection = section + "Rule" + std::to_string(ruleCount);
                add_profile_option(result, "InventoryAgents.ini", ruleSection.c_str(), "graphic",
                    format_hex(static_cast<uint32_t>(graphic), 4));
                const bool matchHue = hue >= 0 && hue < 65535;
                add_profile_option(result, "InventoryAgents.ini", ruleSection.c_str(), "hue",
                    std::to_string(matchHue ? hue : 0));
                add_profile_option(result, "InventoryAgents.ini", ruleSection.c_str(), "matchhue",
                    matchHue ? "true" : "false");
                add_profile_option(result, "InventoryAgents.ini", ruleSection.c_str(), "quantity",
                    std::to_string(amount));
                ++ruleCount;
            }
            if (ruleCount > 0 || destVal > 0 || srcVal > 0)
            {
                add_profile_option(result, "InventoryAgents.ini", section.c_str(), "name", agentName);
                add_profile_option(result, "InventoryAgents.ini", section.c_str(), "mode", std::to_string(mode));
                if (srcVal > 0)
                {
                    add_profile_option(result, "InventoryAgents.ini", section.c_str(), "source",
                        format_hex(static_cast<uint32_t>(srcVal), 8));
                }
                if (destVal > 0)
                {
                    add_profile_option(result, "InventoryAgents.ini", section.c_str(), "destination",
                        format_hex(static_cast<uint32_t>(destVal), 8));
                }
                add_profile_option(result, "InventoryAgents.ini", section.c_str(), "rules",
                    std::to_string(ruleCount));
                ++agentCount;
                result.Imported += ruleCount > 0 ? ruleCount : 1;
            }
        }
        else if (starts_with("useonce"))
        {
            if (agentCount >= MaxClassicAgents)
            {
                ++result.Ignored;
                continue;
            }
            const std::string section = "Agent" + std::to_string(agentCount);
            std::size_t itemCount = 0;
            for (const auto *item = element->FirstChildElement("item"); item != nullptr;
                 item = item->NextSiblingElement("item"))
            {
                if (itemCount >= MaxClassicAgentRules)
                {
                    ++result.Ignored;
                    continue;
                }
                const auto *serAttr = item->Attribute("serial");
                const auto *itemText = item->FirstChildElement() == nullptr ? item->GetText() : nullptr;
                int64_t ser = 0;
                if (!integer(serAttr != nullptr ? serAttr : (itemText != nullptr ? itemText : ""), ser, 1, UINT32_MAX - 1))
                {
                    ++result.Ignored;
                    continue;
                }
                const std::string itemKey = "item" + std::to_string(itemCount);
                add_profile_option(result, "InventoryAgents.ini", section.c_str(), itemKey.c_str(),
                    format_hex(static_cast<uint32_t>(ser), 8));
                ++itemCount;
            }
            if (itemCount > 0)
            {
                add_profile_option(result, "InventoryAgents.ini", section.c_str(), "name", "Use Once");
                add_profile_option(result, "InventoryAgents.ini", section.c_str(), "mode", "3");
                add_profile_option(result, "InventoryAgents.ini", section.c_str(), "useitems",
                    std::to_string(itemCount));
                ++agentCount;
                result.Imported += itemCount;
            }
        }
        else if (tag == "dresslist" || starts_with("dress"))
        {
            if (dressCount >= MaxClassicAgents)
            {
                ++result.Ignored;
                continue;
            }
            const auto *attrName = element->Attribute("name");
            const std::string dressName = (attrName != nullptr && safe_label(trim(attrName))) ?
                trim(attrName) : ("Dress " + std::to_string(dressCount + 1));
            const std::string section = "Dress" + std::to_string(dressCount);
            std::size_t itemCount = 0;
            for (const auto *item = element->FirstChildElement("item"); item != nullptr;
                 item = item->NextSiblingElement("item"))
            {
                if (itemCount >= 25)
                {
                    ++result.Ignored;
                    continue;
                }
                const auto *serAttr = item->Attribute("serial");
                const auto *itemText = item->FirstChildElement() == nullptr ? item->GetText() : nullptr;
                int64_t ser = 0;
                if (!integer(serAttr != nullptr ? serAttr : (itemText != nullptr ? itemText : ""), ser, 1, UINT32_MAX - 1))
                {
                    ++result.Ignored;
                    continue;
                }
                const std::string itemKey = "item" + std::to_string(itemCount);
                add_profile_option(result, "DressSets.ini", section.c_str(), itemKey.c_str(),
                    format_hex(static_cast<uint32_t>(ser), 8));
                const auto *layerAttr = item->Attribute("layer");
                int64_t layer = 0;
                if (layerAttr != nullptr && integer(layerAttr, layer, 1, 24))
                {
                    const std::string layerKey = "layer" + std::to_string(itemCount);
                    add_profile_option(result, "DressSets.ini", section.c_str(), layerKey.c_str(),
                        std::to_string(layer));
                }
                ++itemCount;
            }
            if (itemCount > 0)
            {
                add_profile_option(result, "DressSets.ini", section.c_str(), "name", dressName);
                add_profile_option(result, "DressSets.ini", section.c_str(), "count", std::to_string(itemCount));
                ++dressCount;
                result.Imported += itemCount;
            }
        }
        else if (starts_with("buy") || starts_with("sell"))
        {
            const bool isSell = starts_with("sell");
            if (isSell && !hasSellHotBag)
            {
                const auto *hotbagAttr = element->Attribute("hotbag");
                int64_t bagVal = 0;
                if (hotbagAttr != nullptr && integer(hotbagAttr, bagVal, 1, UINT32_MAX - 1))
                {
                    add_profile_option(result, "SellHotBag.ini", "SellHotBag", "enabled", "true");
                    add_profile_option(result, "SellHotBag.ini", "SellHotBag", "serial",
                        format_hex(static_cast<uint32_t>(bagVal), 8));
                    hasSellHotBag = true;
                    ++result.Imported;
                }
            }
            if (shopCount >= MaxClassicAgents)
            {
                ++result.Ignored;
                continue;
            }
            const auto *attrName = element->Attribute("name");
            std::string listName = (attrName != nullptr && safe_label(trim(attrName))) ? trim(attrName) : tag;
            if (!listName.empty() && listName[0] >= 'a' && listName[0] <= 'z')
            {
                listName[0] = static_cast<char>(listName[0] - 'a' + 'A');
            }
            const std::string section = "ShopList" + std::to_string(shopCount);
            std::size_t itemCount = 0;
            for (const auto *item = element->FirstChildElement("item"); item != nullptr;
                 item = item->NextSiblingElement("item"))
            {
                if (itemCount >= MaxClassicAgentRules)
                {
                    ++result.Ignored;
                    continue;
                }
                const auto *idAttr = item->Attribute("id");
                const auto *itemText = item->FirstChildElement() == nullptr ? item->GetText() : nullptr;
                int64_t graphic = 0;
                if (!integer(idAttr != nullptr ? idAttr : (itemText != nullptr ? itemText : ""), graphic, 1, 65534))
                {
                    ++result.Ignored;
                    continue;
                }
                int64_t amount = 0;
                const auto *amountAttr = item->Attribute("amount");
                if (amountAttr != nullptr && !integer(amountAttr, amount, 0, 65535))
                {
                    amount = 0;
                }
                const std::string itemSec = section + "Item" + std::to_string(itemCount);
                add_profile_option(result, "ShopList.ini", itemSec.c_str(), "graphic",
                    format_hex(static_cast<uint32_t>(graphic), 4));
                add_profile_option(result, "ShopList.ini", itemSec.c_str(), "color", "0xFFFF");
                add_profile_option(result, "ShopList.ini", itemSec.c_str(), "count", std::to_string(amount));
                add_profile_option(result, "ShopList.ini", itemSec.c_str(), "price", "0");
                ++itemCount;
            }
            if (itemCount > 0)
            {
                add_profile_option(result, "ShopList.ini", section.c_str(), "name", listName);
                add_profile_option(result, "ShopList.ini", section.c_str(), "items", std::to_string(itemCount));
                ++shopCount;
                result.Imported += itemCount;
            }
        }
        else if (tag == "friends")
        {
            for (const auto *entry = element->FirstChildElement("friend"); entry != nullptr;
                 entry = entry->NextSiblingElement("friend"))
            {
                if (friendCount >= MaxClassicFriends)
                {
                    ++result.Ignored;
                    continue;
                }
                const auto *serAttr = entry->Attribute("serial");
                const auto *entryText = entry->FirstChildElement() == nullptr ? entry->GetText() : nullptr;
                int64_t ser = 0;
                if (!integer(serAttr != nullptr ? serAttr : (entryText != nullptr ? entryText : ""), ser, 1, UINT32_MAX - 1))
                {
                    ++result.Ignored;
                    continue;
                }
                const auto *nameAttr = entry->Attribute("name");
                const std::string friendName = (nameAttr != nullptr && safe_label(trim(nameAttr))) ?
                    trim(nameAttr) : format_hex(static_cast<uint32_t>(ser), 8);
                const std::string section = "Friend" + std::to_string(friendCount);
                add_profile_option(result, "Friends.ini", section.c_str(), "id",
                    format_hex(static_cast<uint32_t>(ser), 8));
                add_profile_option(result, "Friends.ini", section.c_str(), "name", friendName);
                add_profile_option(result, "Friends.ini", section.c_str(), "enabled", "true");
                ++friendCount;
                ++result.Imported;
            }
        }
    }
    if (agentCount > 0)
    {
        add_profile_option(result, "InventoryAgents.ini", "Meta", "count", std::to_string(agentCount));
    }
    if (dressCount > 0)
    {
        add_profile_option(result, "DressSets.ini", "Meta", "count", std::to_string(dressCount));
    }
    if (shopCount > 0)
    {
        add_profile_option(result, "ShopList.ini", "Meta", "count", std::to_string(shopCount));
    }
    if (friendCount > 0)
    {
        add_profile_option(result, "Friends.ini", "Meta", "count", std::to_string(friendCount));
    }
}

void parse_profile_hotkeys(const tinyxml2::XMLElement &root, RazorImportResult &result)
{
    const auto *hotkeys = root.FirstChildElement("hotkeys");
    if (hotkeys == nullptr)
    {
        return;
    }
    for (const auto *entry = hotkeys->FirstChildElement("key"); entry != nullptr;
         entry = entry->NextSiblingElement("key"))
    {
        const auto *legacyId = entry->Attribute("loc");
        const auto *legacyName = entry->Attribute("name");
        const auto *text = entry->GetText();
        const auto value = text != nullptr && entry->FirstChildElement() == nullptr ? trim(text) : std::string();
        const RazorHotkeyDefinition *binding = nullptr;
        if ((legacyId != nullptr && *legacyId != '\0') || value.compare(0, 2, "L:") == 0)
        {
            const auto loc = number(legacyId != nullptr && *legacyId != '\0' ? legacyId : value.substr(2), 1, 65535);
            if (!loc.empty())
            {
                binding = FindRazorHotkey(static_cast<unsigned>(std::stoul(loc)));
            }
        }
        else if (entry->FirstChildElement() == nullptr)
        {
            binding = FindRazorHotkey(legacyName != nullptr && *legacyName != '\0' ? legacyName : value);
        }
        const auto *macros = entry->Attribute("command");
        const auto *keyText = entry->Attribute("key");
        const auto *modifierText = entry->Attribute("mod");
        const auto *send = entry->Attribute("send");
        const auto key = keyText != nullptr ? number(keyText, -5, 254) : std::string();
        const auto modifiers = modifierText != nullptr ? number(modifierText, 0, 7) : std::string();
        const auto pass = send != nullptr ? boolean(send) : std::string();
        std::string notice;
        if (binding == nullptr ||
            (binding->Group == RazorHotkeyGroup::ClientAction && binding->Action != 0))
        {
            notice = "Skipped a hotkey whose action does not have a native import.";
        }
        else if (key.empty() || modifiers.empty() || pass.empty() ||
                 RazorHotkeyKeyName(std::stoi(key)).empty())
        {
            notice = "Skipped an unsupported key combination for " + std::string(binding->Name) + ".";
        }
        else
        {
            const int virtualKey = std::stoi(key);
            const auto mod = static_cast<unsigned>(std::stoul(modifiers));
            const auto duplicate = std::find_if(result.ProfileHotkeys.begin(), result.ProfileHotkeys.end(),
                [&](const auto &it) { return it.VirtualKey == virtualKey && it.Modifiers == mod; });
            if (virtualKey == 0x73 && (mod & 1) != 0 && pass == "false")
            {
                notice = "Skipped an Alt+F4 binding that wanted to consume the key.";
            }
            else if (duplicate != result.ProfileHotkeys.end())
            {
                notice = "Skipped a duplicate key combination.";
            }
            else
            {
                result.ProfileHotkeys.push_back({ binding->Id, virtualKey, mod, pass == "true" });
                ++result.Imported;
                if (macros != nullptr && *macros != '\0')
                {
                    result.Notices.push_back({ static_cast<size_t>(entry->GetLineNum()),
                        "Imported the key binding. Its custom chat command was skipped." });
                }
                continue;
            }
        }
        result.Notices.push_back({ static_cast<size_t>(entry->GetLineNum()), std::move(notice) });
        ++result.Ignored;
    }
}

constexpr std::size_t MaxClassicCounters = 256;

bool safe_counter_text(const std::string &name)
{
    return !name.empty() && name.size() <= 63 && std::all_of(name.begin(), name.end(),
        [](unsigned char c) { return c >= 32 && c != 127; });
}

void parse_classic_counter_selections(const tinyxml2::XMLElement &root, RazorImportResult &result)
{
    const auto *list = root.FirstChildElement("counters");
    if (list == nullptr)
    {
        return;
    }
    result.HasClassicCounterSelections = true;
    for (const auto *item = list->FirstChildElement("counter"); item != nullptr;
        item = item->NextSiblingElement("counter"))
    {
        const auto *name = item->Attribute("name");
        const auto *enabled = item->Attribute("enabled");
        const auto *image = item->Attribute("image");
        if (enabled != nullptr && boolean(enabled) == "false")
        {
            continue;
        }
        if (name == nullptr || !safe_counter_text(name) || enabled == nullptr || boolean(enabled) != "true" ||
            (image != nullptr && boolean(image).empty()) || result.ClassicCounters.size() >= MaxClassicCounters)
        {
            ++result.Ignored;
            continue;
        }
        result.ClassicCounters.push_back({ lower(trim(name)), image == nullptr || boolean(image) == "true" });
    }
}

// Parse only allowlisted profile settings and hotkeys. TinyXML2 does not
// resolve external entities.
void parse_profile(const std::string &input, RazorImportResult &result)
{
    tinyxml2::XMLDocument xml;
    if (input.find("<!DOCTYPE") != std::string::npos ||
        input.find("<!ENTITY") != std::string::npos ||
        xml.Parse(input.data(), input.size()) != tinyxml2::XML_SUCCESS)
    {
        result.Issues.push_back({ 0, "Choose a valid Razor profile XML file." });
        return;
    }
    const auto *root = xml.RootElement();
    if (root == nullptr || std::string(root->Name()) != "profile" || root->NextSiblingElement() != nullptr)
    {
        result.Issues.push_back({ 0, "Choose a Razor profile with a profile element." });
        return;
    }
    struct Mapping
    {
        const char *Razor;
        const char *Options;
        int64_t Minimum;
        int64_t Maximum;
    };
    constexpr Mapping MAPPINGS[] = {
        { "objectdelay", "moveitemsdelay", 0, 10000 },
        { "objectdelay", "useitemsdelay", 0, 10000 },
        { "corpserange", "opencorpsedistance", 1, 24 },
    };
    struct Switch
    {
        const char *Razor;
        const char *File;
        const char *Section;
        const char *Native;
    };
    constexpr Switch SWITCHES[] = {
        { "smartlasttarget", "Targeting.ini", "Targeting", "smartlast" },
        { "queuetargets", "Targeting.ini", "Targeting", "queue" },
        { "blockdismount", "Targeting.ini", "Targeting", "blockwardismount" },
        { "blockhealpoison", "GameplayFilters.ini", "Options", "guardhealing" },
        { "filtersnoopmsg", "GameplayFilters.ini", "Options", "snooping" },
        { "highlightreagents", "ItemCounters.ini", "Display", "highlight_reagents" },
        { "shownotohue", "ItemCounters.ini", "Display", "name_notoriety" },
        { "potionequip", "PotionHands.ini", "PotionHands", "enabled" },
        { "showhealth", "ItemCounters.ini", "Display", "health_changes" },
        { "showpartystats", "ItemCounters.ini", "Display", "party_stats" },
        { "showbuffdebuffoverhead", "ItemCounters.ini", "Display", "buff_messages" },
        { "showdamagetaken", "ItemCounters.ini", "Display", "damage_taken" },
        { "counterwarn", "ItemCounters.ini", "Meta", "warnings" },
        { "showcounters", "ItemCounters.ini", "Meta", "show" },
        { "showimages", "ItemCounters.ini", "Meta", "icons" },
        { "autosearch", "ItemCounters.ini", "Meta", "auto_recount" },
        { "nosearchpouches", "ItemCounters.ini", "Meta", "skip_pouches" },
        { "titlebardisplay", "Options.ini", "display", "enabled" },
    };
    const char *TARGET_FILE = "Targeting.ini";
    const char *TARGET_SECTION = "Targeting";
    std::string rangeCheck;
    std::string range;
    for (const auto *property = root->FirstChildElement("property");
         property != nullptr; property = property->NextSiblingElement("property"))
    {
        const auto *propertyName = property->Attribute("name");
        const auto *propertyValue = property->GetText();
        const auto key = propertyName != nullptr ? lower(propertyName) : std::string();
        const auto value = propertyValue != nullptr && property->FirstChildElement() == nullptr ?
            trim(propertyValue) : std::string();
        bool supported = false;
        for (const auto &mapping : MAPPINGS)
        {
            if (mapping.Razor != key)
            {
                continue;
            }
            supported = true;
            const auto converted = number(value, mapping.Minimum, mapping.Maximum);
            if (converted.empty())
            {
                result.Issues.push_back({ 0, "Invalid value for profile setting " + key + "." });
                break;
            }
            add_profile_option(result, "Options.ini", "main", mapping.Options, converted);
        }
        for (const auto &entry : SWITCHES)
        {
            if (entry.Razor == key)
            {
                supported = true;
                const auto converted = boolean(value);
                if (converted.empty())
                {
                    result.Issues.push_back({ 0, "Invalid value for profile setting " + key + "." });
                }
                else
                {
                    add_profile_option(result, entry.File, entry.Section, entry.Native, converted);
                }
                break;
            }
        }
        if (key == "counterwarnamount")
        {
            const auto amount = number(value, 0, 65535);
            if (amount.empty())
            {
                result.Notices.push_back({ 0, "Skipped an invalid Classic counter warning threshold." });
            }
            else
            {
                result.ClassicCounterWarning = std::stoi(amount);
                // The threshold is applied when the separate definitions are attached.
                continue;
            }
        }
        if (key == "titlebartext")
        {
            supported = true;
            const bool validTitle = property->FirstChildElement() == nullptr &&
                value.size() <= 512 && std::all_of(value.begin(), value.end(),
                    [](unsigned char c) { return c >= 32 && c != 127; });
            if (!validTitle)
            {
                result.Issues.push_back({ 0, "Invalid value for profile setting " + key + "." });
            }
            else
            {
                add_profile_option(result, "Options.ini", "display", "titledata", value);
            }
        }
        if (key == "rangechecklt" || key == "ltrange")
        {
            supported = true;
            auto &converted = key == "rangechecklt" ? rangeCheck : range;
            converted = key == "rangechecklt" ? boolean(value) : number(value, 1, 30);
            if (converted.empty())
            {
                result.Issues.push_back({ 0, "Invalid value for profile setting " + key + "." });
            }
        }
        if (supported)
        {
            ++result.Imported;
        }
        else
        {
            // Credentials and all other properties are ignored without exposing their values.
            ++result.Ignored;
        }
    }
    if (!rangeCheck.empty() || !range.empty())
    {
        // Classic Razor defaults to a 12-tile range check when the profile
        // omits one of the two fields.
        add_profile_option(result, TARGET_FILE, TARGET_SECTION, "lastrange",
            rangeCheck == "false" ? "0" : range.empty() ? "12" : range);
    }
    parse_profile_hotkeys(*root, result);
    parse_profile_agents(*root, result);
    parse_classic_counter_selections(*root, result);
    if (result.Imported == 0 && !result.HasClassicCounterSelections)
    {
        result.Issues.push_back({ 0, "This profile did not contain a supported setting or hotkey." });
    }
}

} // namespace

const RazorHotkeyDefinition *FindRazorHotkey(const std::string &name)
{
    const auto normalized = lower(trim(name));
    for (const auto &binding : RAZOR_HOTKEYS)
    {
        if (normalized == lower(binding.Name))
        {
            return &binding;
        }
    }
    return nullptr;
}

const RazorHotkeyDefinition *FindRazorHotkey(unsigned id)
{
    for (const auto &binding : RAZOR_HOTKEYS)
    {
        if (binding.Id == id)
        {
            return &binding;
        }
    }
    return nullptr;
}

std::string RazorHotkeyKeyName(int virtualKey)
{
    if ((virtualKey >= 'A' && virtualKey <= 'Z') || (virtualKey >= '0' && virtualKey <= '9'))
    {
        return std::string(1, static_cast<char>(virtualKey));
    }
    if (virtualKey >= 0x70 && virtualKey <= 0x87)
    {
        return "F" + std::to_string(virtualKey - 0x6F);
    }
    if (virtualKey >= 0x60 && virtualKey <= 0x69)
    {
        return "Keypad " + std::to_string(virtualKey - 0x60);
    }
    struct Key { int VirtualKey; const char *Name; };
    // Unambiguous WinForms Keys values. OEM punctuation keys vary by layout.
    constexpr Key KEYS[] = {
        { -1, "Wheel up" }, { -2, "Wheel down" }, { -3, "Middle click" },
        { -4, "Mouse X1" }, { -5, "Mouse X2" }, { 0x08, "Backspace" },
        { 0x09, "Tab" }, { 0x0D, "Return" }, { 0x13, "Pause" },
        { 0x1B, "Escape" }, { 0x20, "Space" }, { 0x21, "PageUp" },
        { 0x22, "PageDown" }, { 0x23, "End" }, { 0x24, "Home" },
        { 0x25, "Left" }, { 0x26, "Up" }, { 0x27, "Right" }, { 0x28, "Down" },
        { 0x2D, "Insert" }, { 0x2E, "Delete" }, { 0x6A, "Keypad *" },
        { 0x6B, "Keypad +" }, { 0x6D, "Keypad -" }, { 0x6E, "Keypad ." },
        { 0x6F, "Keypad /" },
    };
    for (const auto &key : KEYS)
    {
        if (key.VirtualKey == virtualKey)
        {
            return key.Name;
        }
    }
    return {};
}

RazorImportResult AddClassicCounterDefinitions(const RazorImportResult &profile, const std::string &input)
{
    RazorImportResult result = profile;
    tinyxml2::XMLDocument xml;
    const auto *root = input.size() <= MAX_IMPORT_BYTES && input.find('\0') == std::string::npos &&
        input.find("<!DOCTYPE") == std::string::npos && input.find("<!ENTITY") == std::string::npos &&
        xml.Parse(input.data(), input.size()) == tinyxml2::XML_SUCCESS ? xml.RootElement() : nullptr;
    if (root == nullptr || std::string(root->Name()) != "counters" || root->NextSiblingElement() != nullptr)
    {
        result.Issues.push_back({ 0, "Choose the Classic counters.xml definitions file." });
        return result;
    }

    const auto child_text = [](const tinyxml2::XMLElement &element, const char *key) {
        const auto *child = element.FirstChildElement(key);
        const auto *contents = child != nullptr && child->FirstChildElement() == nullptr ? child->GetText() : nullptr;
        return contents != nullptr ? trim(contents) : std::string();
    };
    std::vector<std::string> keys;
    std::vector<std::string> matched;
    std::size_t skipped = 0;
    for (const auto *item = root->FirstChildElement("counter"); item != nullptr;
        item = item->NextSiblingElement("counter"))
    {
        const auto name = child_text(*item, "name");
        const auto folded = lower(name);
        const auto selection = std::find_if(profile.ClassicCounters.begin(), profile.ClassicCounters.end(),
            [&](const auto &entry) { return entry.Name == folded; });
        if (profile.HasClassicCounterSelections && selection == profile.ClassicCounters.end())
        {
            continue;
        }
        const auto key = lower(child_text(*item, "format"));
        const auto graphic = number(child_text(*item, "itemid"), 1, 65534);
        const auto rawHue = child_text(*item, "hue");
        const auto hue = rawHue == "-1" ? "65535" : number(rawHue, 0, 65535);
        const bool validKey = !key.empty() && key.size() < 32 && std::all_of(key.begin(), key.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
        if (!safe_counter_text(name) || !validKey || graphic.empty() || hue.empty() ||
            keys.size() >= MaxClassicCounters || std::find(keys.begin(), keys.end(), key) != keys.end() ||
            (profile.HasClassicCounterSelections && std::find(matched.begin(), matched.end(), folded) != matched.end()))
        {
            ++skipped;
            continue;
        }
        const auto section = "Counter" + std::to_string(keys.size());
        const auto add = [&](const char *option, const std::string &value) {
            add_profile_option(result, "ItemCounters.ini", section.c_str(), option, value);
        };
        add("name", name);
        add("key", key);
        add("graphic", graphic);
        add("hue", hue);
        add("warning", std::to_string(profile.ClassicCounterWarning));
        add("visible", "true");
        add("icon", selection == profile.ClassicCounters.end() || selection->Icon ? "true" : "false");
        keys.push_back(key);
        matched.push_back(folded);
    }
    if (keys.empty())
    {
        result.Notices.push_back({ 0, "The file had no matching counter definitions." });
    }
    else
    {
        add_profile_option(result, "ItemCounters.ini", "Meta", "count", std::to_string(keys.size()));
        result.Imported += keys.size();
    }
    result.Ignored += skipped;
    if (skipped != 0)
    {
        result.Notices.push_back({ 0, "Skipped " + std::to_string(skipped) + " invalid or duplicate counter definitions." });
    }
    if (matched.size() < profile.ClassicCounters.size())
    {
        const auto missing = profile.ClassicCounters.size() - matched.size();
        result.Ignored += missing;
        result.Notices.push_back({ 0, "Could not match " + std::to_string(missing) + " selected Classic counters." });
    }
    return result;
}

RazorImportResult ParseRazorImport(RazorImportKind kind, const std::string &input)
{
    RazorImportResult result;
    if (input.size() > MAX_IMPORT_BYTES || input.find('\0') != std::string::npos)
    {
        result.Issues.push_back({ 0, "The import must be a text file no larger than 256 KiB." });
        return result;
    }
    size_t lineStart = input.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    if (kind == RazorImportKind::Profile)
    {
        parse_profile(input, result);
        return result;
    }

    Translator translator(result);
    size_t line = 0;
    while (lineStart < input.size())
    {
        ++line;
        translator.set_line(line);
        if (line > MAX_IMPORT_LINES)
        {
            translator.issue("The import exceeds 5,000 lines.");
            break;
        }
        const auto end = input.find('\n', lineStart);
        const auto raw = trim(input.substr(lineStart, end == std::string::npos ? end : end - lineStart));
        lineStart = end == std::string::npos ? input.size() : end + 1;
        if (raw.empty() || raw.front() == '#' || raw.substr(0, 2) == "//")
        {
            continue;
        }
        if (kind == RazorImportKind::Macro)
        {
            if (raw == "!Loop")
            {
                translator.loop();
            }
            else
            {
                translator.macro(split_fields(raw, '|'));
            }
        }
        else
        {
            Tokens tokens;
            if (!tokenize(raw, tokens))
            {
                translator.issue("Invalid quoted argument.");
                continue;
            }
            if (kind == RazorImportKind::UOSteam)
            {
                translator.uosteam(tokens);
            }
            else
            {
                translator.script(tokens);
            }
        }
    }
    translator.finish(kind == RazorImportKind::UOSteam ? "UOSteam" : "Razor");
    if (result.Imported == 0 && result.Issues.empty())
    {
        result.Issues.push_back({ 0, "There are no actions to import." });
    }
    return result;
}

} // namespace xuoassist
