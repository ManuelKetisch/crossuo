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

enum class CompareBuildProfile : uint8_t
{
    Auto = 0,
    Melee = 1,
    Caster = 2,
    Hybrid = 3,
    Tank = 4,
    Luck = 5
};

constexpr int kCompareSkillCount = 58;

struct CharacterCompareContext
{
    bool Valid = false;
    CompareBuildProfile Profile = CompareBuildProfile::Auto;

    // Live character stats (from g_Player)
    int Str = 0;
    int Dex = 0;
    int Int = 0;
    int Stam = 0;

    // Live totals & caps (from 0x11 extended status packet + equipped paperdoll scan)
    bool HasStatusTotals = false;
    int PhysicalResist = 0;
    int FireResist = 0;
    int ColdResist = 0;
    int PoisonResist = 0;
    int EnergyResist = 0;
    int MaxPhysicalResist = 70;
    int MaxFireResist = 70;
    int MaxColdResist = 70;
    int MaxPoisonResist = 70;
    int MaxEnergyResist = 70;

    // Uncapped sum of resists across OTHER loaded equipped items (excluding compared target(s)),
    // allowing accurate overcap detection even though 0x11 clamps player resists at Max*Resist.
    bool HasOtherEquippedResists = false;
    int OtherEquippedPhysicalResist = 0;
    int OtherEquippedFireResist = 0;
    int OtherEquippedColdResist = 0;
    int OtherEquippedPoisonResist = 0;
    int OtherEquippedEnergyResist = 0;

    // Suit totals for SA/HS/ToL capped stats across OTHER loaded equipped items
    // (excluding compared target(s)), since 0x11 does not transmit these totals.
    bool HasOtherEquippedSAStats = false;
    int OtherEquippedCastingFocus = 0;
    int MaxCastingFocus = 12;
    int OtherEquippedDamageEater = 0;
    int MaxDamageEater = 18;
    int MaxTotalEater = 30;
    int OtherEquippedKineticEater = 0;
    int OtherEquippedFireEater = 0;
    int OtherEquippedColdEater = 0;
    int OtherEquippedPoisonEater = 0;
    int OtherEquippedEnergyEater = 0;
    int OtherEquippedSoulCharge = 0;
    int MaxSoulCharge = 30;
    int OtherEquippedKineticResonance = 0;
    int OtherEquippedFireResonance = 0;
    int OtherEquippedColdResonance = 0;
    int OtherEquippedPoisonResonance = 0;
    int OtherEquippedEnergyResonance = 0;
    int MaxResonance = 20;
    bool OtherEquippedReactiveParalyze = false;
    bool OtherEquippedManaPhase = false;
    int OtherEquippedEnhancePotions = 0;
    int MaxEnhancePotions = 50;
    int OtherEquippedSplinteringWeapon = 0;
    int MaxSplinteringWeapon = 35;

    int DefenseChance = 0;
    int MaxDefenseChance = 45;
    int AttackChance = 0;
    int MaxAttackChance = 45;
    int WeaponSpeed = 0;      // Suit SSI total
    int MaxWeaponSpeed = 60;
    int WeaponDamage = 0;     // Suit DI total
    int MaxWeaponDamage = 100;
    int LowerReagentCost = 0;
    int MaxLowerReagentCost = 100;
    int LowerManaCost = 0;
    int MaxLowerManaCost = 40;
    int SpellDamage = 0;
    int CastSpeed = 0;        // Suit FC total
    int MaxCastSpeed = 2;
    int CastRecovery = 0;     // Suit FCR total
    int MaxCastRecovery = 6;
    int Luck = 0;

    // Live character skills (indexed by UO SkillName 0..57; values 0.0..120.0+)
    bool HasSkills = false;
    double Skills[kCompareSkillCount] = {};
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
    int CastingFocus = 0;
    int DamageEater = 0;
    int KineticEater = 0;
    int FireEater = 0;
    int ColdEater = 0;
    int PoisonEater = 0;
    int EnergyEater = 0;

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
    bool BattleLust = false;
    int SplinteringWeapon = 0;
    int HitCurse = 0;
    int HitFatigue = 0;
    int HitManaDrain = 0;
    bool BloodDrinker = false;
    bool Bane = false;
    int SelfRepair = 0;
    bool MageArmor = false;
    bool ReactiveParalyze = false;
    int SoulCharge = 0;
    bool ManaPhase = false;
    int KineticResonance = 0;
    int FireResonance = 0;
    int ColdResonance = 0;
    int PoisonResonance = 0;
    int EnergyResonance = 0;

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

    bool HasWeaponIdentity() const;
    double AverageDamage() const;
    double BaseSeconds() const;
    double EffectiveSeconds() const;
    double BaseDps() const;
    double EffectiveDps() const;
    int TotalResists() const;
    double TotalSkillBonuses() const;
    int TotalHitArea() const;
    int TotalElementalEaters() const;
    int TotalResonances() const;
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
    bool MutedByCap = false;
    wstr_t DetailText = {};
    wstr_t CapNote = {};
};

struct EquipmentComparisonEntry
{
    wstr_t TargetLabel = {};
    wstr_t ProfileLabel = {};
    double HoveredScore = 0.0;
    double EquippedScore = 0.0;
    double ScoreDelta = 0.0;
    double ScoreDeltaPct = 0.0;
    UpgradeVerdict Verdict = UpgradeVerdict::Sidegrade;
    std::vector<EquipmentStatDelta> Deltas = {};
    std::vector<wstr_t> Warnings = {};
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

int SkillClilocToIndex(uint32_t skillCliloc);

wstr_t ResolveBuildProfileLabel(const CharacterCompareContext *ctx);

double ScoreEquipment(
    const ParsedEquipmentStats &stats,
    EquipCategory category,
    const CharacterCompareContext *ctx = nullptr);

double ScoreEquippedItemForReplacement(
    const ParsedEquipmentStats &equipped,
    EquipCategory category,
    const CharacterCompareContext *ctx = nullptr);

UpgradeVerdict ClassifyVerdict(double scoreDelta, double equippedScore);

EquipmentComparisonEntry CompareSingleStats(
    const ParsedEquipmentStats &hovered,
    const ParsedEquipmentStats &equipped,
    EquipCategory category,
    const wstr_t &targetLabel,
    const CharacterCompareContext *ctx = nullptr);

EquipmentComparisonEntry CompareAgainstCombinedStats(
    const ParsedEquipmentStats &hovered,
    const ParsedEquipmentStats &eqA,
    const ParsedEquipmentStats &eqB,
    EquipCategory category,
    const wstr_t &targetLabel,
    const CharacterCompareContext *ctx = nullptr);

EquipmentCompareResult BuildEquipmentComparison(
    const EquipmentCompareItemInput &hovered,
    const std::vector<EquipmentCompareItemInput> &equippedTargets,
    int totalSlotsInFamily,
    int occupiedSlotsInFamily,
    const CharacterCompareContext *ctx = nullptr);
