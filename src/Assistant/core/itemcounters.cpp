// SPDX-License-Identifier: AGPL-3.0-or-later

#include "itemcounters.h"
#include "itemcounters_rules.h"

#include "assist.h"
#include "assistantevents.h"
#include "inventoryagent.h"
#include "../macros/macroexecutor.h"
#include "../model/commonitems/journalmessage.h"
#include "Managers/ClilocManager.h"
#include "Managers/GumpManager.h"
#include "Managers/PacketManager.h"
#include "UseItemsList.h"
#include "AssetViewer/AssetGrid.h"
#include "AssetViewer/AssetTextureCache.h"
#include "Managers/ConfigManager.h"
#include "Managers/ColorManager.h"
#include "world.h"
#include "worldutil.h"
#include "../config/assistantconfig.h"
#include "../Misc.h"
#include "../config/configio.h"
#include "../managers/displaymanager.h"
#include "../script/scriptstate.h"
#include "Globals.h"
#include "Party.h"
#include "ServerList.h"
#include "GameObjects/ObjectOnCursor.h"
#include "Spells.h"
#include "Target.h"
#include <external/gfx/imgui/imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <set>

namespace xuoassist {

namespace {

constexpr int MaxCounters = 256;
constexpr int AnyHue = 0xFFFF;
constexpr uint32_t RefreshInterval = 250;
constexpr uint32_t CastHighlightDuration = 2000;
constexpr uint32_t GateDuration = 30000;
// Leave room for the server to process the previous use on its next tick.
constexpr uint32_t RecountInterval = 1200;
constexpr uint32_t RecountTimeout = 2500;
constexpr size_t RecountLimit = 128;
constexpr size_t ContainerLabelLimit = 256;
const ImVec4 LowColor(1.0f, 0.43f, 0.28f, 1.0f);

struct ItemCounter
{
    astr_t Name;
    astr_t Key;
    int Graphic = 0;
    int Hue = AnyHue;
    int Warning = 0;
    bool Visible = true;
    bool Icon = true;
    int Count = -1;
};

struct CounterEditor
{
    std::array<char, 64> Name{};
    std::array<char, 32> Key{};
    int Graphic = 0;
    int Hue = 0;
    int Warning = 0;
    bool AnyColor = true;
    bool Visible = true;
    bool Icon = true;
};

vector<ItemCounter> s_Counters;
astr_t s_Profile;
bool s_ShowHUD = false;
bool s_HUDVisible = false;
bool s_ShowIcons = false;
bool s_Warnings = false;
bool s_AutoRecount = true;
bool s_SkipPouches = true;
bool s_StatusExpanded = false;
bool s_BandageEnabled = false;
int s_BandageSeconds = 5;
bool s_Bandaging = false;
uint32_t s_BandageAt = 0;
bool s_GoldEnabled = false;
bool s_GoldStarted = false;
uint32_t s_GoldAt = 0;
int64_t s_GoldBase = 0;
struct DisplayOptions
{
    bool Caption = false;
    bool Notoriety = false;
    bool Reagents = false;
    bool Health = false;
    bool Party = false;
    bool Buffs = false;
    bool DamageTaken = false;
    bool OtherDamage = false;
    bool MessageRules = true;
    bool UnicodeRules = true;
    astr_t BuffFormat = "[{action}{name}]";
    astr_t HealthFormat = "[{0%}]";
    astr_t PartyFormat = "[{0}% / {1}%]";
    astr_t RuleFormat = "{msg}";
};
DisplayOptions s_Options;
uint16_t s_CastSpell = 0;
uint32_t s_CastAt = 0;
uint32_t s_GateAt = 0;
bool s_Gate = false;
uint32_t s_SkillAt = 0;
bool s_UsedSkill = false;
uint32_t s_OnlineAt = 0;
bool s_Online = false;
uint32_t s_CriminalAt = 0;
bool s_Criminal = false;
vector<uint16_t> s_CastReagents;
struct OverheadRule
{
    astr_t Match;
    astr_t Text;
};
vector<OverheadRule> s_OverheadRules;
std::array<char, 160> s_RuleMatch{};
std::array<char, 160> s_RuleText{};
int s_RuleSelected = -1;
int s_Selected = -1;
CounterEditor s_Editor;
astr_t s_Feedback;
uint32_t s_Pack = 0;
uint32_t s_LastRefresh = 0;
bool s_NeedsRefresh = true;
uint64_t s_Revision = 1;
struct BackpackRecount
{
    uint32_t Backpack = 0;
    uint32_t Pending = 0;
    uint32_t RequestedAt = 0;
    unsigned Unavailable = 0;
    bool Manual = false;
    std::set<uint32_t> Attempted;
};
BackpackRecount s_Recount;
std::set<uint32_t> s_AutoAttempted;
std::set<uint32_t> s_NonBagSerials;
std::vector<uint32_t> s_PriorityBags;
uint32_t s_LastObjectUse = 0;
bool s_HasObjectUse = false;
struct ContainerLabel
{
    uint32_t Serial = 0;
    uint16_t Graphic = 0;
    astr_t Shard;
    astr_t Label;
};
vector<ContainerLabel> s_ContainerLabels;
ContainerLabel s_LabelTarget;
std::array<char, 64> s_LabelText{};

vector<ItemCounter> default_counters()
{
    return {
        { "Bandages", "bandages", 0x0E21, AnyHue, 10 },
        { "Black pearl", "bp", 0x0F7A, AnyHue, 5 },
        { "Blood moss", "bm", 0x0F7B, AnyHue, 5 },
        { "Garlic", "gl", 0x0F84, AnyHue, 5 },
        { "Ginseng", "gs", 0x0F85, AnyHue, 5 },
        { "Mandrake root", "mr", 0x0F86, AnyHue, 5 },
        { "Nightshade", "ns", 0x0F88, AnyHue, 5 },
        { "Spider silk", "ss", 0x0F8D, AnyHue, 5 },
        { "Sulfurous ash", "sa", 0x0F8C, AnyHue, 5 }
    };
}

bool valid_key(const astr_t &key)
{
    return !key.empty() && key.size() < 32 &&
        std::all_of(key.begin(), key.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
}

bool save_file(const vector<ItemCounter> &counters, bool show, bool warnings)
{
    CIniFile ini;
    ini.SetInt("Meta", "count", (int)counters.size());
    ini.SetBool("Meta", "show", show);
    ini.SetBool("Meta", "icons", s_ShowIcons);
    ini.SetBool("Meta", "warnings", warnings);
    ini.SetBool("Meta", "auto_recount", s_AutoRecount);
    ini.SetBool("Meta", "skip_pouches", s_SkipPouches);
    ini.SetBool("Meta", "status_expanded", s_StatusExpanded);
    ini.SetBool("Meta", "bandage", s_BandageEnabled);
    ini.SetInt("Meta", "bandage_seconds", s_BandageSeconds);
    ini.SetBool("Meta", "gold", s_GoldEnabled);
    ini.SetBool("Display", "in_game_caption", s_Options.Caption);
    ini.SetBool("Display", "name_notoriety", s_Options.Notoriety);
    ini.SetBool("Display", "highlight_reagents", s_Options.Reagents);
    ini.SetBool("Display", "health_changes", s_Options.Health);
    ini.SetBool("Display", "party_stats", s_Options.Party);
    ini.SetBool("Display", "buff_messages", s_Options.Buffs);
    ini.SetBool("Display", "damage_taken", s_Options.DamageTaken);
    ini.SetBool("Display", "damage_other", s_Options.OtherDamage);
    ini.SetBool("Display", "message_rules", s_Options.MessageRules);
    ini.SetBool("Display", "unicode_rules", s_Options.UnicodeRules);
    ini.Set("Display", "buff_format", s_Options.BuffFormat);
    ini.Set("Display", "health_format", s_Options.HealthFormat);
    ini.Set("Display", "party_format", s_Options.PartyFormat);
    ini.Set("Display", "rule_format", s_Options.RuleFormat);
    ini.SetInt("Meta", "overhead_count", (int)s_OverheadRules.size());
    ini.SetInt("Meta", "container_labels", static_cast<int>(s_ContainerLabels.size()));
    for (int i = 0; i < static_cast<int>(s_ContainerLabels.size()); ++i)
    {
        const auto section = IniSectionName("Container", i);
        const auto &entry = s_ContainerLabels[i];
        ini.Set(section, "serial", CAssistant::SerialToText(entry.Serial));
        ini.SetInt(section, "graphic", entry.Graphic);
        ini.Set(section, "shard", entry.Shard);
        ini.Set(section, "label", entry.Label);
    }
    for (int i = 0; i < (int)s_OverheadRules.size(); ++i)
    {
        const auto section = IniSectionName("Overhead", i);
        ini.Set(section, "match", s_OverheadRules[i].Match);
        ini.Set(section, "text", s_OverheadRules[i].Text);
    }
    for (int i = 0; i < (int)counters.size(); ++i)
    {
        const auto &counter = counters[i];
        const auto section = IniSectionName("Counter", i);
        ini.Set(section, "name", counter.Name);
        ini.Set(section, "key", counter.Key);
        ini.SetInt(section, "graphic", counter.Graphic);
        ini.SetInt(section, "hue", counter.Hue);
        ini.SetInt(section, "warning", counter.Warning);
        ini.SetBool(section, "visible", counter.Visible);
        ini.SetBool(section, "icon", counter.Icon);
    }

    g_AssistantConfig.EnsureDirectories();
    return ini.Save(fs_path_join(g_AssistantConfig.GetProfilePath(), "ItemCounters.ini"));
}

void switch_profile()
{
    const auto path = g_AssistantConfig.GetProfilePath();
    const auto profile = fs_path_str(path);
    if (profile == s_Profile)
    {
        return;
    }

    s_Profile = profile;
    s_Selected = -1;
    s_Editor = {};
    s_Feedback.clear();
    s_ShowHUD = false;
    s_ShowIcons = false;
    s_Warnings = false;
    s_AutoRecount = true;
    s_SkipPouches = true;
    s_StatusExpanded = false;
    s_BandageEnabled = false;
    s_BandageSeconds = 5;
    s_GoldEnabled = false;
    s_Options = {};
    s_RuleSelected = -1;
    s_RuleMatch = {};
    s_RuleText = {};
    s_OverheadRules.clear();
    s_Counters.clear();
    s_ContainerLabels.clear();
    s_LabelTarget = {};
    s_LabelText = {};
    s_Pack = 0;
    s_NeedsRefresh = true;
    s_Bandaging = false;
    s_GoldStarted = false;
    s_Recount = {};
    s_AutoAttempted.clear();
    s_NonBagSerials.clear();
    s_PriorityBags.clear();
    ++s_Revision;

    CIniFile ini;
    if (!ini.Load(fs_path_join(path, "ItemCounters.ini")))
    {
        s_Counters = default_counters();
        return;
    }

    s_ShowHUD = ini.GetBool("Meta", "show", false);
    s_ShowIcons = ini.GetBool("Meta", "icons", false);
    s_Warnings = ini.GetBool("Meta", "warnings", false);
    s_AutoRecount = ini.GetBool("Meta", "auto_recount", true);
    s_SkipPouches = ini.GetBool("Meta", "skip_pouches", true);
    s_StatusExpanded = ini.GetBool("Meta", "status_expanded", false);
    s_BandageEnabled = ini.GetBool("Meta", "bandage");
    s_BandageSeconds = std::max(1, std::min(ini.GetInt("Meta", "bandage_seconds", 5), 60));
    s_GoldEnabled = ini.GetBool("Meta", "gold");
    s_Options.Caption = ini.GetBool("Display", "in_game_caption");
    s_Options.Notoriety = ini.GetBool("Display", "name_notoriety");
    s_Options.Reagents = ini.GetBool("Display", "highlight_reagents");
    s_Options.Health = ini.GetBool("Display", "health_changes");
    s_Options.Party = ini.GetBool("Display", "party_stats");
    s_Options.Buffs = ini.GetBool("Display", "buff_messages");
    s_Options.DamageTaken = ini.GetBool("Display", "damage_taken");
    s_Options.OtherDamage = ini.GetBool("Display", "damage_other");
    s_Options.MessageRules = ini.GetBool("Display", "message_rules", s_Options.MessageRules);
    s_Options.UnicodeRules = ini.GetBool("Display", "unicode_rules", s_Options.UnicodeRules);
    s_Options.BuffFormat = ini.Get("Display", "buff_format", s_Options.BuffFormat);
    s_Options.HealthFormat = ini.Get("Display", "health_format", s_Options.HealthFormat);
    s_Options.PartyFormat = ini.Get("Display", "party_format", s_Options.PartyFormat);
    s_Options.RuleFormat = ini.Get("Display", "rule_format", s_Options.RuleFormat);
    const int labelCount = std::max(0, std::min(
        ini.GetInt("Meta", "container_labels"), static_cast<int>(ContainerLabelLimit)));
    for (int i = 0; i < labelCount; ++i)
    {
        const auto section = IniSectionName("Container", i);
        const auto serial = CAssistant::TextToSerial(ini.Get(section, "serial"));
        const int graphic = ini.GetInt(section, "graphic");
        const auto shard = ini.Get(section, "shard");
        const auto label = ini.Get(section, "label");
        if (serial >= 0x40000000u && serial < 0x80000000u && graphic > 0 && graphic < 0xFFFF &&
            !shard.empty() && !label.empty() && label.size() < s_LabelText.size())
        {
            s_ContainerLabels.push_back({ serial, static_cast<uint16_t>(graphic), shard, label });
        }
    }
    const int rules = std::max(0, std::min(ini.GetInt("Meta", "overhead_count"), 64));
    for (int i = 0; i < rules; ++i)
    {
        const auto section = IniSectionName("Overhead", i);
        OverheadRule rule{ ini.Get(section, "match"), ini.Get(section, "text") };
        if (!rule.Match.empty() && !rule.Text.empty())
        {
            s_OverheadRules.push_back(rule);
        }
    }
    if (!ini.Has("Meta", "count"))
    {
        s_Counters = default_counters();
    }
    const int count = std::max(0, std::min(ini.GetInt("Meta", "count"), MaxCounters));
    for (int i = 0; i < count; ++i)
    {
        const auto section = IniSectionName("Counter", i);
        ItemCounter counter;
        counter.Name = ini.Get(section, "name");
        counter.Key = str_lower(ini.Get(section, "key"));
        counter.Graphic = ini.GetInt(section, "graphic");
        counter.Hue = ini.GetInt(section, "hue", AnyHue);
        counter.Warning = std::max(0, ini.GetInt(section, "warning"));
        counter.Visible = ini.GetBool(section, "visible", true);
        counter.Icon = ini.GetBool(section, "icon", true);
        if (!counter.Name.empty() && valid_key(counter.Key) &&
            counter.Graphic > 0 && counter.Graphic < AnyHue &&
            (counter.Hue == AnyHue || (counter.Hue >= 0 && counter.Hue <= 0x3FFF)) &&
            std::none_of(s_Counters.begin(), s_Counters.end(), [&](const auto &other) {
                return other.Key == counter.Key;
            }))
        {
            s_Counters.push_back(counter);
        }
    }
}

bool gold_stat(const astr_t &key, astr_t &value)
{
    if (!s_GoldEnabled || g_Player == nullptr)
    {
        return false;
    }
    if (!s_GoldStarted)
    {
        s_GoldStarted = true;
        s_GoldAt = g_Ticks;
        s_GoldBase = g_Player->Gold;
    }
    const int64_t elapsedMs = g_Ticks - s_GoldAt;
    const int64_t gained = static_cast<int64_t>(g_Player->Gold) - s_GoldBase;
    int64_t result = 0;
    if (key == "goldtotal")
    {
        result = gained;
    }
    else if (key == "goldtotalmin")
    {
        result = elapsedMs / 60000;
    }
    else if (key == "gps" || key == "gpm" || key == "gph")
    {
        const int64_t interval = key == "gps" ? 1000 : key == "gpm" ? 60000 : 3600000;
        result = elapsedMs >= 1000 ? gained * interval / elapsedMs : 0;
    }
    else
    {
        return false;
    }

    char text[40]{};
    snprintf(text, sizeof(text), "%lld", static_cast<long long>(result));
    value = text;
    return true;
}

astr_t elapsed_time(uint32_t milliseconds)
{
    const auto seconds = milliseconds / 1000;
    char text[32]{};
    snprintf(text, sizeof(text), "%u:%02u:%02u", seconds / 3600, (seconds / 60) % 60, seconds % 60);
    return text;
}

void update_session_timers()
{
    if (g_Player == nullptr)
    {
        return;
    }
    if (!s_Online)
    {
        s_Online = true;
        s_OnlineAt = g_Ticks;
    }
    const bool criminal = g_Player->Notoriety == NT_CRIMINAL;
    if (criminal != s_Criminal)
    {
        if (criminal)
        {
            s_CriminalAt = g_Ticks;
        }
        s_Criminal = criminal;
        ++s_Revision;
    }
    if (s_Gate && g_Ticks - s_GateAt >= GateDuration)
    {
        s_Gate = false;
        ++s_Revision;
    }
    if (s_Bandaging && g_Ticks - s_BandageAt >= static_cast<uint32_t>(s_BandageSeconds + 5) * 1000)
    {
        s_Bandaging = false;
        ++s_Revision;
    }
    if (s_CastSpell && g_Ticks - s_CastAt >= CastHighlightDuration)
    {
        s_CastSpell = 0;
        s_CastReagents.clear();
        ++s_Revision;
    }
}

bool display_value(const astr_t &key, astr_t &value)
{
    if (g_Player == nullptr)
    {
        return false;
    }
    update_session_timers();
    if (key == "uptime")
    {
        value = elapsed_time(g_Ticks - s_OnlineAt);
    }
    else if (key == "crimtime")
    {
        value = s_Criminal ? elapsed_time(g_Ticks - s_CriminalAt) : astr_t();
    }
    else if (key == "buffsdebuffs")
    {
        value = ScriptBuffs_List();
    }
    else if (key == "gate")
    {
        value = s_Gate ? str_from((GateDuration - (g_Ticks - s_GateAt) + 999) / 1000) + "s" : astr_t();
    }
    else if (key == "skill")
    {
        value = s_UsedSkill ? elapsed_time(g_Ticks - s_SkillAt) : astr_t();
    }
    else if (key == "bandage")
    {
        const auto expected = static_cast<uint32_t>(s_BandageSeconds) * 1000;
        const auto elapsed = g_Ticks - s_BandageAt;
        value = !s_Bandaging ? astr_t()
                              : elapsed < expected ? str_from((expected - elapsed + 999) / 1000) + "s"
                                                   : astr_t("finishing");
    }
    else if (key == "statbar" || key == "mediumstatbar" || key == "largestatbar")
    {
        if (key == "statbar")
        {
            value = str_from(g_Player->Hits) + "/" + str_from(g_Player->Stam) + "/" + str_from(g_Player->Mana);
        }
        else
        {
            value = "HP " + str_from(g_Player->Hits) + "/" + str_from(g_Player->MaxHits) +
                " ST " + str_from(g_Player->Stam) + "/" + str_from(g_Player->MaxStam) +
                " MN " + str_from(g_Player->Mana) + "/" + str_from(g_Player->MaxMana);
            if (key == "largestatbar")
            {
                value += " STR " + str_from(g_Player->Str) + " DEX " + str_from(g_Player->Dex) +
                    " INT " + str_from(g_Player->Int);
            }
        }
    }
    else
    {
        return false;
    }
    return true;
}

uint16_t parse_action_id(const uint8_t *data, size_t size)
{
    uint32_t id = 0;
    for (size_t i = 0; i < size; ++i)
    {
        const uint8_t character = data[i];
        if (character == ' ' || character == '\0')
        {
            return static_cast<uint16_t>(id);
        }
        if (character < '0' || character > '9')
        {
            return 0;
        }
        id = id * 10 + character - '0';
        if (id > UINT16_MAX)
        {
            return 0;
        }
    }
    return static_cast<uint16_t>(id);
}

vector<uint16_t> spell_reagents(const char *reagents)
{
    struct KnownReagent
    {
        const char *Name;
        uint16_t Graphic;
    };
    static const KnownReagent known[] = {
        { "bat wing", Batwing }, { "black pearl", BlackPearl }, { "bloodmoss", Bloodmoss },
        { "blood moss", Bloodmoss }, { "daemon blood", DemonBlood }, { "bone", 0x0F7E },
        { "demon bone", 0x0F80 }, { "dragon's blood", 0x4077 }, { "fertile dirt", 0x0F81 },
        { "garlic", Garlic }, { "ginseng", Ginseng }, { "grave dust", GraveDust },
        { "mandrake root", MandrakeRoot }, { "nightshade", Nightshade }, { "nox crystal", 0x0F8E },
        { "pig iron", PigIron }, { "spider silk", SpidersSilk }, { "spiders' silk", SpidersSilk },
        { "sulfurous ash", SulfurousAsh }
    };
    vector<uint16_t> result;
    if (reagents == nullptr)
    {
        return result;
    }
    const astr_t names = str_lower(reagents);
    for (size_t pos = 0; pos < names.size();)
    {
        const auto end = names.find('\n', pos);
        const auto name = names.substr(pos, end == astr_t::npos ? astr_t::npos : end - pos);
        const auto match = std::find_if(std::begin(known), std::end(known), [&](const KnownReagent &entry) {
            return name == entry.Name;
        });
        if (match != std::end(known))
        {
            result.push_back(match->Graphic);
        }
        if (end == astr_t::npos)
        {
            break;
        }
        pos = end + 1;
    }
    return result;
}

bool is_in_player_backpack(uint32_t containerSerial)
{
    if (g_World == nullptr || g_Player == nullptr || containerSerial == 0 || containerSerial == 0xFFFFFFFF)
    {
        return false;
    }
    const auto *pack = g_Player->FindLayer(OL_BACKPACK);
    if (pack == nullptr)
    {
        return false;
    }
    if (containerSerial == pack->Serial)
    {
        return true;
    }
    constexpr size_t MaxDepth = 16;
    uint32_t current = containerSerial;
    for (size_t depth = 0; depth < MaxDepth && current != 0 && current != 0xFFFFFFFF; ++depth)
    {
        if (current == pack->Serial)
        {
            return true;
        }
        const auto *obj = g_World->FindWorldObject(current);
        if (obj == nullptr || obj->NPC || obj->IsCorpse())
        {
            return false;
        }
        const auto *item = static_cast<const CGameItem *>(obj);
        if (item->Layer == OL_BANK)
        {
            return false;
        }
        current = obj->Container;
    }
    return false;
}

bool matches_tracked_counter(uint16_t graphic, uint16_t hue)
{
    for (const auto &counter : s_Counters)
    {
        if (counter.Graphic == graphic &&
            (counter.Hue == AnyHue || counter.Hue == hue || counter.Hue == (hue & 0x3FFF)))
        {
            return true;
        }
    }
    return false;
}

const CGameItem *find_backpack_item(const CGameItem &root, uint16_t graphic, uint16_t hue, size_t depth = 0)
{
    constexpr size_t MaxDepth = 12;
    if (depth >= MaxDepth)
    {
        return nullptr;
    }
    vector<const CGameItem *> subcontainers;
    for (auto *node = root.m_Items; node != nullptr; node = node->m_Next)
    {
        auto *object = static_cast<CGameObject *>(node);
        if (object->NPC || object->Container != root.Serial || object->IsCorpse())
        {
            continue;
        }
        const auto *item = static_cast<const CGameItem *>(object);
        if (item->Layer != OL_NONE || item->Dragged)
        {
            continue;
        }
        if (item->Graphic == graphic &&
            (hue == AnyHue || item->Color == hue || (item->Color & 0x3FFF) == hue))
        {
            return item;
        }
        if (!item->Empty() && item->Layer != OL_BANK)
        {
            subcontainers.push_back(item);
        }
    }
    for (const auto *sub : subcontainers)
    {
        if (const auto *found = find_backpack_item(*sub, graphic, hue, depth + 1))
        {
            return found;
        }
    }
    return nullptr;
}

bool is_recountable_bag(CGameObject &object, const CGameItem &item)
{
    return object.GetStaticData() != nullptr &&
           item.Graphic != 0 &&
           object.IsContainer() &&
           !object.IsCorpse() &&
           !ItemCounters_IsNonBagContainerGraphic(item.Graphic) &&
           s_NonBagSerials.count(item.Serial) == 0;
}

void mark_unopened_bags_attempted(const CGameItem &root, std::set<uint32_t> &attempted, size_t depth = 0)
{
    constexpr size_t MaxDepth = 12;
    if (depth >= MaxDepth || attempted.size() >= RecountLimit)
    {
        return;
    }
    if (!root.Opened)
    {
        attempted.insert(root.Serial);
    }
    for (auto *node = root.m_Items; node != nullptr; node = node->m_Next)
    {
        auto *object = static_cast<CGameObject *>(node);
        if (object->NPC || object->Container != root.Serial || object->IsCorpse())
        {
            continue;
        }
        const auto *item = static_cast<const CGameItem *>(object);
        if (item->Layer != OL_NONE || item->Dragged)
        {
            continue;
        }
        if (is_recountable_bag(*object, *item))
        {
            mark_unopened_bags_attempted(*item, attempted, depth + 1);
        }
    }
}

void refresh_counts()
{
    switch_profile();
    const auto *pack = g_Player != nullptr ? g_Player->FindLayer(OL_BACKPACK) : nullptr;
    const uint32_t packSerial = pack != nullptr ? pack->Serial : 0;
    if (s_Pack != packSerial)
    {
        s_Pack = packSerial;
        s_NeedsRefresh = true;
        s_Recount = {};
        s_AutoAttempted.clear();
        s_NonBagSerials.clear();
        s_PriorityBags.clear();
        ++s_Revision;
        for (auto &counter : s_Counters)
        {
            counter.Count = -1;
        }
    }

    if (!s_NeedsRefresh && g_Ticks - s_LastRefresh < RefreshInterval)
    {
        return;
    }

    s_NeedsRefresh = false;
    s_LastRefresh = g_Ticks;
    if (pack == nullptr || (!pack->Opened && pack->Empty()))
    {
        bool changed = false;
        for (auto &counter : s_Counters)
        {
            if (counter.Count != -1)
            {
                counter.Count = -1;
                changed = true;
            }
        }
        if (changed)
        {
            ++s_Revision;
        }
        return;
    }

    bool changed = false;
    for (auto &counter : s_Counters)
    {
        const int previous = counter.Count;
        counter.Count = CountItems(pack, (uint16_t)counter.Graphic, (uint16_t)counter.Hue, true);
        if (counter.Count != previous)
        {
            changed = true;
        }
        if (s_Warnings && counter.Visible && counter.Warning > 0 &&
            previous >= counter.Warning && counter.Count < counter.Warning)
        {
            g_Assistant.ClientPrint(
                counter.Name + " is low (" + str_from(counter.Count) + " remaining).", 0x0021);
        }
    }
    if (changed)
    {
        ++s_Revision;
    }
}

const CGameItem *unopened_bag(const CGameItem &root, bool &partial,
    const std::set<uint32_t> *attempted = nullptr, bool skipPouches = false, size_t depth = 0)
{
    constexpr size_t MaxDepth = 12;
    if (depth >= MaxDepth)
    {
        partial = true;
        return nullptr;
    }
    if (!root.Opened)
    {
        partial = true;
        const bool skipPouch = depth > 0 && ItemCounters_ShouldSkipAutoOpenBag(root.Graphic, skipPouches);
        if (!skipPouch && (attempted == nullptr || attempted->count(root.Serial) == 0))
        {
            return &root;
        }
    }
    for (auto *node = root.m_Items; node != nullptr; node = node->m_Next)
    {
        auto *object = static_cast<CGameObject *>(node);
        if (object->NPC || object->Container != root.Serial || object->IsCorpse())
        {
            continue;
        }
        const auto *item = static_cast<const CGameItem *>(object);
        if (item->Layer != OL_NONE || item->Dragged)
        {
            continue;
        }
        if (object->GetStaticData() == nullptr || item->Graphic == 0)
        {
            partial = true;
            continue;
        }
        if (is_recountable_bag(*object, *item))
        {
            if (const auto *bag = unopened_bag(*item, partial, attempted, skipPouches, depth + 1))
            {
                return bag;
            }
        }
    }
    return nullptr;
}

const CGameItem *next_priority_bag(const CGameItem &pack, const std::set<uint32_t> &attempted)
{
    while (!s_PriorityBags.empty())
    {
        const uint32_t serial = s_PriorityBags.front();
        s_PriorityBags.erase(s_PriorityBags.begin());
        if (serial == 0 || attempted.count(serial) != 0 || s_NonBagSerials.count(serial) != 0)
        {
            continue;
        }
        auto *item = g_World != nullptr ? g_World->FindWorldItem(serial) : nullptr;
        if (item == nullptr || item->Opened || item->IsCorpse() || item->Dragged)
        {
            continue;
        }
        if (serial != pack.Serial)
        {
            if (item->Layer != OL_NONE || !is_in_player_backpack(item->Container))
            {
                continue;
            }
            if (!is_recountable_bag(*item, *item) ||
                ItemCounters_ShouldSkipAutoOpenBag(item->Graphic, s_SkipPouches))
            {
                continue;
            }
        }
        return item;
    }
    return nullptr;
}

void recount_backpack(bool hasActiveConsumer = true)
{
    const auto *pack = g_Player != nullptr ? g_Player->FindLayer(OL_BACKPACK) : nullptr;
    if (!g_Connected || g_World == nullptr || pack == nullptr || g_Player->IsDead())
    {
        if (s_Recount.Backpack != 0)
        {
            s_Recount = {};
            ++s_Revision;
        }
        return;
    }
    if (s_Pack != pack->Serial)
    {
        s_Pack = pack->Serial;
        s_NeedsRefresh = true;
        s_Recount = {};
        s_AutoAttempted.clear();
        s_NonBagSerials.clear();
        s_PriorityBags.clear();
        ++s_Revision;
    }
    const bool restorePending = !g_UseItemActions.Empty();
    if (!ItemCounters_ShouldRunRecount(s_AutoRecount, s_Recount.Manual, hasActiveConsumer, restorePending))
    {
        if (!s_Recount.Manual && !restorePending && s_Recount.Backpack != 0)
        {
            s_Recount = {};
            s_PriorityBags.clear();
            ++s_Revision;
        }
        return;
    }
    if (s_Recount.Backpack == 0)
    {
        if (!s_AutoRecount || s_AutoAttempted.size() >= RecountLimit)
        {
            return;
        }
        bool hasWork = !s_PriorityBags.empty();
        if (!hasWork)
        {
            bool partial = false;
            hasWork = unopened_bag(*pack, partial, &s_AutoAttempted, s_SkipPouches) != nullptr;
        }
        if (!hasWork)
        {
            return;
        }
        s_Recount.Backpack = pack->Serial;
        s_Recount.Manual = false;
        ++s_Revision;
    }
    if (pack->Serial != s_Recount.Backpack)
    {
        s_Recount = {};
        ++s_Revision;
        return;
    }
    if (!s_Recount.Manual && !s_AutoRecount)
    {
        s_Recount = {};
        s_PriorityBags.clear();
        ++s_Revision;
        return;
    }
    if (s_Recount.Pending != 0)
    {
        const auto *requested = g_World->FindWorldItem(s_Recount.Pending);
        const bool openedAsSpellbook =
            g_GumpManager.GetGump(s_Recount.Pending, 0, GT_SPELLBOOK) != nullptr;
        if (openedAsSpellbook)
        {
            s_NonBagSerials.insert(s_Recount.Pending);
            s_Recount.Pending = 0;
            s_NeedsRefresh = true;
            ++s_Revision;
        }
        else
        {
            const auto elapsed = g_Ticks - s_Recount.RequestedAt;
            if (requested != nullptr && !requested->Opened && elapsed < RecountTimeout)
            {
                return;
            }
            if (requested == nullptr || !requested->Opened)
            {
                ++s_Recount.Unavailable;
            }
            s_Recount.Pending = 0;
            s_NeedsRefresh = true;
            ++s_Revision;
        }
    }
    if (g_ObjectInHand.Enabled || g_Target.IsTargeting() ||
        g_TargetHandler != nullptr || InventoryAgent_Busy() || g_MacroExecutor.IsPlaying() ||
        g_MacroExecutor.IsRecording() || (s_HasObjectUse && g_Ticks - s_LastObjectUse < RecountInterval))
    {
        return;
    }

    std::set<uint32_t> &attempted = s_Recount.Manual ? s_Recount.Attempted : s_AutoAttempted;
    bool partial = false;
    const CGameItem *bag = nullptr;
    if (attempted.size() < RecountLimit)
    {
        bag = next_priority_bag(*pack, attempted);
        if (bag == nullptr)
        {
            bag = unopened_bag(*pack, partial, &attempted, s_SkipPouches);
        }
    }
    if (bag == nullptr || attempted.size() >= RecountLimit)
    {
        if (s_Recount.Manual)
        {
            s_Feedback = partial || s_Recount.Unavailable != 0
                ? "Recount finished. Some bags could not be opened."
                : "Backpack recounted.";
        }
        s_Recount = {};
        s_NeedsRefresh = true;
        ++s_Revision;
        return;
    }
    s_Recount.Pending = bag->Serial;
    s_Recount.RequestedAt = g_Ticks;
    s_Recount.Attempted.insert(bag->Serial);
    s_AutoAttempted.insert(bag->Serial);
    g_Assistant.DoubleClick(bag->Serial);
}

void edit(const ItemCounter *counter)
{
    s_Editor = {};
    if (counter == nullptr)
    {
        return;
    }

    snprintf(s_Editor.Name.data(), s_Editor.Name.size(), "%s", counter->Name.c_str());
    snprintf(s_Editor.Key.data(), s_Editor.Key.size(), "%s", counter->Key.c_str());
    s_Editor.Graphic = counter->Graphic;
    s_Editor.Hue = counter->Hue == AnyHue ? 0 : counter->Hue;
    s_Editor.AnyColor = counter->Hue == AnyHue;
    s_Editor.Warning = counter->Warning;
    s_Editor.Visible = counter->Visible;
    s_Editor.Icon = counter->Icon;
}

void commit_counter()
{
    const auto key = str_lower(static_cast<const char *>(s_Editor.Key.data()));
    if (s_Editor.Name[0] == '\0' || !valid_key(key) ||
        s_Editor.Graphic <= 0 || s_Editor.Graphic >= AnyHue ||
        (!s_Editor.AnyColor && (s_Editor.Hue < 0 || s_Editor.Hue > 0x3FFF)) ||
        s_Editor.Warning < 0)
    {
        s_Feedback = "Enter a name, tag, valid item graphic and hue, and a warning amount of zero or greater.";
        return;
    }

    for (int i = 0; i < (int)s_Counters.size(); ++i)
    {
        if (i != s_Selected && s_Counters[i].Key == key)
        {
            s_Feedback = "A counter already uses that tag.";
            return;
        }
    }

    auto updated = s_Counters;
    ItemCounter counter{
        s_Editor.Name.data(), key, s_Editor.Graphic, s_Editor.AnyColor ? AnyHue : s_Editor.Hue,
        s_Editor.Warning, s_Editor.Visible, s_Editor.Icon
    };
    if (s_Selected >= 0 && s_Selected < (int)updated.size())
    {
        updated[s_Selected] = counter;
    }
    else if ((int)updated.size() < MaxCounters)
    {
        updated.push_back(counter);
    }
    else
    {
        s_Feedback = "The counter list is full.";
        return;
    }

    if (!save_file(updated, s_ShowHUD, s_Warnings))
    {
        s_Feedback = "Could not save counters.";
        return;
    }

    if (s_Selected < 0)
    {
        s_Selected = (int)updated.size() - 1;
    }
    s_Counters = std::move(updated);
    s_NeedsRefresh = true;
    ++s_Revision;
    s_Feedback.clear();
}

void set_options(bool show, bool warn)
{
    if (!save_file(s_Counters, show, warn))
    {
        s_Feedback = "Could not save display options.";
        return;
    }

    s_ShowHUD = show;
    s_Warnings = warn;
    ++s_Revision;
    s_Feedback.clear();
}

void display_checkbox(const char *label, bool &setting)
{
    if (ImGui::Checkbox(label, &setting) && !save_file(s_Counters, s_ShowHUD, s_Warnings))
    {
        setting = !setting;
        s_Feedback = "Could not save display options.";
    }
}

void display_format(const char *label, astr_t &setting)
{
    char format[160]{};
    snprintf(format, sizeof(format), "%s", setting.c_str());
    if (ImGui::InputText(label, format, sizeof(format), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const auto previous = setting;
        setting = format;
        if (!save_file(s_Counters, s_ShowHUD, s_Warnings))
        {
            setting = previous;
            s_Feedback = "Could not save display options.";
        }
    }
}

vector<ContainerLabel>::iterator find_label(uint32_t serial, uint16_t graphic, const astr_t &shard)
{
    return std::find_if(s_ContainerLabels.begin(), s_ContainerLabels.end(), [&](const auto &entry) {
        return entry.Serial == serial && entry.Graphic == graphic && entry.Shard == shard;
    });
}

void select_label(const ContainerLabel &entry)
{
    s_LabelTarget = entry;
    snprintf(s_LabelText.data(), s_LabelText.size(), "%s", entry.Label.c_str());
}

void draw_container_labels()
{
    if (!ImGui::CollapsingHeader("Container labels"))
    {
        return;
    }
    ImGui::BeginDisabled(g_Player == nullptr || g_TargetHandler != nullptr || g_Target.IsTargeting());
    if (ImGui::Button("Target a container"))
    {
        g_TargetHandler = &CAssistant::HandleTargetContainerLabel;
        g_Assistant.RequestTarget("Target a container to label.");
    }
    ImGui::EndDisabled();

    const auto entry = find_label(s_LabelTarget.Serial, s_LabelTarget.Graphic, s_LabelTarget.Shard);
    const bool found = entry != s_ContainerLabels.end();
    ImGui::BeginDisabled(s_LabelTarget.Serial == 0);
    ImGui::InputText("Label", s_LabelText.data(), s_LabelText.size());
    const bool save = ImGui::Button("Save label");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!found);
    const bool remove = ImGui::Button("Remove label");
    ImGui::EndDisabled();
    if (save || remove)
    {
        auto updated = s_LabelTarget;
        updated.Label = str_trim(s_LabelText.data());
        if (save && updated.Label.empty())
        {
            s_Feedback = "Enter a container label.";
        }
        else if (save && !found && s_ContainerLabels.size() >= ContainerLabelLimit)
        {
            s_Feedback = "The container label list is full.";
        }
        else
        {
            const auto previous = s_ContainerLabels;
            if (remove)
            {
                s_ContainerLabels.erase(entry);
            }
            else if (found)
            {
                *entry = updated;
            }
            else
            {
                s_ContainerLabels.push_back(updated);
            }
            if (save_file(s_Counters, s_ShowHUD, s_Warnings))
            {
                select_label(remove ? ContainerLabel{} : updated);
                s_Feedback.clear();
            }
            else
            {
                s_ContainerLabels = previous;
                s_Feedback = "Could not save container labels.";
            }
        }
    }
    if (ImGui::BeginChild("##container_labels", ImVec2(0, 88), ImGuiChildFlags_Borders))
    {
        for (const auto &saved : s_ContainerLabels)
        {
            if (saved.Shard != g_ServerList.LastServerName)
            {
                continue;
            }
            const auto serial = CAssistant::SerialToText(saved.Serial);
            const auto row = saved.Label + " (" + serial + ")###" + serial;
            if (ImGui::Selectable(row.c_str(), s_LabelTarget.Serial == saved.Serial))
            {
                select_label(saved);
            }
        }
    }
    ImGui::EndChild();
}

astr_t format_message(
    const astr_t &pattern, std::initializer_list<std::pair<astr_t, astr_t>> replacements)
{
    astr_t result;
    for (size_t pos = 0; pos < pattern.size();)
    {
        const auto match = std::find_if(replacements.begin(), replacements.end(), [&](const auto &entry) {
            return pattern.compare(pos, entry.first.size(), entry.first) == 0;
        });
        if (match == replacements.end())
        {
            result += pattern[pos++];
        }
        else
        {
            result += match->second;
            pos += match->first.size();
        }
    }
    return result;
}

void draw_caption()
{
    const auto &parts = g_DisplayManager.GetTitleParts();
    bool onLine = false;
    for (const auto &part : parts)
    {
        if (part.Kind == DisplayTitleKind::Text || part.Kind == DisplayTitleKind::Character)
        {
            if (onLine)
            {
                ImGui::SameLine(0.0f, 0.0f);
            }
            uint32_t rgba = IM_COL32(
                (part.Color >> 16) & 0xFF, (part.Color >> 8) & 0xFF,
                part.Color & 0xFF, (part.Color >> 24) & 0xFF);
            if (s_Options.Notoriety && part.Kind == DisplayTitleKind::Character)
            {
                const auto hue = g_ConfigManager.GetColorByNotoriety(g_Player->Notoriety);
                if (hue != 0)
                {
                    uint16_t base = 0x7FFF;
                    rgba = g_ColorManager.GetPartialHueColor(base, hue) | IM_COL32_A_MASK;
                }
            }
            if (rgba != 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, rgba);
            }
            ImGui::TextUnformatted(part.Text.c_str());
            if (rgba != 0)
            {
                ImGui::PopStyleColor();
            }
            onLine = true;
            continue;
        }

        if (part.Kind == DisplayTitleKind::ItemCounter)
        {
            if (onLine)
            {
                ImGui::SameLine(0.0f, 0.0f);
            }
            constexpr int IconSize = 16;
            constexpr float IconGap = 3.0f;
            ImTextureRef image;
            bool hasIcon = false;
            if (part.IconGraphic != 0)
            {
                image = AssetGrid_AcquireThumb(AssetTextureKind::Static, part.IconGraphic, IconSize);
                hasIcon = AssetTextureCache_IsReady(image);
            }
            if (part.Highlighted)
            {
                const ImVec2 start = ImGui::GetCursorScreenPos();
                const ImVec2 textSize = ImGui::CalcTextSize(part.Text.c_str());
                const float totalWidth =
                    (hasIcon ? static_cast<float>(IconSize) + IconGap : 0.0f) + textSize.x;
                const float totalHeight =
                    std::max(hasIcon ? static_cast<float>(IconSize) : 0.0f, textSize.y);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(start.x - 2.0f, start.y),
                    ImVec2(start.x + totalWidth + 2.0f, start.y + totalHeight),
                    IM_COL32(135, 30, 29, 255),
                    2.0f);
            }
            if (hasIcon)
            {
                ImGui::Image(image, ImVec2(static_cast<float>(IconSize), static_cast<float>(IconSize)));
                ImGui::SameLine(0.0f, IconGap);
            }
            const uint32_t rgba = IM_COL32(
                (part.Color >> 16) & 0xFF, (part.Color >> 8) & 0xFF,
                part.Color & 0xFF, (part.Color >> 24) & 0xFF);
            if (rgba != 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, rgba);
            }
            ImGui::TextUnformatted(part.Text.c_str());
            if (rgba != 0)
            {
                ImGui::PopStyleColor();
            }
            onLine = true;
            continue;
        }

        const bool small = part.Kind == DisplayTitleKind::SmallStatus;
        const bool large = part.Kind == DisplayTitleKind::LargeStatus;
        const ImVec2 size(small ? 72.0f : large ? 178.0f : 120.0f, small ? 7.0f : ImGui::GetFrameHeight());
        const auto bar = [&](const char *name, int value, int maximum, ImU32 color) {
            const auto fraction = maximum > 0 ? static_cast<float>(value) / maximum : 0.0f;
            const auto label = small ? astr_t() : astr_t(name) + " " + str_from(value) + "/" + str_from(maximum);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
            ImGui::ProgressBar(fraction, size, label.c_str());
            ImGui::PopStyleColor();
        };
        bar("Health", g_Player->Hits, g_Player->MaxHits, IM_COL32(123, 37, 38, 255));
        bar("Stamina", g_Player->Stam, g_Player->MaxStam, IM_COL32(69, 107, 55, 255));
        bar("Mana", g_Player->Mana, g_Player->MaxMana, IM_COL32(40, 78, 129, 255));
        if (large)
        {
            ImGui::Text("STR %d  DEX %d  INT %d", g_Player->Str, g_Player->Dex, g_Player->Int);
        }
        onLine = false;
    }
}

} // namespace

void ItemCounters_ResetSession()
{
    s_HUDVisible = false;
    s_Pack = 0;
    s_NeedsRefresh = true;
    s_Bandaging = false;
    s_GoldStarted = false;
    s_Online = false;
    s_Criminal = false;
    s_Gate = false;
    s_UsedSkill = false;
    s_CastSpell = 0;
    s_CastReagents.clear();
    s_Recount = {};
    s_AutoAttempted.clear();
    s_NonBagSerials.clear();
    s_PriorityBags.clear();
    s_HasObjectUse = false;
    ScriptBuffs_Clear();
    for (auto &counter : s_Counters)
    {
        counter.Count = -1;
    }
    ++s_Revision;
}

bool ItemCounters_Read(const astr_t &key, astr_t &value)
{
    if (display_value(key, value))
    {
        return true;
    }
    refresh_counts();
    if (gold_stat(key, value))
    {
        return true;
    }
    for (const auto &counter : s_Counters)
    {
        if (counter.Key == key)
        {
            value = counter.Count >= 0 ? str_from(counter.Count) : "-";
            return true;
        }
    }

    return false;
}

bool ItemCounters_ReadDisplay(const astr_t &key, DisplayCounterInfo &info)
{
    info = {};
    if (display_value(key, info.Value))
    {
        return true;
    }
    update_session_timers();
    refresh_counts();
    if (gold_stat(key, info.Value))
    {
        return true;
    }
    const bool castActive = s_Options.Reagents && s_CastSpell != 0 && GetSpellById(s_CastSpell) != nullptr;
    for (const auto &counter : s_Counters)
    {
        if (counter.Key == key)
        {
            info.Value = counter.Count >= 0 ? str_from(counter.Count) : "-";
            info.Graphic = static_cast<uint16_t>(counter.Graphic);
            info.Hue = static_cast<uint16_t>(counter.Hue);
            info.ShowIcon = s_ShowIcons && counter.Icon;
            info.Low = counter.Count >= 0 && counter.Warning > 0 && counter.Count < counter.Warning;
            info.Highlighted = castActive &&
                std::find(s_CastReagents.begin(), s_CastReagents.end(), counter.Graphic) != s_CastReagents.end();
            return true;
        }
    }

    return false;
}

const astr_t &ItemCounters_ContainerLabel(uint32_t serial, uint16_t graphic)
{
    switch_profile();
    const auto entry = find_label(serial, graphic, g_ServerList.LastServerName);
    static const astr_t empty;
    return entry == s_ContainerLabels.end() ? empty : entry->Label;
}

void ItemCounters_OnGameSend(const uint8_t *data, size_t size)
{
    if (g_Player == nullptr || size == 0)
    {
        return;
    }
    switch (data[0])
    {
        case 0x06: // use
        case 0x07: // lift
        case 0x08: // drop
        case 0x13: // equip
        case 0x6C: // target response
            s_LastObjectUse = g_Ticks;
            s_HasObjectUse = true;
            break;
        case 0xBF: // direct item use on a selected target
            if (size >= 5 && data[3] == 0 && data[4] == 0x2C)
            {
                s_LastObjectUse = g_Ticks;
                s_HasObjectUse = true;
            }
            break;
    }
    if (size < 5)
    {
        return;
    }

    uint16_t spellId = 0;
    bool skillUsed = false;
    if (data[0] == 0x12)
    {
        const bool cast = data[3] == 0x27 || data[3] == 0x56;
        if (cast || data[3] == 0x24)
        {
            const auto action = parse_action_id(data + 4, size - 4);
            spellId = cast ? action : 0;
            skillUsed = !cast && action != 0;
        }
    }
    else if (data[0] == 0xBF && data[3] == 0 && size >= 7)
    {
        if (data[4] == 0x1C && size == 9)
        {
            spellId = static_cast<uint16_t>((data[7] << 8) | data[8]);
        }
        else if (data[4] == 0x2D && size == 11)
        {
            spellId = static_cast<uint16_t>((data[5] << 8) | data[6]);
        }
        else if (data[4] == 0x2E && size == 11)
        {
            skillUsed = true;
        }
    }
    if (skillUsed)
    {
        s_SkillAt = g_Ticks;
        s_UsedSkill = true;
    }
    if (spellId == 52)
    {
        s_GateAt = g_Ticks;
        s_Gate = true;
        ++s_Revision;
    }
    if (spellId != 0)
    {
        switch_profile();
        const auto *spell = GetSpellById(spellId);
        s_CastReagents = s_Options.Reagents && spell != nullptr ? spell_reagents(spell->Reagents)
                                                             : vector<uint16_t>();
        s_CastSpell = !s_CastReagents.empty() ? spellId : 0;
        s_CastAt = g_Ticks;
        ++s_Revision;
    }
}

void ItemCounters_OnBuffChange(const astr_t &name, bool added)
{
    switch_profile();
    if (g_Player == nullptr || !s_Options.Buffs || name.empty())
    {
        return;
    }
    const auto output = format_message(s_Options.BuffFormat, { { "{action}", added ? "+" : "-" }, { "{name}", name } });
    if (!output.empty())
    {
        g_Assistant.ClientCharPrint(g_Player->Serial, output, added ? 0x0044 : 0x0021);
    }
}

DisplayMobileStats ItemCounters_MobileStats(const CGameCharacter &character)
{
    return { character.Hits, character.MaxHits, character.Mana, character.MaxMana, character.Stam, character.MaxStam };
}

void ItemCounters_OnMobileStats(const CGameCharacter &character, const DisplayMobileStats &previous)
{
    switch_profile();
    if (character.IsDead() || (!s_Options.Health && !s_Options.Party))
    {
        return;
    }
    const auto percent = [](int value, int maximum) {
        return maximum > 0 ? std::min(100, 100 * value / maximum) : -1;
    };
    const int hits = percent(character.Hits, character.MaxHits);
    const int prevHits = percent(previous.Hits, previous.MaxHits);
    if (s_Options.Health && prevHits >= 0 && hits >= 0 && prevHits != hits)
    {
        const auto amount = str_from(hits);
        const auto text = format_message(s_Options.HealthFormat, { { "{0%}", amount + "%" }, { "{0}", amount } });
        const auto color = hits > 75 ? 0x0044 : hits > 40 ? 0x0034 : 0x0021;
        if (!text.empty())
        {
            g_Assistant.ClientCharPrint(character.Serial, text, color);
        }
    }
    if (!s_Options.Party || !g_Party.Contains(character.Serial) || character.Serial == g_PlayerSerial)
    {
        return;
    }
    const int mana = percent(character.Mana, character.MaxMana);
    const int stamina = percent(character.Stam, character.MaxStam);
    const int prevMana = percent(previous.Mana, previous.MaxMana);
    const int prevStamina = percent(previous.Stamina, previous.MaxStamina);
    const bool manaChanged = prevMana >= 0 && mana >= 0 && mana != prevMana;
    const bool staminaChanged = prevStamina >= 0 && stamina >= 0 && stamina != prevStamina;
    if (manaChanged || staminaChanged)
    {
        const auto text = format_message(s_Options.PartyFormat,
            { { "{0}", mana >= 0 ? str_from(mana) : astr_t("-") },
              { "{1}", stamina >= 0 ? str_from(stamina) : astr_t("-") } });
        if (!text.empty())
        {
            g_Assistant.ClientCharPrint(character.Serial, text, 0x005A);
        }
    }
}

void ItemCounters_OnDamage(CGameCharacter &character, int damage)
{
    switch_profile();
    const bool taken = character.Serial == g_PlayerSerial;
    if (taken ? s_Options.DamageTaken : s_Options.OtherDamage)
    {
        const astr_t target = taken ? astr_t("You") : character.GetName();
        g_Assistant.ClientPrint(
            (target.empty() ? astr_t("Target") : target) + " took " + str_from(damage) + " damage.",
            taken ? 0x0034 : 0x0021);
    }
}

void CAssistant::HandleTargetCounterItem(uint8_t *packet) const
{
    switch_profile();
    const auto serial = unpack32(packet + 7);
    auto *item = g_World != nullptr && serial != 0 ? g_World->FindWorldItem(serial) : nullptr;
    if (item == nullptr)
    {
        s_Feedback = "Target an item to set its graphic and hue.";
        return;
    }

    s_Editor.Graphic = item->Graphic;
    s_Editor.Hue = item->Color & 0x3FFF;
    s_Editor.AnyColor = false;
    s_Feedback.clear();
}

void CAssistant::HandleTargetContainerLabel(uint8_t *packet) const
{
    switch_profile();
    const auto serial = unpack32(packet + 7);
    auto *item = g_World != nullptr && serial != 0 ? g_World->FindWorldItem(serial) : nullptr;
    if (item == nullptr || item->IsCorpse() || item->GetStaticData() == nullptr || !item->IsContainer() ||
        g_ServerList.LastServerName.empty())
    {
        s_Feedback = "Target a container to label.";
        return;
    }
    select_label({ serial, item->Graphic, g_ServerList.LastServerName,
                   ItemCounters_ContainerLabel(serial, item->Graphic) });
    s_Feedback.clear();
}

SuppliesSnapshot ItemCounters_GetSupplies()
{
    switch_profile();
    recount_backpack();
    update_session_timers();
    refresh_counts();

    SuppliesSnapshot result;
    const auto *pack = g_Player != nullptr ? g_Player->FindLayer(OL_BACKPACK) : nullptr;
    result.Recounting = s_Recount.Backpack != 0;
    result.CanRecount = g_Connected && pack != nullptr && g_World != nullptr && !g_Player->IsDead();
    result.AutoRecount = s_AutoRecount;
    result.SkipPouches = s_SkipPouches;
    if (pack != nullptr && (pack->Opened || !pack->Empty()))
    {
        bool partial = result.Recounting;
        unopened_bag(*pack, partial);
        result.Coverage = partial ? SuppliesCoverage::Partial : SuppliesCoverage::Complete;
    }

    if (s_BandageEnabled && s_Bandaging)
    {
        display_value("bandage", result.BandageText);
    }
    if (s_GoldEnabled)
    {
        astr_t gph;
        if (gold_stat("gph", gph))
        {
            result.GoldText = gph + " gp/h";
        }
    }
    if (s_Gate)
    {
        display_value("gate", result.GateText);
    }
    if (s_Criminal)
    {
        display_value("crimtime", result.CriminalText);
    }

    const auto *cast = s_Options.Reagents && s_CastSpell ? GetSpellById(s_CastSpell) : nullptr;
    if (cast != nullptr && cast->Name != nullptr)
    {
        result.CastSpellName = cast->Name;
    }

    for (const auto &counter : s_Counters)
    {
        if (counter.Visible)
        {
            SupplyCounter entry;
            entry.Name = counter.Name;
            entry.Key = counter.Key;
            entry.Graphic = static_cast<uint16_t>(counter.Graphic);
            entry.Hue = static_cast<uint16_t>(counter.Hue);
            entry.Count = counter.Count;
            entry.Icon = counter.Icon;
            entry.Low = result.Coverage == SuppliesCoverage::Complete && counter.Warning > 0 &&
                counter.Count >= 0 && counter.Count < counter.Warning;
            entry.Warning = counter.Warning;
            entry.Highlighted = cast != nullptr &&
                std::find(s_CastReagents.begin(), s_CastReagents.end(), counter.Graphic) != s_CastReagents.end();
            result.Entries.push_back(std::move(entry));
        }
    }
    return result;
}

bool ItemCounters_StartRecount()
{
    refresh_counts();
    if (!g_Connected || s_Pack == 0 || g_World == nullptr ||
        g_Player == nullptr || g_Player->IsDead())
    {
        return false;
    }
    if (s_Recount.Backpack != 0 && s_Recount.Manual)
    {
        return false;
    }
    s_Recount = {};
    s_AutoAttempted.clear();
    s_PriorityBags.clear();
    s_Recount.Backpack = s_Pack;
    s_Recount.Manual = true;
    s_NeedsRefresh = true;
    ++s_Revision;
    s_Feedback = "Recounting backpack.";
    return true;
}

void ItemCounters_CancelRecount()
{
    if (s_Recount.Backpack == 0)
    {
        return;
    }
    const bool wasManual = s_Recount.Manual;
    if (g_Player != nullptr)
    {
        if (const auto *pack = g_Player->FindLayer(OL_BACKPACK))
        {
            mark_unopened_bags_attempted(*pack, s_AutoAttempted);
        }
    }
    s_Recount = {};
    s_PriorityBags.clear();
    s_NeedsRefresh = true;
    ++s_Revision;
    s_Feedback = wasManual ? "Recount canceled." : "Auto-recount paused.";
}

bool ItemCounters_StatusExpanded()
{
    switch_profile();
    return s_StatusExpanded;
}

void ItemCounters_SetStatusExpanded(bool expanded)
{
    switch_profile();
    if (s_StatusExpanded == expanded)
    {
        return;
    }
    const bool previous = s_StatusExpanded;
    s_StatusExpanded = expanded;
    if (!save_file(s_Counters, s_ShowHUD, s_Warnings))
    {
        s_StatusExpanded = previous;
        s_Feedback = "Could not save display options.";
        return;
    }
    ++s_Revision;
}

bool ItemCounters_UseCounter(uint16_t graphic, uint16_t hue)
{
    if (!g_Connected || g_Player == nullptr || g_World == nullptr || g_Player->IsDead() || graphic == 0)
    {
        return false;
    }
    const auto *pack = g_Player->FindLayer(OL_BACKPACK);
    if (pack == nullptr)
    {
        return false;
    }
    const auto *item = find_backpack_item(*pack, graphic, hue);
    if (item == nullptr)
    {
        return false;
    }
    g_Assistant.DoubleClick(item->Serial);
    return true;
}

void ItemCounters_OnContainerChanged(
    uint32_t containerSerial,
    uint32_t itemSerial,
    uint16_t graphic,
    uint16_t hue,
    bool isContainer,
    bool removed)
{
    if (g_World == nullptr || g_Player == nullptr || containerSerial == 0 || containerSerial == 0xFFFFFFFF)
    {
        return;
    }
    switch_profile();

    const bool insidePack = is_in_player_backpack(containerSerial);
    if (removed)
    {
        if (!insidePack)
        {
            return;
        }
        s_NeedsRefresh = true;
        ++s_Revision;
        if (isContainer && itemSerial != 0)
        {
            // Keep s_AutoAttempted intact: 0x25/0x3C updates remove and re-add
            // items in place, and wiping s_AutoAttempted here re-triggers auto-open.
            s_PriorityBags.erase(
                std::remove(s_PriorityBags.begin(), s_PriorityBags.end(), itemSerial),
                s_PriorityBags.end());
        }
        return;
    }

    const bool nonBagItem =
        isContainer &&
        (ItemCounters_IsNonBagContainerGraphic(graphic) ||
         (itemSerial != 0 && s_NonBagSerials.count(itemSerial) != 0));
    if (nonBagItem)
    {
        isContainer = false;
    }

    ContainerChangeInput input;
    input.InsidePlayerBackpack = insidePack;
    input.AutoRecountEnabled = s_AutoRecount;
    input.SkipPouches = s_SkipPouches;
    input.MatchesTrackedCounter = matches_tracked_counter(graphic, hue);

    uint32_t targetBagSerial = 0;
    if (isContainer && itemSerial != 0)
    {
        const auto *itemObj = g_World->FindWorldItem(itemSerial);
        input.IsNewSubcontainer = itemObj == nullptr || !itemObj->IsCorpse();
        input.ContainerOpened = itemObj != nullptr && itemObj->Opened;
        input.TargetBagGraphic = itemObj != nullptr ? itemObj->Graphic : graphic;
        targetBagSerial = itemSerial;
    }
    else
    {
        const auto *containerObj = g_World->FindWorldItem(containerSerial);
        input.IsNewSubcontainer = false;
        input.ContainerOpened = containerObj != nullptr && containerObj->Opened;
        input.TargetBagGraphic = containerObj != nullptr ? containerObj->Graphic : 0;
        targetBagSerial = containerSerial;
    }

    const auto action = ItemCounters_DecideContainerChange(input);
    if (action == ContainerChangeAction::Ignore)
    {
        return;
    }

    s_NeedsRefresh = true;
    ++s_Revision;

    if (action == ContainerChangeAction::RefreshAndQueueBagOpen &&
        targetBagSerial != 0 &&
        s_NonBagSerials.count(targetBagSerial) == 0 &&
        s_AutoAttempted.count(targetBagSerial) == 0 &&
        s_Recount.Pending != targetBagSerial &&
        s_PriorityBags.size() < RecountLimit &&
        std::find(s_PriorityBags.begin(), s_PriorityBags.end(), targetBagSerial) == s_PriorityBags.end())
    {
        s_PriorityBags.push_back(targetBagSerial);
    }
}

void ItemCounters_MarkNonBag(uint32_t serial)
{
    if (serial == 0)
    {
        return;
    }
    s_NonBagSerials.insert(serial);
    s_AutoAttempted.insert(serial);
    s_PriorityBags.erase(
        std::remove(s_PriorityBags.begin(), s_PriorityBags.end(), serial),
        s_PriorityBags.end());
    if (s_Recount.Pending == serial)
    {
        s_Recount.Pending = 0;
        s_NeedsRefresh = true;
        ++s_Revision;
    }
}

uint64_t ItemCounters_Revision()
{
    return s_Revision;
}

void ItemCounters_DrawEditor()
{
    refresh_counts();
    bool show = s_ShowHUD;
    if (ImGui::Checkbox("Show item counters in game", &show))
    {
        set_options(show, s_Warnings);
    }
    bool icons = s_ShowIcons;
    if (ImGui::Checkbox("Show item icons", &icons))
    {
        const bool previous = s_ShowIcons;
        s_ShowIcons = icons;
        if (!save_file(s_Counters, s_ShowHUD, s_Warnings))
        {
            s_ShowIcons = previous;
            s_Feedback = "Could not save display options.";
        }
        else
        {
            ++s_Revision;
        }
    }
    bool warn = s_Warnings;
    if (ImGui::Checkbox("Warn when a count drops below its limit", &warn))
    {
        set_options(s_ShowHUD, warn);
    }
    bool bandage = s_BandageEnabled;
    if (ImGui::Checkbox("Show bandage timer", &bandage))
    {
        const bool previous = s_BandageEnabled;
        s_BandageEnabled = bandage;
        if (!save_file(s_Counters, s_ShowHUD, s_Warnings))
        {
            s_BandageEnabled = previous;
            s_Feedback = "Could not save display options.";
        }
    }
    int duration = s_BandageSeconds;
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Estimated bandage seconds", &duration, 0, 0))
    {
        const auto previous = s_BandageSeconds;
        s_BandageSeconds = std::max(1, std::min(duration, 60));
        if (!save_file(s_Counters, s_ShowHUD, s_Warnings))
        {
            s_BandageSeconds = previous;
            s_Feedback = "Could not save display options.";
        }
    }
    bool gold = s_GoldEnabled;
    if (ImGui::Checkbox("Track gold per hour", &gold))
    {
        const bool previous = s_GoldEnabled;
        s_GoldEnabled = gold;
        s_GoldStarted = false;
        if (!save_file(s_Counters, s_ShowHUD, s_Warnings))
        {
            s_GoldEnabled = previous;
            s_Feedback = "Could not save display options.";
        }
        else
        {
            ++s_Revision;
        }
    }
    bool autoRecount = s_AutoRecount;
    if (ImGui::Checkbox("Auto-recount backpack and new bags", &autoRecount))
    {
        const bool previous = s_AutoRecount;
        s_AutoRecount = autoRecount;
        if (!save_file(s_Counters, s_ShowHUD, s_Warnings))
        {
            s_AutoRecount = previous;
            s_Feedback = "Could not save display options.";
        }
        else
        {
            if (!s_AutoRecount && s_Recount.Backpack != 0 && !s_Recount.Manual)
            {
                s_Recount = {};
                s_PriorityBags.clear();
            }
            s_NeedsRefresh = true;
            ++s_Revision;
        }
    }
    bool skipPouches = s_SkipPouches;
    if (ImGui::Checkbox("Skip pouches during auto-recount", &skipPouches))
    {
        const bool previous = s_SkipPouches;
        s_SkipPouches = skipPouches;
        if (!save_file(s_Counters, s_ShowHUD, s_Warnings))
        {
            s_SkipPouches = previous;
            s_Feedback = "Could not save display options.";
        }
        else
        {
            s_NeedsRefresh = true;
            ++s_Revision;
        }
    }
    ImGui::TextDisabled("Recount opens bags inside your backpack.");
    if (ImGui::CollapsingHeader("Combat display"))
    {
        display_checkbox("Highlight spell reagents on cast", s_Options.Reagents);
        display_checkbox("Show health changes overhead", s_Options.Health);
        display_checkbox("Show mana and stamina changes above party members", s_Options.Party);
        display_checkbox("Show buff gains and losses overhead", s_Options.Buffs);
        display_checkbox("Log damage you take", s_Options.DamageTaken);
        display_checkbox("Log damage shown for other creatures", s_Options.OtherDamage);
        if (s_Options.Buffs)
        {
            display_format("Buff message", s_Options.BuffFormat);
            ImGui::TextDisabled("Use {action} and {name}. Press Enter to save.");
        }
        if (s_Options.Health)
        {
            display_format("Health message", s_Options.HealthFormat);
            ImGui::TextDisabled("Use {0%%} for health. Press Enter to save.");
        }
        if (s_Options.Party)
        {
            display_format("Party message", s_Options.PartyFormat);
            ImGui::TextDisabled("Use {0} for mana and {1} for stamina. Press Enter to save.");
        }
    }
    if (ImGui::CollapsingHeader("In-game title display"))
    {
        display_checkbox("Show a colored copy of the title in game", s_Options.Caption);
        display_checkbox("Color the character name by notoriety", s_Options.Notoriety);
    }
    draw_container_labels();
    if (ImGui::BeginChild("##item_counter_rows", ImVec2(0.0f, 132.0f), ImGuiChildFlags_Borders))
    {
        for (int i = 0; i < (int)s_Counters.size(); ++i)
        {
            const auto &counter = s_Counters[i];
            const auto count = counter.Count >= 0 ? str_from(counter.Count) : astr_t("-");
            char label[164]{};
            snprintf(label, sizeof(label), "%s (%s)   %s", counter.Name.c_str(), counter.Key.c_str(), count.c_str());
            ImGui::PushID(i);
            if (ImGui::Selectable(label, s_Selected == i))
            {
                s_Selected = i;
                edit(&counter);
                s_Feedback.clear();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("New counter"))
    {
        s_Selected = -1;
        edit(nullptr);
        s_Feedback.clear();
    }
    ImGui::SameLine();
    if (s_Recount.Backpack != 0)
    {
        if (ImGui::Button("Cancel recount"))
        {
            ItemCounters_CancelRecount();
        }
    }
    else
    {
        ImGui::BeginDisabled(!g_Connected || s_Pack == 0 || g_Player == nullptr || g_Player->IsDead());
        if (ImGui::Button("Recount backpack"))
        {
            ItemCounters_StartRecount();
        }
        ImGui::EndDisabled();
    }

    ImGui::InputText("Name", s_Editor.Name.data(), s_Editor.Name.size());
    ImGui::InputText("Title tag", s_Editor.Key.data(), s_Editor.Key.size());
    ImGui::InputScalar("Graphic", ImGuiDataType_S32, &s_Editor.Graphic, nullptr, nullptr, "%04X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    ImGui::BeginDisabled(g_TargetHandler != nullptr || g_Target.IsTargeting());
    if (ImGui::Button("Target item"))
    {
        g_TargetHandler = &CAssistant::HandleTargetCounterItem;
        g_Assistant.RequestTarget("Target an item to set its graphic and hue.");
    }
    ImGui::EndDisabled();

    ImGui::Checkbox("Any hue", &s_Editor.AnyColor);
    ImGui::BeginDisabled(s_Editor.AnyColor);
    ImGui::InputScalar("Hue", ImGuiDataType_S32, &s_Editor.Hue, nullptr, nullptr, "%04X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::EndDisabled();
    ImGui::InputInt("Warn below", &s_Editor.Warning);
    ImGui::Checkbox("Show in game", &s_Editor.Visible);
    ImGui::Checkbox("Use an icon for this counter", &s_Editor.Icon);

    if (ImGui::Button("Save counter"))
    {
        commit_counter();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(s_Selected < 0);
    if (ImGui::Button("Remove counter"))
    {
        auto updated = s_Counters;
        updated.erase(updated.begin() + s_Selected);
        if (save_file(updated, s_ShowHUD, s_Warnings))
        {
            s_Counters = std::move(updated);
            s_Selected = -1;
            edit(nullptr);
            s_NeedsRefresh = true;
            ++s_Revision;
            s_Feedback.clear();
        }
        else
        {
            s_Feedback = "Could not save counters.";
        }
    }
    ImGui::EndDisabled();
    if (!s_Feedback.empty())
    {
        ImGui::TextWrapped("%s", s_Feedback.c_str());
    }
}

bool ItemCounters_IsHUDVisible()
{
    return s_HUDVisible;
}

void ItemCounters_DrawHUD()
{
    s_HUDVisible = false;
    switch_profile();
    const bool hasAutoRecountConsumer =
        s_ShowHUD || s_Warnings || s_StatusExpanded ||
        (g_Player != nullptr && g_AssistantConfig.DisplayEnabled());
    recount_backpack(hasAutoRecountConsumer);
    update_session_timers();
    const auto *cast = s_Options.Reagents && s_CastSpell ? GetSpellById(s_CastSpell) : nullptr;
    const bool captionEligible = s_Options.Caption && g_Player != nullptr && g_AssistantConfig.DisplayEnabled();
    if (!s_ShowHUD && !s_Warnings && !s_BandageEnabled && !s_GoldEnabled && cast == nullptr && !captionEligible)
    {
        return;
    }

    refresh_counts();
    if (captionEligible)
    {
        g_DisplayManager.Redraw(true);
    }
    const bool caption = captionEligible && !g_DisplayManager.GetTitleParts().empty();
    const bool counters = s_ShowHUD && s_Pack &&
        std::any_of(s_Counters.begin(), s_Counters.end(), [](const auto &counter) {
            return counter.Visible;
        });
    const uint32_t bandageMs = g_Ticks - s_BandageAt;
    const uint32_t expectedMs = static_cast<uint32_t>(s_BandageSeconds) * 1000;
    const bool bandaging = s_BandageEnabled && s_Bandaging;
    astr_t gold;
    const bool goldValid = gold_stat("gph", gold);
    if (!counters && !bandaging && !goldValid && cast == nullptr && !caption)
    {
        return;
    }

    const auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 12.0f, viewport->WorkPos.y + 60.0f),
        ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.85f);

    s_HUDVisible = true;
    const bool wasShowing = s_ShowHUD;
    if (ImGui::Begin("Assistant display###Item counters", counters && !caption ? &s_ShowHUD : nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                        ImGuiWindowFlags_NoDocking))
    {
        if (caption)
        {
            draw_caption();
            if (counters || cast != nullptr || bandaging || goldValid)
            {
                ImGui::Separator();
            }
        }
        // Only emit counter rows when counters are actually visible; an empty
        // BeginGroup()/EndGroup() right after Begin() pulls title_bar_rect.Max.x
        // from LastItemData into CursorMaxPos and grows AlwaysAutoResize windows
        // by +WindowPadding.x every frame until they span the whole viewport.
        if (counters)
        {
            int column = 0;
            for (const auto &entry : s_Counters)
            {
                if (!entry.Visible)
                {
                    continue;
                }

                if (column++ % 3 != 0)
                {
                    ImGui::SameLine();
                }

                if (s_ShowIcons && entry.Icon)
                {
                    constexpr int IconSize = 16;
                    const auto image = AssetGrid_AcquireThumb(AssetTextureKind::Static, (uint16_t)entry.Graphic, IconSize);
                    if (AssetTextureCache_IsReady(image))
                    {
                        ImGui::Image(image, ImVec2(IconSize, IconSize));
                        ImGui::SameLine();
                    }
                }

                const bool highlighted = cast != nullptr &&
                    std::find(s_CastReagents.begin(), s_CastReagents.end(), entry.Graphic) != s_CastReagents.end();
                if (highlighted)
                {
                    const auto label = entry.Key + " " + (entry.Count >= 0 ? str_from(entry.Count) : astr_t("-"));
                    const auto start = ImGui::GetCursorScreenPos();
                    const auto size = ImGui::CalcTextSize(label.c_str());
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(start.x - 2.0f, start.y), ImVec2(start.x + size.x + 2.0f, start.y + size.y),
                        IM_COL32(135, 30, 29, 255), 2.0f);
                    ImGui::TextUnformatted(label.c_str());
                }
                else if (entry.Count < 0)
                {
                    ImGui::TextDisabled("%s —", entry.Key.c_str());
                }
                else if (entry.Warning > 0 && entry.Count < entry.Warning)
                {
                    ImGui::TextColored(LowColor, "%s %d", entry.Key.c_str(), entry.Count);
                }
                else
                {
                    ImGui::Text("%s %d", entry.Key.c_str(), entry.Count);
                }
            }
        }
        if (cast != nullptr)
        {
            ImGui::TextUnformatted(cast->Name);
            ImGui::TextUnformatted(cast->Reagents);
        }
        if (bandaging)
        {
            if (bandageMs < expectedMs)
            {
                ImGui::Text("Bandage %.1f s", (expectedMs - bandageMs) / 1000.0);
            }
            else
            {
                ImGui::TextUnformatted("Bandage finishing");
            }
        }
        if (goldValid)
        {
            ImGui::Text("%s gold per hour (net)", gold.c_str());
        }
    }
    ImGui::End();

    if (wasShowing && !s_ShowHUD && !save_file(s_Counters, false, s_Warnings))
    {
        s_ShowHUD = true;
        s_Feedback = "Could not save display options.";
    }
}

void ItemCounters_OnJournalMessage(const CJournalMessage &message)
{
    if (!(message.Flags & JMT_SYSTEM) || g_Player == nullptr)
    {
        return;
    }

    switch_profile();
    const auto lower = str_lower(message.Text);
    if (s_BandageEnabled)
    {
        static astr_t language;
        static astr_t start;
        static vector<astr_t> finishes;
        if (language != g_Language || start.empty())
        {
            language = g_Language;
            auto *cliloc = g_ClilocManager.Cliloc(language);
            start = str_lower(cliloc->GetA(500956, false, "You begin applying the bandages."));
            finishes.clear();
            const int ids[] = { 500962, 500963, 500965, 500966, 500967, 500968, 500969,
                                501042, 1010060, 1010058, 1010059, 1060088, 503255, 503256 };
            for (const auto id : ids)
            {
                const auto text = cliloc->GetA(id);
                if (!text.empty())
                {
                    finishes.push_back(str_lower(text));
                }
            }
        }

        if (lower == start)
        {
            s_BandageAt = g_Ticks;
            s_Bandaging = true;
            ++s_Revision;
        }
        else if (s_Bandaging &&
                 (lower == "you finish applying the bandages." ||
                  std::find(finishes.begin(), finishes.end(), lower) != finishes.end()))
        {
            s_Bandaging = false;
            ++s_Revision;
        }
    }

    if (!s_Options.MessageRules)
    {
        return;
    }
    for (const auto &rule : s_OverheadRules)
    {
        if (lower.find(str_lower(rule.Match)) == astr_t::npos)
        {
            continue;
        }

        astr_t text;
        for (size_t pos = 0; pos < rule.Text.size();)
        {
            if (rule.Text.compare(pos, 5, "{msg}") == 0)
            {
                text += message.Text;
                pos += 5;
            }
            else if (pos + 2 < rule.Text.size() && rule.Text[pos] == '{' &&
                     rule.Text[pos + 1] >= '1' && rule.Text[pos + 1] <= '9' && rule.Text[pos + 2] == '}')
            {
                const int requested = rule.Text[pos + 1] - '0';
                size_t word = 0;
                for (int index = 1; index <= requested; ++index)
                {
                    const size_t start = message.Text.find_first_not_of(" \t\r\n", word);
                    if (start == astr_t::npos)
                    {
                        break;
                    }
                    const size_t end = message.Text.find_first_of(" \t\r\n", start);
                    if (index == requested)
                    {
                        text += message.Text.substr(start, end == astr_t::npos ? end : end - start);
                        break;
                    }
                    if (end == astr_t::npos)
                    {
                        break;
                    }
                    word = end;
                }
                pos += 3;
            }
            else
            {
                text += rule.Text[pos++];
            }
        }
        text = format_message(s_Options.RuleFormat, { { "{msg}", text } });
        if (!text.empty())
        {
            if (s_Options.UnicodeRules)
            {
                g_Assistant.ClientCharPrint(g_Player->Serial, text, message.Color);
            }
            else
            {
                g_PacketManager.DisplayTalkTextA(
                    g_Player->Serial, g_Player, "X:UO Assist", text, message.Color, 3, ST_NORMAL);
            }
        }
        break;
    }
}

void ItemCounters_DrawMessageRules()
{
    switch_profile();
    display_checkbox("Show matched messages overhead", s_Options.MessageRules);
    display_checkbox("Use Unicode message text", s_Options.UnicodeRules);
    display_format("Message format", s_Options.RuleFormat);
    ImGui::TextDisabled("Use {msg} for the matched rule's text. Press Enter to save.");
    ImGui::TextWrapped("Match a system message and show overhead text. Use {msg} for the whole message or {1} for its first word.");
    if (ImGui::BeginChild("##overhead_rules", ImVec2(0, 120), ImGuiChildFlags_Borders))
    {
        for (int i = 0; i < (int)s_OverheadRules.size(); ++i)
        {
            const auto &rule = s_OverheadRules[i];
            ImGui::PushID(i);
            if (ImGui::Selectable(rule.Match.c_str(), i == s_RuleSelected))
            {
                s_RuleSelected = i;
                snprintf(s_RuleMatch.data(), s_RuleMatch.size(), "%s", rule.Match.c_str());
                snprintf(s_RuleText.data(), s_RuleText.size(), "%s", rule.Text.c_str());
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::InputText("Match text", s_RuleMatch.data(), s_RuleMatch.size());
    ImGui::InputText("Overhead text", s_RuleText.data(), s_RuleText.size());
    if (ImGui::Button("New rule"))
    {
        s_RuleSelected = -1;
        s_RuleMatch = {};
        s_RuleText = {};
    }
    ImGui::SameLine();
    if (ImGui::Button("Save rule") && s_RuleMatch[0] && s_RuleText[0])
    {
        const auto previous = s_OverheadRules;
        OverheadRule entry{ s_RuleMatch.data(), s_RuleText.data() };
        if (s_RuleSelected >= 0)
        {
            s_OverheadRules[s_RuleSelected] = entry;
        }
        else if (s_OverheadRules.size() < 64)
        {
            s_OverheadRules.push_back(entry);
        }
        if (!save_file(s_Counters, s_ShowHUD, s_Warnings))
        {
            s_OverheadRules = previous;
            s_Feedback = "Could not save overhead rules.";
        }
        else
        {
            s_Feedback.clear();
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(s_RuleSelected < 0);
    if (ImGui::Button("Remove rule"))
    {
        const auto previous = s_OverheadRules;
        s_OverheadRules.erase(s_OverheadRules.begin() + s_RuleSelected);
        if (!save_file(s_Counters, s_ShowHUD, s_Warnings))
        {
            s_OverheadRules = previous;
            s_Feedback = "Could not save overhead rules.";
        }
        else
        {
            s_RuleSelected = -1;
            s_RuleMatch = {};
            s_RuleText = {};
            s_Feedback.clear();
        }
    }
    ImGui::EndDisabled();
    if (!s_Feedback.empty())
    {
        ImGui::TextWrapped("%s", s_Feedback.c_str());
    }
}

} // namespace xuoassist
