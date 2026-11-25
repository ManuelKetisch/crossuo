// SPDX-License-Identifier: AGPL-3.0-or-later

#include "razorenhancedimport.h"

#include "../script/quickjs/quickjs.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

namespace xuoassist {
namespace {

// Razor Enhanced release/1.0 serializes its DataTables as separate JSON arrays.
constexpr std::size_t MaxBytes = 256 * 1024;
constexpr int MaxRows = 4096;
constexpr int MaxToolbarSlots = 12;
constexpr int AnyHue = 0xFFFF;
constexpr std::size_t TableCount = static_cast<std::size_t>(RazorEnhancedTable::Count);
constexpr std::array<const char *, TableCount> TableFiles = {{
    "RazorEnhanced.settings.GENERAL",
    "RazorEnhanced.settings.HOTKEYS",
    "RazorEnhanced.settings.TOOLBAR_ITEMS",
    "RazorEnhanced.settings.AUTOLOOT",
    "RazorEnhanced.settings.SCAVENGER",
    "RazorEnhanced.settings.ORGANIZER",
    "RazorEnhanced.settings.RESTOCK",
    "RazorEnhanced.settings.DRESS",
    "RazorEnhanced.settings.BUY",
    "RazorEnhanced.settings.SELL",
    "RazorEnhanced.settings.BANDAGE_HEAL",
    "RazorEnhanced.settings.TARGETS",
    "RazorEnhanced.settings.FRIENDS",
    "RazorEnhanced.settings.SCRIPTS",
}};

class JsonValue
{
public:
    JsonValue(JSContext *context, JSValue value) : m_Context(context), m_Value(value) {}
    JsonValue(const JsonValue &) = delete;
    JsonValue(JsonValue &&other) : m_Context(other.m_Context), m_Value(other.m_Value)
    {
        other.m_Value = JS_UNDEFINED;
    }
    JsonValue &operator=(JsonValue &&other)
    {
        if (this != &other)
        {
            JS_FreeValue(m_Context, m_Value);
            m_Context = other.m_Context;
            m_Value = other.m_Value;
            other.m_Value = JS_UNDEFINED;
        }
        return *this;
    }
    ~JsonValue() { JS_FreeValue(m_Context, m_Value); }
    JSValueConst Get() const { return m_Value; }

private:
    JSContext *m_Context;
    JSValue m_Value;
};

JsonValue field(JSContext *ctx, JSValueConst row, const char *key)
{
    return JsonValue(ctx, JS_GetPropertyStr(ctx, row, key));
}

bool integer(JSContext *ctx, JSValueConst value, int minimum, int maximum, int &output)
{
    double raw;
    if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &raw, value) != 0 ||
        !(raw >= minimum && raw <= maximum))
    {
        return false;
    }
    output = static_cast<int>(raw);
    return output == raw;
}

bool boolean(JSContext *ctx, JSValueConst value, bool &output)
{
    if (!JS_IsBool(value))
    {
        return false;
    }
    output = JS_ToBool(ctx, value) != 0;
    return true;
}

bool text(JSContext *ctx, JSValueConst value, std::size_t maximum, std::string &output)
{
    if (!JS_IsString(value))
    {
        return false;
    }
    size_t size = 0;
    const auto *raw = JS_ToCStringLen(ctx, &size, value);
    if (raw == nullptr)
    {
        return false;
    }
    const bool safe = size <= maximum && std::all_of(raw, raw + size,
        [](unsigned char c) { return c >= 32 && c != 127; });
    if (safe)
    {
        output.assign(raw, size);
    }
    JS_FreeCString(ctx, raw);
    return safe;
}

void put(RazorImportResult &result, const char *file, const std::string &section,
    const char *key, std::string value)
{
    result.ProfileOptions.push_back({ key, std::move(value), file, section });
}

struct Setting
{
    const char *Source;
    const char *File;
    const char *Section;
    const char *Key;
    int Minimum = 0;
    int Maximum = 0;
};

// Equal bounds here mean a Boolean source. The fields are explicitly allowlisted.
constexpr Setting GeneralSettings[] = {
    { "ObjectDelay", "Options.ini", "main", "useitemsdelay", 0, 10000 },
    { "CorpseRange", "Options.ini", "main", "opencorpsedistance", 1, 24 },
    { "SmartLastTarget", "Targeting.ini", "Targeting", "smartlast" },
    { "QueueTargets", "Targeting.ini", "Targeting", "queue" },
    { "BlockDismount", "Targeting.ini", "Targeting", "blockwardismount" },
    { "BlockHealPoison", "GameplayFilters.ini", "Options", "guardhealing" },
    { "FilterSnoopMsg", "GameplayFilters.ini", "Options", "snooping" },
    { "FilterSpam", "GameplayFilters.ini", "Options", "systemrepeats" },
    { "ShowHealth", "ItemCounters.ini", "Display", "health_changes" },
    { "ShowPartyStats", "ItemCounters.ini", "Display", "party_stats" },
    { "BandageHealcountdownCheckBox", "ItemCounters.ini", "Meta", "bandage" },
    { "BandageHealhpTextBox", "BandageHeal.ini", "BandageHeal", "hit_percent", 1, 100 },
    { "BandageHealMaxRangeTextBox", "BandageHeal.ini", "BandageHeal", "distance", 1, 2 },
    { "BandageHealpoisonCheckBox", "BandageHeal.ini", "BandageHeal", "treat_poison" },
    { "BandageHealmortalCheckBox", "BandageHeal.ini", "BandageHeal", "pause_mortal" },
    { "BandageHealhiddedCheckBox", "BandageHeal.ini", "BandageHeal", "pause_hidden" },
    { "BandageHealcustomCheckBox", "BandageHeal.ini", "BandageHeal", "specified_bandage" },
    { "BandageHealcustomIDTextBox", "BandageHeal.ini", "BandageHeal", "graphic", 1, 65534 },
    { "BandageHealcustomcolorTextBox", "BandageHeal.ini", "BandageHeal", "hue", -1, AnyHue },
    { "PotionEquip", "PotionHands.ini", "PotionHands", "enabled" },
    { "LockToolBarCheckBox", "Toolbar.ini", "Toolbar", "locked" },
    { "AutoopenToolBarCheckBox", "Toolbar.ini", "Toolbar", "open_on_login" },
};

uint32_t count_fields(JSContext *ctx, JSValueConst row)
{
    JSPropertyEnum *entries = nullptr;
    uint32_t count = 0;
    if (JS_GetOwnPropertyNames(ctx, &entries, &count, row, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0)
    {
        return 0;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        JS_FreeAtom(ctx, entries[i].atom);
    }
    js_free(ctx, entries);
    return count;
}

void parse_general(JSContext *ctx, JSValueConst row, RazorImportResult &result)
{
    std::size_t handled = 0;
    for (const auto &setting : GeneralSettings)
    {
        auto value = field(ctx, row, setting.Source);
        if (JS_IsUndefined(value.Get()))
        {
            continue;
        }
        std::string converted;
        if (setting.Minimum == setting.Maximum)
        {
            bool enabled;
            if (boolean(ctx, value.Get(), enabled))
            {
                converted = enabled ? "true" : "false";
            }
        }
        else
        {
            int number;
            if (integer(ctx, value.Get(), setting.Minimum, setting.Maximum, number))
            {
                converted = std::to_string(number == -1 ? AnyHue : number);
            }
        }
        if (converted.empty())
        {
            result.Notices.push_back({ 0, std::string("Skipped an invalid value for ") + setting.Source + "." });
            continue;
        }
        put(result, setting.File, setting.Section, setting.Key, converted);
        if (std::string(setting.Source) == "ObjectDelay")
        {
            put(result, "Options.ini", "main", "moveitemsdelay", converted);
        }
        ++handled;
    }

    auto check = field(ctx, row, "RangeCheckLT");
    auto range = field(ctx, row, "LTRange");
    bool enabled = false;
    int tiles = 12;
    if (boolean(ctx, check.Get(), enabled) &&
        (!enabled || JS_IsUndefined(range.Get()) || integer(ctx, range.Get(), 1, 30, tiles)))
    {
        put(result, "Targeting.ini", "Targeting", "lastrange", std::to_string(enabled ? tiles : 0));
        ++handled;
        if (!JS_IsUndefined(range.Get()))
        {
            ++handled;
        }
    }
    result.Imported += handled;
    const auto count = count_fields(ctx, row);
    if (count > handled)
    {
        result.Ignored += count - handled;
        result.Notices.push_back({ 0, "Skipped " + std::to_string(count - handled) +
            " Enhanced general settings. Only supported preferences are copied." });
    }
}

// The group matters: Enhanced uses labels such as Self in more than one group.
struct HotkeyMapping { const char *Group; const char *Name; unsigned ClassicId; };
constexpr HotkeyMapping EnhancedHotkeys[] = {
    { "Target", "Target Self", 1059 }, { "Target", "Target Last", 1058 },
    { "Target", "Clear Target Queue", 1195 }, { "Target", "Cancel", 1394 },
    { "Target", "Set Last", 1396 },
    { "Target", "Closest Grey", 1311 }, { "Target", "Closest Criminal", 1312 },
    { "Target", "Closest Murderer", 1313 }, { "Target", "Closest Enemy", 1314 },
    { "Target", "Closest Friend", 1315 }, { "Target", "Closest Non-Friendly", 1316 },
    { "Target", "Next Grey", 1317 }, { "Target", "Next Criminal", 1318 },
    { "Target", "Next Murderer", 1319 }, { "Target", "Next Enemy", 1320 },
    { "Target", "Next Friend", 1321 }, { "Target", "Next Non-Friendly", 1322 },
    { "Target", "Closest Innocent", 1325 }, { "Target", "Next Innocent", 1326 },
    { "Attack", "Attack Last", 1196 }, { "Attack", "Attack Last Target", 1395 },
    { "Use", "Last Item", 1371 }, { "Use", "Right Hand", 2069 }, { "Use", "Left Hand", 2070 },
    { "Bandage", "Self", 1025 }, { "Bandage", "Last", 1026 }, { "Bandage", "Friend", 1027 },
    { "Potions", "Potion Heal", 1028 }, { "Potions", "Potion Cure", 1029 },
    { "Potions", "Potion Refresh", 1030 }, { "Potions", "Potion Nightsight", 1031 },
    { "Potions", "Potion Explosion", 1032 }, { "Potions", "Potion Strength", 1033 },
    { "Potions", "Potion Agility", 1034 },
    { "Show Names", "All", 1372 }, { "Show Names", "Corpses", 1969 },
    { "Show Names", "Mobiles", 1970 }, { "Show Names", "Humanoids", 1971 },
    { "Skills", "Last Skill", 1369 },
    { "Spells", "Last Spell", 1370 },
    { "Dress", "Undress All", 1039 }, { "Dress", "Undress Both Hands", 1040 },
    { "Dress", "Undress Left Hand", 1041 }, { "Dress", "Undress Right Hand", 1042 },
    { "Dress", "Arm/Disarm Right Hand", 1043 }, { "Dress", "Arm/Disarm Left Hand", 1045 },
    { "General", "Toggle War/Peace", 1055 }, { "General", "Stop Current Macro", 1004 },
    { "Agents", "UseOnce", 1380 }, { "Agents", "Scavenger", 1381 },
    { "Agents", "Organizer", 1382 }, { "Agents", "Restock", 1383 },
    { "Agents", "Clear Scavenger Cache", 1397 }, { "Agents", "Stop Agents", 1398 },
};

bool parse_hotkey(JSContext *ctx, JSValueConst row, RazorImportResult &result)
{
    auto keyValue = field(ctx, row, "Key");
    int packed;
    if (!integer(ctx, keyValue.Get(), 0, 0x7FFFF, packed))
    {
        return false;
    }
    if (packed == 0)
    {
        return true; // Ordinary unassigned placeholders in upstream profiles.
    }
    std::string group, name;
    bool pass;
    if (!text(ctx, field(ctx, row, "Group").Get(), 63, group) ||
        !text(ctx, field(ctx, row, "Name").Get(), 63, name) ||
        !boolean(ctx, field(ctx, row, "Pass").Get(), pass))
    {
        return false;
    }
    const auto mapping = std::find_if(std::begin(EnhancedHotkeys), std::end(EnhancedHotkeys), [&](const auto &it) {
        return group == it.Group && name == it.Name;
    });
    unsigned classicId = 0;
    if (mapping != std::end(EnhancedHotkeys))
    {
        classicId = mapping->ClassicId;
    }
    else if (group == "Spells" || group == "Spells Magery")
    {
        const auto *spellDef = FindRazorHotkey(name);
        if (spellDef != nullptr && spellDef->Group == RazorHotkeyGroup::CastSpell)
        {
            classicId = spellDef->Id;
        }
    }
    const auto *resolved = classicId != 0 ? FindRazorHotkey(classicId) : nullptr;
    if (resolved == nullptr ||
        (resolved->Group == RazorHotkeyGroup::ClientAction && resolved->Action != 0))
    {
        return false;
    }

    int key = packed & 0xFFFF;
    if (key >= 500 && key <= 504)
    {
        constexpr int MouseKeys[] = { -3, -2, -1, -4, -5 };
        key = MouseKeys[key - 500];
    }
    const unsigned mods = ((packed & 0x40000) ? 1 : 0) | ((packed & 0x20000) ? 2 : 0) |
        ((packed & 0x10000) ? 4 : 0);
    if (RazorHotkeyKeyName(key).empty() || (key == 0x73 && (mods & 1) && !pass) ||
        std::any_of(result.ProfileHotkeys.begin(), result.ProfileHotkeys.end(), [&](const auto &binding) {
            return binding.VirtualKey == key && binding.Modifiers == mods;
        }))
    {
        return false;
    }
    result.ProfileHotkeys.push_back({ classicId, key, mods, pass });
    ++result.Imported;
    return true;
}

std::string hex_string(uint32_t value, int digits)
{
    static const char HEX[] = "0123456789ABCDEF";
    std::string out = "0x";
    for (int i = digits - 1; i >= 0; --i)
    {
        out += HEX[(value >> (i * 4)) & 0xF];
    }
    return out;
}

bool unsigned32(JSContext *ctx, JSValueConst value, uint32_t minimum, uint32_t maximum, uint32_t &output)
{
    double raw;
    if (!JS_IsNumber(value) || JS_ToFloat64(ctx, &raw, value) != 0 ||
        !(raw >= static_cast<double>(minimum) && raw <= static_cast<double>(maximum)))
    {
        return false;
    }
    output = static_cast<uint32_t>(raw);
    return static_cast<double>(output) == raw;
}

struct EnhancedTableState
{
    std::size_t AgentCount = 0;
    std::size_t DressCount = 0;
    std::size_t ShopCount = 0;
    std::size_t TargetCount = 0;
    std::size_t FriendCount = 0;
    std::size_t ScriptCount = 0;
    bool HasSellHotBag = false;
    bool SawNonJsScript = false;
};

constexpr std::size_t MaxEnhancedLists = 32;
constexpr int MaxEnhancedItems = 256;
constexpr std::size_t MaxTargetPresets = 24;
constexpr std::size_t MaxFriends = 256;
constexpr std::size_t MaxScripts = 128;

std::string fold_lower(std::string s)
{
    for (auto &ch : s)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

bool parse_inventory_item_rule(JSContext *ctx, JSValueConst item, const std::string &section,
    std::size_t ruleIndex, RazorImportResult &result)
{
    int graphic = 0;
    auto graphicVal = field(ctx, item, "Graphics");
    if (JS_IsUndefined(graphicVal.Get()))
    {
        graphicVal = field(ctx, item, "ItemID");
    }
    if (!integer(ctx, graphicVal.Get(), 1, 65534, graphic))
    {
        return false;
    }
    int color = -1;
    auto colorVal = field(ctx, item, "Color");
    if (!JS_IsUndefined(colorVal.Get()) && !integer(ctx, colorVal.Get(), -1, AnyHue, color))
    {
        return false;
    }
    int amount = 1;
    auto amountVal = field(ctx, item, "Amount");
    if (!JS_IsUndefined(amountVal.Get()) && !integer(ctx, amountVal.Get(), 1, 65535, amount))
    {
        return false;
    }
    const std::string ruleSec = section + "Rule" + std::to_string(ruleIndex);
    put(result, "InventoryAgents.ini", ruleSec, "graphic", hex_string(static_cast<uint32_t>(graphic), 4));
    const bool matchHue = color >= 0 && color < AnyHue;
    put(result, "InventoryAgents.ini", ruleSec, "hue", std::to_string(matchHue ? color : 0));
    put(result, "InventoryAgents.ini", ruleSec, "matchhue", matchHue ? "true" : "false");
    put(result, "InventoryAgents.ini", ruleSec, "quantity", std::to_string(amount));
    return true;
}

bool parse_inventory_agent_row(JSContext *ctx, JSValueConst row, int mode, const char *defaultPrefix,
    EnhancedTableState &state, RazorImportResult &result)
{
    if (state.AgentCount >= MaxEnhancedLists)
    {
        return false;
    }
    std::string name;
    if (!text(ctx, field(ctx, row, "Description").Get(), 63, name) &&
        !text(ctx, field(ctx, row, "Name").Get(), 63, name))
    {
        name = std::string(defaultPrefix) + " " + std::to_string(state.AgentCount + 1);
    }
    if (name.empty())
    {
        return false;
    }
    uint32_t sourceSerial = 0;
    uint32_t destSerial = 0;
    auto srcVal = field(ctx, row, "Source");
    if (!JS_IsUndefined(srcVal.Get())) unsigned32(ctx, srcVal.Get(), 1, 0xFFFFFFFEu, sourceSerial);
    auto destVal = field(ctx, row, "Destination");
    if (JS_IsUndefined(destVal.Get())) destVal = field(ctx, row, "Bag");
    if (!JS_IsUndefined(destVal.Get())) unsigned32(ctx, destVal.Get(), 1, 0xFFFFFFFEu, destSerial);

    const std::string section = "Agent" + std::to_string(state.AgentCount);
    std::size_t ruleCount = 0;
    auto itemsVal = field(ctx, row, "Items");
    if (JS_IsArray(itemsVal.Get()))
    {
        int len = 0;
        if (!integer(ctx, field(ctx, itemsVal.Get(), "length").Get(), 0, MaxEnhancedItems, len))
        {
            return false;
        }
        for (int i = 0; i < len; ++i)
        {
            const JsonValue item(ctx, JS_GetPropertyUint32(ctx, itemsVal.Get(), static_cast<uint32_t>(i)));
            if (!JS_IsObject(item.Get()) || JS_IsArray(item.Get()))
            {
                ++result.Ignored;
                continue;
            }
            bool selected = true;
            auto selVal = field(ctx, item.Get(), "Selected");
            if (!JS_IsUndefined(selVal.Get()) && boolean(ctx, selVal.Get(), selected) && !selected)
            {
                continue;
            }
            if (parse_inventory_item_rule(ctx, item.Get(), section, ruleCount, result))
            {
                ++ruleCount;
            }
            else
            {
                ++result.Ignored;
            }
        }
    }
    else if (!JS_IsUndefined(field(ctx, row, "Graphics").Get()) ||
             !JS_IsUndefined(field(ctx, row, "ItemID").Get()))
    {
        bool selected = true;
        auto selVal = field(ctx, row, "Selected");
        if (!JS_IsUndefined(selVal.Get()) && boolean(ctx, selVal.Get(), selected) && !selected)
        {
            return true;
        }
        if (!parse_inventory_item_rule(ctx, row, section, 0, result))
        {
            return false;
        }
        ruleCount = 1;
    }
    else if (sourceSerial == 0 && destSerial == 0)
    {
        return false;
    }

    put(result, "InventoryAgents.ini", section, "name", name);
    put(result, "InventoryAgents.ini", section, "mode", std::to_string(mode));
    if (sourceSerial != 0)
    {
        put(result, "InventoryAgents.ini", section, "source", hex_string(sourceSerial, 8));
    }
    if (destSerial != 0)
    {
        put(result, "InventoryAgents.ini", section, "destination", hex_string(destSerial, 8));
    }
    put(result, "InventoryAgents.ini", section, "rules", std::to_string(ruleCount));
    ++state.AgentCount;
    result.Imported += ruleCount > 0 ? ruleCount : 1;
    return true;
}

bool parse_dress_row(JSContext *ctx, JSValueConst row, EnhancedTableState &state, RazorImportResult &result)
{
    if (state.DressCount >= MaxEnhancedLists)
    {
        return false;
    }
    std::string name;
    if (!text(ctx, field(ctx, row, "Description").Get(), 63, name) &&
        !text(ctx, field(ctx, row, "Name").Get(), 63, name))
    {
        name = "Dress " + std::to_string(state.DressCount + 1);
    }
    if (name.empty())
    {
        return false;
    }
    const std::string section = "Dress" + std::to_string(state.DressCount);
    std::size_t itemCount = 0;
    auto itemsVal = field(ctx, row, "Items");
    if (JS_IsArray(itemsVal.Get()))
    {
        int len = 0;
        if (!integer(ctx, field(ctx, itemsVal.Get(), "length").Get(), 0, 25, len))
        {
            return false;
        }
        for (int i = 0; i < len; ++i)
        {
            const JsonValue item(ctx, JS_GetPropertyUint32(ctx, itemsVal.Get(), static_cast<uint32_t>(i)));
            if (!JS_IsObject(item.Get()) || JS_IsArray(item.Get()))
            {
                ++result.Ignored;
                continue;
            }
            bool selected = true;
            auto selVal = field(ctx, item.Get(), "Selected");
            if (!JS_IsUndefined(selVal.Get()) && boolean(ctx, selVal.Get(), selected) && !selected)
            {
                continue;
            }
            uint32_t ser = 0;
            int layer = 0;
            if (!unsigned32(ctx, field(ctx, item.Get(), "Serial").Get(), 1, 0xFFFFFFFEu, ser))
            {
                ++result.Ignored;
                continue;
            }
            const std::string itemKey = "item" + std::to_string(itemCount);
            put(result, "DressSets.ini", section, itemKey.c_str(), hex_string(ser, 8));
            if (integer(ctx, field(ctx, item.Get(), "Layer").Get(), 1, 24, layer))
            {
                const std::string layerKey = "layer" + std::to_string(itemCount);
                put(result, "DressSets.ini", section, layerKey.c_str(), std::to_string(layer));
            }
            ++itemCount;
        }
    }
    else
    {
        uint32_t ser = 0;
        if (!unsigned32(ctx, field(ctx, row, "Serial").Get(), 1, 0xFFFFFFFEu, ser))
        {
            return false;
        }
        put(result, "DressSets.ini", section, "item0", hex_string(ser, 8));
        int layer = 0;
        if (integer(ctx, field(ctx, row, "Layer").Get(), 1, 24, layer))
        {
            put(result, "DressSets.ini", section, "layer0", std::to_string(layer));
        }
        itemCount = 1;
    }
    if (itemCount == 0)
    {
        return false;
    }
    put(result, "DressSets.ini", section, "name", name);
    put(result, "DressSets.ini", section, "count", std::to_string(itemCount));
    ++state.DressCount;
    result.Imported += itemCount;
    return true;
}

bool parse_shop_row(JSContext *ctx, JSValueConst row, bool isSell, EnhancedTableState &state, RazorImportResult &result)
{
    if (isSell && !state.HasSellHotBag)
    {
        uint32_t bag = 0;
        if (unsigned32(ctx, field(ctx, row, "Bag").Get(), 1, 0xFFFFFFFEu, bag))
        {
            put(result, "SellHotBag.ini", "SellHotBag", "enabled", "true");
            put(result, "SellHotBag.ini", "SellHotBag", "serial", hex_string(bag, 8));
            state.HasSellHotBag = true;
            ++result.Imported;
        }
    }
    if (state.ShopCount >= MaxEnhancedLists)
    {
        return false;
    }
    std::string name;
    if (!text(ctx, field(ctx, row, "Description").Get(), 63, name) &&
        !text(ctx, field(ctx, row, "Name").Get(), 63, name))
    {
        name = std::string(isSell ? "Sell " : "Buy ") + std::to_string(state.ShopCount + 1);
    }
    if (name.empty())
    {
        return false;
    }
    const std::string section = "ShopList" + std::to_string(state.ShopCount);
    std::size_t itemCount = 0;
    auto add_item = [&](JSValueConst item) -> bool {
        int graphic = 0;
        auto graphicVal = field(ctx, item, "Graphics");
        if (JS_IsUndefined(graphicVal.Get())) graphicVal = field(ctx, item, "ItemID");
        if (!integer(ctx, graphicVal.Get(), 1, 65534, graphic))
        {
            return false;
        }
        int color = -1;
        auto colorVal = field(ctx, item, "Color");
        if (!JS_IsUndefined(colorVal.Get()) && !integer(ctx, colorVal.Get(), -1, AnyHue, color))
        {
            return false;
        }
        int amount = 0;
        auto amountVal = field(ctx, item, "Amount");
        if (!JS_IsUndefined(amountVal.Get()) && !integer(ctx, amountVal.Get(), 0, 65535, amount))
        {
            return false;
        }
        std::string itemName;
        text(ctx, field(ctx, item, "Name").Get(), 63, itemName);
        const std::string itemSec = section + "Item" + std::to_string(itemCount);
        put(result, "ShopList.ini", itemSec, "graphic", hex_string(static_cast<uint32_t>(graphic), 4));
        put(result, "ShopList.ini", itemSec, "color", hex_string(static_cast<uint32_t>(color < 0 ? AnyHue : color), 4));
        if (!itemName.empty())
        {
            put(result, "ShopList.ini", itemSec, "name", itemName);
        }
        put(result, "ShopList.ini", itemSec, "count", std::to_string(amount));
        put(result, "ShopList.ini", itemSec, "price", "0");
        ++itemCount;
        return true;
    };

    auto itemsVal = field(ctx, row, "Items");
    if (JS_IsArray(itemsVal.Get()))
    {
        int len = 0;
        if (!integer(ctx, field(ctx, itemsVal.Get(), "length").Get(), 0, MaxEnhancedItems, len))
        {
            return false;
        }
        for (int i = 0; i < len; ++i)
        {
            const JsonValue item(ctx, JS_GetPropertyUint32(ctx, itemsVal.Get(), static_cast<uint32_t>(i)));
            if (!JS_IsObject(item.Get()) || JS_IsArray(item.Get()))
            {
                ++result.Ignored;
                continue;
            }
            bool selected = true;
            auto selVal = field(ctx, item.Get(), "Selected");
            if (!JS_IsUndefined(selVal.Get()) && boolean(ctx, selVal.Get(), selected) && !selected)
            {
                continue;
            }
            if (!add_item(item.Get()))
            {
                ++result.Ignored;
            }
        }
    }
    else if (!add_item(row))
    {
        return false;
    }
    if (itemCount == 0)
    {
        return false;
    }
    put(result, "ShopList.ini", section, "name", name);
    put(result, "ShopList.ini", section, "items", std::to_string(itemCount));
    ++state.ShopCount;
    result.Imported += itemCount;
    return true;
}

bool parse_bandage_heal_row(JSContext *ctx, JSValueConst row, RazorImportResult &result)
{
    std::size_t handled = 0;
    auto check_int = [&](const char *source, const char *key, int lo, int hi, bool mapMinusOne) {
        auto val = field(ctx, row, source);
        if (JS_IsUndefined(val.Get())) return;
        int num = 0;
        if (integer(ctx, val.Get(), lo, hi, num))
        {
            put(result, "BandageHeal.ini", "BandageHeal", key,
                std::to_string((mapMinusOne && num == -1) ? AnyHue : num));
            ++handled;
        }
    };
    auto check_bool = [&](const char *source, const char *key, bool invert) {
        auto val = field(ctx, row, source);
        if (JS_IsUndefined(val.Get())) return;
        bool b = false;
        if (boolean(ctx, val.Get(), b))
        {
            put(result, "BandageHeal.ini", "BandageHeal", key, (invert ? !b : b) ? "true" : "false");
            ++handled;
        }
    };
    check_int("HpLimit", "hit_percent", 1, 100, false);
    check_int("BandageHealhpTextBox", "hit_percent", 1, 100, false);
    check_int("MaxRange", "distance", 1, 2, false);
    check_int("BandageHealMaxRangeTextBox", "distance", 1, 2, false);
    check_int("Delay", "delay", 1000, 60000, false);
    check_int("CustomDelay", "delay", 1000, 60000, false);
    check_bool("UseCustomDelay", "automatic_delay", true);
    check_bool("CustomDelayCheck", "automatic_delay", true);
    check_bool("TreatPoison", "treat_poison", false);
    check_bool("PoisonBlock", "treat_poison", false);
    check_bool("PauseMortal", "pause_mortal", false);
    check_bool("MortalBlock", "pause_mortal", false);
    check_bool("PauseHidden", "pause_hidden", false);
    check_bool("HiddenBlock", "pause_hidden", false);
    check_bool("UseCustom", "specified_bandage", false);
    check_int("CustomID", "graphic", 1, 65534, false);
    check_int("CustomColor", "hue", -1, AnyHue, true);
    check_bool("ShowCountdown", "announce", false);

    result.Imported += handled;
    const auto total = count_fields(ctx, row);
    if (total > handled)
    {
        result.Ignored += total - handled;
    }
    return handled > 0;
}

bool parse_target_row(JSContext *ctx, JSValueConst row, EnhancedTableState &state, RazorImportResult &result)
{
    if (state.TargetCount >= MaxTargetPresets)
    {
        return false;
    }
    std::string name;
    if (!text(ctx, field(ctx, row, "Name").Get(), 63, name) || name.empty())
    {
        return false;
    }
    std::string mobileName;
    text(ctx, field(ctx, row, "MobileName").Get(), 63, mobileName);
    int body = -1;
    auto bodyVal = field(ctx, row, "Body");
    if (!JS_IsUndefined(bodyVal.Get()) && !integer(ctx, bodyVal.Get(), -1, 65535, body))
    {
        return false;
    }
    if (body == -1)
    {
        auto bodiesVal = field(ctx, row, "BodyIDs");
        if (JS_IsArray(bodiesVal.Get()))
        {
            const JsonValue first(ctx, JS_GetPropertyUint32(ctx, bodiesVal.Get(), 0));
            integer(ctx, first.Get(), 0, 65535, body);
        }
    }
    int hue = -1;
    auto hueVal = field(ctx, row, "Hue");
    if (!JS_IsUndefined(hueVal.Get()) && !integer(ctx, hueVal.Get(), -1, 65535, hue))
    {
        return false;
    }
    int minDist = 0;
    int maxDist = 12;
    auto minVal = field(ctx, row, "RangeMin");
    if (!JS_IsUndefined(minVal.Get()) && !integer(ctx, minVal.Get(), 0, 30, minDist))
    {
        return false;
    }
    auto maxVal = field(ctx, row, "RangeMax");
    if (!JS_IsUndefined(maxVal.Get()) && !integer(ctx, maxVal.Get(), 0, 30, maxDist))
    {
        return false;
    }
    if (minDist > maxDist)
    {
        minDist = maxDist;
    }
    int notorieties = 0;
    auto notoVal = field(ctx, row, "Notorieties");
    if (JS_IsArray(notoVal.Get()))
    {
        int len = 0;
        if (integer(ctx, field(ctx, notoVal.Get(), "length").Get(), 0, 16, len))
        {
            for (int i = 0; i < len; ++i)
            {
                const JsonValue entry(ctx, JS_GetPropertyUint32(ctx, notoVal.Get(), static_cast<uint32_t>(i)));
                int n = 0;
                if (integer(ctx, entry.Get(), 1, 7, n))
                {
                    notorieties |= (1 << (n - 1));
                }
            }
        }
    }
    else if (!JS_IsUndefined(notoVal.Get()))
    {
        integer(ctx, notoVal.Get(), 0, 0x7F, notorieties);
    }
    int selector = 0;
    std::string selText;
    if (text(ctx, field(ctx, row, "Selector").Get(), 31, selText))
    {
        const auto folded = fold_lower(selText);
        if (folded == "weakest" || folded == "lowest")
        {
            selector = 1;
        }
    }
    const std::string section = "Preset" + std::to_string(state.TargetCount);
    put(result, "Targeting.ini", section, "name", name);
    put(result, "Targeting.ini", section, "mobile_name", mobileName);
    put(result, "Targeting.ini", section, "body", std::to_string(body));
    put(result, "Targeting.ini", section, "hue", std::to_string(hue));
    put(result, "Targeting.ini", section, "min_distance", std::to_string(minDist));
    put(result, "Targeting.ini", section, "max_distance", std::to_string(maxDist));
    put(result, "Targeting.ini", section, "notorieties", std::to_string(notorieties));
    put(result, "Targeting.ini", section, "selector", std::to_string(selector));
    ++state.TargetCount;
    ++result.Imported;
    return true;
}

bool parse_friend_row(JSContext *ctx, JSValueConst row, EnhancedTableState &state, RazorImportResult &result)
{
    bool handledAny = false;
    bool partyFriend = false;
    auto partyVal = field(ctx, row, "IncludeParty");
    if (JS_IsUndefined(partyVal.Get())) partyVal = field(ctx, row, "PartyAsFriend");
    if (!JS_IsUndefined(partyVal.Get()) && boolean(ctx, partyVal.Get(), partyFriend))
    {
        put(result, "Friends.ini", "Meta", "feel_party_members_as_friends", partyFriend ? "true" : "false");
        ++result.Imported;
        handledAny = true;
    }
    auto add_player = [&](JSValueConst entry) -> bool {
        if (state.FriendCount >= MaxFriends)
        {
            return false;
        }
        uint32_t ser = 0;
        if (!unsigned32(ctx, field(ctx, entry, "Serial").Get(), 1, 0xFFFFFFFEu, ser))
        {
            return false;
        }
        std::string name;
        if (!text(ctx, field(ctx, entry, "Name").Get(), 63, name) || name.empty())
        {
            name = hex_string(ser, 8);
        }
        bool enabled = true;
        auto enVal = field(ctx, entry, "Enabled");
        if (!JS_IsUndefined(enVal.Get()) && !boolean(ctx, enVal.Get(), enabled))
        {
            return false;
        }
        const std::string section = "Friend" + std::to_string(state.FriendCount);
        put(result, "Friends.ini", section, "id", hex_string(ser, 8));
        put(result, "Friends.ini", section, "name", name);
        put(result, "Friends.ini", section, "enabled", enabled ? "true" : "false");
        ++state.FriendCount;
        ++result.Imported;
        return true;
    };
    auto playersVal = field(ctx, row, "Players");
    if (JS_IsArray(playersVal.Get()))
    {
        int len = 0;
        if (!integer(ctx, field(ctx, playersVal.Get(), "length").Get(), 0, static_cast<int>(MaxFriends), len))
        {
            return false;
        }
        for (int i = 0; i < len; ++i)
        {
            const JsonValue player(ctx, JS_GetPropertyUint32(ctx, playersVal.Get(), static_cast<uint32_t>(i)));
            if (!JS_IsObject(player.Get()) || JS_IsArray(player.Get()) || !add_player(player.Get()))
            {
                ++result.Ignored;
            }
            else
            {
                handledAny = true;
            }
        }
        return handledAny;
    }
    if (!JS_IsUndefined(field(ctx, row, "Serial").Get()))
    {
        return add_player(row) || handledAny;
    }
    return handledAny;
}

bool safe_script_filename(const std::string &name)
{
    if (name.empty() || name.size() > 63 || name.find("..") != std::string::npos ||
        name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos)
    {
        return false;
    }
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
    {
        return false;
    }
    const auto ext = fold_lower(name.substr(dot));
    return ext == ".js" || ext == ".uos" || ext == ".txt" || ext == ".py" || ext == ".cs";
}

bool parse_script_row(JSContext *ctx, JSValueConst row, EnhancedTableState &state, RazorImportResult &result)
{
    if (state.ScriptCount >= MaxScripts)
    {
        return false;
    }
    std::string filename;
    if (!text(ctx, field(ctx, row, "Filename").Get(), 63, filename) &&
        !text(ctx, field(ctx, row, "Name").Get(), 63, filename))
    {
        return false;
    }
    if (!safe_script_filename(filename))
    {
        return false;
    }
    bool loop = false;
    auto loopVal = field(ctx, row, "Loop");
    if (!JS_IsUndefined(loopVal.Get()) && !boolean(ctx, loopVal.Get(), loop))
    {
        return false;
    }
    const auto dot = filename.find_last_of('.');
    const auto ext = fold_lower(filename.substr(dot));
    if (ext == ".py" || ext == ".cs")
    {
        state.SawNonJsScript = true;
    }
    const std::string section = "Script" + std::to_string(state.ScriptCount);
    put(result, "Scripts.ini", section, "filename", filename);
    put(result, "Scripts.ini", section, "loop", loop ? "true" : "false");
    put(result, "Scripts.ini", section, "autostart", "false");
    ++state.ScriptCount;
    ++result.Imported;
    return true;
}

bool parse_toolbar_slot(JSContext *ctx, JSValueConst row, int index, RazorImportResult &result)
{
    int graphic;
    if (!integer(ctx, field(ctx, row, "Graphics").Get(), 0, 65534, graphic))
    {
        return false;
    }
    if (index >= MaxToolbarSlots)
    {
        return graphic == 0;
    }
    int hue = AnyHue;
    int limit = 0;
    bool warnings = false;
    std::string name;
    if (graphic != 0 && (!integer(ctx, field(ctx, row, "Color").Get(), -1, AnyHue, hue) ||
        !boolean(ctx, field(ctx, row, "Warning").Get(), warnings) ||
        (warnings && !integer(ctx, field(ctx, row, "WarningLimit").Get(), 0, 65535, limit)) ||
        !text(ctx, field(ctx, row, "Name").Get(), 63, name)))
    {
        return false;
    }
    const auto section = "Slot" + std::to_string(index);
    put(result, "Toolbar.ini", section, "graphic", std::to_string(graphic));
    put(result, "Toolbar.ini", section, "hue", std::to_string(hue == -1 ? AnyHue : hue));
    put(result, "Toolbar.ini", section, "alert", std::to_string(limit));
    put(result, "Toolbar.ini", section, "name", name);
    ++result.Imported;
    return true;
}

} // namespace

const char *RazorEnhancedTableFile(RazorEnhancedTable table)
{
    const auto index = static_cast<std::size_t>(table);
    return index < TableFiles.size() ? TableFiles[index] : nullptr;
}

RazorImportResult ParseRazorEnhancedProfile(const std::vector<RazorEnhancedProfileInput> &inputs)
{
    RazorImportResult result;
    std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)> runtime(JS_NewRuntime(), JS_FreeRuntime);
    if (runtime)
    {
        JS_SetMemoryLimit(runtime.get(), 8 * 1024 * 1024);
        JS_SetMaxStackSize(runtime.get(), 128 * 1024);
    }
    std::unique_ptr<JSContext, decltype(&JS_FreeContext)> context(
        runtime ? JS_NewContext(runtime.get()) : nullptr, JS_FreeContext);
    if (!context)
    {
        result.Issues.push_back({ 0, "Could not open the Enhanced profile reader." });
        return result;
    }
    auto *ctx = context.get();
    std::array<bool, TableCount> seen{};
    EnhancedTableState state;
    for (const auto &input : inputs)
    {
        const auto index = static_cast<std::size_t>(input.Table);
        const auto *filename = RazorEnhancedTableFile(input.Table);
        if (filename == nullptr || seen[index] || input.Text.size() > MaxBytes ||
            input.Text.find('\0') != std::string::npos)
        {
            result.Issues.push_back({ 0, "Choose distinct supported Enhanced tables of up to 256 KiB each." });
            break;
        }
        seen[index] = true;
        const std::size_t start = input.Text.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
        const JsonValue parsed(ctx, JS_ParseJSON(ctx, input.Text.data() + start, input.Text.size() - start, filename));
        int length = 0;
        if (!JS_IsArray(parsed.Get()) || !integer(ctx, field(ctx, parsed.Get(), "length").Get(), 0, MaxRows, length) ||
            (input.Table == RazorEnhancedTable::General && length != 1))
        {
            result.Issues.push_back({ 0, std::string("Invalid Enhanced profile table ") + filename + "." });
            continue;
        }
        std::size_t skipped = 0;
        for (int i = 0; i < length; ++i)
        {
            const JsonValue row(ctx, JS_GetPropertyUint32(ctx, parsed.Get(), i));
            if (!JS_IsObject(row.Get()) || JS_IsArray(row.Get()))
            {
                ++skipped;
                if (input.Table == RazorEnhancedTable::General)
                {
                    result.Issues.push_back({ 0, "Enhanced general settings must contain one object." });
                }
                continue;
            }
            switch (input.Table)
            {
                case RazorEnhancedTable::General: parse_general(ctx, row.Get(), result); break;
                case RazorEnhancedTable::Hotkeys: skipped += !parse_hotkey(ctx, row.Get(), result); break;
                case RazorEnhancedTable::ToolbarItems: skipped += !parse_toolbar_slot(ctx, row.Get(), i, result); break;
                case RazorEnhancedTable::AutoLoot:
                    skipped += !parse_inventory_agent_row(ctx, row.Get(), 4, "AutoLoot", state, result); break;
                case RazorEnhancedTable::Scavenger:
                    skipped += !parse_inventory_agent_row(ctx, row.Get(), 2, "Scavenger", state, result); break;
                case RazorEnhancedTable::Organizer:
                    skipped += !parse_inventory_agent_row(ctx, row.Get(), 0, "Organizer", state, result); break;
                case RazorEnhancedTable::Restock:
                    skipped += !parse_inventory_agent_row(ctx, row.Get(), 1, "Restock", state, result); break;
                case RazorEnhancedTable::Dress:
                    skipped += !parse_dress_row(ctx, row.Get(), state, result); break;
                case RazorEnhancedTable::Buy:
                    skipped += !parse_shop_row(ctx, row.Get(), false, state, result); break;
                case RazorEnhancedTable::Sell:
                    skipped += !parse_shop_row(ctx, row.Get(), true, state, result); break;
                case RazorEnhancedTable::BandageHeal:
                    skipped += !parse_bandage_heal_row(ctx, row.Get(), result); break;
                case RazorEnhancedTable::Targets:
                    skipped += !parse_target_row(ctx, row.Get(), state, result); break;
                case RazorEnhancedTable::Friends:
                    skipped += !parse_friend_row(ctx, row.Get(), state, result); break;
                case RazorEnhancedTable::Scripts:
                    skipped += !parse_script_row(ctx, row.Get(), state, result); break;
                case RazorEnhancedTable::Count: break;
            }
        }
        if (skipped != 0)
        {
            result.Ignored += skipped;
            result.Notices.push_back({ 0, "Skipped " + std::to_string(skipped) + " unsupported or invalid entries in " +
                filename + "." });
        }
    }
    if (state.AgentCount > 0)
    {
        put(result, "InventoryAgents.ini", "Meta", "count", std::to_string(state.AgentCount));
    }
    if (state.DressCount > 0)
    {
        put(result, "DressSets.ini", "Meta", "count", std::to_string(state.DressCount));
    }
    if (state.ShopCount > 0)
    {
        put(result, "ShopList.ini", "Meta", "count", std::to_string(state.ShopCount));
    }
    if (state.TargetCount > 0)
    {
        put(result, "Targeting.ini", "Targeting", "presetcount", std::to_string(state.TargetCount));
    }
    if (state.FriendCount > 0)
    {
        put(result, "Friends.ini", "Meta", "count", std::to_string(state.FriendCount));
    }
    if (state.ScriptCount > 0)
    {
        put(result, "Scripts.ini", "Meta", "count", std::to_string(state.ScriptCount));
        result.Notices.push_back({ 0, "Imported script entries are saved inactive without autostart." });
        if (state.SawNonJsScript)
        {
            result.Notices.push_back({ 0, "Python and C# script references need native JavaScript versions before playing." });
        }
    }
    if (result.Imported == 0 && result.Issues.empty())
    {
        result.Issues.push_back({ 0, "These Enhanced tables contain no supported settings or bindings." });
    }
    return result;
}

} // namespace xuoassist
