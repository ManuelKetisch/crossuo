// SPDX-License-Identifier: AGPL-3.0-or-later

#include "targetassist.h"

#include "../config/assistantconfig.h"
#include "../config/configio.h"
#include "actions.h"
#include "assist.h"
#include "gameplayfilters.h"
#include "targetassist_rules.h"
#include "world.h"
#include "Config.h"
#include "Globals.h"
#include "Target.h"
#include <external/gfx/imgui/imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace xuoassist {

namespace {

constexpr uint8_t Harmful = 1;
constexpr uint8_t Beneficial = 2;
constexpr uint32_t TargetQueueTimeout = 5000;
constexpr int MaxPresets = 24;
constexpr int MaxPresetRange = TargetPresetMaxRange;

struct TargetPreset
{
    std::array<char, 64> Name{};
    std::array<char, 64> MobileName{};
    std::array<char, 128> BodiesText{};
    std::vector<uint16_t> Bodies;
    int Hue = -1;
    int MinDistance = 0;
    int MaxDistance = 12;
    int Notorieties = 0;
    int Selector = static_cast<int>(TargetSelectorMode::Nearest);
    int Poisoned = static_cast<int>(TargetFlagFilter::Any);
    int Blessed = static_cast<int>(TargetFlagFilter::Any);
    int Paralyzed = static_cast<int>(TargetFlagFilter::Any);
    int WarMode = static_cast<int>(TargetFlagFilter::Any);
    int Friends = static_cast<int>(TargetFriendFilter::Any);
    bool RespectIgnoreList = true;
};

struct Settings
{
    bool SmartLast = false;
    bool ProtectFriends = false;
    bool SkipFriends = false;
    bool Queue = false;
    bool BlockWarDismount = false;
    int LastRange = 0;
};

Settings s_Settings;
vector<TargetPreset> s_Presets;
vector<uint32_t> s_LastCycledSerials;
uint32_t s_RandomCounter = 0;
TargetPreset s_Draft;
int s_SelectedPreset = -1;
astr_t s_PresetStatus;
astr_t s_Profile;
bool s_SaveFailed = false;
uint32_t s_LastHarmful = 0;
uint32_t s_LastBeneficial = 0;
uint32_t s_QueuedAt = 0;
TargetAssistSelection s_Queued = TargetAssistSelection::None;

TargetFilterRule to_filter_rule(const TargetPreset &preset)
{
    TargetFilterRule rule;
    rule.MobileName = preset.MobileName.data();
    rule.Bodies = preset.Bodies;
    rule.Hue = preset.Hue;
    rule.MinDistance = preset.MinDistance;
    rule.MaxDistance = preset.MaxDistance;
    rule.Notorieties = preset.Notorieties;
    rule.Selector = TargetRules_ClampSelector(preset.Selector);
    rule.Poisoned = TargetRules_ClampFlag(preset.Poisoned);
    rule.Blessed = TargetRules_ClampFlag(preset.Blessed);
    rule.Paralyzed = TargetRules_ClampFlag(preset.Paralyzed);
    rule.WarMode = TargetRules_ClampFlag(preset.WarMode);
    rule.Friends = TargetRules_ClampFriendFilter(preset.Friends);
    rule.RespectIgnoreList = preset.RespectIgnoreList;
    TargetRules_Normalize(rule);
    return rule;
}

void sync_bodies_text(TargetPreset &preset)
{
    const std::string formatted = TargetRules_FormatBodyList(preset.Bodies);
    std::snprintf(preset.BodiesText.data(), preset.BodiesText.size(), "%s", formatted.c_str());
}

void normalize(TargetPreset &preset)
{
    TargetFilterRule rule = to_filter_rule(preset);
    preset.Bodies = std::move(rule.Bodies);
    sync_bodies_text(preset);
    preset.Hue = rule.Hue;
    preset.MinDistance = rule.MinDistance;
    preset.MaxDistance = rule.MaxDistance;
    preset.Notorieties = rule.Notorieties;
    preset.Selector = static_cast<int>(rule.Selector);
    preset.Poisoned = static_cast<int>(rule.Poisoned);
    preset.Blessed = static_cast<int>(rule.Blessed);
    preset.Paralyzed = static_cast<int>(rule.Paralyzed);
    preset.WarMode = static_cast<int>(rule.WarMode);
    preset.Friends = static_cast<int>(rule.Friends);
    preset.RespectIgnoreList = rule.RespectIgnoreList;
}

int find_preset(const astr_t &name)
{
    const auto key = str_lower(name);
    for (size_t i = 0; i < s_Presets.size(); ++i)
    {
        if (str_lower(static_cast<const char *>(s_Presets[i].Name.data())) == key)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void switch_profile()
{
    const fs_path path = g_AssistantConfig.GetProfilePath();
    const astr_t key = fs_path_str(path);
    if (key == s_Profile)
    {
        return;
    }

    s_Profile = key;
    TargetAssist_ResetSession();
    CIniFile ini;
    ini.Load(fs_path_join(path, "Targeting.ini"));
    s_Settings.SmartLast = ini.GetBool("Targeting", "smartlast", false);
    s_Settings.ProtectFriends = ini.GetBool("Targeting", "protectfriends", false);
    s_Settings.SkipFriends = ini.GetBool("Targeting", "skipfriends", false);
    s_Settings.Queue = ini.GetBool("Targeting", "queue", false);
    s_Settings.BlockWarDismount = ini.GetBool("Targeting", "blockwardismount", false);
    s_Settings.LastRange = std::max(0, std::min(ini.GetInt("Targeting", "lastrange"), 30));
    s_SaveFailed = false;
    s_Presets.clear();
    s_LastCycledSerials.clear();
    s_Draft = {};
    s_SelectedPreset = -1;
    const int count = std::max(0, std::min(MaxPresets, ini.GetInt("Targeting", "presetcount")));
    for (int i = 0; i < count; ++i)
    {
        const auto section = IniSectionName("Preset", i);
        const auto name = str_trim(ini.Get(section, "name"));
        TargetPreset preset;
        if (name.empty() || name.size() >= preset.Name.size() || find_preset(name) >= 0)
        {
            continue;
        }
        const auto mobileName = ini.Get(section, "mobile_name");
        if (mobileName.size() >= preset.MobileName.size())
        {
            continue;
        }
        std::snprintf(preset.Name.data(), preset.Name.size(), "%s", name.c_str());
        std::snprintf(preset.MobileName.data(), preset.MobileName.size(), "%s", mobileName.c_str());

        const auto bodiesStr = ini.Get(section, "bodies");
        if (!bodiesStr.empty())
        {
            TargetRules_ParseBodyList(bodiesStr, preset.Bodies);
        }
        else
        {
            const int legacyBody = ini.GetInt(section, "body", -1);
            if (legacyBody >= 0 && legacyBody <= 0xFFFF)
            {
                preset.Bodies.push_back(static_cast<uint16_t>(legacyBody));
            }
        }
        preset.Hue = ini.GetInt(section, "hue", -1);
        preset.MinDistance = ini.GetInt(section, "min_distance", 0);
        preset.MaxDistance = ini.GetInt(section, "max_distance", 12);
        preset.Notorieties = ini.GetInt(section, "notorieties", 0);
        preset.Selector = ini.GetInt(section, "selector", 0);
        preset.Poisoned = ini.GetInt(section, "poisoned", 0);
        preset.Blessed = ini.GetInt(section, "blessed", 0);
        preset.Paralyzed = ini.GetInt(section, "paralyzed", 0);
        preset.WarMode = ini.GetInt(section, "warmode", 0);
        preset.Friends = ini.GetInt(section, "friend_mode", 0);
        preset.RespectIgnoreList = ini.GetBool(section, "respect_ignore_list", true);
        normalize(preset);
        s_Presets.push_back(preset);
        s_LastCycledSerials.push_back(0);
    }
}

bool save(const Settings &values, const vector<TargetPreset> &presets)
{
    CIniFile ini;
    ini.SetBool("Targeting", "smartlast", values.SmartLast);
    ini.SetBool("Targeting", "protectfriends", values.ProtectFriends);
    ini.SetBool("Targeting", "skipfriends", values.SkipFriends);
    ini.SetBool("Targeting", "queue", values.Queue);
    ini.SetBool("Targeting", "blockwardismount", values.BlockWarDismount);
    ini.SetInt("Targeting", "lastrange", values.LastRange);
    ini.SetInt("Targeting", "presetcount", static_cast<int>(presets.size()));
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto section = IniSectionName("Preset", static_cast<int>(i));
        const auto &preset = presets[i];
        ini.Set(section, "name", preset.Name.data());
        ini.Set(section, "mobile_name", preset.MobileName.data());
        ini.SetInt(section, "body", preset.Bodies.empty() ? -1 : static_cast<int>(preset.Bodies.front()));
        ini.Set(section, "bodies", TargetRules_FormatBodyList(preset.Bodies));
        ini.SetInt(section, "hue", preset.Hue);
        ini.SetInt(section, "min_distance", preset.MinDistance);
        ini.SetInt(section, "max_distance", preset.MaxDistance);
        ini.SetInt(section, "notorieties", preset.Notorieties);
        ini.SetInt(section, "selector", preset.Selector);
        ini.SetInt(section, "poisoned", preset.Poisoned);
        ini.SetInt(section, "blessed", preset.Blessed);
        ini.SetInt(section, "paralyzed", preset.Paralyzed);
        ini.SetInt(section, "warmode", preset.WarMode);
        ini.SetInt(section, "friend_mode", preset.Friends);
        ini.SetBool(section, "respect_ignore_list", preset.RespectIgnoreList);
    }
    g_AssistantConfig.EnsureDirectories();
    return ini.Save(fs_path_join(g_AssistantConfig.GetProfilePath(), "Targeting.ini"));
}

bool enabled()
{
    if (!g_Config.AssistantEnabled)
    {
        return false;
    }

    switch_profile();
    return true;
}

bool ordinary_cursor()
{
    return g_Target.IsTargeting() && !g_Target.FromAssist && g_TargetHandler == nullptr &&
        g_Target.MultiGraphic == 0 && g_Target.Type <= 1 && g_Target.CursorType < 3;
}

bool preset_allowed()
{
    return g_Connected && g_GameState == GS_GAME && g_World != nullptr && g_Player != nullptr &&
        !g_Player->IsDead() && EnabledCommandSearchGround() && EnabledCommandFindTypeOrIgnore() &&
        (g_ServerAssistantFeatures & ASF_CLOSEST_TARGETS) == 0;
}

CGameCharacter *choose_mobile(
    const TargetPreset &preset,
    int presetIndex,
    bool globalProtectFriends,
    bool globalSkipFriends)
{
    const TargetFilterRule rule = to_filter_rule(preset);
    std::vector<CGameCharacter *> mobiles;
    std::vector<TargetCandidate> candidates;

    for (auto *object = g_World->m_Items; object != nullptr;
         object = static_cast<CGameObject *>(object->m_Next))
    {
        auto *mobile = object->GameCharacterPtr();
        if (mobile == nullptr || mobile->Serial == g_PlayerSerial || mobile->MapIndex != g_CurrentMap)
        {
            continue;
        }
        TargetCandidate cand;
        cand.Serial = mobile->Serial;
        cand.Name = mobile->GetName();
        cand.Body = mobile->Graphic;
        cand.Hue = mobile->Color;
        cand.Notoriety = mobile->Notoriety;
        cand.Distance = GetDistance(g_Player, mobile);
        cand.Hits = mobile->Hits;
        cand.MaxHits = mobile->MaxHits;
        cand.Dead = mobile->IsDead();
        cand.Hidden = mobile->Hidden();
        cand.Ignored = mobile->Ignored || GameplayFilters_IgnoreTarget(mobile->Serial);
        cand.Friend = TargetAssist_OnlyFriend(mobile);
        cand.Poisoned = mobile->IsPoisoned();
        cand.Blessed = mobile->YellowHits();
        cand.Paralyzed = mobile->IsParalyzed();
        cand.WarMode = mobile->InWarMode();

        mobiles.push_back(mobile);
        candidates.push_back(std::move(cand));
    }

    TargetSelectionContext ctx;
    ctx.GlobalProtectFriends = globalProtectFriends;
    ctx.GlobalSkipFriends = globalSkipFriends;
    if (presetIndex >= 0 && static_cast<size_t>(presetIndex) < s_LastCycledSerials.size())
    {
        ctx.LastCycledSerial = s_LastCycledSerials[presetIndex];
    }
    ctx.RandomSeed = g_Ticks ^ (++s_RandomCounter * 2654435761u);

    const int chosen = TargetRules_SelectCandidate(rule, candidates, ctx);
    if (chosen < 0)
    {
        return nullptr;
    }
    if (rule.Selector == TargetSelectorMode::Next &&
        presetIndex >= 0 && static_cast<size_t>(presetIndex) < s_LastCycledSerials.size())
    {
        s_LastCycledSerials[presetIndex] = mobiles[chosen]->Serial;
    }
    return mobiles[chosen];
}

void draw_presets()
{
    ImGui::SeparatorText("Target presets");
    const char *currentName = s_SelectedPreset < 0 ? "New preset" :
        s_Presets[s_SelectedPreset].Name.data();
    if (ImGui::BeginCombo("Preset", currentName))
    {
        if (ImGui::Selectable("New preset", s_SelectedPreset < 0))
        {
            s_SelectedPreset = -1;
            s_Draft = {};
            s_PresetStatus.clear();
        }
        for (size_t i = 0; i < s_Presets.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(s_Presets[i].Name.data(), s_SelectedPreset == static_cast<int>(i)))
            {
                s_SelectedPreset = static_cast<int>(i);
                s_Draft = s_Presets[i];
                s_PresetStatus.clear();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::InputText("Preset name", s_Draft.Name.data(), s_Draft.Name.size());
    ImGui::InputText("Mobile name contains", s_Draft.MobileName.data(), s_Draft.MobileName.size());
    ImGui::InputText("Body IDs", s_Draft.BodiesText.data(), s_Draft.BodiesText.size());
    ImGui::TextDisabled("Comma-separated decimal or hex IDs (for example 0x0190, 0x0191). Leave blank for any body.");
    ImGui::InputInt("Hue ID", &s_Draft.Hue);
    ImGui::TextDisabled("Enter -1 to match any hue.");
    ImGui::SliderInt("Minimum distance", &s_Draft.MinDistance, 0, MaxPresetRange);
    ImGui::SliderInt("Maximum distance", &s_Draft.MaxDistance, 0, MaxPresetRange);
    ImGui::Combo("Choose by", &s_Draft.Selector,
        "Nearest\0Lowest health percentage\0Farthest\0Highest health percentage\0Random\0Next (cycle)\0");
    if (ImGui::BeginCombo("Notoriety", s_Draft.Notorieties == 0 ? "Any" : "Selected"))
    {
        constexpr const char *names[] = { "Innocent", "Friendly", "Attackable", "Criminal",
            "Enemy", "Murderer", "Invulnerable" };
        for (int i = 0; i < 7; ++i)
        {
            const int bit = 1 << i;
            bool selected = (s_Draft.Notorieties & bit) != 0;
            if (ImGui::Checkbox(names[i], &selected))
            {
                s_Draft.Notorieties ^= bit;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Combo("Poisoned", &s_Draft.Poisoned, "Any\0Only poisoned\0Exclude poisoned\0");
    ImGui::Combo("Blessed", &s_Draft.Blessed, "Any\0Only blessed\0Exclude blessed\0");
    ImGui::Combo("Paralyzed", &s_Draft.Paralyzed, "Any\0Only paralyzed\0Exclude paralyzed\0");
    ImGui::Combo("War mode", &s_Draft.WarMode, "Any\0Only in war mode\0Exclude war mode\0");
    ImGui::Combo("Friends", &s_Draft.Friends, "Any\0Only friends\0Exclude friends\0");
    ImGui::Checkbox("Skip ignored mobiles", &s_Draft.RespectIgnoreList);

    if (ImGui::Button("Save preset"))
    {
        const auto name = str_trim(s_Draft.Name.data());
        const int matching = find_preset(name);
        std::vector<uint16_t> parsedBodies;
        if (name.empty() || (matching >= 0 && matching != s_SelectedPreset))
        {
            s_PresetStatus = "Give this preset a unique name.";
        }
        else if (s_SelectedPreset < 0 && s_Presets.size() >= MaxPresets)
        {
            s_PresetStatus = "Remove a preset before adding another.";
        }
        else if (s_Draft.MinDistance > s_Draft.MaxDistance)
        {
            s_PresetStatus = "Minimum distance must be within maximum distance.";
        }
        else if (!TargetRules_ParseBodyList(s_Draft.BodiesText.data(), parsedBodies))
        {
            s_PresetStatus = "Enter valid body IDs separated by commas.";
        }
        else
        {
            s_Draft.Bodies = std::move(parsedBodies);
            std::snprintf(s_Draft.Name.data(), s_Draft.Name.size(), "%s", name.c_str());
            normalize(s_Draft);
            auto updated = s_Presets;
            const int index = s_SelectedPreset >= 0 ? s_SelectedPreset : static_cast<int>(updated.size());
            if (s_SelectedPreset >= 0)
            {
                updated[index] = s_Draft;
            }
            else
            {
                updated.push_back(s_Draft);
            }
            if (save(s_Settings, updated))
            {
                s_Presets.swap(updated);
                s_LastCycledSerials.resize(s_Presets.size(), 0);
                s_SelectedPreset = index;
                s_PresetStatus = "Preset saved.";
            }
            else
            {
                s_PresetStatus = "Could not save preset.";
            }
        }
    }

    if (s_SelectedPreset >= 0)
    {
        ImGui::SameLine();
        if (ImGui::Button("Remove preset"))
        {
            auto updated = s_Presets;
            updated.erase(updated.begin() + s_SelectedPreset);
            if (save(s_Settings, updated))
            {
                s_Presets.swap(updated);
                if (static_cast<size_t>(s_SelectedPreset) < s_LastCycledSerials.size())
                {
                    s_LastCycledSerials.erase(s_LastCycledSerials.begin() + s_SelectedPreset);
                }
                s_SelectedPreset = -1;
                s_Draft = {};
                s_PresetStatus = "Preset removed.";
            }
            else
            {
                s_PresetStatus = "Could not remove preset.";
            }
        }
    }
    if (s_SelectedPreset >= 0)
    {
        ImGui::BeginDisabled(!preset_allowed());
        if (ImGui::Button("Select matching mobile"))
        {
            TargetAssist_RunPreset(s_Presets[s_SelectedPreset].Name.data(), false);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!preset_allowed() || !ordinary_cursor());
        if (ImGui::Button("Use active cursor"))
        {
            TargetAssist_RunPreset(s_Presets[s_SelectedPreset].Name.data(), true);
        }
        ImGui::EndDisabled();
    }
    if (!s_PresetStatus.empty())
    {
        ImGui::TextWrapped("%s", s_PresetStatus.c_str());
    }
}

} // namespace

uint32_t TargetAssist_RunPreset(const std::string &name, bool useCursor)
{
    if (!enabled() || !preset_allowed())
    {
        s_PresetStatus = "Target presets are unavailable here.";
        return 0;
    }
    if (useCursor && !ordinary_cursor())
    {
        s_PresetStatus = "Open a target cursor first.";
        return 0;
    }
    const int index = find_preset(str_trim(name));
    if (index < 0)
    {
        s_PresetStatus = "Unknown target preset.";
        return 0;
    }

    auto preset = s_Presets[index];
    if (useCursor && s_Settings.LastRange != 0)
    {
        preset.MaxDistance = std::min(preset.MaxDistance, s_Settings.LastRange);
    }
    const bool harmfulCursor = useCursor && g_Target.CursorType == Harmful;
    const bool protectFriends = harmfulCursor && s_Settings.ProtectFriends;
    auto *mobile = choose_mobile(preset, index, protectFriends, s_Settings.SkipFriends);
    if (mobile == nullptr)
    {
        s_PresetStatus = "No mobile matches this preset.";
        return 0;
    }

    const uint32_t serial = mobile->Serial;
    const auto mobileName = mobile->GetName();
    if (useCursor)
    {
        g_Target.SendTargetObject(serial);
        if (g_Target.IsTargeting())
        {
            s_PresetStatus = "The target cursor is still open.";
            return 0;
        }
    }
    else
    {
        g_Target.RememberTargetObject(*mobile);
        g_LastTargetObject = serial;
        // A deliberate selection becomes the fallback for both cursor types.
        s_LastHarmful = 0;
        s_LastBeneficial = 0;
    }
    Actions_SetNewTargetSystemSerial(serial);
    s_PresetStatus = "Selected " + (mobileName.empty() ? CAssistant::SerialToText(serial) : mobileName) + ".";
    return serial;
}

void TargetAssist_ResetSession()
{
    s_LastHarmful = 0;
    s_LastBeneficial = 0;
    s_Queued = TargetAssistSelection::None;
    s_QueuedAt = 0;
    s_PresetStatus.clear();
    std::fill(s_LastCycledSerials.begin(), s_LastCycledSerials.end(), 0u);
}

bool TargetAssist_BlockDismount(uint32_t serial)
{
    if (serial == g_PlayerSerial && g_Player != nullptr && g_Player->Warmode &&
        g_Player->IsMounted() && !g_Player->IsDrivingBoat() &&
        enabled() && s_Settings.BlockWarDismount)
    {
        g_Assistant.ClientPrint("Dismount blocked in war mode.", 0x0021);
        return true;
    }

    return false;
}

bool TargetAssist_OnlyFriend(const CGameObject *object)
{
    return object != nullptr && object->NPC && object->Serial != g_PlayerSerial &&
        g_AssistantConfig.InFriendList(object->Serial);
}

TargetAssistFilter TargetAssist_CycleFilter()
{
    if (!enabled())
    {
        return nullptr;
    }

    return [](const CGameObject *object) {
        return object == nullptr || (!GameplayFilters_IgnoreTarget(object->Serial) &&
            (!s_Settings.SkipFriends || !TargetAssist_OnlyFriend(object)));
    };
}

bool TargetAssist_AllowObject(uint8_t cursor, uint32_t serial)
{
    if (cursor == Harmful && serial != 0 && enabled() && s_Settings.ProtectFriends &&
        g_AssistantConfig.InFriendList(serial))
    {
        g_Assistant.ClientPrint("Harmful action blocked for a friend.", 0x0021);
        return false;
    }

    return true;
}

bool TargetAssist_AllowLastRange(uint32_t serial)
{
    if (serial == 0 || !enabled() || s_Settings.LastRange == 0 || g_Player == nullptr)
    {
        return true;
    }

    auto *target = g_World != nullptr ? g_World->FindWorldObject(serial) : nullptr;
    if (target == nullptr || GetDistance(target, g_Player) > s_Settings.LastRange)
    {
        g_Assistant.ClientPrint("Last target is out of range.", 0x0021);
        return false;
    }

    return true;
}

void TargetAssist_Record(uint8_t cursor, uint32_t serial)
{
    if (serial == 0 || serial == g_PlayerSerial || serial >= 0x40000000 ||
        (cursor != Harmful && cursor != Beneficial) || !enabled())
    {
        return;
    }

    (cursor == Harmful ? s_LastHarmful : s_LastBeneficial) = serial;
}

uint32_t TargetAssist_LastObject(uint8_t cursor, uint32_t fallback)
{
    if (!enabled() || !s_Settings.SmartLast)
    {
        return fallback;
    }

    if (cursor == Harmful && s_LastHarmful != 0)
    {
        return s_LastHarmful;
    }

    if (cursor == Beneficial && s_LastBeneficial != 0)
    {
        return s_LastBeneficial;
    }

    return fallback;
}

void TargetAssist_SendLast()
{
    const uint32_t serial = TargetAssist_LastObject(g_Target.CursorType, g_LastTargetObject);
    if (serial != 0)
    {
        if (TargetAssist_AllowLastRange(serial))
        {
            g_Target.SendTargetObject(serial);
        }
    }
}

void TargetAssist_Queue(TargetAssistSelection selection)
{
    if (selection == TargetAssistSelection::None)
    {
        s_Queued = selection;
    }
    else if (enabled() && s_Settings.Queue && g_Player != nullptr)
    {
        s_Queued = selection;
        s_QueuedAt = g_Ticks;
    }
}

TargetAssistSelection TargetAssist_TakeQueue()
{
    if (!enabled() || !s_Settings.Queue || g_Ticks - s_QueuedAt >= TargetQueueTimeout)
    {
        s_Queued = TargetAssistSelection::None;
    }

    const auto pending = s_Queued;
    s_Queued = TargetAssistSelection::None;
    return pending;
}

void TargetAssist_DrawOptions()
{
    switch_profile();
    Settings updated = s_Settings;
    bool changed = ImGui::Checkbox("Remember harmful and beneficial targets separately", &updated.SmartLast);
    changed |= ImGui::Checkbox("Protect friends from harmful targets and attack hotkeys", &updated.ProtectFriends);
    changed |= ImGui::Checkbox("Skip friends when cycling through mobiles or hostiles", &updated.SkipFriends);
    changed |= ImGui::Checkbox("Queue Target Self and Last Target for the next cursor", &updated.Queue);
    changed |= ImGui::Checkbox("Block self-dismount in war mode", &updated.BlockWarDismount);
    changed |= ImGui::SliderInt("Last target range", &updated.LastRange, 0, 30);
    ImGui::TextDisabled("Set range to zero to turn off the check.");
    ImGui::TextDisabled("A queued target expires after five seconds.");

    if (changed)
    {
        s_SaveFailed = !save(updated, s_Presets);
        if (!s_SaveFailed)
        {
            s_Settings = updated;
            if (!s_Settings.Queue)
            {
                s_Queued = TargetAssistSelection::None;
            }
        }
    }

    if (s_SaveFailed)
    {
        ImGui::TextUnformatted("Could not save targeting options.");
    }
    draw_presets();
}

} // namespace xuoassist
