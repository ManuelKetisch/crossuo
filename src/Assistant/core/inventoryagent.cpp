// SPDX-License-Identifier: AGPL-3.0-or-later

#include "inventoryagent.h"
#include "inventoryagent_plan.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "assist.h"
#include "dressagent.h"
#include "shopagent.h"
#include "world.h"
#include "../config/assistantconfig.h"
#include "../config/configio.h"
#include "../macros/macroexecutor.h"
#include "Config.h"
#include "Constants.h"
#include "GameObjects/ObjectOnCursor.h"
#include "Globals.h"
#include "Gumps/Gump.h"
#include "Managers/GumpManager.h"
#include "Misc.h"
#include "Network/Packets.h"
#include "ServerList.h"
#include "Target.h"

namespace xuoassist {
namespace {

constexpr uint32_t OnGround = 0xFFFFFFFF;
constexpr uint32_t MoveTimeout = 2500;
constexpr uint32_t ScavengerScanInterval = 500;
constexpr uint32_t AutoLootScanInterval = 500;
constexpr uint32_t PropertyTimeout = 2500;
constexpr uint32_t PropertyFreshness = 2500;
constexpr size_t MaxPropertyChecks = 4096;
constexpr size_t MaxScavengerHistory = 4096;
constexpr int MaxContainerDepth = 16;

struct PendingMove
{
    uint32_t Serial = 0;
    uint32_t Source = 0;
    uint32_t Destination = 0;
    uint32_t OriginalCount = 0;
    uint32_t DestinationCount = 0;
    uint16_t Count = 0;
    uint16_t Graphic = 0;
    uint16_t Hue = 0;
    uint32_t Deadline = 0;
};

enum class PropertyState { Requestable, Waiting, Resolved, Unavailable };

struct PropertyCheck
{
    InventoryAgentStack Item;
    uint32_t Container = 0;
    uint32_t Updated = 0;
    uint32_t Revision = 0;
    bool HasRevision = false;
    PropertyState State = PropertyState::Requestable;
    InventoryAgentPropertyMatches Matches;
};

std::vector<InventoryAgentProfile> s_Profiles;
fs_path s_Path{};
astr_t s_Shard, s_AssistantProfile;
uint32_t s_Character = 0;
int s_Active = -1;
bool s_AutoLootOnce = false;
uint32_t s_NextAction = 0;
uint32_t s_ItemActionAt = 0;
bool s_RecentItemAction = false;
PendingMove s_Pending;
std::unordered_set<uint32_t> s_ScavengerHistory;
InventoryAgentCorpseTracker s_CorpseTracker;
std::unordered_map<uint32_t, PropertyCheck> s_PropertyChecks;
uint32_t s_PropertyRequest = 0;
astr_t s_Status;

bool Due(uint32_t now, uint32_t then)
{
    return static_cast<int32_t>(now - then) >= 0;
}

bool SessionReady()
{
    return g_Connected && g_GameState == GS_GAME && g_World != nullptr && g_Player != nullptr;
}

uint32_t Backpack()
{
    const auto *pack = g_Player != nullptr ? g_Player->FindLayer(OL_BACKPACK) : nullptr;
    return pack != nullptr ? pack->Serial : 0;
}

uint32_t ResolveContainer(uint32_t serial)
{
    return serial != 0 ? serial : Backpack();
}

bool IsWithin(uint32_t serial, uint32_t container)
{
    for (int depth = 0; depth < MaxContainerDepth && serial != 0 && serial != OnGround; ++depth)
    {
        if (serial == container)
        {
            return true;
        }
        const auto *object = g_World->FindWorldObject(serial);
        serial = object != nullptr ? object->Container : 0;
    }
    return false;
}

bool CanReach(CGameObject *object)
{
    for (int depth = 0; object != nullptr && depth < MaxContainerDepth; ++depth)
    {
        if (object->Container == OnGround || object->Container == 0)
        {
            return object->MapIndex == g_Player->MapIndex && (!object->NPC || object == g_Player) &&
                   // The shard checks two tiles for ordinary lifting and container drops.
                   GetDistance(g_Player, object) <= 2;
        }
        object = g_World->FindWorldObject(object->Container);
    }
    return false;
}

CGameItem *OpenContainer(uint32_t serial)
{
    auto *container = g_World->FindWorldItem(serial);
    return container != nullptr && container->GetStaticData() != nullptr && container->IsContainer() &&
            !container->IsCorpse() &&
            container->Opened && CanReach(container)
        ? container
        : nullptr;
}

InventoryAgentStack Snapshot(const CGameItem &item)
{
    return {
        item.Serial, item.Graphic, item.Color, std::max(uint32_t{ 1 }, item.Count),
    };
}

void CollectContents(
    CGameItem *container,
    std::vector<InventoryAgentStack> &out,
    bool recurse,
    uint32_t excluded,
    int depth = 0)
{
    if (depth == MaxContainerDepth)
    {
        return;
    }
    for (auto *child = container->m_Items; child != nullptr; child = child->m_Next)
    {
        auto *object = static_cast<CGameObject *>(child);
        if (object->NPC || object->Serial == excluded || object->IsCorpse())
        {
            continue;
        }
        auto *item = static_cast<CGameItem *>(object);
        if (excluded == 0 || !IsWithin(excluded, item->Serial))
        {
            out.push_back(Snapshot(*item));
        }
        if (recurse && item->GetStaticData() != nullptr && item->IsContainer() && item->Opened)
        {
            CollectContents(item, out, true, excluded, depth + 1);
        }
    }
}

uint32_t DirectMatchingCount(CGameItem &container, uint16_t graphic, uint16_t hue)
{
    uint32_t matching = 0;
    for (auto *child = container.m_Items; child != nullptr; child = child->m_Next)
    {
        const auto *object = static_cast<const CGameObject *>(child);
        if (!object->NPC && object->Graphic == graphic && object->Color == hue)
        {
            matching += std::max(uint32_t{ 1 }, object->Count);
        }
    }
    return matching;
}

InventoryAgentProfile *FindProfile(const astr_t &name)
{
    const auto folded = str_lower(str_trim(name));
    for (auto &profile : s_Profiles)
    {
        if (str_lower(profile.Name) == folded)
        {
            return &profile;
        }
    }
    return nullptr;
}

bool ReadProperties(const CIniFile &ini, const astr_t &section, InventoryAgentRule &rule)
{
    int count = 0;
    if (ini.Has(section, "properties") &&
        (!InventoryAgent_ParsePropertyInteger(ini.Get(section, "properties"), count) ||
         count < 0 || count > InventoryAgent_MaxProperties))
    {
        return false;
    }

    // A partially edited file must not turn a filtered rule into an unfiltered one.
    const auto prefix = section + "Property";
    const auto first = ini.Sections.lower_bound(prefix);
    int actualCount = 0;
    for (auto it = first; it != ini.Sections.end() &&
         it->first.compare(0, prefix.size(), prefix) == 0; ++it)
    {
        ++actualCount;
    }
    if (actualCount != count)
    {
        return false;
    }

    for (int i = 0; i < count; ++i)
    {
        const auto row = section + IniSectionName("Property", i);
        InventoryAgentPropertyRule property;
        property.HasMinimum = ini.Has(row, "minimum");
        property.HasMaximum = ini.Has(row, "maximum");
        if (!InventoryAgent_ParsePropertyInteger(ini.Get(row, "cliloc"), property.Cliloc) ||
            (property.HasMinimum &&
             !InventoryAgent_ParsePropertyInteger(ini.Get(row, "minimum"), property.Minimum)) ||
            (property.HasMaximum &&
             !InventoryAgent_ParsePropertyInteger(ini.Get(row, "maximum"), property.Maximum)) ||
            !InventoryAgent_ValidPropertyRule(property))
        {
            return false;
        }
        rule.Properties.push_back(property);
    }
    return true;
}

void Load()
{
    s_Profiles.clear();
    CIniFile ini;
    if (ini.Load(s_Path))
    {
        const int count = std::max(0, std::min(ini.GetInt("Meta", "count"), InventoryAgent_MaxProfiles));
        for (int i = 0; i < count; ++i)
        {
            const auto section = IniSectionName("Agent", i);
            InventoryAgentProfile profile;
            profile.Name = ini.Get(section, "name");
            const int mode = ini.GetInt(section, "mode", -1);
            if (mode < 0 || mode > static_cast<int>(InventoryAgentMode::AutoLoot) ||
                profile.Name.empty() || profile.Name.size() > 63 || FindProfile(profile.Name) != nullptr)
            {
                continue;
            }
            profile.Mode = static_cast<InventoryAgentMode>(mode);
            profile.Source = CAssistant::TextToSerial(ini.Get(section, "source"));
            profile.Destination = CAssistant::TextToSerial(ini.Get(section, "destination"));
            profile.IncludeSubcontainers = ini.GetBool(section, "subcontainers");
            profile.AutoOpenCorpses =
                profile.Mode == InventoryAgentMode::AutoLoot && ini.GetBool(section, "autoopencorpses");
            const int rules = std::max(0, std::min(ini.GetInt(section, "rules"), InventoryAgent_MaxRules));
            for (int j = 0; j < rules; ++j)
            {
                const auto ruleSection = section + IniSectionName("Rule", j);
                const auto graphic = CAssistant::TextToGraphic(ini.Get(ruleSection, "graphic"));
                const int hue = ini.GetInt(ruleSection, "hue");
                if (graphic == 0 || hue < 0 || hue > UINT16_MAX)
                {
                    continue;
                }
                InventoryAgentRule rule{
                    graphic, static_cast<uint16_t>(hue), ini.GetBool(ruleSection, "matchhue", true),
                    std::max(1, std::min(ini.GetInt(ruleSection, "quantity", 1), InventoryAgent_MaxQuantity))
                };
                if (profile.Mode == InventoryAgentMode::AutoLoot)
                {
                    if (ini.Has(ruleSection, "destination"))
                    {
                        const auto dest = CAssistant::TextToSerial(ini.Get(ruleSection, "destination"));
                        if (dest != 0 && dest != OnGround)
                        {
                            rule.Destination = dest;
                        }
                    }
                    if (!ReadProperties(ini, ruleSection, rule))
                    {
                        continue;
                    }
                }
                profile.Rules.push_back(std::move(rule));
            }

            const int useItems = std::max(0, std::min(ini.GetInt(section, "useitems"), InventoryAgent_MaxUseItems));
            for (int j = 0; j < useItems; ++j)
            {
                const auto serial = CAssistant::TextToSerial(ini.Get(section, IniSectionName("item", j)));
                if (serial != 0 && serial != OnGround &&
                    std::find(profile.UseItems.begin(), profile.UseItems.end(), serial) == profile.UseItems.end())
                {
                    profile.UseItems.push_back(serial);
                }
            }
            s_Profiles.push_back(std::move(profile));
        }
    }

    if (s_Profiles.empty())
    {
        for (const auto mode : { InventoryAgentMode::Organizer, InventoryAgentMode::Restock,
                                 InventoryAgentMode::Scavenger, InventoryAgentMode::UseOnce,
                                 InventoryAgentMode::AutoLoot })
        {
            InventoryAgentProfile profile;
            profile.Mode = mode;
            profile.Name = InventoryAgent_ModeName(mode);
            s_Profiles.push_back(std::move(profile));
        }
    }
}

bool EnsureProfile()
{
    if (!g_Connected || g_World == nullptr || g_Player == nullptr)
    {
        return false;
    }

    const auto *server = g_ServerList.GetSelectedServer();
    const auto &shard = server != nullptr ? server->Name : g_Config.ShardName;
    const auto &profileName = g_AssistantConfig.ProfileName;
    if (s_Character != g_PlayerSerial || s_Shard != shard || s_AssistantProfile != profileName)
    {
        InventoryAgent_Stop();
        s_ScavengerHistory.clear();
        s_CorpseTracker.Clear();
        s_Character = g_PlayerSerial;
        s_Shard = shard;
        s_AssistantProfile = profileName;
        const auto file = "InventoryAgents-" + SanitizeShardName(s_Shard) + "-" +
            CAssistant::SerialToText(s_Character) + ".ini";
        s_Path = fs_path_join(g_AssistantConfig.GetProfilePath(), file.c_str());
        Load();
        s_Status.clear();
    }
    return true;
}

bool Allowed(InventoryAgentMode mode)
{
    switch (mode)
    {
        case InventoryAgentMode::UseOnce:
            return (g_ServerAssistantFeatures & ASF_USE_ONCE_AGENT) == 0;
        case InventoryAgentMode::Restock:
        case InventoryAgentMode::Organizer:
            return EnabledCommandMoveItems() &&
                   (g_ServerAssistantFeatures & ASF_RESTOCK_AGENT) == 0;
        case InventoryAgentMode::Scavenger:
        case InventoryAgentMode::AutoLoot:
            return EnabledCommandMoveItems() &&
                   (g_ServerAssistantFeatures & ASF_AUTOLOOT_AGENT) == 0;
    }
    return false;
}

void Report(const astr_t &text)
{
    s_Status = text;
}

void ClearPropertyChecks()
{
    s_PropertyChecks.clear();
    s_PropertyRequest = 0;
}

void Fail(const astr_t &text)
{
    s_Active = -1;
    s_AutoLootOnce = false;
    s_Pending = {};
    ClearPropertyChecks();
    Report(text);
}

uint32_t MoveInterval()
{
    // ModernUO's standard lift cooldown is one second. A fast acknowledgement does not end it.
    const auto autoLoot = s_Active >= 0 && s_Active < static_cast<int>(s_Profiles.size()) &&
        s_Profiles[s_Active].Mode == InventoryAgentMode::AutoLoot;
    // AutoLoot also waits for user corpse opens, whose request time can differ from the server's.
    const int minimum = autoLoot ? 1200 : 1000;
    return static_cast<uint32_t>(std::max(minimum, std::min(g_AssistantConfig.MoveItemsDelay(), 60000)));
}

uint32_t CorpseOpenUseCooldown()
{
    const uint32_t useDelay = static_cast<uint32_t>(std::max(600, std::min(g_AssistantConfig.UseItemsDelay(), 60000)));
    return std::max(MoveInterval(), useDelay);
}

void PollMove()
{
    auto *source = g_World->FindWorldItem(s_Pending.Serial);
    auto *destination = g_World->FindWorldItem(s_Pending.Destination);
    const bool arrived = source != nullptr && source->Container == s_Pending.Destination;
    // A split or merge can change serial. The source must lose the requested
    // amount and the destination gain it before a second lift is sent.
    const uint32_t currentCount = source != nullptr ? Snapshot(*source).Count : 0;
    const bool sourceChanged = source == nullptr || source->Container != s_Pending.Source ||
        (currentCount <= s_Pending.OriginalCount &&
         s_Pending.OriginalCount - currentCount >= s_Pending.Count);
    const bool merged = sourceChanged && destination != nullptr &&
        DirectMatchingCount(*destination, s_Pending.Graphic, s_Pending.Hue) >=
            s_Pending.DestinationCount + s_Pending.Count;

    if (arrived || merged)
    {
        s_Pending = {};
        s_NextAction = g_Ticks + MoveInterval();
        if (s_Active < 0)
        {
            Report("Stopped");
        }
    }
    else if (Due(g_Ticks, s_Pending.Deadline))
    {
        Fail("Move was not confirmed. Check the bags before trying again.");
    }
}

void StartMove(const InventoryAgentMove &move, CGameItem &destination)
{
    auto *item = g_World->FindWorldItem(move.Serial);
    if (item == nullptr || move.Count == 0 || !CanReach(item) ||
        !CanReach(&destination) || item->Serial == destination.Serial ||
        IsWithin(destination.Serial, item->Serial))
    {
        Fail("The item or destination is no longer in reach.");
        return;
    }

    const auto snapshot = Snapshot(*item);
    const auto countBefore = DirectMatchingCount(destination, snapshot.Graphic, snapshot.Hue);
    const uint32_t originalContainer = item->Container;
    g_MoveItemData.Reset();
    g_MoveItemData.Count = move.Count;
    g_MoveItemData.Container = destination.Serial;
    g_Assistant.MoveItem(item);
    s_Pending = { snapshot.Serial, originalContainer, destination.Serial, snapshot.Count,
                  countBefore, move.Count, snapshot.Graphic, snapshot.Hue, g_Ticks + MoveTimeout };
}

void PlanBagMove(const InventoryAgentProfile &profile)
{
    const auto sourceSerial = ResolveContainer(profile.Source);
    const auto destinationSerial = ResolveContainer(profile.Destination);
    if (sourceSerial == destinationSerial)
    {
        Fail("Choose different source and destination bags.");
        return;
    }

    auto *source = OpenContainer(sourceSerial);
    auto *destination = OpenContainer(destinationSerial);
    if (source == nullptr || destination == nullptr)
    {
        Fail("Open both bags within reach before running this agent.");
        return;
    }

    std::vector<InventoryAgentStack> sources;
    std::vector<InventoryAgentStack> destinations;
    CollectContents(source, sources, profile.IncludeSubcontainers, destinationSerial);
    if (profile.Mode == InventoryAgentMode::Restock)
    {
        const bool sourceInsideDestination = IsWithin(sourceSerial, destinationSerial);
        CollectContents(destination, destinations, profile.IncludeSubcontainers,
                        sourceInsideDestination ? sourceSerial : 0);
    }

    const auto move = InventoryAgent_Plan(profile.Mode, profile.Rules, sources, destinations);
    if (move.Serial == 0)
    {
        s_Active = -1;
        Report("Finished moving available matching items.");
        return;
    }
    StartMove(move, *destination);
}

void ScanGround(const InventoryAgentProfile &profile)
{
    auto *destination = OpenContainer(ResolveContainer(profile.Destination));
    if (destination == nullptr)
    {
        Fail("Open the scavenger's destination bag within reach.");
        return;
    }

    s_NextAction = g_Ticks + ScavengerScanInterval;
    for (auto *child = g_World->m_Items; child != nullptr; child = static_cast<CGameObject *>(child->m_Next))
    {
        if (child->NPC || child->Container != OnGround || child->IsCorpse() ||
            child->Ignored || !CanReach(child) || s_ScavengerHistory.count(child->Serial) != 0)
        {
            continue;
        }
        auto &item = *static_cast<CGameItem *>(child);
        const auto stack = Snapshot(item);
        if (std::none_of(profile.Rules.begin(), profile.Rules.end(),
                         [&stack](const auto &rule) { return InventoryAgent_Matches(rule, stack); }))
        {
            continue;
        }

        if (s_ScavengerHistory.size() >= MaxScavengerHistory)
        {
            Fail("Scavenger history is full. Clear the list of attempted items to resume.");
            return;
        }

        s_ScavengerHistory.insert(stack.Serial);
        StartMove({ stack.Serial, static_cast<uint16_t>(
                                     std::min(stack.Count, static_cast<uint32_t>(InventoryAgent_MaxQuantity))) },
                  *destination);
        return;
    }
}

void CollectCorpseLoot(CGameItem &container, std::vector<InventoryAgentStack> &out,
    bool recurse, int depth = 0)
{
    if (depth == MaxContainerDepth)
    {
        return;
    }
    for (auto *child = container.m_Items; child != nullptr; child = child->m_Next)
    {
        auto *object = static_cast<CGameObject *>(child);
        if (object->NPC || object->Container != container.Serial || object->Ignored || object->IsCorpse())
        {
            continue;
        }
        auto *item = static_cast<CGameItem *>(object);
        if (item->Layer != OL_NONE || item->Dragged || item->MultiBody || item->GetStaticData() == nullptr)
        {
            continue;
        }
        if (!item->IsContainer())
        {
            out.push_back(Snapshot(*item));
        }
        else if (recurse && item->Opened)
        {
            const auto *gump = g_GumpManager.GetGump(item->Serial, 0, GT_CONTAINER);
            if (gump != nullptr && !gump->RemoveMark)
            {
                CollectCorpseLoot(*item, out, true, depth + 1);
            }
        }
    }
}

bool SameCheckedItem(const PropertyCheck &check, const CGameItem *item)
{
    return item != nullptr && item->Container == check.Container &&
           item->Graphic == check.Item.Graphic && item->Color == check.Item.Hue &&
           std::max(uint32_t{ 1 }, item->Count) == check.Item.Count;
}

void RefreshPropertyChecks(std::vector<InventoryAgentStack> &sources)
{
    if (s_PropertyChecks.empty())
    {
        return;
    }
    std::unordered_set<uint32_t> visible;
    for (const auto &source : sources)
    {
        visible.insert(source.Serial);
    }
    for (auto check = s_PropertyChecks.begin(); check != s_PropertyChecks.end();)
    {
        const uint32_t serial = check->first;
        auto &record = check->second;
        if (visible.count(serial) == 0 || !SameCheckedItem(record, g_World->FindWorldItem(serial)))
        {
            if (s_PropertyRequest == serial)
            {
                s_PropertyRequest = 0;
            }
            check = s_PropertyChecks.erase(check);
            continue;
        }
        if (record.State == PropertyState::Waiting && g_Ticks - record.Updated >= PropertyTimeout)
        {
            record.State = PropertyState::Unavailable;
            s_PropertyRequest = 0;
        }
        else if (record.State == PropertyState::Resolved && record.Matches.any() &&
                 g_Ticks - record.Updated >= PropertyFreshness)
        {
            // A cached negative never causes a lift. Recheck cached positives before moving.
            record.State = PropertyState::Requestable;
        }
        ++check;
    }
    for (auto &source : sources)
    {
        const auto check = s_PropertyChecks.find(source.Serial);
        if (check != s_PropertyChecks.end() && check->second.State == PropertyState::Resolved)
        {
            source.PropertyMatches = &check->second.Matches;
        }
    }
}

bool RequestMatchingProperties(
    const InventoryAgentProfile &profile, const std::vector<InventoryAgentStack> &sources)
{
    if (s_PropertyRequest != 0)
    {
        return true;
    }
    for (const auto &rule : profile.Rules)
    {
        if (rule.Properties.empty())
        {
            continue;
        }
        for (const auto &source : sources)
        {
            if (!InventoryAgent_MatchesAppearance(rule, source))
            {
                continue;
            }
            auto check = s_PropertyChecks.find(source.Serial);
            if (check == s_PropertyChecks.end())
            {
                if (s_PropertyChecks.size() >= MaxPropertyChecks)
                {
                    continue;
                }
                auto *item = g_World->FindWorldItem(source.Serial);
                if (item == nullptr)
                {
                    continue;
                }
                PropertyCheck record{};
                record.Item = Snapshot(*item);
                record.Container = item->Container;
                check = s_PropertyChecks.emplace(source.Serial, record).first;
            }
            if (check->second.State != PropertyState::Requestable)
            {
                continue;
            }
            check->second.State = PropertyState::Waiting;
            check->second.Updated = g_Ticks;
            s_PropertyRequest = source.Serial;
            // One in-flight query at the normal AutoLoot scan cadence. Do not use the tooltip
            // queue, which can be disabled and can already contain unrelated requests.
            if (g_Config.ProtocolClientVersion >= CV_500A)
            {
                std::vector<uint32_t> request{ source.Serial };
                CPacketMegaClilocRequest(request).Send();
            }
            else
            {
                CPacketMegaClilocRequestOld(source.Serial).Send();
            }
            return true;
        }
    }
    return false;
}

CGameItem *OpenLootDestination(uint32_t ruleDestination, uint32_t profileDestination, uint32_t backpack)
{
    if (backpack == 0)
    {
        return nullptr;
    }
    const auto targetSerial = InventoryAgent_ResolveDestination(ruleDestination, profileDestination, backpack);
    return targetSerial != 0 && IsWithin(targetSerial, backpack) ? OpenContainer(targetSerial) : nullptr;
}

bool HasAnyOpenLootDestination(const InventoryAgentProfile &profile, uint32_t backpack)
{
    if (OpenLootDestination(0, profile.Destination, backpack) != nullptr)
    {
        return true;
    }
    for (const auto &rule : profile.Rules)
    {
        if (rule.Destination != 0 && OpenLootDestination(rule.Destination, profile.Destination, backpack) != nullptr)
        {
            return true;
        }
    }
    return false;
}

uint32_t FindCorpseToAutoOpen()
{
    std::vector<InventoryAgentCorpseCandidate> candidates;
    for (auto *child = g_World->m_Items; child != nullptr; child = static_cast<CGameObject *>(child->m_Next))
    {
        if (child->NPC || child->Container != OnGround || !child->IsCorpse())
        {
            continue;
        }
        auto *corpse = static_cast<CGameItem *>(child);
        const auto *gump = g_GumpManager.GetGump(corpse->Serial, 0, GT_CONTAINER);
        InventoryAgentCorpseCandidate candidate;
        candidate.Serial = corpse->Serial;
        candidate.Graphic = corpse->Graphic;
        candidate.Body = corpse->Count;
        candidate.Container = corpse->Container;
        candidate.Distance = GetDistance(g_Player, corpse);
        candidate.IsNpc = corpse->NPC;
        candidate.HasStaticData = corpse->GetStaticData() != nullptr;
        candidate.IsContainer = candidate.HasStaticData && corpse->IsContainer();
        candidate.SameMap = corpse->MapIndex == g_Player->MapIndex;
        candidate.Opened = corpse->Opened;
        candidate.HasOpenGump = gump != nullptr && !gump->RemoveMark;
        candidate.Ignored = corpse->Ignored;
        candidate.Dragged = corpse->Dragged;
        candidate.MultiBody = corpse->MultiBody;
        candidates.push_back(candidate);
    }
    return s_CorpseTracker.SelectNext(candidates);
}

void ScanCorpses(const InventoryAgentProfile &profile)
{
    s_NextAction = g_Ticks + AutoLootScanInterval;
    const auto backpack = Backpack();
    if (!HasAnyOpenLootDestination(profile, backpack))
    {
        ClearPropertyChecks();
        const char *notice = "Open your backpack or the selected loot bag inside it.";
        if (s_AutoLootOnce)
        {
            Fail(notice);
        }
        else
        {
            Report(notice);
        }
        return;
    }

    bool openedCorpse = false;
    std::vector<InventoryAgentStack> sources;
    for (auto *node = g_GumpManager.m_Items; node != nullptr; node = node->m_Next)
    {
        const auto *gump = static_cast<const CGump *>(node);
        if (gump->GumpType != GT_CONTAINER || gump->RemoveMark)
        {
            continue;
        }
        auto *corpse = g_World->FindWorldItem(gump->Serial);
        if (corpse == nullptr || corpse->Container != OnGround || !corpse->IsCorpse() ||
            corpse->Ignored || corpse->Dragged || corpse->MultiBody || !corpse->Opened ||
            (corpse->GetStaticData() != nullptr && !corpse->IsContainer()) ||
            !InventoryAgent_AutoLootCorpseBody(corpse->Count) || !CanReach(corpse))
        {
            continue;
        }
        openedCorpse = true;
        s_CorpseTracker.RecordAttempted(corpse->Serial);
        CollectCorpseLoot(*corpse, sources, profile.IncludeSubcontainers);
    }

    RefreshPropertyChecks(sources);
    const auto move = InventoryAgent_Plan(InventoryAgentMode::AutoLoot, profile.Rules, sources, {});
    if (move.Serial != 0)
    {
        auto *destination = OpenLootDestination(move.Destination, profile.Destination, backpack);
        if (destination == nullptr)
        {
            const char *notice = move.Destination != 0
                ? "Open the rule's loot bag inside your backpack, or clear its bag override."
                : "Open your backpack or the selected loot bag inside it.";
            if (s_AutoLootOnce)
            {
                Fail(notice);
            }
            else
            {
                Report(notice);
            }
            return;
        }
        Report(profile.Name + " moving loot");
        StartMove(move, *destination);
    }
    else if (RequestMatchingProperties(profile, sources))
    {
        Report("Waiting for item properties.");
    }
    else if (profile.AutoOpenCorpses)
    {
        const uint32_t corpseToOpen = FindCorpseToAutoOpen();
        if (corpseToOpen != 0)
        {
            const uint32_t cooldown = CorpseOpenUseCooldown();
            if (g_LastUseObjectTimer != 0 && static_cast<uint32_t>(g_Ticks - g_LastUseObjectTimer) < cooldown)
            {
                Report("Waiting before opening the next corpse.");
                return;
            }
            if (s_CorpseTracker.IsFull())
            {
                const char *notice = "Opened corpse history is full. Clear opened corpses to resume.";
                if (s_AutoLootOnce)
                {
                    Fail(notice);
                }
                else
                {
                    Report(notice);
                }
                return;
            }
            s_CorpseTracker.RecordAttempted(corpseToOpen);
            g_Assistant.DoubleClick(corpseToOpen);
            s_NextAction = g_Ticks + cooldown;
            Report("Opening nearby creature corpse.");
            return;
        }

        if (s_AutoLootOnce)
        {
            const bool missingProperties = s_PropertyChecks.size() >= MaxPropertyChecks ||
                std::any_of(s_PropertyChecks.begin(), s_PropertyChecks.end(), [](const auto &check) {
                    return check.second.State == PropertyState::Unavailable;
                });
            s_Active = -1;
            s_AutoLootOnce = false;
            ClearPropertyChecks();
            if (missingProperties)
            {
                Report("Finished. Skipped items with unavailable properties.");
            }
            else
            {
                Report(openedCorpse ? "Finished moving available loot." : "No nearby creature corpses had matching loot.");
            }
        }
        else
        {
            Report(openedCorpse ? "Waiting for matching loot." : "Waiting for nearby creature corpses.");
        }
    }
    else if (s_AutoLootOnce)
    {
        const bool missingProperties = s_PropertyChecks.size() >= MaxPropertyChecks ||
            std::any_of(s_PropertyChecks.begin(), s_PropertyChecks.end(), [](const auto &check) {
                return check.second.State == PropertyState::Unavailable;
            });
        s_Active = -1;
        s_AutoLootOnce = false;
        ClearPropertyChecks();
        if (missingProperties)
        {
            Report("Finished. Skipped items with unavailable properties.");
        }
        else
        {
            Report(openedCorpse ? "Finished moving available loot." : "Open a nearby creature corpse first.");
        }
    }
    else
    {
        Report(openedCorpse ? "Waiting for matching loot." : "Open a nearby creature corpse.");
    }
}

bool UseNext(InventoryAgentProfile &profile)
{
    const auto backpack = Backpack();
    if (backpack != 0)
    {
        auto &items = profile.UseItems;
        for (auto it = items.begin(); it != items.end(); ++it)
        {
            if (*it != backpack && g_World->FindWorldItem(*it) != nullptr && IsWithin(*it, backpack))
            {
                g_Assistant.DoubleClick(*it);
                items.erase(it);
                const bool saved = InventoryAgent_Save();
                Report(saved ? "Use request sent." : "Use request sent. The profile could not be saved.");
                return true;
            }
        }
    }
    Report("No items from this list are in your backpack.");
    return false;
}

bool CursorBusy()
{
    if (s_RecentItemAction)
    {
        if (g_Ticks - s_ItemActionAt < MoveInterval())
        {
            return true;
        }

        s_RecentItemAction = false;
    }

    // A manual stack merge can remove the moved serial without clearing Dropped.
    return g_ObjectInHand.Enabled || ::g_Target.IsTargeting();
}

} // namespace

std::vector<InventoryAgentProfile> &InventoryAgent_Profiles()
{
    EnsureProfile();
    return s_Profiles;
}

bool InventoryAgent_Save()
{
    if (!EnsureProfile())
    {
        return false;
    }
    g_AssistantConfig.EnsureDirectories();

    CIniFile ini;
    ini.SetInt("Meta", "count", static_cast<int>(s_Profiles.size()));
    for (int i = 0; i < static_cast<int>(s_Profiles.size()); ++i)
    {
        const auto &profile = s_Profiles[i];
        const auto section = IniSectionName("Agent", i);
        ini.Set(section, "name", profile.Name);
        ini.SetInt(section, "mode", static_cast<int>(profile.Mode));
        ini.Set(section, "source", CAssistant::SerialToText(profile.Source));
        ini.Set(section, "destination", CAssistant::SerialToText(profile.Destination));
        ini.SetBool(section, "subcontainers", profile.IncludeSubcontainers);
        if (profile.Mode == InventoryAgentMode::AutoLoot)
        {
            ini.SetBool(section, "autoopencorpses", profile.AutoOpenCorpses);
        }
        ini.SetInt(section, "rules", static_cast<int>(profile.Rules.size()));
        for (int j = 0; j < static_cast<int>(profile.Rules.size()); ++j)
        {
            const auto &rule = profile.Rules[j];
            const auto ruleSection = section + IniSectionName("Rule", j);
            ini.Set(ruleSection, "graphic", CAssistant::GraphicToText(rule.Graphic));
            ini.SetInt(ruleSection, "hue", rule.Hue);
            ini.SetBool(ruleSection, "matchhue", rule.MatchHue);
            ini.SetInt(ruleSection, "quantity", rule.Quantity);
            if (profile.Mode == InventoryAgentMode::AutoLoot && rule.Destination != 0 && rule.Destination != OnGround)
            {
                ini.Set(ruleSection, "destination", CAssistant::SerialToText(rule.Destination));
            }
            if (!rule.Properties.empty())
            {
                ini.SetInt(ruleSection, "properties", static_cast<int>(rule.Properties.size()));
                for (int k = 0; k < static_cast<int>(rule.Properties.size()); ++k)
                {
                    const auto &property = rule.Properties[k];
                    const auto row = ruleSection + IniSectionName("Property", k);
                    ini.SetInt(row, "cliloc", property.Cliloc);
                    if (property.HasMinimum)
                    {
                        ini.SetInt(row, "minimum", property.Minimum);
                    }
                    if (property.HasMaximum)
                    {
                        ini.SetInt(row, "maximum", property.Maximum);
                    }
                }
            }
        }
        ini.SetInt(section, "useitems", static_cast<int>(profile.UseItems.size()));
        for (int j = 0; j < static_cast<int>(profile.UseItems.size()); ++j)
        {
            ini.Set(section, IniSectionName("item", j), CAssistant::SerialToText(profile.UseItems[j]));
        }
    }
    return ini.Save(s_Path);
}

bool InventoryAgent_AddProfile(InventoryAgentMode mode, const astr_t &name)
{
    const auto trimmedName = str_trim(name);
    if (!EnsureProfile() || InventoryAgent_Busy() || trimmedName.empty() || trimmedName.size() > 63 ||
        s_Profiles.size() >= InventoryAgent_MaxProfiles || FindProfile(trimmedName) != nullptr)
    {
        return false;
    }

    InventoryAgentProfile profile;
    profile.Mode = mode;
    profile.Name = trimmedName;
    s_Profiles.push_back(std::move(profile));
    return true;
}

bool InventoryAgent_Run(const astr_t &alias)
{
    if (!SessionReady() || !EnsureProfile() || g_Player->IsDead())
    {
        Report("Enter the world before running an inventory agent.");
        return false;
    }
    if (InventoryAgent_Busy())
    {
        Report("Stop the active agent or wait for its current move.");
        return false;
    }

    auto *profile = FindProfile(alias);
    if (profile == nullptr)
    {
        Report("No inventory agent has that name.");
        return false;
    }
    if (!Allowed(profile->Mode))
    {
        Report("This agent is disabled on this shard.");
        return false;
    }

    if (profile->Mode == InventoryAgentMode::UseOnce)
    {
        if (CursorBusy() || g_DressAgent.Active() || g_ShopAgent.IsActiveShopping() || g_MacroExecutor.IsPlaying())
        {
            Report("Finish the current game action before using an item.");
            return false;
        }
        return UseNext(*profile);
    }

    if (profile->Rules.empty())
    {
        Report("Add at least one item rule first.");
        return false;
    }

    s_Active = static_cast<int>(profile - s_Profiles.data());
    s_AutoLootOnce = false;
    ClearPropertyChecks();
    s_NextAction = g_Ticks;
    Report(profile->Name + " running");
    return true;
}

bool InventoryAgent_AutoLootOnce(const astr_t &alias)
{
    const auto *profile = EnsureProfile() ? FindProfile(alias) : nullptr;
    if (profile == nullptr || profile->Mode != InventoryAgentMode::AutoLoot)
    {
        Report("Choose an AutoLoot preset first.");
        return false;
    }
    if (!InventoryAgent_Run(alias))
    {
        return false;
    }
    s_AutoLootOnce = true;
    return true;
}

bool InventoryAgent_RunIndexed(InventoryAgentMode mode, int oneBasedIndex)
{
    if (!EnsureProfile() || oneBasedIndex < 1)
    {
        return false;
    }
    for (const auto &profile : s_Profiles)
    {
        if (profile.Mode == mode && --oneBasedIndex == 0)
        {
            return InventoryAgent_Run(profile.Name);
        }
    }
    Report("That inventory agent has not been configured.");
    return false;
}

bool InventoryAgent_MoveInFlight()
{
    return s_Pending.Serial != 0;
}

bool InventoryAgent_Busy()
{
    return s_Active >= 0 || InventoryAgent_MoveInFlight();
}

void InventoryAgent_Stop()
{
    s_Active = -1;
    s_AutoLootOnce = false;
    ClearPropertyChecks();
    Report(InventoryAgent_MoveInFlight() ? "Stopping after the current move." : "Stopped");
}

void InventoryAgent_ClearScavengerCache()
{
    s_ScavengerHistory.clear();
    if (s_Active < 0)
    {
        Report("Scavenger item history cleared.");
    }
}

void InventoryAgent_ClearAutoLootCorpseCache()
{
    s_CorpseTracker.Clear();
    if (s_Active < 0)
    {
        Report("Opened corpse history cleared.");
    }
}

size_t InventoryAgent_AutoLootCorpseCacheSize()
{
    return s_CorpseTracker.Size();
}

bool InventoryAgent_ExportAutoLootList(const astr_t &alias, bool asJson, astr_t &outText, astr_t &outError)
{
    outText.clear();
    outError.clear();
    if (!EnsureProfile())
    {
        outError = "Enter the world before exporting an AutoLoot list.";
        return false;
    }
    const auto *profile = FindProfile(alias);
    if (profile == nullptr || profile->Mode != InventoryAgentMode::AutoLoot)
    {
        outError = "Choose an AutoLoot preset to export.";
        return false;
    }
    outText = asJson ? InventoryAgent_ExportAutoLootJson(*profile) : InventoryAgent_ExportAutoLootIni(*profile);
    if (outText.empty())
    {
        outError = "Could not format this AutoLoot list for export.";
        return false;
    }
    return true;
}

bool InventoryAgent_ImportAutoLootList(
    const astr_t &text,
    const astr_t &targetAlias,
    bool createNewProfile,
    const astr_t &newProfileName,
    size_t listIndex,
    astr_t &outMessage,
    astr_t *outProfileName)
{
    outMessage.clear();
    if (!EnsureProfile())
    {
        outMessage = "Enter the world before importing an AutoLoot list.";
        return false;
    }
    if (InventoryAgent_Busy())
    {
        outMessage = "Stop the active inventory agent before importing rules.";
        return false;
    }

    const auto parsed = InventoryAgent_ParseAutoLootImport(text);
    if (!parsed.Success || parsed.Lists.empty())
    {
        outMessage = parsed.Error.empty() ? "Could not read the AutoLoot list." : parsed.Error;
        return false;
    }
    if (listIndex >= parsed.Lists.size())
    {
        outMessage = "Choose a valid AutoLoot list to import.";
        return false;
    }

    const auto &imported = parsed.Lists[listIndex];
    if (createNewProfile)
    {
        const auto chosenName = str_trim(newProfileName.empty() ? imported.Name : newProfileName);
        if (chosenName.empty() || chosenName.size() > 63 ||
            s_Profiles.size() >= InventoryAgent_MaxProfiles || FindProfile(chosenName) != nullptr)
        {
            outMessage = "Choose a unique preset name or remove an unused agent first.";
            return false;
        }
        InventoryAgentProfile created;
        created.Mode = InventoryAgentMode::AutoLoot;
        created.Name = chosenName;
        created.IncludeSubcontainers = imported.HasIncludeSubcontainers ? imported.IncludeSubcontainers : false;
        created.AutoOpenCorpses = imported.HasAutoOpenCorpses ? imported.AutoOpenCorpses : false;
        created.Rules = imported.Rules;

        s_Profiles.push_back(std::move(created));
        if (!InventoryAgent_Save())
        {
            s_Profiles.pop_back();
            outMessage = "The imported AutoLoot preset could not be saved.";
            return false;
        }
        if (outProfileName != nullptr)
        {
            *outProfileName = chosenName;
        }
        outMessage = "Imported " + std::to_string(imported.Rules.size()) + " rules as \"" + chosenName + "\".";
        if (parsed.SkippedDisabled > 0)
        {
            outMessage += " Skipped " + std::to_string(parsed.SkippedDisabled) + " disabled entries.";
        }
        return true;
    }

    auto *target = FindProfile(targetAlias);
    if (target == nullptr || target->Mode != InventoryAgentMode::AutoLoot)
    {
        outMessage = "Choose an AutoLoot preset first.";
        return false;
    }

    const InventoryAgentProfile backup = *target;
    target->Rules = imported.Rules;
    if (imported.HasIncludeSubcontainers)
    {
        target->IncludeSubcontainers = imported.IncludeSubcontainers;
    }
    if (imported.HasAutoOpenCorpses)
    {
        target->AutoOpenCorpses = imported.AutoOpenCorpses;
    }
    if (!InventoryAgent_Save())
    {
        *target = backup;
        outMessage = "The imported AutoLoot rules could not be saved.";
        return false;
    }
    if (outProfileName != nullptr)
    {
        *outProfileName = target->Name;
    }
    outMessage = "Imported " + std::to_string(imported.Rules.size()) + " rules into \"" + target->Name + "\".";
    if (parsed.SkippedDisabled > 0)
    {
        outMessage += " Skipped " + std::to_string(parsed.SkippedDisabled) + " disabled entries.";
    }
    return true;
}

void InventoryAgent_OnItemActionSent()
{
    s_ItemActionAt = g_Ticks;
    s_RecentItemAction = true;
}

void InventoryAgent_OnMoveRejected()
{
    if (!InventoryAgent_MoveInFlight())
    {
        return;
    }

    if (s_Active >= 0 && s_Active < static_cast<int>(s_Profiles.size()) &&
        s_Profiles[s_Active].Mode == InventoryAgentMode::Scavenger)
    {
        // The attempted serial is already in the scavenger history.
        s_Pending = {};
        s_NextAction = g_Ticks + MoveInterval();
        Report("The server refused that item. Looking for other matches.");
        return;
    }

    Fail("The server refused the move. The inventory agent stopped.");
}

void InventoryAgent_OnPropertyList(uint32_t serial, uint32_t revision, const InventoryAgentPropertyRows &rows)
{
    if (serial != s_PropertyRequest || s_Active < 0 ||
        s_Active >= static_cast<int>(s_Profiles.size()) || g_World == nullptr)
    {
        return;
    }
    const auto check = s_PropertyChecks.find(serial);
    if (check == s_PropertyChecks.end() || check->second.State != PropertyState::Waiting ||
        g_Ticks - check->second.Updated >= PropertyTimeout ||
        !SameCheckedItem(check->second, g_World->FindWorldItem(serial)))
    {
        return;
    }
    auto &record = check->second;
    if (record.HasRevision && revision != record.Revision)
    {
        return;
    }

    record.HasRevision = true;
    record.Revision = revision;
    record.State = PropertyState::Resolved;
    record.Updated = g_Ticks;
    record.Matches.reset();
    const auto &rules = s_Profiles[s_Active].Rules;
    for (size_t i = 0; i < rules.size() && i < record.Matches.size(); ++i)
    {
        if (!rules[i].Properties.empty() && InventoryAgent_MatchesAppearance(rules[i], record.Item) &&
            InventoryAgent_MatchesProperties(rules[i].Properties, rows))
        {
            record.Matches.set(i);
        }
    }
    s_PropertyRequest = 0;
}

void InventoryAgent_OnPropertyRevision(uint32_t serial, uint32_t revision)
{
    const auto check = s_PropertyChecks.find(serial);
    if (check == s_PropertyChecks.end() ||
        (check->second.HasRevision && check->second.Revision == revision))
    {
        return;
    }
    auto &record = check->second;
    record.HasRevision = true;
    record.Revision = revision;
    if (record.State != PropertyState::Waiting)
    {
        record.State = PropertyState::Requestable;
    }
}

void InventoryAgent_ForgetProperties(uint32_t serial)
{
    if (s_PropertyRequest == serial)
    {
        s_PropertyRequest = 0;
    }
    s_PropertyChecks.erase(serial);
}

void InventoryAgent_ResetSession()
{
    s_Active = -1;
    s_AutoLootOnce = false;
    s_Pending = {};
    ClearPropertyChecks();
    s_Character = 0;
    s_RecentItemAction = false;
    s_Profiles.clear();
    s_ScavengerHistory.clear();
    s_CorpseTracker.Clear();
    s_Status.clear();
}

void InventoryAgent_Tick()
{
    if (!InventoryAgent_Busy())
    {
        return;
    }
    if (!SessionReady() || g_Player->IsDead())
    {
        Fail("Stopped because the character is no longer active.");
        return;
    }
    if (InventoryAgent_MoveInFlight())
    {
        PollMove();
        return;
    }

    if (!EnsureProfile() || s_Active < 0 || s_Active >= static_cast<int>(s_Profiles.size()))
    {
        return;
    }
    if (!Due(g_Ticks, s_NextAction) || CursorBusy() || g_DressAgent.Active() ||
        g_ShopAgent.IsActiveShopping() || g_MacroExecutor.IsPlaying())
    {
        return;
    }

    const auto &profile = s_Profiles[s_Active];
    if (!Allowed(profile.Mode))
    {
        Fail("This agent is disabled on this shard.");
        return;
    }

    if (profile.Mode == InventoryAgentMode::Scavenger)
    {
        ScanGround(profile);
    }
    else if (profile.Mode == InventoryAgentMode::AutoLoot)
    {
        ScanCorpses(profile);
    }
    else
    {
        PlanBagMove(profile);
    }
}

const astr_t &InventoryAgent_Status()
{
    return s_Status;
}

const char *InventoryAgent_ModeName(InventoryAgentMode mode)
{
    switch (mode)
    {
        case InventoryAgentMode::Organizer:
            return "Organizer";
        case InventoryAgentMode::Restock:
            return "Restock";
        case InventoryAgentMode::Scavenger:
            return "Scavenger";
        case InventoryAgentMode::UseOnce:
            return "Use Once";
        case InventoryAgentMode::AutoLoot:
            return "AutoLoot";
    }
    return "Inventory";
}

} // namespace xuoassist
