// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2017 Hotride

#include "ObjectPropertiesManager.h"
#include "ConfigManager.h"
#include "PacketManager.h"
#include "SkillsManager.h"
#include "GameObjects/GameObject.h"
#include "GumpManager.h"
#include "../EquipmentCompare.h"
#include "../PaperdollSlots_rules.h"
#include "../ToolTip.h"
#include "../SelectedObject.h"
#include "../Gumps/GumpProperty.h"
#include "../Gumps/GumpPropertyIcon.h"
#include "../GameObjects/GameWorld.h"
#include "../GameObjects/GameCharacter.h"
#include "../GameObjects/GamePlayer.h"
#include "xuocore/enumlist.h"

namespace
{
constexpr uint32_t kCompareOplRetryMs = 2500u;
constexpr uint32_t kCompareOplPruneMs = kCompareOplRetryMs * 4u;

void HashMixU64(uint64_t &hash, uint64_t value)
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}

const wchar_t *EquipmentSlotLabel(int layer)
{
    switch (layer)
    {
        case OL_1_HAND:
            return L"Main Hand";
        case OL_2_HAND:
            return L"Off Hand";
        case OL_SHOES:
            return L"Shoes";
        case OL_PANTS:
            return L"Pants";
        case OL_SHIRT:
            return L"Shirt";
        case OL_HELMET:
            return L"Helmet";
        case OL_GLOVES:
            return L"Gloves";
        case OL_RING:
        case OL_RING_2:
        case OL_RING_3:
        case OL_RING_4:
        case OL_RING_5:
        case OL_RING_6:
            return L"Ring";
        case OL_TALISMAN:
            return L"Talisman";
        case OL_NECKLACE:
            return L"Necklace";
        case OL_WAIST:
            return L"Waist";
        case OL_TORSO:
            return L"Chest";
        case OL_BRACELET:
            return L"Left Bracelet";
        case OL_BRACELET_2:
            return L"Right Bracelet";
        case OL_15:
            return L"Face";
        case OL_TUNIC:
            return L"Tunic";
        case OL_EARRINGS:
            return L"Left Earring";
        case OL_EARRINGS_2:
            return L"Right Earring";
        case OL_ARMS:
            return L"Arms";
        case OL_CLOAK:
            return L"Cloak";
        case OL_ROBE:
            return L"Robe";
        case OL_SKIRT:
            return L"Skirt";
        case OL_LEGS:
            return L"Legs";
        default:
            return L"Equipped";
    }
}

bool HasWeaponOplCliloc(const CObjectProperty *prop)
{
    if (prop == nullptr)
    {
        return false;
    }

    for (const auto &entry : prop->Properties)
    {
        switch (entry.first)
        {
            case 1061168u: // weapon damage
            case 1061167u: // weapon speed
            case 1061171u: // two-handed weapon
            case 1061824u: // one-handed weapon
            case 1061172u: // skill required: swordsmanship
            case 1061173u: // skill required: mace fighting
            case 1061174u: // skill required: fencing
            case 1061175u: // skill required: archery
            case 1112075u: // skill required: throwing
                return true;
            default:
                break;
        }
    }

    return false;
}

struct ResolvedCompareTarget
{
    CGameItem *Item = nullptr;
    wstr_t SlotLabel = {};
    bool IsOffHandWeapon = false;
};
} // namespace

CObjectPropertiesManager g_ObjectPropertiesManager;

CObjectProperty::CObjectProperty(
    int serial,
    int revision,
    const wstr_t &name,
    const wstr_t &data,
    std::vector<std::pair<uint32_t, wstr_t>> properties)
    : Serial(serial)
    , Revision(revision)
    , Name(name)
    , Data(data)
    , Properties(std::move(properties))
{
}

bool CObjectProperty::Empty() const
{
    return ((Name.length() == 0u) && (Data.length() == 0u));
}

wstr_t CObjectProperty::CreateTextData(bool extended) const
{
    CGameObject *obj = g_World->FindWorldObject(Serial);
    bool coloredStartFont = false;
    wstr_t result = {};

    if (Name.length() != 0u)
    {
        if (obj != nullptr)
        {
            if (!obj->NPC)
            {
                result = L"<basefont color=\"yellow\">";
                coloredStartFont = true;
            }
            else
            {
                CGameCharacter *gc = obj->GameCharacterPtr();
                coloredStartFont = true;

                switch (gc->Notoriety)
                {
                    case NT_INNOCENT:
                    {
                        result = L"<basefont color=\"cyan\">";
                        break;
                    }
                    case NT_SOMEONE_GRAY:
                    case NT_CRIMINAL:
                    {
                        result = L"<basefont color=\"gray\">";
                        break;
                    }
                    case NT_MURDERER:
                    {
                        result = L"<basefont color=\"red\">";
                        break;
                    }
                    case NT_INVULNERABLE:
                    {
                        result = L"<basefont color=\"yellow\">";
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        result += Name;

        if (coloredStartFont)
        {
            result += L"<basefont color=\"#FFFFFFFF\">";
        }
    }

    if (Data.length() != 0u)
    {
        result += L"\n" + Data;
    }
    else if (extended)
    {
        if (result.length() != 0u)
        {
            result += L"\nNo significant properties...";
        }
        else
        {
            result = L"\nNo Data";
        }
    }

    return result;
}

CObjectPropertiesManager::~CObjectPropertiesManager()
{
    m_Map.clear();
    m_PendingCompareRequests.clear();
}

void CObjectPropertiesManager::Reset()
{
    m_Object = nullptr;
    m_PendingCompareRequests.clear();
    g_ToolTip.Reset();
}

bool CObjectPropertiesManager::RevisionCheck(int serial, int revision) const
{
    const auto it = m_Map.find(serial);
    if (it == m_Map.end())
    {
        return false;
    }
    return (it->second.Revision == revision);
}

const CObjectProperty *CObjectPropertiesManager::Find(uint32_t serial) const
{
    const auto it = m_Map.find(serial);
    if (it == m_Map.end())
    {
        return nullptr;
    }
    return &it->second;
}

void CObjectPropertiesManager::OnItemClicked(int serial)
{
    if (!g_ConfigManager.GetItemPropertiesIcon() || !g_TooltipsEnabled ||
        g_ConfigManager.GetItemPropertiesMode() != OPM_SINGLE_CLICK)
    {
        return;
    }

    auto it = m_Map.find(serial);
    if (it == m_Map.end() || it->second.Empty())
    {
        return;
    }

    g_GumpManager.CloseGump(0, 0, GT_PROPERTY);
    g_GumpManager.AddGump(new CGumpProperty(it->second.CreateTextData(true)));
}

void CObjectPropertiesManager::Display(int serial)
{
    auto it = m_Map.find(serial);
    if (it == m_Map.end() || it->second.Empty())
    {
        if (m_Object != nullptr)
        {
            if (g_ConfigManager.GetItemPropertiesMode() == OPM_AT_ICON)
            {
                CGumpPropertyIcon *gump =
                    (CGumpPropertyIcon *)g_GumpManager.UpdateContent(0, 0, GT_PROPERTY_ICON);

                if (gump != nullptr)
                {
                    gump->Object = nullptr;
                }
            }

            Reset();
        }

        return;
    }

    if (!g_ConfigManager.GetItemPropertiesIcon() ||
        g_ConfigManager.GetItemPropertiesMode() == OPM_FOLLOW_MOUSE)
    {
        const uint32_t itemSerial = static_cast<uint32_t>(serial);
        const CObjectProperty &hoveredProp = it->second;
        CGameItem *item = (g_ConfigManager.CompareEquipmentOnCtrl && g_World != nullptr)
                              ? g_World->FindWorldItem(itemSerial)
                              : nullptr;
        const MulStaticTile2 *td = item != nullptr ? item->GetStaticData() : nullptr;
        const bool isComparableWearable =
            item != nullptr && !item->NPC && !item->IsCorpse() && g_Player != nullptr &&
            item->Container != g_PlayerSerial && td != nullptr &&
            (item->IsWearable() || item->IsWeapon() || item->IsArmor()) &&
            IsPaperdollEquipmentLayer(td->Layer) && td->Layer != OL_HAIR &&
            td->Layer != OL_BEARD && td->Layer != OL_BACKPACK;

        if (!isComparableWearable)
        {
            g_ToolTip.Set(hoveredProp.CreateTextData(false));
            return;
        }

        const bool compareHeld = g_CtrlPressed && !g_ShiftPressed && !g_AltPressed;
        if (!compareHeld)
        {
            wstr_t primaryText = hoveredProp.CreateTextData(false);
            primaryText +=
                L"\n<basefont color=\"#99BBDD\">Hold Ctrl to compare<basefont color=\"#FFFFFF\">";
            uint64_t sig = 0xcbf29ce484222325ULL;
            HashMixU64(sig, itemSerial);
            HashMixU64(sig, hoveredProp.Revision);
            HashMixU64(sig, 0ULL);
            if (sig == 0ULL)
            {
                sig = 1ULL;
            }
            g_ToolTip.SetWithComparison(primaryText, {}, {}, sig);
            return;
        }

        auto isEquippedWeapon = [&](CGameItem *eq, bool allowUnloadedTwoHandFallback) -> bool {
            if (eq == nullptr)
            {
                return false;
            }
            const CObjectProperty *eqProp = Find(eq->Serial);
            if (eqProp != nullptr && !eqProp->Empty())
            {
                return HasWeaponOplCliloc(eqProp);
            }
            // OPL not loaded yet: UO tiledata.mul sets TileFlag.Weapon (0x02) on armor,
            // shields, and jewelry as well, so only allow hand-layer items as fallback.
            const MulStaticTile2 *eqTd = eq->GetStaticData();
            if (eqTd == nullptr)
            {
                return false;
            }
            if (eqTd->Layer == OL_1_HAND)
            {
                return eq->IsWeapon();
            }
            if (allowUnloadedTwoHandFallback && eqTd->Layer == OL_2_HAND)
            {
                return eq->IsWeapon();
            }
            return false;
        };

        // Build live character context (profile, stats, caps, skills) for SOTA comparison.
        CharacterCompareContext compareCtx;
        compareCtx.Valid = true;
        compareCtx.Profile = (g_ConfigManager.CompareEquipmentProfile <= 5u)
                                 ? static_cast<CompareBuildProfile>(g_ConfigManager.CompareEquipmentProfile)
                                 : CompareBuildProfile::Auto;
        compareCtx.Str = g_Player->Str;
        compareCtx.Dex = g_Player->Dex;
        compareCtx.Int = g_Player->Int;
        compareCtx.Stam = g_Player->MaxStam > 0 ? static_cast<int>(g_Player->MaxStam)
                                                : static_cast<int>(g_Player->Stam);

        const bool hasExtendedStatus =
            g_Player->MaxPhysicalResistance > 0 || g_Player->MaxDefenceChance > 0 ||
            g_Player->Armor != 0 || g_Player->FireResistance != 0 ||
            g_Player->ColdResistance != 0 || g_Player->PoisonResistance != 0 ||
            g_Player->EnergyResistance != 0 || g_Player->LowerRegCost != 0 ||
            g_Player->LowerManaCost != 0 || g_Player->WeaponDamage != 0 ||
            g_Player->WeaponSpeed != 0 || g_Player->AttackChance != 0 ||
            g_Player->DefenceChance != 0 || g_Player->SpellDamage != 0 ||
            g_Player->CastSpeed != 0 || g_Player->CastRecovery != 0;
        if (hasExtendedStatus)
        {
            compareCtx.HasStatusTotals = true;
            compareCtx.PhysicalResist = g_Player->Armor;
            compareCtx.FireResist = g_Player->FireResistance;
            compareCtx.ColdResist = g_Player->ColdResistance;
            compareCtx.PoisonResist = g_Player->PoisonResistance;
            compareCtx.EnergyResist = g_Player->EnergyResistance;
            compareCtx.MaxPhysicalResist =
                g_Player->MaxPhysicalResistance > 0 ? g_Player->MaxPhysicalResistance : 70;
            compareCtx.MaxFireResist =
                g_Player->MaxFireResistance > 0 ? g_Player->MaxFireResistance : 70;
            compareCtx.MaxColdResist =
                g_Player->MaxColdResistance > 0 ? g_Player->MaxColdResistance : 70;
            compareCtx.MaxPoisonResist =
                g_Player->MaxPoisonResistance > 0 ? g_Player->MaxPoisonResistance : 70;
            compareCtx.MaxEnergyResist =
                g_Player->MaxEnergyResistance > 0 ? g_Player->MaxEnergyResistance : 70;
            compareCtx.DefenseChance = g_Player->DefenceChance;
            compareCtx.MaxDefenseChance =
                g_Player->MaxDefenceChance > 0 ? g_Player->MaxDefenceChance : 45;
            compareCtx.AttackChance = g_Player->AttackChance;
            compareCtx.WeaponSpeed = g_Player->WeaponSpeed;
            compareCtx.WeaponDamage = g_Player->WeaponDamage;
            compareCtx.LowerReagentCost = g_Player->LowerRegCost;
            compareCtx.LowerManaCost = g_Player->LowerManaCost;
            compareCtx.SpellDamage = g_Player->SpellDamage;
            compareCtx.CastSpeed = g_Player->CastSpeed;
            compareCtx.CastRecovery = g_Player->CastRecovery;
            compareCtx.Luck = g_Player->Luck;
        }

        if (g_SkillsManager.Count > 0u && g_SkillsManager.SkillsTotal > 0.0f)
        {
            compareCtx.HasSkills = true;
            const uint32_t skillLimit =
                std::min<uint32_t>(g_SkillsManager.Count, static_cast<uint32_t>(kCompareSkillCount));
            for (uint32_t i = 0; i < skillLimit; ++i)
            {
                if (const CSkill *sk = g_SkillsManager.Get(i))
                {
                    compareCtx.Skills[i] = static_cast<double>(sk->Value);
                }
            }
        }

        const ParsedEquipmentStats hoveredParsed = ParseEquipmentOpl(hoveredProp.Properties);
        const bool hoveredIsWeapon = hoveredParsed.HasWeaponIdentity();

        std::vector<ResolvedCompareTarget> targets;
        std::vector<uint32_t> prefetchSerials;
        int totalSlots = 1;
        int occupiedSlots = 0;

        if (td->Layer == OL_1_HAND)
        {
            CGameItem *mh = g_Player->FindLayer(OL_1_HAND);
            CGameItem *oh = g_Player->FindLayer(OL_2_HAND);
            const bool dualWield =
                (g_Player->GetOffHandWeapon() != nullptr) ||
                (isEquippedWeapon(mh, false) && isEquippedWeapon(oh, false));
            if (hoveredIsWeapon && dualWield)
            {
                totalSlots = 2;
                if (mh != nullptr)
                {
                    targets.push_back({ mh, L"Main Hand", false });
                    ++occupiedSlots;
                }
                if (oh != nullptr)
                {
                    targets.push_back({ oh, L"Off Hand", true });
                    ++occupiedSlots;
                }
            }
            else if (mh != nullptr)
            {
                targets.push_back({ mh, L"Main Hand", false });
                occupiedSlots = 1;
            }
            else if (isEquippedWeapon(oh, true))
            {
                targets.push_back({ oh, L"Two-Handed", false });
                occupiedSlots = 1;
            }
        }
        else if (td->Layer == OL_2_HAND)
        {
            CGameItem *mh = g_Player->FindLayer(OL_1_HAND);
            CGameItem *oh = g_Player->FindLayer(OL_2_HAND);
            if (hoveredIsWeapon)
            {
                if (mh != nullptr)
                {
                    targets.push_back({ mh, L"Main Hand", false });
                    ++occupiedSlots;
                }
                if (oh != nullptr)
                {
                    const bool ohWeapon = isEquippedWeapon(oh, true);
                    const wchar_t *ohLabel =
                        ohWeapon ? (mh != nullptr ? L"Off Hand" : L"Two-Handed")
                                 : L"Shield / Off Hand";
                    targets.push_back({ oh, ohLabel, ohWeapon && mh != nullptr });
                    ++occupiedSlots;
                }
                totalSlots = occupiedSlots > 1 ? occupiedSlots : 1;
            }
            else
            {
                if (oh != nullptr)
                {
                    const bool ohWeapon = isEquippedWeapon(oh, true);
                    const wchar_t *ohLabel =
                        ohWeapon ? (mh != nullptr ? L"Off Hand Weapon" : L"Two-Handed Weapon")
                                 : L"Shield";
                    targets.push_back({ oh, ohLabel, ohWeapon && mh != nullptr });
                    occupiedSlots = 1;
                }
            }
        }
        else if (IsBraceletLayer(td->Layer))
        {
            totalSlots = static_cast<int>(kPaperdollBraceletLayers.size());
            for (size_t i = 0; i < kPaperdollBraceletLayers.size(); ++i)
            {
                CGameItem *eq = g_Player->FindLayer(kPaperdollBraceletLayers[i]);
                if (eq != nullptr)
                {
                    targets.push_back(
                        { eq, i == 0 ? L"Left Bracelet" : L"Right Bracelet", false });
                    ++occupiedSlots;
                }
            }
        }
        else if (IsEarringsLayer(td->Layer))
        {
            totalSlots = static_cast<int>(kPaperdollEarringLayers.size());
            for (size_t i = 0; i < kPaperdollEarringLayers.size(); ++i)
            {
                CGameItem *eq = g_Player->FindLayer(kPaperdollEarringLayers[i]);
                if (eq != nullptr)
                {
                    targets.push_back({ eq, i == 0 ? L"Left Earring" : L"Right Earring", false });
                    ++occupiedSlots;
                }
            }
        }
        else if (IsRingLayer(td->Layer))
        {
            totalSlots = static_cast<int>(kPaperdollRingLayers.size());
            std::vector<CGameItem *> rings;
            for (int layer : kPaperdollRingLayers)
            {
                CGameItem *eq = g_Player->FindLayer(layer);
                if (eq != nullptr)
                {
                    rings.push_back(eq);
                }
            }
            occupiedSlots = static_cast<int>(rings.size());
            if (rings.size() == 1u)
            {
                targets.push_back({ rings[0], L"Ring", false });
            }
            else if (rings.size() > 1u)
            {
                CGameItem *weakestLoaded = nullptr;
                double weakestScore = 0.0;
                CGameItem *firstUnloaded = nullptr;
                for (CGameItem *ring : rings)
                {
                    const CObjectProperty *ringProp = Find(ring->Serial);
                    if (ringProp == nullptr || ringProp->Empty())
                    {
                        prefetchSerials.push_back(ring->Serial);
                        if (firstUnloaded == nullptr)
                        {
                            firstUnloaded = ring;
                        }
                        continue;
                    }
                    const double score = ScoreEquipment(
                        ParseEquipmentOpl(ringProp->Properties),
                        EquipCategory::JewelryOrUtility,
                        &compareCtx);
                    if (weakestLoaded == nullptr || score < weakestScore)
                    {
                        weakestLoaded = ring;
                        weakestScore = score;
                    }
                }
                CGameItem *chosen = firstUnloaded != nullptr ? firstUnloaded : weakestLoaded;
                if (chosen != nullptr)
                {
                    targets.push_back({ chosen, L"Weakest Ring", false });
                }
            }
        }
        else
        {
            totalSlots = 1;
            CGameItem *eq = g_Player->FindLayer(td->Layer);
            if (eq != nullptr)
            {
                targets.push_back({ eq, EquipmentSlotLabel(td->Layer), false });
                occupiedSlots = 1;
            }
        }

        if (m_PendingCompareRequests.size() > 16u)
        {
            for (auto reqIt = m_PendingCompareRequests.begin();
                 reqIt != m_PendingCompareRequests.end();)
            {
                const bool expired = (g_Ticks - reqIt->second) >= kCompareOplPruneMs;
                const bool gone =
                    (g_World == nullptr || g_World->FindWorldItem(reqIt->first) == nullptr);
                if (expired || gone)
                {
                    reqIt = m_PendingCompareRequests.erase(reqIt);
                }
                else
                {
                    ++reqIt;
                }
            }
        }

        auto requestOplIfMissing = [&](uint32_t targetSerial) {
            const CObjectProperty *existing = Find(targetSerial);
            if (existing != nullptr && !existing->Empty())
            {
                return;
            }
            const auto reqIt = m_PendingCompareRequests.find(targetSerial);
            if (reqIt == m_PendingCompareRequests.end() ||
                (g_Ticks - reqIt->second) >= kCompareOplRetryMs)
            {
                m_PendingCompareRequests[targetSerial] = g_Ticks;
                g_PacketManager.AddMegaClilocRequest(static_cast<int>(targetSerial));
            }
        };

        auto addComparingSerial = [](std::vector<uint32_t> &serials, uint32_t s) {
            for (uint32_t existing : serials)
            {
                if (existing == s)
                {
                    return;
                }
            }
            serials.push_back(s);
        };

        std::vector<uint32_t> comparingSerials;
        for (uint32_t s : prefetchSerials)
        {
            requestOplIfMissing(s);
            addComparingSerial(comparingSerials, s);
        }

        EquipmentCompareItemInput hoveredInput;
        hoveredInput.Serial = itemSerial;
        hoveredInput.Layer = td->Layer;
        hoveredInput.IsWeapon = hoveredIsWeapon;
        hoveredInput.IsOffHandWeapon = false;
        hoveredInput.PropertiesLoaded = true;
        hoveredInput.Name = hoveredProp.Name;
        hoveredInput.FormattedTooltip = hoveredProp.CreateTextData(false);
        hoveredInput.Properties = hoveredProp.Properties;
        hoveredInput.SlotLabel = EquipmentSlotLabel(td->Layer);

        std::vector<EquipmentCompareItemInput> equippedInputs;
        equippedInputs.reserve(targets.size());

        // Scan other loaded worn paperdoll pieces for uncapped resist totals so overcap
        // buffers are detected accurately even though 0x11 clamps player resists at Max*Resist.
        if (compareCtx.HasStatusTotals)
        {
            auto isTargetSerial = [&](uint32_t s) {
                for (const ResolvedCompareTarget &t : targets)
                {
                    if (t.Item != nullptr && t.Item->Serial == s)
                    {
                        return true;
                    }
                }
                return false;
            };
            for (int layer = OL_1_HAND; layer < OL_EXTENDED_COUNT; ++layer)
            {
                if (!IsPaperdollEquipmentLayer(layer) || layer == OL_HAIR || layer == OL_BEARD ||
                    layer == OL_BACKPACK)
                {
                    continue;
                }
                CGameItem *worn = g_Player->FindLayer(layer);
                if (worn == nullptr || isTargetSerial(worn->Serial))
                {
                    continue;
                }
                const CObjectProperty *wornProp = Find(worn->Serial);
                if (wornProp == nullptr || wornProp->Empty())
                {
                    continue;
                }
                const ParsedEquipmentStats wornStats = ParseEquipmentOpl(wornProp->Properties);
                compareCtx.HasOtherEquippedResists = true;
                compareCtx.OtherEquippedPhysicalResist += wornStats.PhysicalResist;
                compareCtx.OtherEquippedFireResist += wornStats.FireResist;
                compareCtx.OtherEquippedColdResist += wornStats.ColdResist;
                compareCtx.OtherEquippedPoisonResist += wornStats.PoisonResist;
                compareCtx.OtherEquippedEnergyResist += wornStats.EnergyResist;
            }
        }

        uint64_t sig = 0xcbf29ce484222325ULL;
        HashMixU64(sig, itemSerial);
        HashMixU64(sig, hoveredProp.Revision);
        HashMixU64(sig, 1ULL);
        HashMixU64(sig, static_cast<uint64_t>(totalSlots));
        HashMixU64(sig, static_cast<uint64_t>(occupiedSlots));
        HashMixU64(sig, static_cast<uint64_t>(prefetchSerials.size()));
        HashMixU64(sig, static_cast<uint64_t>(compareCtx.Profile));
        HashMixU64(sig, static_cast<uint64_t>(compareCtx.Str));
        HashMixU64(sig, static_cast<uint64_t>(compareCtx.Stam));
        HashMixU64(sig, static_cast<uint64_t>(compareCtx.HasStatusTotals ? 1 : 0));
        if (compareCtx.HasStatusTotals)
        {
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.PhysicalResist + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.FireResist + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.ColdResist + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.PoisonResist + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.EnergyResist + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.MaxPhysicalResist));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.DefenseChance + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.MaxDefenseChance));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.AttackChance + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.WeaponSpeed + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.WeaponDamage + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.LowerReagentCost + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.LowerManaCost + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.CastSpeed + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.CastRecovery + 1000));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.OtherEquippedPhysicalResist));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.OtherEquippedFireResist));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.OtherEquippedColdResist));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.OtherEquippedPoisonResist));
            HashMixU64(sig, static_cast<uint64_t>(compareCtx.OtherEquippedEnergyResist));
        }

        for (const ResolvedCompareTarget &target : targets)
        {
            CGameItem *eq = target.Item;
            if (eq == nullptr)
            {
                continue;
            }

            const CObjectProperty *eqProp = Find(eq->Serial);
            const bool loaded = (eqProp != nullptr && !eqProp->Empty());
            if (!loaded)
            {
                requestOplIfMissing(eq->Serial);
            }
            addComparingSerial(comparingSerials, eq->Serial);

            EquipmentCompareItemInput eqInput;
            eqInput.Serial = eq->Serial;
            eqInput.Layer = eq->Layer != OL_NONE
                                ? static_cast<int>(eq->Layer)
                                : (eq->GetStaticData() != nullptr
                                       ? static_cast<int>(eq->GetStaticData()->Layer)
                                       : OL_NONE);
            eqInput.IsWeapon = isEquippedWeapon(eq, false);
            eqInput.IsOffHandWeapon = target.IsOffHandWeapon;
            eqInput.PropertiesLoaded = loaded;
            eqInput.SlotLabel = target.SlotLabel;
            if (loaded)
            {
                eqInput.Name = eqProp->Name;
                eqInput.FormattedTooltip = eqProp->CreateTextData(false);
                eqInput.Properties = eqProp->Properties;
            }
            else
            {
                eqInput.Name = wstr_from(eq->GetName());
            }

            HashMixU64(sig, eq->Serial);
            HashMixU64(sig, loaded ? 1ULL : 0ULL);
            HashMixU64(sig, loaded ? static_cast<uint64_t>(eqProp->Revision) : 0ULL);
            HashMixU64(sig, target.IsOffHandWeapon ? 1ULL : 0ULL);

            equippedInputs.push_back(std::move(eqInput));
        }

        if (sig == 0ULL)
        {
            sig = 1ULL;
        }

        const EquipmentCompareResult compareResult = BuildEquipmentComparison(
            hoveredInput, equippedInputs, totalSlots, occupiedSlots, &compareCtx);

        std::vector<CToolTip::TooltipSideCardDesc> sideCards;
        sideCards.reserve(compareResult.SideCards.size());
        for (const ComparisonCardOutput &card : compareResult.SideCards)
        {
            sideCards.emplace_back();
            CToolTip::TooltipSideCardDesc &sideCard = sideCards.back();
            sideCard.Data = card.HtmlText;
            sideCard.MaxWidth = card.MaxWidth;
            sideCard.Align = card.AlignLeft ? TS_LEFT : TS_CENTER;
            sideCard.BorderColorARGB = card.BorderColorARGB;
        }

        g_ToolTip.SetWithComparison(
            hoveredInput.FormattedTooltip, sideCards, comparingSerials, sig, 280);
        return;
    }
    if (g_ConfigManager.GetItemPropertiesMode() == OPM_SINGLE_CLICK)
    {
        return;
    }

    CGumpPropertyIcon *gump = (CGumpPropertyIcon *)g_GumpManager.GetGump(0, 0, GT_PROPERTY_ICON);

    if (gump == nullptr)
    {
        return;
    }

    bool condition = (g_ConfigManager.GetItemPropertiesMode() == OPM_ALWAYS_UP);

    CRenderObject *object = g_SelectedObject.Object;

    if (object != m_Object)
    {
        m_Object = object;
        Timer = g_Ticks + g_ConfigManager.ToolTipsDelay;
    }

    if (!condition)
    {
        condition = !(Timer > g_Ticks);
    }

    if (condition && gump->Object != m_Object)
    {
        gump->SetTextW(it->second.CreateTextData(true));
        gump->Object = m_Object;
    }
}

void CObjectPropertiesManager::Add(int serial, const CObjectProperty &objectProperty)
{
    const uint32_t key = static_cast<uint32_t>(serial);
    m_Map[key] = objectProperty;
    m_PendingCompareRequests.erase(key);
}

astr_t CObjectPropertiesManager::GetData(const uint32_t &serial)
{
    auto it = m_Map.find(serial);
    if (it == m_Map.end())
    {
        return {};
    }
    return str_from(it->second.Data);
}
