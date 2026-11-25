// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <common/str.h>

class CGameCharacter;

namespace xuoassist {

class CJournalMessage;

struct DisplayMobileStats
{
    uint16_t Hits;
    uint16_t MaxHits;
    uint16_t Mana;
    uint16_t MaxMana;
    uint16_t Stamina;
    uint16_t MaxStamina;
};

enum class SuppliesCoverage
{
    Unknown,
    Partial,
    Complete
};

struct SupplyCounter
{
    astr_t Name;
    astr_t Key;
    uint16_t Graphic = 0;
    uint16_t Hue = 0;
    int Count = 0;
    bool Icon = false;
    bool Low = false;
    int Warning = 0;
    bool Highlighted = false;

    bool operator==(const SupplyCounter &other) const
    {
        return Name == other.Name && Key == other.Key && Graphic == other.Graphic &&
               Hue == other.Hue && Count == other.Count && Icon == other.Icon && Low == other.Low &&
               Warning == other.Warning && Highlighted == other.Highlighted;
    }
};

struct SuppliesSnapshot
{
    std::vector<SupplyCounter> Entries;
    SuppliesCoverage Coverage = SuppliesCoverage::Unknown;
    bool Recounting = false;
    bool CanRecount = false;
    astr_t BandageText;
    astr_t GoldText;
    astr_t GateText;
    astr_t CriminalText;
    astr_t CastSpellName;
    bool AutoRecount = true;
    bool SkipPouches = true;

    bool operator==(const SuppliesSnapshot &other) const
    {
        return Coverage == other.Coverage && Recounting == other.Recounting &&
               CanRecount == other.CanRecount && BandageText == other.BandageText &&
               GoldText == other.GoldText && GateText == other.GateText &&
               CriminalText == other.CriminalText && CastSpellName == other.CastSpellName &&
               AutoRecount == other.AutoRecount && SkipPouches == other.SkipPouches &&
               Entries == other.Entries;
    }
};

SuppliesSnapshot ItemCounters_GetSupplies();
bool ItemCounters_StartRecount();
void ItemCounters_CancelRecount();
bool ItemCounters_StatusExpanded();
void ItemCounters_SetStatusExpanded(bool expanded);
bool ItemCounters_UseCounter(uint16_t graphic, uint16_t hue);
void ItemCounters_OnContainerChanged(
    uint32_t containerSerial,
    uint32_t itemSerial,
    uint16_t graphic,
    uint16_t hue,
    bool isContainer,
    bool removed);
void ItemCounters_MarkNonBag(uint32_t serial);
uint64_t ItemCounters_Revision();

struct DisplayCounterInfo
{
    astr_t Value;
    uint16_t Graphic = 0;
    uint16_t Hue = 0;
    bool ShowIcon = false;
    bool Low = false;
    bool Highlighted = false;
};

void ItemCounters_DrawEditor();
void ItemCounters_DrawMessageRules();
void ItemCounters_OnJournalMessage(const CJournalMessage &message);
void ItemCounters_DrawHUD();
bool ItemCounters_IsHUDVisible();
void ItemCounters_ResetSession();
bool ItemCounters_Read(const astr_t &key, astr_t &value);
bool ItemCounters_ReadDisplay(const astr_t &key, DisplayCounterInfo &info);
const astr_t &ItemCounters_ContainerLabel(uint32_t serial, uint16_t graphic);
void ItemCounters_OnGameSend(const uint8_t *data, size_t size);
void ItemCounters_OnBuffChange(const astr_t &name, bool added);
DisplayMobileStats ItemCounters_MobileStats(const CGameCharacter &character);
void ItemCounters_OnMobileStats(const CGameCharacter &character, const DisplayMobileStats &previous);
void ItemCounters_OnDamage(CGameCharacter &character, int damage);

} // namespace xuoassist
