// SPDX-License-Identifier: MIT

#include "EquipmentCompare.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
constexpr uint32_t kBorderNeutralARGB = 0xFF565E66;
constexpr uint32_t kBorderUpgradeARGB = 0xFF44CC66;
constexpr uint32_t kBorderDowngradeARGB = 0xFFCC4444;
constexpr uint32_t kBorderEquippedARGB = 0xFFD8B058;

const wchar_t *const kColorGreen = L"<basefont color=\"#44FF66\">";
const wchar_t *const kColorRed = L"<basefont color=\"#FF5555\">";
const wchar_t *const kColorGold = L"<basefont color=\"#FFD700\">";
const wchar_t *const kColorBlue = L"<basefont color=\"#66CCFF\">";
const wchar_t *const kColorGray = L"<basefont color=\"#CCCCCC\">";
const wchar_t *const kColorWhite = L"<basefont color=\"#FFFFFF\">";

bool IsAsciiSpace(wchar_t ch)
{
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

wchar_t ToLowerAscii(wchar_t ch)
{
    if (ch >= L'A' && ch <= L'Z')
    {
        return static_cast<wchar_t>(ch + (L'a' - L'A'));
    }
    return ch;
}

wstr_t TrimWide(const wstr_t &text)
{
    size_t start = 0;
    while (start < text.size() && IsAsciiSpace(text[start]))
    {
        ++start;
    }

    size_t end = text.size();
    while (end > start && IsAsciiSpace(text[end - 1]))
    {
        --end;
    }

    return text.substr(start, end - start);
}

std::vector<wstr_t> SplitTabs(const wstr_t &text)
{
    std::vector<wstr_t> parts;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t tab = text.find(L'\t', start);
        if (tab == wstr_t::npos)
        {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, tab - start));
        start = tab + 1;
    }
    return parts;
}

bool EqualsIgnoreCaseAscii(const wstr_t &lhs, const wchar_t *rhs)
{
    size_t i = 0;
    for (; i < lhs.size() && rhs[i] != L'\0'; ++i)
    {
        if (ToLowerAscii(lhs[i]) != ToLowerAscii(rhs[i]))
        {
            return false;
        }
    }
    return i == lhs.size() && rhs[i] == L'\0';
}

bool ParseIntSafe(const wstr_t &raw, int &out)
{
    const wstr_t text = TrimWide(raw);
    if (text.empty())
    {
        return false;
    }

    size_t idx = 0;
    bool negative = false;
    if (text[idx] == L'+')
    {
        ++idx;
    }
    else if (text[idx] == L'-')
    {
        negative = true;
        ++idx;
    }

    bool hasDigit = false;
    int64_t value = 0;
    for (; idx < text.size(); ++idx)
    {
        const wchar_t ch = text[idx];
        if (ch < L'0' || ch > L'9')
        {
            break;
        }
        hasDigit = true;
        value = value * 10 + static_cast<int64_t>(ch - L'0');
        if (value > 1000000000LL)
        {
            value = 1000000000LL;
            break;
        }
    }

    if (!hasDigit)
    {
        return false;
    }

    out = static_cast<int>(negative ? -value : value);
    return true;
}

bool ParseUint32Safe(const wstr_t &raw, uint32_t &out)
{
    const wstr_t text = TrimWide(raw);
    if (text.empty())
    {
        return false;
    }

    size_t idx = 0;
    if (text[idx] == L'#')
    {
        ++idx;
    }

    bool hasDigit = false;
    uint64_t value = 0;
    for (; idx < text.size(); ++idx)
    {
        const wchar_t ch = text[idx];
        if (ch < L'0' || ch > L'9')
        {
            break;
        }
        hasDigit = true;
        value = value * 10ULL + static_cast<uint64_t>(ch - L'0');
        if (value > 0xFFFFFFFFULL)
        {
            value = 0xFFFFFFFFULL;
            break;
        }
    }

    if (!hasDigit)
    {
        return false;
    }

    out = static_cast<uint32_t>(value);
    return true;
}

bool ParseDoubleSafe(const wstr_t &raw, double &out)
{
    const wstr_t text = TrimWide(raw);
    if (text.empty())
    {
        return false;
    }

    size_t idx = 0;
    bool negative = false;
    if (text[idx] == L'+')
    {
        ++idx;
    }
    else if (text[idx] == L'-')
    {
        negative = true;
        ++idx;
    }

    bool hasDigit = false;
    double value = 0.0;
    for (; idx < text.size(); ++idx)
    {
        const wchar_t ch = text[idx];
        if (ch >= L'0' && ch <= L'9')
        {
            hasDigit = true;
            value = value * 10.0 + static_cast<double>(ch - L'0');
        }
        else
        {
            break;
        }
    }

    if (idx < text.size() && (text[idx] == L'.' || text[idx] == L','))
    {
        ++idx;
        double fracScale = 0.1;
        for (; idx < text.size(); ++idx)
        {
            const wchar_t ch = text[idx];
            if (ch >= L'0' && ch <= L'9')
            {
                hasDigit = true;
                value += static_cast<double>(ch - L'0') * fracScale;
                fracScale *= 0.1;
            }
            else
            {
                break;
            }
        }
    }

    if (!hasDigit || !std::isfinite(value))
    {
        return false;
    }

    out = negative ? -value : value;
    return true;
}

bool IsSlayerCliloc(uint32_t cliloc)
{
    if (cliloc >= 1060457u && cliloc <= 1060481u)
    {
        return true;
    }
    if (cliloc >= 1017384u && cliloc <= 1017409u)
    {
        return true;
    }
    if (cliloc == 1070855u)
    {
        return true;
    }
    if (cliloc >= 1072504u && cliloc <= 1072512u)
    {
        return true;
    }
    return false;
}

bool IsPassthroughCliloc(uint32_t cliloc)
{
    return cliloc == 1042971u || cliloc == 1070722u || cliloc == 1114057u || cliloc == 1114778u ||
           cliloc == 1114779u;
}

bool IsRingFamilyLayer(int layer)
{
    return layer == OL_RING || layer == OL_RING_2 || layer == OL_RING_3 || layer == OL_RING_4 ||
           layer == OL_RING_5 || layer == OL_RING_6;
}

bool IsBraceletFamilyLayer(int layer)
{
    return layer == OL_BRACELET || layer == OL_BRACELET_2;
}

bool IsEarringFamilyLayer(int layer)
{
    return layer == OL_EARRINGS || layer == OL_EARRINGS_2;
}

wstr_t SlotFamilyPluralName(int layer)
{
    if (IsBraceletFamilyLayer(layer))
    {
        return L"bracelets";
    }
    if (IsEarringFamilyLayer(layer))
    {
        return L"earrings";
    }
    if (IsRingFamilyLayer(layer))
    {
        return L"rings";
    }
    return L"slots";
}

wstr_t AsciiToWide(const char *ascii)
{
    wstr_t out;
    if (ascii == nullptr)
    {
        return out;
    }
    while (*ascii != '\0')
    {
        const unsigned char c = static_cast<unsigned char>(*ascii++);
        out.push_back(static_cast<wchar_t>(c < 0x80u ? c : '?'));
    }
    return out;
}

wstr_t SanitizeAsciiLabel(const wstr_t &input)
{
    wstr_t out;
    out.reserve(input.size());
    for (wchar_t ch : input)
    {
        if (ch == L'\n' || (ch >= 0x20 && ch <= 0x7E))
        {
            out.push_back(ch);
        }
        else if (ch == L'\t')
        {
            out.push_back(L' ');
        }
    }
    return out;
}

wstr_t FormatFloat1(double val, bool withSign)
{
    double rounded = std::round(val * 10.0) / 10.0;
    if (std::fabs(rounded) < 0.05)
    {
        rounded = 0.0;
    }

    char buf[64];
    if (withSign)
    {
        std::snprintf(buf, sizeof(buf), "%+.1f", rounded);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%.1f", rounded);
    }
    return AsciiToWide(buf);
}

wstr_t FormatFloatTrimmed(double val, bool withSign)
{
    double rounded = std::round(val * 100.0) / 100.0;
    if (std::fabs(rounded) < 0.005)
    {
        rounded = 0.0;
    }

    char buf[64];
    const double absVal = std::fabs(rounded);
    const double nearestInt = std::round(absVal);
    if (std::fabs(absVal - nearestInt) < 0.005)
    {
        const int intVal = static_cast<int>(std::round(rounded));
        if (withSign)
        {
            std::snprintf(buf, sizeof(buf), "%+d", intVal);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%d", intVal);
        }
        return AsciiToWide(buf);
    }

    const double nearestTenth = std::round(absVal * 10.0) / 10.0;
    if (std::fabs(absVal - nearestTenth) < 0.005)
    {
        if (withSign)
        {
            std::snprintf(buf, sizeof(buf), "%+.1f", rounded);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%.1f", rounded);
        }
        return AsciiToWide(buf);
    }

    if (withSign)
    {
        std::snprintf(buf, sizeof(buf), "%+.2f", rounded);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%.2f", rounded);
    }
    return AsciiToWide(buf);
}

wstr_t FormatInt(int val, bool withSign)
{
    char buf[32];
    if (withSign)
    {
        std::snprintf(buf, sizeof(buf), "%+d", val);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%d", val);
    }
    return AsciiToWide(buf);
}

bool IsDeltaGain(const EquipmentStatDelta &d)
{
    if (d.IsInformational)
    {
        return false;
    }
    return d.LowerIsBetter ? (d.Delta < 0.0) : (d.Delta > 0.0);
}

bool IsDeltaLoss(const EquipmentStatDelta &d)
{
    if (d.IsInformational)
    {
        return false;
    }
    return d.LowerIsBetter ? (d.Delta > 0.0) : (d.Delta < 0.0);
}

int DeltaPriority(const EquipmentStatDelta &d)
{
    if (d.Label == L"Eff DPS")
    {
        return 100;
    }
    if (d.Label == L"Total Resists")
    {
        return 95;
    }
    if (d.Label == L"Base DPS")
    {
        return 90;
    }
    if (d.Label == L"Avg Dmg")
    {
        return 85;
    }
    if (d.Label == L"Speed")
    {
        return 80;
    }
    if (d.Label == L"Damage Inc" || d.Label == L"Swing Speed" || d.Label == L"Hit Chance" ||
        d.Label == L"Def Chance" || d.Label == L"Spell Damage" || d.Label == L"Lower Mana" ||
        d.Label == L"Faster Cast" || d.Label == L"Cast Recovery")
    {
        return 70;
    }
    if (d.Label == L"Spell Channeling" || d.Label == L"Mage Armor" || d.Label == L"Slayers" ||
        d.Label == L"Life Leech" || d.Label == L"Mana Leech" || d.Label == L"Stam Leech")
    {
        return 60;
    }
    return 40;
}

void SortDeltasForDisplay(std::vector<EquipmentStatDelta> &deltas)
{
    std::stable_sort(
        deltas.begin(),
        deltas.end(),
        [](const EquipmentStatDelta &a, const EquipmentStatDelta &b) {
            const int bucketA = a.IsInformational ? 2 : (IsDeltaGain(a) ? 0 : 1);
            const int bucketB = b.IsInformational ? 2 : (IsDeltaGain(b) ? 0 : 1);
            if (bucketA != bucketB)
            {
                return bucketA < bucketB;
            }
            const int prioA = DeltaPriority(a);
            const int prioB = DeltaPriority(b);
            if (prioA != prioB)
            {
                return prioA > prioB;
            }
            return std::fabs(a.Delta) > std::fabs(b.Delta);
        });
}

void AddIntDelta(
    std::vector<EquipmentStatDelta> &out,
    const wchar_t *label,
    int hoveredVal,
    int equippedVal,
    bool isPercent,
    bool lowerIsBetter = false,
    bool isInformational = false)
{
    const int diff = hoveredVal - equippedVal;
    if (diff == 0)
    {
        return;
    }

    EquipmentStatDelta d;
    d.Label = label;
    d.Delta = static_cast<double>(diff);
    d.LowerIsBetter = lowerIsBetter;
    d.IsPercent = isPercent;
    d.IsFloat = false;
    d.IsBooleanFlag = false;
    d.IsInformational = isInformational;
    out.push_back(std::move(d));
}

void AddFloatDelta(
    std::vector<EquipmentStatDelta> &out,
    const wchar_t *label,
    double hoveredVal,
    double equippedVal,
    bool isPercent,
    bool lowerIsBetter = false,
    double minEpsilon = 0.05,
    const wstr_t &detailText = {})
{
    const double diff = hoveredVal - equippedVal;
    if (std::fabs(diff) < minEpsilon)
    {
        return;
    }

    EquipmentStatDelta d;
    d.Label = label;
    d.Delta = diff;
    d.LowerIsBetter = lowerIsBetter;
    d.IsPercent = isPercent;
    d.IsFloat = true;
    d.IsBooleanFlag = false;
    d.IsInformational = false;
    d.DetailText = detailText;
    out.push_back(std::move(d));
}

void AddBoolDelta(
    std::vector<EquipmentStatDelta> &out,
    const wchar_t *label,
    bool hoveredHas,
    bool equippedHas)
{
    if (hoveredHas == equippedHas)
    {
        return;
    }

    EquipmentStatDelta d;
    d.Label = label;
    d.Delta = hoveredHas ? 1.0 : -1.0;
    d.IsBooleanFlag = true;
    out.push_back(std::move(d));
}

void CollectSharedAttributeDeltas(
    std::vector<EquipmentStatDelta> &deltas,
    const ParsedEquipmentStats &hovered,
    const ParsedEquipmentStats &equipped)
{
    // Shared AosAttributes
    AddIntDelta(deltas, L"Damage Inc", hovered.DamageIncrease, equipped.DamageIncrease, true);
    AddIntDelta(
        deltas, L"Swing Speed", hovered.SwingSpeedIncrease, equipped.SwingSpeedIncrease, true);
    AddIntDelta(
        deltas, L"Hit Chance", hovered.HitChanceIncrease, equipped.HitChanceIncrease, true);
    AddIntDelta(
        deltas, L"Def Chance", hovered.DefenseChanceIncrease, equipped.DefenseChanceIncrease, true);
    AddBoolDelta(deltas, L"Spell Channeling", hovered.SpellChanneling, equipped.SpellChanneling);
    AddIntDelta(
        deltas, L"Spell Damage", hovered.SpellDamageIncrease, equipped.SpellDamageIncrease, true);
    AddIntDelta(deltas, L"Lower Mana", hovered.LowerManaCost, equipped.LowerManaCost, true);
    AddIntDelta(deltas, L"Lower Reag", hovered.LowerReagentCost, equipped.LowerReagentCost, true);
    AddIntDelta(deltas, L"Faster Cast", hovered.FasterCasting, equipped.FasterCasting, false);
    AddIntDelta(
        deltas, L"Cast Recovery", hovered.FasterCastRecovery, equipped.FasterCastRecovery, false);

    AddIntDelta(deltas, L"Strength", hovered.BonusStr, equipped.BonusStr, false);
    AddIntDelta(deltas, L"Dexterity", hovered.BonusDex, equipped.BonusDex, false);
    AddIntDelta(deltas, L"Intelligence", hovered.BonusInt, equipped.BonusInt, false);
    AddIntDelta(deltas, L"Hit Points", hovered.BonusHits, equipped.BonusHits, false);
    AddIntDelta(deltas, L"Mana", hovered.BonusMana, equipped.BonusMana, false);
    AddIntDelta(deltas, L"Stamina", hovered.BonusStam, equipped.BonusStam, false);
    AddIntDelta(deltas, L"HP Regen", hovered.RegenHits, equipped.RegenHits, false);
    AddIntDelta(deltas, L"Mana Regen", hovered.RegenMana, equipped.RegenMana, false);
    AddIntDelta(deltas, L"Stam Regen", hovered.RegenStam, equipped.RegenStam, false);

    AddIntDelta(deltas, L"Luck", hovered.Luck, equipped.Luck, false);
    AddIntDelta(deltas, L"Reflect Phys", hovered.ReflectPhysical, equipped.ReflectPhysical, true);
    AddIntDelta(
        deltas, L"Enhance Potions", hovered.EnhancePotions, equipped.EnhancePotions, true);
    AddBoolDelta(deltas, L"Night Sight", hovered.NightSight, equipped.NightSight);
    AddIntDelta(
        deltas,
        L"Karma Loss",
        hovered.IncreasedKarmaLoss,
        equipped.IncreasedKarmaLoss,
        true,
        true /*LowerIsBetter*/);

    // Weapon & Armor attributes
    AddBoolDelta(deltas, L"Use Best Skill", hovered.UseBestSkill, equipped.UseBestSkill);
    AddBoolDelta(deltas, L"Mage Armor", hovered.MageArmor, equipped.MageArmor);
    AddBoolDelta(deltas, L"Balanced", hovered.Balanced, equipped.Balanced);
    AddIntDelta(deltas, L"Velocity", hovered.Velocity, equipped.Velocity, true);

    if (hovered.HasMageWeapon && equipped.HasMageWeapon)
    {
        const int penaltyDiff = hovered.MageWeaponPenalty - equipped.MageWeaponPenalty;
        if (penaltyDiff != 0)
        {
            EquipmentStatDelta d;
            d.Label = L"Mage Weapon";
            d.Delta = static_cast<double>(penaltyDiff);
            d.LowerIsBetter = true;
            d.DetailText = L"-" + FormatInt(hovered.MageWeaponPenalty, false) + L" vs -" +
                           FormatInt(equipped.MageWeaponPenalty, false);
            deltas.push_back(std::move(d));
        }
    }
    else if (hovered.HasMageWeapon != equipped.HasMageWeapon)
    {
        EquipmentStatDelta d;
        d.Label = L"Mage Weapon";
        d.Delta = hovered.HasMageWeapon ? 1.0 : -1.0;
        d.IsBooleanFlag = true;
        const int pen = hovered.HasMageWeapon ? hovered.MageWeaponPenalty : equipped.MageWeaponPenalty;
        d.DetailText = L"(-" + FormatInt(pen, false) + L" skill)";
        deltas.push_back(std::move(d));
    }

    AddIntDelta(deltas, L"Life Leech", hovered.HitLifeLeech, equipped.HitLifeLeech, true);
    AddIntDelta(deltas, L"Mana Leech", hovered.HitManaLeech, equipped.HitManaLeech, true);
    AddIntDelta(deltas, L"Stam Leech", hovered.HitStamLeech, equipped.HitStamLeech, true);
    AddIntDelta(deltas, L"Hit Lightning", hovered.HitLightning, equipped.HitLightning, true);
    AddIntDelta(deltas, L"Hit Fireball", hovered.HitFireball, equipped.HitFireball, true);
    AddIntDelta(deltas, L"Hit Harm", hovered.HitHarm, equipped.HitHarm, true);
    AddIntDelta(deltas, L"Hit Magic Arrow", hovered.HitMagicArrow, equipped.HitMagicArrow, true);
    AddIntDelta(deltas, L"Hit Lower Def", hovered.HitLowerDefense, equipped.HitLowerDefense, true);
    AddIntDelta(deltas, L"Hit Lower Atk", hovered.HitLowerAttack, equipped.HitLowerAttack, true);
    AddIntDelta(deltas, L"Hit Dispel", hovered.HitDispel, equipped.HitDispel, true);
    AddIntDelta(deltas, L"Hit Cold Area", hovered.HitColdArea, equipped.HitColdArea, true);
    AddIntDelta(deltas, L"Hit Energy Area", hovered.HitEnergyArea, equipped.HitEnergyArea, true);
    AddIntDelta(deltas, L"Hit Fire Area", hovered.HitFireArea, equipped.HitFireArea, true);
    AddIntDelta(deltas, L"Hit Phys Area", hovered.HitPhysicalArea, equipped.HitPhysicalArea, true);
    AddIntDelta(deltas, L"Hit Poison Area", hovered.HitPoisonArea, equipped.HitPoisonArea, true);
    AddIntDelta(deltas, L"Self Repair", hovered.SelfRepair, equipped.SelfRepair, false);

    // Resists
    AddIntDelta(deltas, L"Total Resists", hovered.TotalResists(), equipped.TotalResists(), true);
    AddIntDelta(deltas, L"Phys Resist", hovered.PhysicalResist, equipped.PhysicalResist, true);
    AddIntDelta(deltas, L"Fire Resist", hovered.FireResist, equipped.FireResist, true);
    AddIntDelta(deltas, L"Cold Resist", hovered.ColdResist, equipped.ColdResist, true);
    AddIntDelta(deltas, L"Poison Resist", hovered.PoisonResist, equipped.PoisonResist, true);
    AddIntDelta(deltas, L"Energy Resist", hovered.EnergyResist, equipped.EnergyResist, true);

    // Skills, Slayers, Talisman, Quiver, Level
    const double hoveredSkills = hovered.TotalSkillBonuses();
    const double equippedSkills = equipped.TotalSkillBonuses();
    if (std::fabs(hoveredSkills - equippedSkills) >= 0.05)
    {
        EquipmentStatDelta d;
        d.Label = L"Skill Bonuses";
        d.Delta = hoveredSkills - equippedSkills;
        d.IsFloat = (std::fabs(d.Delta - std::round(d.Delta)) >= 0.05);
        deltas.push_back(std::move(d));
    }

    AddIntDelta(deltas, L"Slayers", hovered.SlayerCount(), equipped.SlayerCount(), false);
    AddIntDelta(deltas, L"Quiver Dmg", hovered.QuiverDamageMod, equipped.QuiverDamageMod, true);
    AddIntDelta(
        deltas, L"Talisman Killer", hovered.TalismanKiller, equipped.TalismanKiller, true);
    AddIntDelta(
        deltas, L"Talisman Prot", hovered.TalismanProtection, equipped.TalismanProtection, true);
    AddIntDelta(
        deltas,
        L"Talisman Excep",
        hovered.TalismanExceptional,
        equipped.TalismanExceptional,
        true);
    AddIntDelta(
        deltas, L"Talisman Succ", hovered.TalismanSuccess, equipped.TalismanSuccess, true);

    if (hovered.WeaponLevel > 0 || equipped.WeaponLevel > 0)
    {
        AddIntDelta(deltas, L"Weapon Level", hovered.WeaponLevel, equipped.WeaponLevel, false);
    }

    // Informational differences
    if (hovered.StrengthReq != equipped.StrengthReq)
    {
        AddIntDelta(
            deltas,
            L"Str Req",
            hovered.StrengthReq,
            equipped.StrengthReq,
            false,
            false /*LowerIsBetter*/,
            true /*IsInformational*/);
    }

    if (hovered.HasDurability && equipped.HasDurability &&
        hovered.DurabilityMax != equipped.DurabilityMax)
    {
        AddIntDelta(
            deltas,
            L"Max Durability",
            hovered.DurabilityMax,
            equipped.DurabilityMax,
            false,
            false,
            true /*IsInformational*/);
    }

    if ((!hovered.MaterialGrade.empty() || !equipped.MaterialGrade.empty()) &&
        hovered.MaterialGrade != equipped.MaterialGrade)
    {
        EquipmentStatDelta d;
        d.Label = L"Material";
        d.IsInformational = true;
        const wstr_t hMat = hovered.MaterialGrade.empty()
                                ? L"None"
                                : SanitizeAsciiLabel(hovered.MaterialGrade);
        const wstr_t eMat = equipped.MaterialGrade.empty()
                                ? L"None"
                                : SanitizeAsciiLabel(equipped.MaterialGrade);
        d.DetailText = hMat + L" (was " + eMat + L")";
        deltas.push_back(std::move(d));
    }
}

ParsedEquipmentStats CombineStatsForComparison(
    const ParsedEquipmentStats &eqA,
    const ParsedEquipmentStats &eqB)
{
    ParsedEquipmentStats sum;

    sum.HasWeaponDamage = eqA.HasWeaponDamage || eqB.HasWeaponDamage;
    if (eqA.HasWeaponDamage && eqB.HasWeaponDamage)
    {
        sum.MinDamage = eqA.MinDamage + eqB.MinDamage;
        sum.MaxDamage = eqA.MaxDamage + eqB.MaxDamage;
    }
    else if (eqA.HasWeaponDamage)
    {
        sum.MinDamage = eqA.MinDamage;
        sum.MaxDamage = eqA.MaxDamage;
    }
    else if (eqB.HasWeaponDamage)
    {
        sum.MinDamage = eqB.MinDamage;
        sum.MaxDamage = eqB.MaxDamage;
    }

    sum.Balanced = eqA.Balanced || eqB.Balanced;
    sum.Velocity = eqA.Velocity + eqB.Velocity;

    // Shared AosAttributes sum across both equipped hands
    sum.DamageIncrease = eqA.DamageIncrease + eqB.DamageIncrease;
    sum.SwingSpeedIncrease = eqA.SwingSpeedIncrease + eqB.SwingSpeedIncrease;
    sum.HitChanceIncrease = eqA.HitChanceIncrease + eqB.HitChanceIncrease;
    sum.DefenseChanceIncrease = eqA.DefenseChanceIncrease + eqB.DefenseChanceIncrease;
    sum.SpellChanneling = eqA.SpellChanneling || eqB.SpellChanneling;
    sum.SpellDamageIncrease = eqA.SpellDamageIncrease + eqB.SpellDamageIncrease;
    sum.LowerManaCost = eqA.LowerManaCost + eqB.LowerManaCost;
    sum.LowerReagentCost = eqA.LowerReagentCost + eqB.LowerReagentCost;
    sum.FasterCasting = eqA.FasterCasting + eqB.FasterCasting;
    sum.FasterCastRecovery = eqA.FasterCastRecovery + eqB.FasterCastRecovery;
    sum.BonusStr = eqA.BonusStr + eqB.BonusStr;
    sum.BonusDex = eqA.BonusDex + eqB.BonusDex;
    sum.BonusInt = eqA.BonusInt + eqB.BonusInt;
    sum.BonusHits = eqA.BonusHits + eqB.BonusHits;
    sum.BonusMana = eqA.BonusMana + eqB.BonusMana;
    sum.BonusStam = eqA.BonusStam + eqB.BonusStam;
    sum.RegenHits = eqA.RegenHits + eqB.RegenHits;
    sum.RegenMana = eqA.RegenMana + eqB.RegenMana;
    sum.RegenStam = eqA.RegenStam + eqB.RegenStam;
    sum.Luck = eqA.Luck + eqB.Luck;
    sum.ReflectPhysical = eqA.ReflectPhysical + eqB.ReflectPhysical;
    sum.EnhancePotions = eqA.EnhancePotions + eqB.EnhancePotions;
    sum.NightSight = eqA.NightSight || eqB.NightSight;
    sum.IncreasedKarmaLoss = eqA.IncreasedKarmaLoss + eqB.IncreasedKarmaLoss;

    sum.UseBestSkill = eqA.UseBestSkill || eqB.UseBestSkill;
    if (eqA.HasMageWeapon && eqB.HasMageWeapon)
    {
        sum.HasMageWeapon = true;
        sum.MageWeaponPenalty = std::min(eqA.MageWeaponPenalty, eqB.MageWeaponPenalty);
    }
    else if (eqA.HasMageWeapon)
    {
        sum.HasMageWeapon = true;
        sum.MageWeaponPenalty = eqA.MageWeaponPenalty;
    }
    else if (eqB.HasMageWeapon)
    {
        sum.HasMageWeapon = true;
        sum.MageWeaponPenalty = eqB.MageWeaponPenalty;
    }

    sum.HitLifeLeech = eqA.HitLifeLeech + eqB.HitLifeLeech;
    sum.HitManaLeech = eqA.HitManaLeech + eqB.HitManaLeech;
    sum.HitStamLeech = eqA.HitStamLeech + eqB.HitStamLeech;
    sum.HitLightning = eqA.HitLightning + eqB.HitLightning;
    sum.HitFireball = eqA.HitFireball + eqB.HitFireball;
    sum.HitHarm = eqA.HitHarm + eqB.HitHarm;
    sum.HitMagicArrow = eqA.HitMagicArrow + eqB.HitMagicArrow;
    sum.HitLowerAttack = eqA.HitLowerAttack + eqB.HitLowerAttack;
    sum.HitLowerDefense = eqA.HitLowerDefense + eqB.HitLowerDefense;
    sum.HitDispel = eqA.HitDispel + eqB.HitDispel;
    sum.HitColdArea = eqA.HitColdArea + eqB.HitColdArea;
    sum.HitEnergyArea = eqA.HitEnergyArea + eqB.HitEnergyArea;
    sum.HitFireArea = eqA.HitFireArea + eqB.HitFireArea;
    sum.HitPhysicalArea = eqA.HitPhysicalArea + eqB.HitPhysicalArea;
    sum.HitPoisonArea = eqA.HitPoisonArea + eqB.HitPoisonArea;
    sum.SelfRepair = eqA.SelfRepair + eqB.SelfRepair;
    sum.MageArmor = eqA.MageArmor || eqB.MageArmor;

    sum.PhysicalResist = eqA.PhysicalResist + eqB.PhysicalResist;
    sum.FireResist = eqA.FireResist + eqB.FireResist;
    sum.ColdResist = eqA.ColdResist + eqB.ColdResist;
    sum.PoisonResist = eqA.PoisonResist + eqB.PoisonResist;
    sum.EnergyResist = eqA.EnergyResist + eqB.EnergyResist;

    sum.SkillBonuses = eqA.SkillBonuses;
    sum.SkillBonuses.insert(
        sum.SkillBonuses.end(), eqB.SkillBonuses.begin(), eqB.SkillBonuses.end());

    sum.SlayerClilocs = eqA.SlayerClilocs;
    for (uint32_t slayer : eqB.SlayerClilocs)
    {
        if (std::find(sum.SlayerClilocs.begin(), sum.SlayerClilocs.end(), slayer) ==
            sum.SlayerClilocs.end())
        {
            sum.SlayerClilocs.push_back(slayer);
        }
    }

    sum.QuiverDamageMod = eqA.QuiverDamageMod + eqB.QuiverDamageMod;
    sum.TalismanKiller = eqA.TalismanKiller + eqB.TalismanKiller;
    sum.TalismanProtection = eqA.TalismanProtection + eqB.TalismanProtection;
    sum.TalismanExceptional = eqA.TalismanExceptional + eqB.TalismanExceptional;
    sum.TalismanSuccess = eqA.TalismanSuccess + eqB.TalismanSuccess;
    sum.WeaponLevel = std::max(eqA.WeaponLevel, eqB.WeaponLevel);

    return sum;
}

uint32_t BorderColorForVerdict(UpgradeVerdict verdict)
{
    switch (verdict)
    {
        case UpgradeVerdict::Upgrade:
            return kBorderUpgradeARGB;
        case UpgradeVerdict::Downgrade:
            return kBorderDowngradeARGB;
        case UpgradeVerdict::Sidegrade:
        default:
            return kBorderNeutralARGB;
    }
}

wstr_t FormatVerdictBanner(UpgradeVerdict verdict, double scoreDeltaPct, bool hasBaseline)
{
    if (verdict == UpgradeVerdict::Upgrade)
    {
        if (!hasBaseline)
        {
            return wstr_t(kColorGreen) + L"^ UPGRADE (Empty Slot)" + kColorWhite;
        }
        return wstr_t(kColorGreen) + L"^ UPGRADE (" + FormatFloat1(scoreDeltaPct, true) + L"%)" +
               kColorWhite;
    }
    if (verdict == UpgradeVerdict::Downgrade)
    {
        if (!hasBaseline)
        {
            return wstr_t(kColorRed) + L"v DOWNGRADE (Empty Slot)" + kColorWhite;
        }
        return wstr_t(kColorRed) + L"v DOWNGRADE (" + FormatFloat1(scoreDeltaPct, true) + L"%)" +
               kColorWhite;
    }
    if (!hasBaseline)
    {
        return wstr_t(kColorGold) + L"= SIDEGRADE (Empty Slot)" + kColorWhite;
    }
    return wstr_t(kColorGold) + L"= SIDEGRADE (" + FormatFloat1(scoreDeltaPct, true) + L"%)" +
           kColorWhite;
}

EquipCategory InferEquippedCategory(
    const ParsedEquipmentStats &equipped,
    EquipCategory fallbackCategory)
{
    if (equipped.HasWeaponDamage)
    {
        return EquipCategory::Weapon;
    }

    if (fallbackCategory == EquipCategory::Weapon)
    {
        const bool looksLikeArmorOrShield = equipped.TotalResists() > 0 || equipped.MageArmor ||
                                            equipped.HasDurability ||
                                            equipped.DefenseChanceIncrease > 0 ||
                                            equipped.ReflectPhysical > 0;
        const bool looksLikeUtility =
            equipped.SpellDamageIncrease > 0 || equipped.FasterCasting > 0 ||
            equipped.FasterCastRecovery > 0 || equipped.LowerReagentCost > 0 ||
            equipped.TalismanKiller > 0 || equipped.TalismanProtection > 0 ||
            equipped.QuiverDamageMod > 0;
        if (looksLikeUtility && !looksLikeArmorOrShield)
        {
            return EquipCategory::JewelryOrUtility;
        }
        if (looksLikeArmorOrShield)
        {
            return EquipCategory::ArmorOrShield;
        }
        return EquipCategory::JewelryOrUtility;
    }

    return fallbackCategory;
}

double ComputeScoreDeltaPct(double scoreDelta, double equippedScore)
{
    if (std::fabs(equippedScore) <= 0.001)
    {
        if (scoreDelta > 0.05)
        {
            return 100.0;
        }
        if (scoreDelta < -0.05)
        {
            return -100.0;
        }
        return 0.0;
    }

    const double denom = std::max(1.0, std::fabs(equippedScore));
    return (scoreDelta / denom) * 100.0;
}

wstr_t FormatDeltaLine(const EquipmentStatDelta &d)
{
    const wchar_t *color = kColorGray;
    if (!d.IsInformational)
    {
        if (IsDeltaGain(d))
        {
            color = kColorGreen;
        }
        else if (IsDeltaLoss(d))
        {
            color = kColorRed;
        }
    }

    wstr_t line = color;

    if (d.IsInformational)
    {
        if (!d.DetailText.empty())
        {
            line += L"* " + d.Label + L": " + SanitizeAsciiLabel(d.DetailText);
        }
        else
        {
            const wstr_t num = d.IsFloat
                                   ? FormatFloatTrimmed(d.Delta, true)
                                   : FormatInt(static_cast<int>(std::round(d.Delta)), true);
            line += L"* " + num + (d.IsPercent ? L"% " : L" ") + d.Label;
        }
        line += kColorWhite;
        return line;
    }

    if (d.IsBooleanFlag)
    {
        line += (d.Delta > 0.0 ? L"+ " : L"- ") + d.Label;
        if (!d.DetailText.empty())
        {
            line += L" " + SanitizeAsciiLabel(d.DetailText);
        }
        line += kColorWhite;
        return line;
    }

    if (!d.DetailText.empty() && d.Label == L"Mage Weapon")
    {
        line += (IsDeltaGain(d) ? L"+ " : L"- ") + d.Label + L": " +
                SanitizeAsciiLabel(d.DetailText);
        line += kColorWhite;
        return line;
    }

    const wstr_t num = d.IsFloat ? FormatFloatTrimmed(d.Delta, true)
                                 : FormatInt(static_cast<int>(std::round(d.Delta)), true);
    line += num;
    if (d.IsPercent)
    {
        line += L"%";
    }
    else if (!d.DetailText.empty() && d.Label == L"Speed")
    {
        line += SanitizeAsciiLabel(d.DetailText);
    }
    line += L" " + d.Label;
    line += kColorWhite;
    return line;
}

void AppendComparisonBlock(
    wstr_t &html,
    const EquipmentComparisonEntry &entry,
    size_t maxDeltaLines,
    bool showTargetHeader,
    const wstr_t &extraNote)
{
    if (showTargetHeader && !entry.TargetLabel.empty())
    {
        html += wstr_t(kColorBlue) + L"vs " + SanitizeAsciiLabel(entry.TargetLabel) + kColorWhite +
                L"\n";
    }

    html += FormatVerdictBanner(entry.Verdict, entry.ScoreDeltaPct, true) + L"\n";

    if (!extraNote.empty())
    {
        html += wstr_t(kColorGold) + L"* " + SanitizeAsciiLabel(extraNote) + kColorWhite + L"\n";
    }

    const wchar_t *scoreColor =
        entry.ScoreDelta > 0.05 ? kColorGreen : (entry.ScoreDelta < -0.05 ? kColorRed : kColorGray);
    html += wstr_t(kColorGray) + L"Score: " + kColorWhite +
            FormatFloat1(entry.HoveredScore, false) + L" " + scoreColor + L"(" +
            FormatFloat1(entry.ScoreDelta, true) + L")" + kColorWhite + L"\n";

    if (entry.Deltas.empty())
    {
        html += wstr_t(kColorGray) + L"= Identical stats" + kColorWhite + L"\n";
        return;
    }

    const size_t limit = std::min(maxDeltaLines, entry.Deltas.size());
    for (size_t i = 0; i < limit; ++i)
    {
        html += FormatDeltaLine(entry.Deltas[i]) + L"\n";
    }
    if (entry.Deltas.size() > limit)
    {
        const int remaining = static_cast<int>(entry.Deltas.size() - limit);
        html += wstr_t(kColorGray) + L"... +" + FormatInt(remaining, false) + L" more" +
                kColorWhite + L"\n";
    }
}

void AppendEmptySlotSummaryLines(
    wstr_t &html,
    const ParsedEquipmentStats &stats,
    EquipCategory category)
{
    auto addStat = [&](const wchar_t *label, const wstr_t &val) {
        html += wstr_t(kColorGreen) + L"+ " + val + L" " + label + kColorWhite + L"\n";
    };

    if (stats.HasWeaponDamage)
    {
        if (stats.EffectiveDps() > 0.0)
        {
            addStat(L"Eff DPS", FormatFloat1(stats.EffectiveDps(), false));
        }
        if (stats.BaseDps() > 0.0)
        {
            addStat(L"Base DPS", FormatFloat1(stats.BaseDps(), false));
        }
    }
    if (stats.TotalResists() > 0)
    {
        addStat(L"Total Resists", FormatInt(stats.TotalResists(), false) + L"%");
    }
    if (stats.DamageIncrease > 0)
    {
        addStat(L"Damage Inc", FormatInt(stats.DamageIncrease, false) + L"%");
    }
    if (stats.SwingSpeedIncrease > 0)
    {
        addStat(L"Swing Speed", FormatInt(stats.SwingSpeedIncrease, false) + L"%");
    }
    if (stats.HitChanceIncrease > 0)
    {
        addStat(L"Hit Chance", FormatInt(stats.HitChanceIncrease, false) + L"%");
    }
    if (stats.DefenseChanceIncrease > 0)
    {
        addStat(L"Def Chance", FormatInt(stats.DefenseChanceIncrease, false) + L"%");
    }
    if (stats.SpellDamageIncrease > 0)
    {
        addStat(L"Spell Damage", FormatInt(stats.SpellDamageIncrease, false) + L"%");
    }
    if (stats.LowerManaCost > 0)
    {
        addStat(L"Lower Mana", FormatInt(stats.LowerManaCost, false) + L"%");
    }
    if (stats.FasterCasting > 0)
    {
        addStat(L"Faster Cast", FormatInt(stats.FasterCasting, false));
    }
    if (stats.FasterCastRecovery > 0)
    {
        addStat(L"Cast Recovery", FormatInt(stats.FasterCastRecovery, false));
    }
    if (stats.SpellChanneling)
    {
        html += wstr_t(kColorGreen) + L"+ Spell Channeling" + kColorWhite + L"\n";
    }
    if (stats.TotalSkillBonuses() > 0.0)
    {
        addStat(L"Skill Bonuses", FormatFloatTrimmed(stats.TotalSkillBonuses(), false));
    }
    if (stats.Luck > 0)
    {
        addStat(L"Luck", FormatInt(stats.Luck, false));
    }
    if (stats.WeaponLevel > 0)
    {
        addStat(L"Weapon Level", FormatInt(stats.WeaponLevel, false));
    }
    if (stats.IncreasedKarmaLoss > 0)
    {
        html += wstr_t(kColorRed) + L"+" + FormatInt(stats.IncreasedKarmaLoss, false) +
                L"% Karma Loss" + kColorWhite + L"\n";
    }
    if (!stats.MaterialGrade.empty())
    {
        html += wstr_t(kColorGray) + L"* Material: " + SanitizeAsciiLabel(stats.MaterialGrade) +
                kColorWhite + L"\n";
    }
    (void)category;
}

void TrimTrailingNewline(wstr_t &s)
{
    while (!s.empty() && s.back() == L'\n')
    {
        s.pop_back();
    }
}

wstr_t DefaultSlotLabelForItem(const EquipmentCompareItemInput &item)
{
    if (!item.SlotLabel.empty())
    {
        return SanitizeAsciiLabel(item.SlotLabel);
    }
    if (item.IsOffHandWeapon)
    {
        return L"Off Hand";
    }
    switch (item.Layer)
    {
        case OL_1_HAND:
            return L"Main Hand";
        case OL_2_HAND:
            return item.IsWeapon ? L"Two-Handed" : L"Off Hand";
        case OL_HELMET:
            return L"Helmet";
        case OL_NECKLACE:
            return L"Necklace";
        case OL_TORSO:
        case OL_TUNIC:
            return L"Chest";
        case OL_ARMS:
            return L"Arms";
        case OL_GLOVES:
            return L"Gloves";
        case OL_LEGS:
        case OL_PANTS:
            return L"Legs";
        case OL_SHOES:
            return L"Shoes";
        case OL_CLOAK:
            return L"Cloak";
        case OL_ROBE:
            return L"Robe";
        case OL_TALISMAN:
            return L"Talisman";
        case OL_BRACELET:
            return L"Left Bracelet";
        case OL_BRACELET_2:
            return L"Right Bracelet";
        case OL_EARRINGS:
            return L"Left Earring";
        case OL_EARRINGS_2:
            return L"Right Earring";
        case OL_RING:
        case OL_RING_2:
        case OL_RING_3:
        case OL_RING_4:
        case OL_RING_5:
        case OL_RING_6:
            return L"Ring";
        default:
            return L"Equipped";
    }
}

ComparisonCardOutput BuildEquippedSideCard(const EquipmentCompareItemInput &eq)
{
    ComparisonCardOutput card;
    card.AlignLeft = false;
    card.MaxWidth = 270;
    card.BorderColorARGB = kBorderEquippedARGB;

    const wstr_t slotLabel = DefaultSlotLabelForItem(eq);
    card.HtmlText =
        wstr_t(kColorGold) + L"[EQUIPPED: " + slotLabel + L"]" + kColorWhite + L"\n";

    if (!eq.PropertiesLoaded)
    {
        card.HtmlText += wstr_t(kColorGray) + L"Loading equipped stats..." + kColorWhite;
    }
    else if (!eq.FormattedTooltip.empty())
    {
        card.HtmlText += eq.FormattedTooltip;
    }
    else if (!eq.Name.empty())
    {
        card.HtmlText += eq.Name;
    }
    else
    {
        card.HtmlText += wstr_t(kColorGray) + L"(No properties)" + kColorWhite;
    }

    return card;
}
} // namespace

double ParsedEquipmentStats::AverageDamage() const
{
    if (!HasWeaponDamage)
    {
        return 0.0;
    }
    return (static_cast<double>(MinDamage) + static_cast<double>(MaxDamage)) * 0.5;
}

double ParsedEquipmentStats::BaseSeconds() const
{
    if (!HasWeaponSpeed || WeaponSpeed <= 0.0)
    {
        return 0.0;
    }
    if (SpeedIsMlSeconds)
    {
        return WeaponSpeed;
    }
    return std::max(1.25, 400.0 / std::max(1.0, WeaponSpeed));
}

double ParsedEquipmentStats::EffectiveSeconds() const
{
    const double baseSec = BaseSeconds();
    if (baseSec <= 0.0)
    {
        return 0.0;
    }
    const double ssiClamped =
        std::max(-50.0, std::min(60.0, static_cast<double>(SwingSpeedIncrease)));
    return std::max(1.25, baseSec * (100.0 / (100.0 + ssiClamped)));
}

double ParsedEquipmentStats::BaseDps() const
{
    if (!HasWeaponDamage)
    {
        return 0.0;
    }
    const double baseSec = BaseSeconds();
    if (baseSec <= 0.0)
    {
        return 0.0;
    }
    return AverageDamage() / baseSec;
}

double ParsedEquipmentStats::EffectiveDps() const
{
    if (!HasWeaponDamage)
    {
        return 0.0;
    }
    const double effSec = EffectiveSeconds();
    if (effSec <= 0.0)
    {
        return 0.0;
    }
    const double diMult = std::max(0.0, 1.0 + static_cast<double>(DamageIncrease) / 100.0);
    return (AverageDamage() * diMult) / effSec;
}

int ParsedEquipmentStats::TotalResists() const
{
    return PhysicalResist + FireResist + ColdResist + PoisonResist + EnergyResist;
}

double ParsedEquipmentStats::TotalSkillBonuses() const
{
    double total = 0.0;
    for (const auto &entry : SkillBonuses)
    {
        total += entry.Value;
    }
    return total;
}

int ParsedEquipmentStats::TotalHitArea() const
{
    return HitColdArea + HitEnergyArea + HitFireArea + HitPhysicalArea + HitPoisonArea;
}

int ParsedEquipmentStats::SlayerCount() const
{
    return static_cast<int>(SlayerClilocs.size());
}

ParsedEquipmentStats ParseEquipmentOpl(const OplPropertyList &props)
{
    ParsedEquipmentStats stats;

    for (const auto &entry : props)
    {
        const uint32_t cliloc = entry.first;
        const wstr_t &arg = entry.second;
        int intVal = 0;

        switch (cliloc)
        {
            // Weapon core & ranged
            case 1061168u:
            {
                const auto parts = SplitTabs(arg);
                int minD = 0;
                int maxD = 0;
                if (parts.size() >= 2 && ParseIntSafe(parts[0], minD) &&
                    ParseIntSafe(parts[1], maxD))
                {
                    stats.HasWeaponDamage = true;
                    stats.MinDamage = minD;
                    stats.MaxDamage = maxD;
                }
                break;
            }
            case 1061167u:
            {
                wstr_t trimmed = TrimWide(arg);
                bool isMl = false;
                if (!trimmed.empty() && (trimmed.back() == L's' || trimmed.back() == L'S'))
                {
                    isMl = true;
                    trimmed.pop_back();
                }
                double spd = 0.0;
                if (ParseDoubleSafe(trimmed, spd) && spd > 0.0)
                {
                    stats.HasWeaponSpeed = true;
                    stats.SpeedIsMlSeconds = isMl;
                    stats.WeaponSpeed = spd;
                }
                break;
            }
            case 1061171u:
                stats.IsTwoHanded = true;
                break;
            case 1061824u:
                stats.IsOneHanded = true;
                break;
            case 1061169u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.MaxRange = intVal;
                }
                break;
            case 1149791u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.MinThrowRange = intVal;
                }
                break;
            case 1061172u:
            case 1061173u:
            case 1061174u:
            case 1061175u:
            case 1112075u:
                stats.WeaponSkillCliloc = cliloc;
                break;
            case 1060403u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.PhysDamagePct = intVal;
                }
                break;
            case 1060404u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.ColdDamagePct = intVal;
                }
                break;
            case 1060405u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.FireDamagePct = intVal;
                }
                break;
            case 1060406u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.PoisonDamagePct = intVal;
                }
                break;
            case 1060407u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.EnergyDamagePct = intVal;
                }
                break;
            case 1072846u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.ChaosDamagePct = intVal;
                }
                break;
            case 1079978u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.DirectDamagePct = intVal;
                }
                break;
            case 1072792u:
                stats.Balanced = true;
                break;
            case 1072793u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.Velocity = intVal;
                }
                break;
            case 1062412u:
            case 1062413u:
            case 1062414u:
            case 1062415u:
            case 1062416u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.PoisonCharges = intVal;
                }
                break;

            // Shared AosAttributes
            case 1060401u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.DamageIncrease = intVal;
                }
                break;
            case 1060486u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.SwingSpeedIncrease = intVal;
                }
                break;
            case 1060415u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitChanceIncrease = intVal;
                }
                break;
            case 1060408u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.DefenseChanceIncrease = intVal;
                }
                break;
            case 1060482u:
                stats.SpellChanneling = true;
                break;
            case 1060483u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.SpellDamageIncrease = intVal;
                }
                break;
            case 1060433u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.LowerManaCost = intVal;
                }
                break;
            case 1060434u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.LowerReagentCost = intVal;
                }
                break;
            case 1060413u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.FasterCasting = intVal;
                }
                break;
            case 1060412u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.FasterCastRecovery = intVal;
                }
                break;
            case 1060485u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.BonusStr = intVal;
                }
                break;
            case 1060409u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.BonusDex = intVal;
                }
                break;
            case 1060432u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.BonusInt = intVal;
                }
                break;
            case 1060431u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.BonusHits = intVal;
                }
                break;
            case 1060439u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.BonusMana = intVal;
                }
                break;
            case 1060484u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.BonusStam = intVal;
                }
                break;
            case 1060444u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.RegenHits = intVal;
                }
                break;
            case 1060440u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.RegenMana = intVal;
                }
                break;
            case 1060443u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.RegenStam = intVal;
                }
                break;
            case 1060436u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.Luck = intVal;
                }
                break;
            case 1060442u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.ReflectPhysical = intVal;
                }
                break;
            case 1060411u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.EnhancePotions = intVal;
                }
                break;
            case 1060441u:
                stats.NightSight = true;
                break;
            case 1075210u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.IncreasedKarmaLoss = intVal;
                }
                break;

            // Weapon & Armor attributes
            case 1060400u:
                stats.UseBestSkill = true;
                break;
            case 1060438u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HasMageWeapon = true;
                    stats.MageWeaponPenalty = std::abs(intVal);
                }
                break;
            case 1060422u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitLifeLeech = intVal;
                }
                break;
            case 1060427u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitManaLeech = intVal;
                }
                break;
            case 1060430u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitStamLeech = intVal;
                }
                break;
            case 1060423u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitLightning = intVal;
                }
                break;
            case 1060420u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitFireball = intVal;
                }
                break;
            case 1060421u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitHarm = intVal;
                }
                break;
            case 1060426u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitMagicArrow = intVal;
                }
                break;
            case 1060424u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitLowerAttack = intVal;
                }
                break;
            case 1060425u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitLowerDefense = intVal;
                }
                break;
            case 1060417u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitDispel = intVal;
                }
                break;
            case 1060416u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitColdArea = intVal;
                }
                break;
            case 1060418u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitEnergyArea = intVal;
                }
                break;
            case 1060419u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitFireArea = intVal;
                }
                break;
            case 1060428u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitPhysicalArea = intVal;
                }
                break;
            case 1060429u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.HitPoisonArea = intVal;
                }
                break;
            case 1060450u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.SelfRepair = intVal;
                }
                break;
            case 1060437u:
                stats.MageArmor = true;
                break;

            // Resists
            case 1060448u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.PhysicalResist = intVal;
                }
                break;
            case 1060447u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.FireResist = intVal;
                }
                break;
            case 1060445u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.ColdResist = intVal;
                }
                break;
            case 1060449u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.PoisonResist = intVal;
                }
                break;
            case 1060446u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.EnergyResist = intVal;
                }
                break;

            // Skill bonuses
            case 1060451u:
            case 1060452u:
            case 1060453u:
            case 1060454u:
            case 1060455u:
            {
                const auto parts = SplitTabs(arg);
                uint32_t skillCliloc = 0;
                double bonusVal = 0.0;
                if (parts.size() >= 2 && ParseUint32Safe(parts[0], skillCliloc) &&
                    ParseDoubleSafe(parts[1], bonusVal))
                {
                    EquipmentSkillBonus bonus;
                    bonus.SkillCliloc = skillCliloc;
                    bonus.Value = bonusVal;
                    stats.SkillBonuses.push_back(bonus);
                }
                break;
            }

            // Durability, requirements, rarity, quiver, talisman
            case 1060639u:
            {
                const auto parts = SplitTabs(arg);
                int curD = 0;
                int maxD = 0;
                if (parts.size() >= 2 && ParseIntSafe(parts[0], curD) &&
                    ParseIntSafe(parts[1], maxD))
                {
                    stats.HasDurability = true;
                    stats.DurabilityCur = curD;
                    stats.DurabilityMax = maxD;
                }
                break;
            }
            case 1060410u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.DurabilityBonus = intVal;
                }
                break;
            case 1060435u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.LowerStatReq = intVal;
                }
                break;
            case 1061170u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.StrengthReq = intVal;
                }
                break;
            case 1061078u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.ArtifactRarity = intVal;
                }
                break;
            case 1074762u:
                if (ParseIntSafe(arg, intVal))
                {
                    stats.QuiverDamageMod = intVal;
                }
                break;
            case 1072388u:
            {
                const auto parts = SplitTabs(arg);
                const wstr_t &valPart = parts.size() >= 2 ? parts[1] : arg;
                if (ParseIntSafe(valPart, intVal))
                {
                    stats.TalismanKiller = intVal;
                }
                break;
            }
            case 1072387u:
            {
                const auto parts = SplitTabs(arg);
                const wstr_t &valPart = parts.size() >= 2 ? parts[1] : arg;
                if (ParseIntSafe(valPart, intVal))
                {
                    stats.TalismanProtection = intVal;
                }
                break;
            }
            case 1072395u:
            {
                const auto parts = SplitTabs(arg);
                const wstr_t &valPart = parts.size() >= 2 ? parts[1] : arg;
                if (ParseIntSafe(valPart, intVal))
                {
                    stats.TalismanExceptional = intVal;
                }
                break;
            }
            case 1072394u:
            {
                const auto parts = SplitTabs(arg);
                const wstr_t &valPart = parts.size() >= 2 ? parts[1] : arg;
                if (ParseIntSafe(valPart, intVal))
                {
                    stats.TalismanSuccess = intVal;
                }
                break;
            }

            default:
            {
                if (IsSlayerCliloc(cliloc))
                {
                    if (std::find(
                            stats.SlayerClilocs.begin(), stats.SlayerClilocs.end(), cliloc) ==
                        stats.SlayerClilocs.end())
                    {
                        stats.SlayerClilocs.push_back(cliloc);
                    }
                }
                else if (IsPassthroughCliloc(cliloc))
                {
                    const auto parts = SplitTabs(arg);
                    if (parts.size() >= 2)
                    {
                        const wstr_t key = TrimWide(parts[0]);
                        const wstr_t val = TrimWide(parts[1]);
                        if (EqualsIgnoreCaseAscii(key, L"Material") && !val.empty())
                        {
                            stats.MaterialGrade = val;
                        }
                        else if (EqualsIgnoreCaseAscii(key, L"Weapon level"))
                        {
                            int lvl = 0;
                            if (ParseIntSafe(val, lvl) && lvl >= 0)
                            {
                                stats.WeaponLevel = lvl;
                            }
                        }
                    }
                }
                break;
            }
        }
    }

    return stats;
}

EquipCategory ClassifyEquipmentCategory(
    const ParsedEquipmentStats &stats,
    int tileLayer,
    bool isWeapon)
{
    if (isWeapon || stats.HasWeaponDamage)
    {
        return EquipCategory::Weapon;
    }

    if (IsRingFamilyLayer(tileLayer) || IsBraceletFamilyLayer(tileLayer) ||
        IsEarringFamilyLayer(tileLayer) || tileLayer == OL_TALISMAN)
    {
        return EquipCategory::JewelryOrUtility;
    }

    if (tileLayer == OL_CLOAK && stats.QuiverDamageMod > 0)
    {
        return EquipCategory::JewelryOrUtility;
    }

    if (tileLayer == OL_1_HAND)
    {
        // Non-weapon in 1H slot (e.g., Spellbook)
        return EquipCategory::JewelryOrUtility;
    }

    if (tileLayer == OL_NECKLACE)
    {
        // Gorgets carry armor resists/durability; necklaces behave as jewelry
        if (stats.TotalResists() > 0 || stats.MageArmor || stats.HasDurability)
        {
            return EquipCategory::ArmorOrShield;
        }
        return EquipCategory::JewelryOrUtility;
    }

    switch (tileLayer)
    {
        case OL_2_HAND: // Shield / off-hand non-weapon
        case OL_SHOES:
        case OL_PANTS:
        case OL_SHIRT:
        case OL_HELMET:
        case OL_GLOVES:
        case OL_WAIST:
        case OL_TORSO:
        case OL_TUNIC:
        case OL_ARMS:
        case OL_CLOAK:
        case OL_ROBE:
        case OL_SKIRT:
        case OL_LEGS:
            return EquipCategory::ArmorOrShield;
        default:
            break;
    }

    if (stats.TalismanKiller > 0 || stats.TalismanProtection > 0 || stats.QuiverDamageMod > 0)
    {
        return EquipCategory::JewelryOrUtility;
    }

    if (stats.TotalResists() > 0 || stats.MageArmor)
    {
        return EquipCategory::ArmorOrShield;
    }

    return EquipCategory::Unknown;
}

double ScoreEquipment(const ParsedEquipmentStats &stats, EquipCategory category)
{
    EquipCategory effectiveCat = category;
    if (effectiveCat == EquipCategory::Unknown)
    {
        effectiveCat = stats.HasWeaponDamage ? EquipCategory::Weapon : EquipCategory::JewelryOrUtility;
    }

    double score = 0.0;
    const int statSum = stats.BonusStr + stats.BonusDex + stats.BonusInt;
    const int poolSum = stats.BonusHits + stats.BonusMana + stats.BonusStam;
    const int totalResists = stats.TotalResists();
    const double skillSum = stats.TotalSkillBonuses();

    if (effectiveCat == EquipCategory::Weapon)
    {
        score += stats.EffectiveDps() * 6.0;
        score += static_cast<double>(stats.DamageIncrease) * 0.35;
        score += static_cast<double>(stats.SwingSpeedIncrease) * 0.55;
        score += static_cast<double>(stats.HitChanceIncrease) * 1.0;
        score += static_cast<double>(stats.DefenseChanceIncrease) * 0.7;
        score += static_cast<double>(stats.Velocity) * 0.6;
        score += stats.Balanced ? 10.0 : 0.0;

        score += static_cast<double>(stats.HitLifeLeech) * 0.85;
        score += static_cast<double>(stats.HitManaLeech) * 0.85;
        score += static_cast<double>(stats.HitStamLeech) * 0.55;
        score += static_cast<double>(stats.HitLowerDefense) * 0.75;
        score += static_cast<double>(stats.HitLowerAttack) * 0.55;
        score += static_cast<double>(stats.HitLightning + stats.HitFireball) * 0.65;
        score += static_cast<double>(stats.HitHarm + stats.HitMagicArrow + stats.HitDispel) * 0.45;
        score += static_cast<double>(stats.TotalHitArea()) * 0.50;

        score += stats.SpellChanneling ? 12.0 : 0.0;
        score += stats.UseBestSkill ? 8.0 : 0.0;
        score += static_cast<double>(stats.SlayerCount()) * 12.0;
        score += static_cast<double>(stats.SpellDamageIncrease) * 0.4;
        score += static_cast<double>(stats.LowerManaCost) * 0.6;
        score += static_cast<double>(stats.LowerReagentCost) * 0.3;
        score += static_cast<double>(stats.FasterCasting) * 8.0;
        score += static_cast<double>(stats.FasterCastRecovery) * 3.0;
        score += static_cast<double>(totalResists) * 0.5;
        score += static_cast<double>(statSum) * 0.8;
        score += static_cast<double>(poolSum) * 0.6;
        score += static_cast<double>(stats.RegenHits + stats.RegenMana + stats.RegenStam) * 2.5;
        score += static_cast<double>(stats.Luck) * 0.08;
        score += static_cast<double>(stats.SelfRepair) * 1.5;
        score += skillSum * 1.2;
        score += static_cast<double>(stats.WeaponLevel) * 0.5;
        if (stats.HasMageWeapon)
        {
            score += static_cast<double>(std::max(0, 30 - stats.MageWeaponPenalty)) * 0.3;
        }
        score += static_cast<double>(stats.ReflectPhysical) * 0.3;
        score += static_cast<double>(stats.EnhancePotions) * 0.2;
    }
    else if (effectiveCat == EquipCategory::ArmorOrShield)
    {
        score += static_cast<double>(totalResists) * 1.5;
        score += static_cast<double>(stats.LowerManaCost) * 2.2;
        score += static_cast<double>(stats.LowerReagentCost) * 1.0;
        score += static_cast<double>(stats.RegenHits) * 4.5;
        score += static_cast<double>(stats.RegenMana) * 4.5;
        score += static_cast<double>(stats.RegenStam) * 3.0;
        score += static_cast<double>(stats.DefenseChanceIncrease) * 1.5;
        score += static_cast<double>(stats.HitChanceIncrease) * 1.5;
        score += static_cast<double>(stats.DamageIncrease) * 1.0;
        score += static_cast<double>(stats.SwingSpeedIncrease) * 1.8;
        score += static_cast<double>(stats.SpellDamageIncrease) * 1.4;
        score += static_cast<double>(stats.FasterCasting) * 12.0;
        score += static_cast<double>(stats.FasterCastRecovery) * 5.0;
        score += static_cast<double>(statSum) * 1.4;
        score += static_cast<double>(poolSum) * 1.2;
        score += static_cast<double>(stats.ReflectPhysical) * 0.5;
        score += static_cast<double>(stats.Luck) * 0.12;
        score += skillSum * 1.5;
        score += stats.MageArmor ? 8.0 : 0.0;
        score += stats.SpellChanneling ? 12.0 : 0.0;
        score += static_cast<double>(stats.SelfRepair) * 1.5;
        score += static_cast<double>(stats.EnhancePotions) * 0.3;
        score += stats.NightSight ? 1.0 : 0.0;
        score += static_cast<double>(stats.SlayerCount()) * 10.0;
    }
    else
    {
        // JewelryOrUtility
        score += static_cast<double>(stats.DamageIncrease) * 1.5;
        score += static_cast<double>(stats.SpellDamageIncrease) * 1.8;
        score += static_cast<double>(stats.HitChanceIncrease) * 1.8;
        score += static_cast<double>(stats.DefenseChanceIncrease) * 1.6;
        score += static_cast<double>(stats.SwingSpeedIncrease) * 2.2;
        score += static_cast<double>(stats.FasterCasting) * 14.0;
        score += static_cast<double>(stats.FasterCastRecovery) * 6.0;
        score += static_cast<double>(stats.LowerManaCost) * 2.2;
        score += static_cast<double>(stats.LowerReagentCost) * 1.1;
        score += static_cast<double>(totalResists) * 1.0;
        score += static_cast<double>(statSum) * 1.5;
        score += static_cast<double>(poolSum) * 1.2;
        score += static_cast<double>(stats.RegenHits + stats.RegenMana + stats.RegenStam) * 4.0;
        score += skillSum * 1.8;
        score += static_cast<double>(stats.Luck) * 0.12;
        score += static_cast<double>(stats.EnhancePotions) * 0.5;
        score += static_cast<double>(stats.ReflectPhysical) * 0.5;
        score += stats.SpellChanneling ? 12.0 : 0.0;
        score += static_cast<double>(stats.SlayerCount()) * 10.0;
        score += static_cast<double>(stats.TalismanKiller + stats.TalismanProtection) * 0.8;
        score += static_cast<double>(stats.TalismanExceptional + stats.TalismanSuccess) * 0.4;
        score += static_cast<double>(stats.QuiverDamageMod) * 1.5;
        score += stats.NightSight ? 1.0 : 0.0;
        score += static_cast<double>(stats.SelfRepair) * 1.0;
    }

    score -= static_cast<double>(stats.IncreasedKarmaLoss) * 0.5;
    return score;
}

UpgradeVerdict ClassifyVerdict(double scoreDelta, double equippedScore)
{
    if (std::fabs(equippedScore) <= 0.001)
    {
        if (scoreDelta > 0.05)
        {
            return UpgradeVerdict::Upgrade;
        }
        if (scoreDelta < -0.05)
        {
            return UpgradeVerdict::Downgrade;
        }
        return UpgradeVerdict::Sidegrade;
    }

    const double threshold = std::max(1.0, std::fabs(equippedScore) * 0.02);
    if (scoreDelta >= threshold)
    {
        return UpgradeVerdict::Upgrade;
    }
    if (scoreDelta <= -threshold)
    {
        return UpgradeVerdict::Downgrade;
    }
    return UpgradeVerdict::Sidegrade;
}

EquipmentComparisonEntry CompareSingleStats(
    const ParsedEquipmentStats &hovered,
    const ParsedEquipmentStats &equipped,
    EquipCategory category,
    const wstr_t &targetLabel)
{
    EquipmentComparisonEntry entry;
    entry.TargetLabel = SanitizeAsciiLabel(targetLabel);
    entry.HoveredScore = ScoreEquipment(hovered, category);

    const EquipCategory eqCategory = InferEquippedCategory(equipped, category);

    entry.EquippedScore = ScoreEquipment(equipped, eqCategory);
    entry.ScoreDelta = entry.HoveredScore - entry.EquippedScore;
    entry.ScoreDeltaPct = ComputeScoreDeltaPct(entry.ScoreDelta, entry.EquippedScore);
    entry.Verdict = ClassifyVerdict(entry.ScoreDelta, entry.EquippedScore);

    // Weapon derived & core deltas
    if (hovered.HasWeaponDamage || equipped.HasWeaponDamage)
    {
        AddFloatDelta(
            entry.Deltas, L"Eff DPS", hovered.EffectiveDps(), equipped.EffectiveDps(), false);
        AddFloatDelta(entry.Deltas, L"Base DPS", hovered.BaseDps(), equipped.BaseDps(), false);
        AddFloatDelta(
            entry.Deltas, L"Avg Dmg", hovered.AverageDamage(), equipped.AverageDamage(), false);
    }

    if (hovered.HasWeaponSpeed && equipped.HasWeaponSpeed)
    {
        if (hovered.SpeedIsMlSeconds && equipped.SpeedIsMlSeconds)
        {
            AddFloatDelta(
                entry.Deltas,
                L"Speed",
                hovered.WeaponSpeed,
                equipped.WeaponSpeed,
                false,
                true /*LowerIsBetter*/,
                0.01,
                L"s");
        }
        else if (!hovered.SpeedIsMlSeconds && !equipped.SpeedIsMlSeconds)
        {
            AddFloatDelta(
                entry.Deltas,
                L"Speed",
                hovered.WeaponSpeed,
                equipped.WeaponSpeed,
                false,
                false /*higher is faster in pre-ML*/,
                0.05);
        }
        else
        {
            AddFloatDelta(
                entry.Deltas,
                L"Speed",
                hovered.BaseSeconds(),
                equipped.BaseSeconds(),
                false,
                true /*LowerIsBetter*/,
                0.01,
                L"s");
        }
    }

    if (hovered.MaxRange > 0 || equipped.MaxRange > 0)
    {
        AddIntDelta(entry.Deltas, L"Max Range", hovered.MaxRange, equipped.MaxRange, false);
    }

    CollectSharedAttributeDeltas(entry.Deltas, hovered, equipped);
    SortDeltasForDisplay(entry.Deltas);
    return entry;
}

EquipmentComparisonEntry CompareAgainstCombinedStats(
    const ParsedEquipmentStats &hovered,
    const ParsedEquipmentStats &eqA,
    const ParsedEquipmentStats &eqB,
    EquipCategory category,
    const wstr_t &targetLabel)
{
    EquipmentComparisonEntry entry;
    entry.TargetLabel = SanitizeAsciiLabel(targetLabel);
    entry.HoveredScore = ScoreEquipment(hovered, category);

    const EquipCategory catA = InferEquippedCategory(eqA, EquipCategory::Weapon);
    const EquipCategory catB = InferEquippedCategory(eqB, EquipCategory::Weapon);
    entry.EquippedScore = ScoreEquipment(eqA, catA) + ScoreEquipment(eqB, catB);
    entry.ScoreDelta = entry.HoveredScore - entry.EquippedScore;
    entry.ScoreDeltaPct = ComputeScoreDeltaPct(entry.ScoreDelta, entry.EquippedScore);
    entry.Verdict = ClassifyVerdict(entry.ScoreDelta, entry.EquippedScore);

    const ParsedEquipmentStats combined = CombineStatsForComparison(eqA, eqB);

    // Combined independent-hand DPS (dual-wield swings both hands; 1H+shield swings 1H only)
    const double eqEffDps = (eqA.HasWeaponDamage ? eqA.EffectiveDps() : 0.0) +
                            (eqB.HasWeaponDamage ? eqB.EffectiveDps() : 0.0);
    const double eqBaseDps = (eqA.HasWeaponDamage ? eqA.BaseDps() : 0.0) +
                             (eqB.HasWeaponDamage ? eqB.BaseDps() : 0.0);

    if (hovered.HasWeaponDamage || eqA.HasWeaponDamage || eqB.HasWeaponDamage)
    {
        AddFloatDelta(entry.Deltas, L"Eff DPS", hovered.EffectiveDps(), eqEffDps, false);
        AddFloatDelta(entry.Deltas, L"Base DPS", hovered.BaseDps(), eqBaseDps, false);
    }

    CollectSharedAttributeDeltas(entry.Deltas, hovered, combined);
    SortDeltasForDisplay(entry.Deltas);
    return entry;
}

EquipmentCompareResult BuildEquipmentComparison(
    const EquipmentCompareItemInput &hovered,
    const std::vector<EquipmentCompareItemInput> &equippedTargets,
    int totalSlotsInFamily,
    int occupiedSlotsInFamily)
{
    EquipmentCompareResult result;
    if (!hovered.PropertiesLoaded)
    {
        return result;
    }

    const ParsedEquipmentStats hoveredStats = ParseEquipmentOpl(hovered.Properties);
    const EquipCategory hoveredCat =
        ClassifyEquipmentCategory(hoveredStats, hovered.Layer, hovered.IsWeapon);
    const bool hoveredIsWeapon = hovered.IsWeapon || hoveredStats.HasWeaponDamage;

    result.Valid = true;

    const bool hasOpenSlot =
        totalSlotsInFamily > 1 && occupiedSlotsInFamily < totalSlotsInFamily;
    const wstr_t openSlotBanner =
        hasOpenSlot ? (L"Open slot available (" + FormatInt(occupiedSlotsInFamily, false) + L"/" +
                       FormatInt(totalSlotsInFamily, false) + L" " +
                       SlotFamilyPluralName(hovered.Layer) + L")")
                    : wstr_t{};

    // Case 1: Empty slot (nothing equipped in matching slot family)
    if (equippedTargets.empty())
    {
        const double hoveredScore = ScoreEquipment(hoveredStats, hoveredCat);
        const UpgradeVerdict emptyVerdict = ClassifyVerdict(hoveredScore, 0.0);
        EquipmentComparisonEntry emptyEntry;
        emptyEntry.TargetLabel = L"Empty Slot";
        emptyEntry.HoveredScore = hoveredScore;
        emptyEntry.EquippedScore = 0.0;
        emptyEntry.ScoreDelta = hoveredScore;
        emptyEntry.ScoreDeltaPct = ComputeScoreDeltaPct(hoveredScore, 0.0);
        emptyEntry.Verdict = emptyVerdict;
        result.Comparisons.push_back(emptyEntry);

        ComparisonCardOutput card;
        card.AlignLeft = true;
        card.MaxWidth = 260;
        card.BorderColorARGB = BorderColorForVerdict(emptyVerdict);

        wstr_t html;
        html += wstr_t(kColorGold) + L"--- COMPARE ---" + kColorWhite + L"\n";
        html += FormatVerdictBanner(emptyVerdict, emptyEntry.ScoreDeltaPct, false) + L"\n";
        if (!openSlotBanner.empty())
        {
            html += wstr_t(kColorBlue) + L"* " + openSlotBanner + kColorWhite + L"\n";
        }
        html += wstr_t(kColorGray) + L"Score: " + kColorWhite +
                FormatFloat1(hoveredScore, false) + L"\n";
        AppendEmptySlotSummaryLines(html, hoveredStats, hoveredCat);
        TrimTrailingNewline(html);
        card.HtmlText = std::move(html);
        result.SideCards.push_back(std::move(card));
        return result;
    }

    // Track loading state across equipped targets
    for (const auto &eq : equippedTargets)
    {
        if (!eq.PropertiesLoaded)
        {
            result.LoadingEquipped = true;
        }
    }

    const bool isCombinedTwoHanded =
        (equippedTargets.size() == 2) && (hovered.Layer == OL_2_HAND) && hoveredIsWeapon;

    ComparisonCardOutput compareCard;
    compareCard.AlignLeft = true;
    compareCard.MaxWidth = 260;
    compareCard.BorderColorARGB = kBorderNeutralARGB;

    wstr_t html;
    html += wstr_t(kColorGold) + L"--- COMPARE ---" + kColorWhite + L"\n";
    if (!openSlotBanner.empty())
    {
        html += wstr_t(kColorBlue) + L"* " + openSlotBanner + kColorWhite + L"\n";
    }

    if (isCombinedTwoHanded)
    {
        if (equippedTargets[0].PropertiesLoaded && equippedTargets[1].PropertiesLoaded)
        {
            const ParsedEquipmentStats statsA = ParseEquipmentOpl(equippedTargets[0].Properties);
            const ParsedEquipmentStats statsB = ParseEquipmentOpl(equippedTargets[1].Properties);
            const bool aIsWeapon = equippedTargets[0].IsWeapon || statsA.HasWeaponDamage;
            const bool bIsWeapon = equippedTargets[1].IsWeapon || statsB.HasWeaponDamage;
            const wstr_t combinedLabel =
                (aIsWeapon && bIsWeapon) ? L"Combined (MH + OH)" : L"Combined (1H + Shield)";

            EquipmentComparisonEntry combined = CompareAgainstCombinedStats(
                hoveredStats, statsA, statsB, hoveredCat, combinedLabel);
            compareCard.BorderColorARGB = BorderColorForVerdict(combined.Verdict);
            AppendComparisonBlock(
                html, combined, 16, true, L"Replaces both hands");
            result.Comparisons.push_back(std::move(combined));
        }
        else
        {
            html += wstr_t(kColorGray) + L"Loading equipped stats..." + kColorWhite + L"\n";
        }
    }
    else
    {
        // 1 or 2 independent targets (single slot, 1H vs Dual-Wield, 2 Bracelets, 2 Earrings, Weakest Ring)
        std::vector<wstr_t> notes;
        const size_t targetCount = std::min<size_t>(2, equippedTargets.size());
        for (size_t i = 0; i < targetCount; ++i)
        {
            const auto &eq = equippedTargets[i];
            if (!eq.PropertiesLoaded)
            {
                continue;
            }
            const ParsedEquipmentStats eqStats = ParseEquipmentOpl(eq.Properties);
            wstr_t targetLabel = DefaultSlotLabelForItem(eq);

            // Annotate multi-ring weakest swap with count if not already present
            if (totalSlotsInFamily == 6 && occupiedSlotsInFamily > 1 &&
                targetLabel.find(L'/') == wstr_t::npos)
            {
                targetLabel += L" (" + FormatInt(occupiedSlotsInFamily, false) + L"/" +
                               FormatInt(totalSlotsInFamily, false) + L" equipped)";
            }

            wstr_t note;
            if (equippedTargets.size() == 1 && hovered.Layer == OL_1_HAND && hoveredIsWeapon &&
                eq.Layer == OL_2_HAND)
            {
                note = L"Replaces 2H weapon";
            }
            else if (
                equippedTargets.size() == 1 && hovered.Layer == OL_2_HAND && !hoveredIsWeapon &&
                (eq.IsWeapon || eqStats.HasWeaponDamage))
            {
                note = L"Replaces Off-Hand / 2H";
            }

            result.Comparisons.push_back(
                CompareSingleStats(hoveredStats, eqStats, hoveredCat, targetLabel));
            notes.push_back(std::move(note));
        }

        if (result.Comparisons.empty())
        {
            html += wstr_t(kColorGray) + L"Loading equipped stats..." + kColorWhite + L"\n";
        }
        else if (result.Comparisons.size() == 1)
        {
            compareCard.BorderColorARGB = BorderColorForVerdict(result.Comparisons[0].Verdict);
            AppendComparisonBlock(html, result.Comparisons[0], 16, true, notes[0]);
            if (result.LoadingEquipped)
            {
                html += wstr_t(kColorGray) + L"Loading other slot..." + kColorWhite + L"\n";
            }
        }
        else
        {
            // Dual comparison (Dual-Wield 1H, 2 Bracelets, 2 Earrings)
            const size_t bestIdx =
                (result.Comparisons[1].ScoreDelta > result.Comparisons[0].ScoreDelta) ? 1u : 0u;
            const auto &best = result.Comparisons[bestIdx];
            compareCard.BorderColorARGB = BorderColorForVerdict(best.Verdict);

            html += wstr_t(kColorGold) + L"Best swap: " + kColorWhite +
                    SanitizeAsciiLabel(best.TargetLabel) + L" " +
                    (best.ScoreDelta >= 0.0 ? kColorGreen : kColorRed) + L"(" +
                    FormatFloat1(best.ScoreDeltaPct, true) + L"%)" + kColorWhite + L"\n";

            AppendComparisonBlock(html, result.Comparisons[0], 10, true, notes[0]);
            html += wstr_t(kColorGray) + L"----------------" + kColorWhite + L"\n";
            AppendComparisonBlock(html, result.Comparisons[1], 10, true, notes[1]);
        }
    }

    TrimTrailingNewline(html);
    compareCard.HtmlText = std::move(html);
    result.SideCards.push_back(std::move(compareCard));

    // SideCards[1..2]: Currently Equipped Card(s)
    const size_t equippedCardCount = std::min<size_t>(2, equippedTargets.size());
    for (size_t i = 0; i < equippedCardCount; ++i)
    {
        result.SideCards.push_back(BuildEquippedSideCard(equippedTargets[i]));
    }

    return result;
}
