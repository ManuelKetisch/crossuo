// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include <common/str.h>

namespace xuoassist {

enum class InventoryAgentMode
{
    Organizer,
    Restock,
    Scavenger,
    UseOnce,
    AutoLoot
};

struct InventoryAgentPropertyRule
{
    int Cliloc = 0;
    int Minimum = 0;
    int Maximum = 0;
    bool HasMinimum = false;
    bool HasMaximum = false;
};

using InventoryAgentPropertyRows = std::vector<std::pair<uint32_t, wstr_t>>;

struct InventoryAgentRule
{
    uint16_t Graphic = 0;
    uint16_t Hue = 0;
    bool MatchHue = true;
    int Quantity = 1; // Restock tops up to this count.
    std::vector<InventoryAgentPropertyRule> Properties{}; // AutoLoot only.
    uint32_t Destination = 0; // Optional per-rule container override (0 = inherit profile destination).
};

struct InventoryAgentProfile
{
    astr_t Name;
    InventoryAgentMode Mode = InventoryAgentMode::Organizer;
    uint32_t Source = 0; // Zero uses the player's backpack.
    uint32_t Destination = 0;
    bool IncludeSubcontainers = false;
    bool AutoOpenCorpses = false; // AutoLoot only: auto-open nearby creature corpses within reach.
    std::vector<InventoryAgentRule> Rules;
    std::vector<uint32_t> UseItems;
};

constexpr int InventoryAgent_MaxProfiles = 32;
constexpr int InventoryAgent_MaxRules = 128;
constexpr int InventoryAgent_MaxProperties = 16;
constexpr int InventoryAgent_MaxUseItems = 256;
constexpr int InventoryAgent_MaxQuantity = 60000;

// All entry points run on the game thread.
std::vector<InventoryAgentProfile> &InventoryAgent_Profiles();
bool InventoryAgent_Save();
bool InventoryAgent_AddProfile(InventoryAgentMode mode, const astr_t &name);
bool InventoryAgent_Run(const astr_t &alias);
bool InventoryAgent_AutoLootOnce(const astr_t &alias);
bool InventoryAgent_RunIndexed(InventoryAgentMode mode, int oneBasedIndex);
bool InventoryAgent_Busy();
bool InventoryAgent_MoveInFlight();
void InventoryAgent_Stop();
void InventoryAgent_ClearScavengerCache();
void InventoryAgent_ClearAutoLootCorpseCache();
size_t InventoryAgent_AutoLootCorpseCacheSize();
bool InventoryAgent_ExportAutoLootList(const astr_t &alias, bool asJson, astr_t &outText, astr_t &outError);
bool InventoryAgent_ImportAutoLootList(
    const astr_t &text,
    const astr_t &targetAlias,
    bool createNewProfile,
    const astr_t &newProfileName,
    size_t listIndex,
    astr_t &outMessage,
    astr_t *outProfileName = nullptr);
void InventoryAgent_OnItemActionSent();
void InventoryAgent_OnMoveRejected();
void InventoryAgent_OnPropertyList(uint32_t serial, uint32_t revision, const InventoryAgentPropertyRows &rows);
void InventoryAgent_OnPropertyRevision(uint32_t serial, uint32_t revision);
void InventoryAgent_ForgetProperties(uint32_t serial);
void InventoryAgent_ResetSession();
void InventoryAgent_Tick();
const astr_t &InventoryAgent_Status();
const char *InventoryAgent_ModeName(InventoryAgentMode mode);

} // namespace xuoassist
