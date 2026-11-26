// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include <common/str.h>
#include <xuocore/enumlist.h>

using OplPropertyEntry = std::pair<uint32_t, wstr_t>;
using OplPropertyList = std::vector<OplPropertyEntry>;

enum class EquipCategory : uint8_t
{
    Unknown = 0,
    Weapon,
    ArmorOrShield,
    JewelryOrUtility
};

enum class UpgradeVerdict : uint8_t
{
    Sidegrade = 0,
    Upgrade,
    Downgrade
};

struct EquipmentSkillBonus
{
    uint32_t SkillCliloc = 0;
    double Value = 0.0;
};

struct ParsedEquipmentStats
{
    // Weapon core & ranged (BaseWeapon.cs / BaseThrown.cs)
    bool HasWeaponDamage = false;
    int MinDamage = 0;
    int MaxDamage = 0;
    bool HasWeaponSpeed = false;
    bool SpeedIsMlSeconds = false;
    double WeaponSpeed = 0.0;
    bool IsOneHanded = false;
    bool IsTwoHanded = false;
    int MaxRange = 0;
    int MinThrowRange = 0;
    uint32_t WeaponSkillCliloc = 0;
    int PhysDamagePct = 0;
    int ColdDamagePct = 0;
    int FireDamagePct = 0;
    int PoisonDamagePct = 0;
    int EnergyDamagePct = 0;
    int ChaosDamagePct = 0;
    int DirectDamagePct = 0;
    bool Balanced = false;
    int Velocity = 0;
    int PoisonCharges = 0;

    // Shared AosAttributes
    int DamageIncrease = 0;
    int SwingSpeedIncrease = 0;
    int HitChanceIncrease = 0;
    int DefenseChanceIncrease = 0;
    bool SpellChanneling = false;
    int SpellDamageIncrease = 0;
    int LowerManaCost = 0;
    int LowerReagentCost = 0;
    int FasterCasting = 0;
    int FasterCastRecovery = 0;
    int BonusStr = 0;
    int BonusDex = 0;
    int BonusInt = 0;
    int BonusHits = 0;
    int BonusMana = 0;
    int BonusStam = 0;
    int RegenHits = 0;
    int RegenMana = 0;
    int RegenStam = 0;
    int Luck = 0;
    int ReflectPhysical = 0;
    int EnhancePotions = 0;
    bool NightSight = false;
    int IncreasedKarmaLoss = 0;

    // Weapon & armor attributes
    bool UseBestSkill = false;
    bool HasMageWeapon = false;
    int MageWeaponPenalty = 0; // Lower is better: 0 = -0 skill (best), 29 = -29 skill (worst)
    int HitLifeLeech = 0;
    int HitManaLeech = 0;
    int HitStamLeech = 0;
    int HitLightning = 0;
    int HitFireball = 0;
    int HitHarm = 0;
    int HitMagicArrow = 0;
    int HitLowerAttack = 0;
    int HitLowerDefense = 0;
    int HitDispel = 0;
    int HitColdArea = 0;
    int HitEnergyArea = 0;
    int HitFireArea = 0;
    int HitPhysicalArea = 0;
    int HitPoisonArea = 0;
    int SelfRepair = 0;
    bool MageArmor = false;

    // Resists, skills, slayers, durability, misc
    int PhysicalResist = 0;
    int FireResist = 0;
    int ColdResist = 0;
    int PoisonResist = 0;
    int EnergyResist = 0;
    std::vector<EquipmentSkillBonus> SkillBonuses = {};
    std::vector<uint32_t> SlayerClilocs = {};
    bool HasDurability = false;
    int DurabilityCur = 0;
    int DurabilityMax = 0;
    int DurabilityBonus = 0;
    int LowerStatReq = 0;
    int StrengthReq = 0;
    int ArtifactRarity = 0;
    int QuiverDamageMod = 0;
    int TalismanKiller = 0;
    int TalismanProtection = 0;
    int TalismanExceptional = 0;
    int TalismanSuccess = 0;

    // Custom passthrough clilocs (Excelsior Material Grade & Weapon Level)
    wstr_t MaterialGrade = {};
    int WeaponLevel = 0;

    double AverageDamage() const;
    double BaseSeconds() const;
    double EffectiveSeconds() const;
    double BaseDps() const;
    double EffectiveDps() const;
    int TotalResists() const;
    double TotalSkillBonuses() const;
    int TotalHitArea() const;
    int SlayerCount() const;
};

struct EquipmentCompareItemInput
{
    uint32_t Serial = 0;
    int Layer = OL_NONE;
    bool IsWeapon = false;
    bool IsOffHandWeapon = false;
    bool PropertiesLoaded = false;
    wstr_t Name = {};
    wstr_t FormattedTooltip = {};
    OplPropertyList Properties = {};
    wstr_t SlotLabel = {};
};

struct EquipmentStatDelta
{
    wstr_t Label = {};
    double Delta = 0.0;
    bool LowerIsBetter = false;
    bool IsPercent = false;
    bool IsFloat = false;
    bool IsBooleanFlag = false;
    bool IsInformational = false;
    wstr_t DetailText = {};
};

struct EquipmentComparisonEntry
{
    wstr_t TargetLabel = {};
    double HoveredScore = 0.0;
    double EquippedScore = 0.0;
    double ScoreDelta = 0.0;
    double ScoreDeltaPct = 0.0;
    UpgradeVerdict Verdict = UpgradeVerdict::Sidegrade;
    std::vector<EquipmentStatDelta> Deltas = {};
};

struct ComparisonCardOutput
{
    wstr_t HtmlText = {};
    int MaxWidth = 280;
    bool AlignLeft = false;
    uint32_t BorderColorARGB = 0xFF565E66;
};

struct EquipmentCompareResult
{
    bool Valid = false;
    bool LoadingEquipped = false;
    std::vector<EquipmentComparisonEntry> Comparisons = {};
    std::vector<ComparisonCardOutput> SideCards = {};
};

ParsedEquipmentStats ParseEquipmentOpl(const OplPropertyList &props);

EquipCategory ClassifyEquipmentCategory(
    const ParsedEquipmentStats &stats,
    int tileLayer,
    bool isWeapon);

double ScoreEquipment(const ParsedEquipmentStats &stats, EquipCategory category);

UpgradeVerdict ClassifyVerdict(double scoreDelta, double equippedScore);

EquipmentComparisonEntry CompareSingleStats(
    const ParsedEquipmentStats &hovered,
    const ParsedEquipmentStats &equipped,
    EquipCategory category,
    const wstr_t &targetLabel);

EquipmentComparisonEntry CompareAgainstCombinedStats(
    const ParsedEquipmentStats &hovered,
    const ParsedEquipmentStats &eqA,
    const ParsedEquipmentStats &eqB,
    EquipCategory category,
    const wstr_t &targetLabel);

EquipmentCompareResult BuildEquipmentComparison(
    const EquipmentCompareItemInput &hovered,
    const std::vector<EquipmentCompareItemInput> &equippedTargets,
    int totalSlotsInFamily,
    int occupiedSlotsInFamily);
