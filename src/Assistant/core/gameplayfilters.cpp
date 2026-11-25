// SPDX-License-Identifier: AGPL-3.0-or-later

#include "gameplayfilters.h"

#include "../Misc.h"
#include "../config/assistantconfig.h"
#include "../config/configio.h"
#include "Config.h"
#include "GameObjects/GameObject.h"
#include "Globals.h"
#include "Target.h"
#include "assist.h"
#include "potionhands.h"
#include "world.h"

#include <algorithm>
#include <deque>
#include <external/gfx/imgui/imgui.h>

namespace xuoassist {

namespace {

constexpr char Options[] = "Options";
constexpr uint32_t DefaultRepeatDelay = 3;
constexpr uint32_t CastCursorTimeout = 5000;
constexpr size_t MaxRecent = 96;
constexpr size_t MaxRememberedLength = 512;
constexpr int MaxMobiles = 200;

enum class Channel : uint8_t
{
    System,
    Assistant,
    Overhead
};

struct Settings
{
    bool RepeatSystem = false;
    bool RepeatAssistant = false;
    bool RepeatOverhead = false;
    bool Snooping = false;
    bool FieldLabels = false;
    bool BlockTradeOffers = false;
    bool AllowFriendTrades = false;
    bool BlockedSounds = false;
    bool UnblockedSounds = false;
    bool GuardHealing = false;
    bool SmallDragons = false;
    bool SmallDrakes = false;
    bool SmallDaemons = false;
    int RepeatSeconds = DefaultRepeatDelay;
    std::vector<uint32_t> MutedMobiles;
    std::vector<uint32_t> IgnoredTargets;
};

struct Speech
{
    Channel Source;
    uint32_t Serial;
    uint32_t ShownAt;
    std::wstring Text;
};

struct HealCast
{
    bool WaitingForCursor = false;
    uint32_t StartedAt = 0;
    uint32_t CursorId = 0;
    bool HasCursor = false;
};

enum class MobileList
{
    Mute,
    Ignore
};

Settings s_Settings;
astr_t s_Profile;
std::deque<Speech> s_Recent;
HealCast s_HealCast;
MobileList s_TargetList = MobileList::Mute;
astr_t s_TargetProfile;
uint32_t s_TargetCharacter = 0;
astr_t s_Feedback;
int s_DeclinedTradeCount = 0;
astr_t s_LastDeclinedTrade;

const char *const BoolKeys[] = {
    "systemrepeats", "assistantrepeats", "overheadrepeats", "snooping",
    "guardhealing", "smalldragons", "smalldrakes", "smalldaemons",
    "blockedsounds", "unblockedsounds", "fieldlabels", "blocktradeoffers",
    "allowfriendtrades",
};
bool Settings::*const BoolFields[] = {
    &Settings::RepeatSystem, &Settings::RepeatAssistant, &Settings::RepeatOverhead,
    &Settings::Snooping, &Settings::GuardHealing, &Settings::SmallDragons,
    &Settings::SmallDrakes, &Settings::SmallDaemons,
    &Settings::BlockedSounds, &Settings::UnblockedSounds, &Settings::FieldLabels,
    &Settings::BlockTradeOffers, &Settings::AllowFriendTrades,
};
static_assert(sizeof(BoolKeys) / sizeof(BoolKeys[0]) == sizeof(BoolFields) / sizeof(BoolFields[0]),
              "Every Gameplay filter switch needs a save key");

bool contains(const std::vector<uint32_t> &list, uint32_t serial)
{
    return std::find(list.begin(), list.end(), serial) != list.end();
}

void load_mobiles(const CIniFile &ini, const char *section, std::vector<uint32_t> &list)
{
    const int count = std::min(ini.GetInt(section, "count"), MaxMobiles);
    for (int i = 0; i < count; ++i)
    {
        const auto serial = CAssistant::TextToSerial(ini.Get(section, str_from(i)));
        if (serial > 0 && serial < 0x40000000 && !contains(list, serial))
        {
            list.push_back(serial);
        }
    }
}

void save_mobiles(CIniFile &ini, const char *section, const std::vector<uint32_t> &list)
{
    ini.SetInt(section, "count", static_cast<int>(list.size()));
    for (size_t i = 0; i < list.size(); ++i)
    {
        ini.Set(section, str_from(i), CAssistant::SerialToText(list[i]));
    }
}

void switch_profile()
{
    if (g_AssistantConfig.ProfileName == s_Profile)
    {
        return;
    }

    s_Profile = g_AssistantConfig.ProfileName;
    GameplayFilters_ResetSession();
    s_Feedback.clear();

    CIniFile ini;
    ini.Load(fs_path_join(g_AssistantConfig.GetProfilePath(), "GameplayFilters.ini"));
    s_Settings = {};
    for (size_t i = 0; i < sizeof(BoolKeys) / sizeof(BoolKeys[0]); ++i)
    {
        s_Settings.*BoolFields[i] = ini.GetBool(Options, BoolKeys[i]);
    }
    s_Settings.RepeatSeconds =
        std::max(1, std::min(60, ini.GetInt(Options, "repeatseconds", DefaultRepeatDelay)));
    load_mobiles(ini, "Muted", s_Settings.MutedMobiles);
    load_mobiles(ini, "Ignored", s_Settings.IgnoredTargets);
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

void save(const Settings &settings)
{
    CIniFile ini;
    for (size_t i = 0; i < sizeof(BoolKeys) / sizeof(BoolKeys[0]); ++i)
    {
        ini.SetBool(Options, BoolKeys[i], settings.*BoolFields[i]);
    }
    ini.SetInt(Options, "repeatseconds", settings.RepeatSeconds);
    save_mobiles(ini, "Muted", settings.MutedMobiles);
    save_mobiles(ini, "Ignored", settings.IgnoredTargets);
    g_AssistantConfig.EnsureDirectories();
    if (ini.Save(fs_path_join(g_AssistantConfig.GetProfilePath(), "GameplayFilters.ini")))
    {
        s_Settings = settings;
        s_Recent.clear();
        if (!s_Settings.GuardHealing)
        {
            s_HealCast = {};
        }
        s_Feedback.clear();
    }
    else
    {
        s_Feedback = "Could not save gameplay filters.";
    }
}

bool repeat(Channel source, uint32_t serial, const std::wstring &text)
{
    if (text.empty() || text.size() > MaxRememberedLength)
    {
        return false;
    }

    const uint32_t expiry = static_cast<uint32_t>(s_Settings.RepeatSeconds) * 1000;
    const auto now = static_cast<uint32_t>(g_Ticks);
    while (!s_Recent.empty() && now - s_Recent.front().ShownAt >= expiry)
    {
        s_Recent.pop_front();
    }
    const auto match = std::find_if(s_Recent.begin(), s_Recent.end(), [&](const Speech &entry) {
        return entry.Source == source && entry.Serial == serial && entry.Text == text;
    });
    if (match != s_Recent.end())
    {
        return true;
    }
    if (s_Recent.size() >= MaxRecent)
    {
        s_Recent.pop_front();
    }
    s_Recent.push_back({ source, serial, now, text });
    return false;
}

bool heals(int spell)
{
    return spell == 4 || spell == 29; // Magery Heal and Greater Heal, respectively.
}

bool allow_heal(uint32_t serial)
{
    const auto *object = g_World != nullptr ? g_World->FindWorldObject(serial) : nullptr;
    if (object != nullptr && object->NPC && object->IsPoisoned())
    {
        g_Assistant.ClientPrint("Heal blocked for a poisoned target.", 0x0021);
        return false;
    }

    return true;
}

void draw_list(const char *title, MobileList purpose, std::vector<uint32_t> Settings::*field)
{
    const auto &mobiles = s_Settings.*field;
    ImGui::PushID(title);
    ImGui::TextUnformatted(title);
    ImGui::SameLine();
    ImGui::BeginDisabled(g_World == nullptr || g_PlayerSerial == 0 || g_TargetHandler != nullptr ||
                         g_Target.IsTargeting() || mobiles.size() >= MaxMobiles);
    if (ImGui::Button("Target mobile"))
    {
        s_TargetList = purpose;
        s_TargetProfile = s_Profile;
        s_TargetCharacter = g_PlayerSerial;
        g_TargetHandler = &CAssistant::HandleGameplayFilterTarget;
        g_Assistant.RequestTarget("Choose a mobile.");
    }
    ImGui::EndDisabled();

    ImGui::BeginChild("Targets", ImVec2(0, 104), true);
    for (size_t i = 0; i < mobiles.size(); ++i)
    {
        const uint32_t serial = mobiles[i];
        auto *object = g_World != nullptr ? g_World->FindWorldObject(serial) : nullptr;
        const auto name = object != nullptr ? object->GetName() : astr_t{};
        ImGui::PushID(static_cast<int>(serial));
        if (ImGui::SmallButton("Remove"))
        {
            auto updated = s_Settings;
            (updated.*field).erase((updated.*field).begin() + i);
            save(updated);
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        if (name.empty())
        {
            ImGui::Text("0x%08X", serial);
        }
        else
        {
            ImGui::Text("%s (0x%08X)", name.c_str(), serial);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopID();
}

} // namespace

bool GameplayFilters_AllowSpeech(
    uint32_t serial, bool system, bool assistant, bool mobile, const std::wstring &text)
{
    if (!enabled())
    {
        return true;
    }
    if (!system && mobile && contains(s_Settings.MutedMobiles, serial))
    {
        return false;
    }
    if (system && !assistant && s_Settings.Snooping &&
        text.compare(0, 11, L"You notice ") == 0 &&
        text.find(L" attempting to peek into ") != std::wstring::npos)
    {
        return false;
    }
    if (assistant)
    {
        return !s_Settings.RepeatAssistant || !repeat(Channel::Assistant, serial, text);
    }
    if (system)
    {
        return !s_Settings.RepeatSystem || !repeat(Channel::System, 0, text);
    }
    return !s_Settings.RepeatOverhead || !repeat(Channel::Overhead, serial, text);
}

void GameplayFilters_ReportSound(uint16_t index, bool blocked)
{
    if (!enabled() || !(blocked ? s_Settings.BlockedSounds : s_Settings.UnblockedSounds))
    {
        return;
    }
    const auto found = std::find_if(
        g_AssistantConfig.m_SoundFilters.begin(), g_AssistantConfig.m_SoundFilters.end(),
        [index](const CFilterSoundEntry &rule) {
            return CAssistant::TextToGraphic(rule.Index) == index;
        });
    astr_t detail = CAssistant::GraphicToText(index);
    if (found != g_AssistantConfig.m_SoundFilters.end() && !found->Name.empty())
    {
        detail += " (" + found->Name + ")";
    }
    g_Assistant.ClientPrint((blocked ? "Blocked sound " : "Received sound ") + detail);
}

void GameplayFilters_DrawSoundLogging()
{
    switch_profile();
    auto edited = s_Settings;
    bool changed = ImGui::Checkbox("Show blocked sounds", &edited.BlockedSounds);
    changed |= ImGui::Checkbox("Show unblocked sounds", &edited.UnblockedSounds);
    if (changed)
    {
        save(edited);
    }
}

uint16_t GameplayFilters_AnimationBody(uint32_t serial, uint16_t graphic)
{
    if (serial != g_PlayerSerial && enabled())
    {
        switch (graphic)
        {
            case 0x000C:
            case 0x003B:
                return s_Settings.SmallDragons ? 0x00D3 : graphic; // black bear
            case 0x003C:
            case 0x003D:
                return s_Settings.SmallDrakes ? 0x00D0 : graphic; // chicken
            case 0x0009:
            case 0x000A:
                return s_Settings.SmallDaemons ? 0x0032 : graphic; // skeleton
            default:
                break;
        }
    }
    return graphic;
}

bool GameplayFilters_IgnoreTarget(uint32_t serial)
{
    return enabled() && contains(s_Settings.IgnoredTargets, serial);
}

bool GameplayFilters_ShowFieldLabels()
{
    return enabled() && s_Settings.FieldLabels;
}

bool GameplayFilters_BlockTradeOffers()
{
    return enabled() && s_Settings.BlockTradeOffers;
}

bool GameplayFilters_AllowFriendTrades()
{
    return enabled() && s_Settings.AllowFriendTrades;
}

void GameplayFilters_ReportTradeDeclined(const std::string &opponentName)
{
    ++s_DeclinedTradeCount;
    s_LastDeclinedTrade = opponentName;
}

void GameplayFilters_BeginCast(int spell)
{
    s_HealCast = {};
    if (heals(spell) && enabled() && s_Settings.GuardHealing)
    {
        s_HealCast.WaitingForCursor = true;
        s_HealCast.StartedAt = g_Ticks;
    }
}

void GameplayFilters_ReceiveCursor(uint32_t cursorId, uint8_t type, uint8_t flags)
{
    PotionHands_OnTargetCursor(cursorId, type);

    if (!enabled() || !s_Settings.GuardHealing)
    {
        return;
    }
    if (s_HealCast.HasCursor && s_HealCast.CursorId == cursorId && type == 0 && flags == 2)
    {
        return;
    }

    const bool isHeal = s_HealCast.WaitingForCursor && type == 0 && flags == 2 &&
        static_cast<uint32_t>(g_Ticks) - s_HealCast.StartedAt < CastCursorTimeout;
    s_HealCast = {};
    s_HealCast.HasCursor = isHeal;
    s_HealCast.CursorId = cursorId;
}

void GameplayFilters_EndCursor(uint32_t cursorId)
{
    PotionHands_OnTargetCursorEnd(cursorId);

    if (s_HealCast.HasCursor && s_HealCast.CursorId == cursorId)
    {
        s_HealCast = {};
    }
}

bool GameplayFilters_AllowHealTarget(uint32_t cursorId, uint32_t serial)
{
    return !enabled() || !s_Settings.GuardHealing || !s_HealCast.HasCursor ||
        s_HealCast.CursorId != cursorId || allow_heal(serial);
}

bool GameplayFilters_AllowSpellTarget(int spell, uint32_t serial)
{
    return !heals(spell) || !enabled() || !s_Settings.GuardHealing || allow_heal(serial);
}

void GameplayFilters_ResetSession()
{
    s_Recent.clear();
    s_HealCast = {};
    s_TargetProfile.clear();
    s_TargetCharacter = 0;
    s_DeclinedTradeCount = 0;
    s_LastDeclinedTrade.clear();
}

void CAssistant::HandleGameplayFilterTarget(uint8_t *packet) const
{
    switch_profile();
    if (s_TargetProfile != s_Profile || s_TargetCharacter != g_PlayerSerial)
    {
        return;
    }

    const uint32_t serial = unpack32(packet + 7);
    auto *object = g_World != nullptr ? g_World->FindWorldObject(serial) : nullptr;
    if (object == nullptr || !object->NPC || serial == g_PlayerSerial)
    {
        s_Feedback = "Choose another mobile.";
        return;
    }

    auto updated = s_Settings;
    auto &list = s_TargetList == MobileList::Mute ? updated.MutedMobiles : updated.IgnoredTargets;
    if (!contains(list, serial) && list.size() < MaxMobiles)
    {
        list.push_back(serial);
        save(updated);
    }
}

void GameplayFilters_Draw()
{
    switch_profile();
    auto edited = s_Settings;
    bool changed = ImGui::Checkbox("Hide repeated system messages", &edited.RepeatSystem);
    changed |= ImGui::Checkbox("Hide repeated assistant messages", &edited.RepeatAssistant);
    changed |= ImGui::Checkbox("Hide repeated overhead messages", &edited.RepeatOverhead);
    changed |= ImGui::SliderInt("Repeat delay (seconds)", &edited.RepeatSeconds, 1, 60);
    changed |= ImGui::Checkbox("Hide snooping notices", &edited.Snooping);
    changed |= ImGui::Checkbox("Show spell field names on hover", &edited.FieldLabels);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Enable Change animated fields to tiles in the game's Display settings.");
    }
    changed |= ImGui::Checkbox("Block Heal and Greater Heal on poisoned mobiles", &edited.GuardHealing);
    changed |= ImGui::Checkbox("Block incoming trade offers", &edited.BlockTradeOffers);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Declines unsolicited trades opened with an item offered by another player. "
            "Empty trade windows do not identify who opened them and stay open.");
    }
    ImGui::Indent();
    ImGui::BeginDisabled(!edited.BlockTradeOffers);
    changed |= ImGui::Checkbox("Allow trade offers from friends", &edited.AllowFriendTrades);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Keep incoming trade offers open when the player is on your Friends list "
            "(or in your party when party members count as friends).");
    }
    ImGui::EndDisabled();
    if (edited.BlockTradeOffers && s_DeclinedTradeCount > 0)
    {
        if (!s_LastDeclinedTrade.empty())
        {
            ImGui::TextWrapped(
                "Declined %d unsolicited trade %s this session (last: %s).",
                s_DeclinedTradeCount,
                s_DeclinedTradeCount == 1 ? "offer" : "offers",
                s_LastDeclinedTrade.c_str());
        }
        else
        {
            ImGui::TextWrapped(
                "Declined %d unsolicited trade %s this session.",
                s_DeclinedTradeCount,
                s_DeclinedTradeCount == 1 ? "offer" : "offers");
        }
    }
    ImGui::Unindent();

    ImGui::Separator();
    ImGui::TextUnformatted("Creature appearance");
    changed |= ImGui::Checkbox("Show dragons as black bears", &edited.SmallDragons);
    changed |= ImGui::Checkbox("Show drakes as chickens", &edited.SmallDrakes);
    changed |= ImGui::Checkbox("Show daemons as skeletons", &edited.SmallDaemons);
    if (changed)
    {
        save(edited);
    }

    ImGui::Separator();
    draw_list("Mute overhead text", MobileList::Mute, &Settings::MutedMobiles);
    draw_list("Skip when cycling targets", MobileList::Ignore, &Settings::IgnoredTargets);
    if (!s_Feedback.empty())
    {
        ImGui::TextWrapped("%s", s_Feedback.c_str());
    }
}

} // namespace xuoassist
