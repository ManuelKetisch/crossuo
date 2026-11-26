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
const wchar_t *const kColorMuted = L"<basefont color=\"#999999\">";
const wchar_t *const kColorWhite = L"<basefont color=\"#FFFFFF\">";

wstr_t FormatFloat1(double val, bool withSign);
wstr_t FormatInt(int val, bool withSign);

enum SkillIndex
{
    kSkillAlchemy = 0,
    kSkillAnatomy = 1,
    kSkillAnimalLore = 2,
    kSkillParry = 5,
    kSkillPeacemaking = 9,
    kSkillDiscordance = 15,
    kSkillEvalInt = 16,
    kSkillHealing = 17,
    kSkillHiding = 21,
    kSkillProvocation = 22,
    kSkillInscribe = 23,
    kSkillMagery = 25,
    kSkillMagicResist = 26,
    kSkillTactics = 27,
    kSkillMusicianship = 29,
    kSkillPoisoning = 30,
    kSkillArchery = 31,
    kSkillSpiritSpeak = 32,
    kSkillAnimalTaming = 35,
    kSkillVeterinary = 39,
    kSkillSwords = 40,
    kSkillMacing = 41,
    kSkillFencing = 42,
    kSkillWrestling = 43,
    kSkillLumberjacking = 44,
    kSkillMeditation = 46,
    kSkillStealth = 47,
    kSkillNecromancy = 49,
    kSkillFocus = 50,
    kSkillChivalry = 51,
    kSkillBushido = 52,
    kSkillNinjitsu = 53,
    kSkillSpellweaving = 54,
    kSkillMysticism = 55,
    kSkillThrowing = 57
};

constexpr double kOvercapBufferFactor = 0.10;
constexpr double kIrrelevantSkillFactor = 0.15;

struct BuildStatWeights
{
    wstr_t Label = L"Balanced";
    CompareBuildProfile EffectiveArchetype = CompareBuildProfile::Hybrid;
    bool IsPaladin = false;
    bool IsReagentCaster = true;

    // Weapon-specific weights
    double WeaponDps = 6.5;
    double WeaponDi = 0.35;
    double WeaponSsi = 0.50;
    double WeaponHci = 1.15;
    double WeaponDci = 0.75;
    double WeaponVelocity = 0.65;
    double WeaponBalanced = 10.0;
    double WeaponLeech = 0.90;
    double WeaponStamLeech = 0.60;
    double WeaponHld = 0.80;
    double WeaponHla = 0.55;
    double WeaponSpellProc = 0.65;
    double WeaponMinorProc = 0.45;
    double WeaponHitArea = 0.55;
    double WeaponSpellChanneling = 10.0;
    double WeaponUseBestSkill = 8.0;
    double WeaponSlayer = 13.0;
    double WeaponSdi = 0.45;
    double WeaponLmc = 0.70;
    double WeaponLrc = 0.25;
    double WeaponFc = 7.0;
    double WeaponFcr = 3.0;
    double WeaponResist = 0.55;
    double WeaponStr = 0.85;
    double WeaponDex = 0.85;
    double WeaponInt = 0.55;
    double WeaponHits = 0.70;
    double WeaponStam = 0.60;
    double WeaponMana = 0.55;
    double WeaponRegenHits = 2.6;
    double WeaponRegenMana = 2.5;
    double WeaponRegenStam = 2.4;
    double WeaponLuck = 0.08;
    double WeaponSelfRepair = 1.5;
    double WeaponSkill = 1.4;
    double WeaponLevel = 0.5;
    double WeaponReflect = 0.30;
    double WeaponEnhancePotions = 0.25;

    // Armor/Shield weights
    double ArmorResist = 1.65;
    double ArmorLmc = 2.20;
    double ArmorLrc = 0.65;
    double ArmorRegenHits = 4.5;
    double ArmorRegenMana = 4.8;
    double ArmorRegenStam = 3.2;
    double ArmorDci = 1.65;
    double ArmorHci = 1.65;
    double ArmorDi = 1.45;
    double ArmorSsi = 1.90;
    double ArmorSdi = 1.35;
    double ArmorFc = 11.0;
    double ArmorFcr = 5.0;
    double ArmorStr = 1.4;
    double ArmorDex = 1.3;
    double ArmorInt = 1.4;
    double ArmorHits = 1.5;
    double ArmorStam = 1.1;
    double ArmorMana = 1.2;
    double ArmorReflect = 0.50;
    double ArmorLuck = 0.10;
    double ArmorSkill = 1.8;
    double ArmorMageArmor = 6.5;
    double ArmorSpellChanneling = 10.0;
    double ArmorSelfRepair = 1.5;
    double ArmorEnhancePotions = 0.35;
    double ArmorNightSight = 1.0;
    double ArmorSlayer = 10.0;

    // Jewelry/Utility weights
    double JewelDi = 1.70;
    double JewelSdi = 1.65;
    double JewelHci = 1.90;
    double JewelDci = 1.75;
    double JewelSsi = 2.20;
    double JewelFc = 13.0;
    double JewelFcr = 5.8;
    double JewelLmc = 2.30;
    double JewelLrc = 0.75;
    double JewelResist = 1.10;
    double JewelStr = 1.5;
    double JewelDex = 1.4;
    double JewelInt = 1.5;
    double JewelHits = 1.5;
    double JewelStam = 1.1;
    double JewelMana = 1.3;
    double JewelRegenHits = 4.2;
    double JewelRegenMana = 4.5;
    double JewelRegenStam = 3.2;
    double JewelSkill = 2.0;
    double JewelLuck = 0.10;
    double JewelEnhancePotions = 0.50;
    double JewelReflect = 0.50;
    double JewelSpellChanneling = 10.0;
    double JewelSlayer = 10.0;
    double JewelTalismanCombat = 0.85;
    double JewelTalismanCraft = 0.40;
    double JewelQuiverDmg = 1.60;
    double JewelNightSight = 1.0;
    double JewelSelfRepair = 1.0;
};

struct OtherGearBaseline
{
    bool Active = false;
    int Phys = 0;
    int Fire = 0;
    int Cold = 0;
    int Poison = 0;
    int Energy = 0;
    int MaxPhys = 70;
    int MaxFire = 70;
    int MaxCold = 70;
    int MaxPoison = 70;
    int MaxEnergy = 70;

    int Dci = 0;
    int MaxDci = 45;
    int Hci = 0;
    int MaxHci = 45;
    int Ssi = 0;
    int MaxSsi = 60;
    int Di = 0;
    int MaxDi = 100;
    int Lrc = 0;
    int MaxLrc = 100;
    int Lmc = 0;
    int MaxLmc = 40;
    int Fc = 0;
    int MaxFc = 2;
    int Fcr = 0;
    int MaxFcr = 6;
};

struct CapImpact
{
    int RawDelta = 0;
    int EffBefore = 0;
    int EffAfter = 0;
    int EffDelta = 0;
    int Cap = 0;
};

double ClampDouble(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

double SkillVal(const CharacterCompareContext *ctx, int idx)
{
    if (ctx == nullptr || !ctx->HasSkills || idx < 0 || idx >= kCompareSkillCount)
    {
        return 0.0;
    }
    return std::max(0.0, ctx->Skills[idx]);
}

double BestWeaponSkillValue(const CharacterCompareContext *ctx)
{
    if (ctx == nullptr || !ctx->HasSkills)
    {
        return 0.0;
    }
    return std::max(
        { SkillVal(ctx, kSkillSwords),
          SkillVal(ctx, kSkillMacing),
          SkillVal(ctx, kSkillFencing),
          SkillVal(ctx, kSkillArchery),
          SkillVal(ctx, kSkillThrowing) });
}

int WeaponSkillClilocToIndex(uint32_t cliloc)
{
    switch (cliloc)
    {
        case 1061172u:
            return kSkillSwords;
        case 1061173u:
            return kSkillMacing;
        case 1061174u:
            return kSkillFencing;
        case 1061175u:
            return kSkillArchery;
        case 1112075u:
            return kSkillThrowing;
        default:
            return -1;
    }
}

const wchar_t *WeaponSkillDisplayName(uint32_t cliloc)
{
    switch (cliloc)
    {
        case 1061172u:
            return L"Swordsmanship";
        case 1061173u:
            return L"Mace Fighting";
        case 1061174u:
            return L"Fencing";
        case 1061175u:
            return L"Archery";
        case 1112075u:
            return L"Throwing";
        default:
            return L"Weapon Skill";
    }
}

BuildStatWeights MakeProfilePreset(CompareBuildProfile profile, bool paladin = false, bool reagentCaster = true)
{
    BuildStatWeights w;
    w.EffectiveArchetype = profile;
    w.IsPaladin = paladin;
    w.IsReagentCaster = reagentCaster;

    switch (profile)
    {
        case CompareBuildProfile::Melee:
        {
            w.Label = paladin ? L"Paladin" : L"Melee";
            w.WeaponDps = 7.5;
            w.WeaponDi = 0.45;
            w.WeaponSsi = 0.60;
            w.WeaponHci = 1.25;
            w.WeaponDci = 0.80;
            w.WeaponLeech = 1.00;
            w.WeaponStamLeech = 0.65;
            w.WeaponHld = 0.85;
            w.WeaponHla = 0.60;
            w.WeaponSpellChanneling = paladin ? 10.0 : 3.0;
            w.WeaponSlayer = 14.0;
            w.WeaponSdi = 0.05;
            w.WeaponLmc = paladin ? 0.85 : 0.70;
            w.WeaponLrc = 0.05;
            w.WeaponFc = paladin ? 7.0 : 1.5;
            w.WeaponFcr = paladin ? 3.0 : 0.8;
            w.WeaponStr = 1.0;
            w.WeaponDex = 1.0;
            w.WeaponInt = paladin ? 0.45 : 0.20;
            w.WeaponHits = 0.85;
            w.WeaponStam = 0.80;
            w.WeaponMana = paladin ? 0.45 : 0.25;
            w.WeaponRegenStam = 3.0;
            w.WeaponRegenMana = paladin ? 2.5 : 1.6;

            w.ArmorResist = 1.70;
            w.ArmorDi = 1.80;
            w.ArmorSsi = 2.40;
            w.ArmorHci = 2.10;
            w.ArmorDci = 1.75;
            w.ArmorSdi = 0.10;
            w.ArmorLmc = paladin ? 2.20 : 2.00;
            w.ArmorLrc = paladin ? 0.15 : 0.10;
            w.ArmorFc = paladin ? 9.0 : 2.5;
            w.ArmorFcr = paladin ? 4.0 : 1.5;
            w.ArmorRegenHits = 5.0;
            w.ArmorRegenStam = 4.5;
            w.ArmorRegenMana = paladin ? 4.5 : 3.5;
            w.ArmorStr = 1.8;
            w.ArmorDex = 1.8;
            w.ArmorInt = paladin ? 0.6 : 0.35;
            w.ArmorHits = 1.6;
            w.ArmorStam = 1.5;
            w.ArmorMana = paladin ? 0.8 : 0.5;
            w.ArmorMageArmor = paladin ? 3.5 : 1.5;
            w.ArmorSpellChanneling = paladin ? 10.0 : 3.0;
            w.ArmorLuck = 0.08;
            w.ArmorSkill = 2.0;

            w.JewelDi = 2.10;
            w.JewelSsi = 2.80;
            w.JewelHci = 2.40;
            w.JewelDci = 1.85;
            w.JewelSdi = 0.10;
            w.JewelLmc = paladin ? 2.30 : 2.10;
            w.JewelLrc = paladin ? 0.15 : 0.10;
            w.JewelFc = paladin ? 10.0 : 2.5;
            w.JewelFcr = paladin ? 4.5 : 1.5;
            w.JewelResist = 1.15;
            w.JewelStr = 1.9;
            w.JewelDex = 1.9;
            w.JewelInt = paladin ? 0.6 : 0.35;
            w.JewelHits = 1.7;
            w.JewelStam = 1.6;
            w.JewelMana = paladin ? 0.8 : 0.5;
            w.JewelRegenHits = 4.8;
            w.JewelRegenStam = 4.2;
            w.JewelRegenMana = paladin ? 4.2 : 3.0;
            w.JewelSkill = 2.1;
            w.JewelLuck = 0.08;
            w.JewelSpellChanneling = paladin ? 10.0 : 3.0;
            break;
        }
        case CompareBuildProfile::Caster:
        {
            w.Label = L"Caster";
            w.WeaponDps = 2.0;
            w.WeaponDi = 0.08;
            w.WeaponSsi = 0.10;
            w.WeaponHci = 0.20;
            w.WeaponDci = 0.80;
            w.WeaponLeech = 0.25;
            w.WeaponStamLeech = 0.10;
            w.WeaponHld = 0.20;
            w.WeaponHla = 0.20;
            w.WeaponSpellChanneling = 16.0;
            w.WeaponSlayer = 14.0;
            w.WeaponSdi = 1.60;
            w.WeaponLmc = 1.60;
            w.WeaponLrc = reagentCaster ? 0.80 : 0.15;
            w.WeaponFc = 14.0;
            w.WeaponFcr = 6.0;
            w.WeaponResist = 1.00;
            w.WeaponStr = 0.50;
            w.WeaponDex = 0.30;
            w.WeaponInt = 1.50;
            w.WeaponHits = 0.80;
            w.WeaponStam = 0.25;
            w.WeaponMana = 1.20;
            w.WeaponRegenMana = 4.5;
            w.WeaponRegenHits = 2.8;
            w.WeaponRegenStam = 0.8;

            w.ArmorResist = 1.70;
            w.ArmorDi = 0.15;
            w.ArmorSsi = 0.15;
            w.ArmorHci = 0.25;
            w.ArmorDci = 1.50;
            w.ArmorSdi = 2.20;
            w.ArmorLmc = 2.60;
            w.ArmorLrc = reagentCaster ? 1.10 : 0.20;
            w.ArmorFc = 15.0;
            w.ArmorFcr = 6.5;
            w.ArmorRegenHits = 4.0;
            w.ArmorRegenMana = 6.0;
            w.ArmorRegenStam = 1.2;
            w.ArmorStr = 0.8;
            w.ArmorDex = 0.5;
            w.ArmorInt = 2.0;
            w.ArmorHits = 1.4;
            w.ArmorStam = 0.4;
            w.ArmorMana = 1.6;
            w.ArmorMageArmor = 10.0;
            w.ArmorSpellChanneling = 14.0;
            w.ArmorLuck = 0.08;
            w.ArmorSkill = 2.0;

            w.JewelDi = 0.15;
            w.JewelSsi = 0.15;
            w.JewelHci = 0.25;
            w.JewelDci = 1.60;
            w.JewelSdi = 2.40;
            w.JewelLmc = 2.70;
            w.JewelLrc = reagentCaster ? 1.20 : 0.20;
            w.JewelFc = 16.0;
            w.JewelFcr = 7.0;
            w.JewelResist = 1.15;
            w.JewelStr = 0.8;
            w.JewelDex = 0.5;
            w.JewelInt = 2.1;
            w.JewelHits = 1.4;
            w.JewelStam = 0.4;
            w.JewelMana = 1.7;
            w.JewelRegenHits = 3.8;
            w.JewelRegenMana = 6.0;
            w.JewelRegenStam = 1.0;
            w.JewelSkill = 2.1;
            w.JewelLuck = 0.08;
            w.JewelSpellChanneling = 14.0;
            break;
        }
        case CompareBuildProfile::Tank:
        {
            w.Label = L"Tank";
            w.WeaponDps = 5.0;
            w.WeaponDci = 1.40;
            w.WeaponLeech = 1.10;
            w.WeaponHla = 0.85;
            w.WeaponResist = 1.10;
            w.WeaponRegenHits = 4.0;
            w.WeaponHits = 1.2;
            w.WeaponStr = 1.0;
            w.WeaponLrc = 0.15;

            w.ArmorResist = 2.60;
            w.ArmorDci = 2.60;
            w.ArmorRegenHits = 7.5;
            w.ArmorRegenMana = 4.0;
            w.ArmorRegenStam = 3.5;
            w.ArmorLmc = 2.00;
            w.ArmorLrc = 0.25;
            w.ArmorDi = 0.70;
            w.ArmorSsi = 0.90;
            w.ArmorHci = 0.90;
            w.ArmorSdi = 0.35;
            w.ArmorFc = 5.0;
            w.ArmorFcr = 2.5;
            w.ArmorStr = 2.2;
            w.ArmorDex = 1.2;
            w.ArmorInt = 0.6;
            w.ArmorHits = 2.4;
            w.ArmorStam = 1.2;
            w.ArmorMana = 0.7;
            w.ArmorReflect = 1.10;
            w.ArmorSelfRepair = 2.5;
            w.ArmorMageArmor = 3.0;
            w.ArmorSpellChanneling = 5.0;
            w.ArmorLuck = 0.08;

            w.JewelResist = 1.80;
            w.JewelDci = 2.60;
            w.JewelRegenHits = 7.0;
            w.JewelRegenMana = 3.8;
            w.JewelRegenStam = 3.0;
            w.JewelLmc = 2.00;
            w.JewelLrc = 0.25;
            w.JewelDi = 0.80;
            w.JewelSsi = 1.00;
            w.JewelHci = 1.00;
            w.JewelSdi = 0.40;
            w.JewelFc = 5.5;
            w.JewelFcr = 2.8;
            w.JewelStr = 2.2;
            w.JewelDex = 1.2;
            w.JewelInt = 0.6;
            w.JewelHits = 2.4;
            w.JewelStam = 1.2;
            w.JewelMana = 0.7;
            w.JewelReflect = 1.10;
            w.JewelLuck = 0.08;
            break;
        }
        case CompareBuildProfile::Luck:
        {
            w.Label = L"Luck";
            w.WeaponLuck = 0.40;
            w.ArmorLuck = 0.55;
            w.JewelLuck = 0.55;
            w.ArmorResist = 1.45;
            w.ArmorDci = 1.40;
            w.ArmorDi = 1.10;
            w.ArmorSsi = 1.30;
            w.ArmorHci = 1.20;
            w.ArmorSdi = 0.90;
            w.ArmorLmc = 1.50;
            w.ArmorLrc = 0.50;
            w.ArmorFc = 7.0;
            w.ArmorFcr = 3.5;
            w.ArmorRegenHits = 3.5;
            w.ArmorRegenMana = 3.5;
            w.ArmorRegenStam = 2.5;
            w.ArmorStr = 1.0;
            w.ArmorDex = 1.0;
            w.ArmorInt = 1.0;
            w.ArmorHits = 1.0;
            w.ArmorStam = 1.0;
            w.ArmorMana = 1.0;
            w.ArmorMageArmor = 3.5;
            w.ArmorSpellChanneling = 6.0;
            w.ArmorSkill = 1.4;

            w.JewelResist = 1.00;
            w.JewelDci = 1.40;
            w.JewelDi = 1.20;
            w.JewelSsi = 1.40;
            w.JewelHci = 1.30;
            w.JewelSdi = 1.00;
            w.JewelLmc = 1.50;
            w.JewelLrc = 0.55;
            w.JewelFc = 8.0;
            w.JewelFcr = 4.0;
            w.JewelSkill = 1.5;
            break;
        }
        case CompareBuildProfile::Hybrid:
        case CompareBuildProfile::Auto:
        default:
        {
            w.Label = L"Hybrid";
            w.EffectiveArchetype = CompareBuildProfile::Hybrid;
            break;
        }
    }

    return w;
}

BuildStatWeights ResolveBuildWeights(const CharacterCompareContext *ctx)
{
    if (ctx == nullptr || !ctx->Valid)
    {
        BuildStatWeights w = MakeProfilePreset(CompareBuildProfile::Hybrid);
        w.Label = L"Balanced";
        return w;
    }

    if (ctx->Profile != CompareBuildProfile::Auto)
    {
        return MakeProfilePreset(ctx->Profile);
    }

    if (!ctx->HasSkills)
    {
        BuildStatWeights w = MakeProfilePreset(CompareBuildProfile::Hybrid);
        w.Label = L"Auto: Balanced";
        return w;
    }

    const double bestWeapon = std::min(100.0, BestWeaponSkillValue(ctx));
    const double wrestling = std::min(100.0, SkillVal(ctx, kSkillWrestling));
    const double martialCore = std::max(bestWeapon, wrestling * 0.5);
    const double martialSupport = std::min(100.0, std::max(SkillVal(ctx, kSkillTactics), SkillVal(ctx, kSkillAnatomy)));
    const double martialStyle = std::min(
        100.0,
        std::max(
            { SkillVal(ctx, kSkillBushido),
              SkillVal(ctx, kSkillChivalry),
              SkillVal(ctx, kSkillParry),
              SkillVal(ctx, kSkillNinjitsu) }));
    const double martial = ClampDouble(
        (martialCore * 0.55 + martialSupport * 0.25 + martialStyle * 0.20) / 100.0, 0.0, 1.0);

    const double magery = std::min(100.0, SkillVal(ctx, kSkillMagery));
    const double necro = std::min(100.0, SkillVal(ctx, kSkillNecromancy));
    const double myst = std::min(100.0, SkillVal(ctx, kSkillMysticism));
    const double sw = std::min(100.0, SkillVal(ctx, kSkillSpellweaving));
    const double bestCaster = std::max({ magery, necro, myst, sw });
    const double casterSupport =
        std::min(100.0, std::max(SkillVal(ctx, kSkillEvalInt), SkillVal(ctx, kSkillSpiritSpeak)));
    const double med = std::min(100.0, SkillVal(ctx, kSkillMeditation));
    const double caster =
        ClampDouble((bestCaster * 0.60 + casterSupport * 0.25 + med * 0.15) / 100.0, 0.0, 1.0);

    const double chiv = std::min(100.0, SkillVal(ctx, kSkillChivalry));
    const bool reagentCaster = std::max({ magery, necro, myst }) >= 25.0;

    if (martial < 0.25 && caster < 0.25)
    {
        BuildStatWeights w = MakeProfilePreset(CompareBuildProfile::Hybrid, false, reagentCaster);
        w.Label = L"Auto: Balanced";
        return w;
    }

    if (martial >= 0.40 && caster < 0.30)
    {
        const bool paladin = (chiv >= 35.0);
        BuildStatWeights w = MakeProfilePreset(CompareBuildProfile::Melee, paladin, false);
        w.Label = paladin ? L"Auto: Paladin" : L"Auto: Melee";
        return w;
    }

    if (caster >= 0.40 && martial < 0.30)
    {
        BuildStatWeights w = MakeProfilePreset(CompareBuildProfile::Caster, false, reagentCaster);
        w.Label = L"Auto: Caster";
        return w;
    }

    if (martial >= 0.30 && caster >= 0.30)
    {
        BuildStatWeights w = MakeProfilePreset(CompareBuildProfile::Hybrid, chiv >= 35.0, reagentCaster);
        if (!reagentCaster)
        {
            w.ArmorLrc = 0.20;
            w.JewelLrc = 0.20;
            w.WeaponLrc = 0.10;
        }
        w.Label = L"Auto: Hybrid";
        return w;
    }

    if (martial >= caster)
    {
        const bool paladin = (chiv >= 35.0);
        BuildStatWeights w = MakeProfilePreset(CompareBuildProfile::Melee, paladin, false);
        w.Label = paladin ? L"Auto: Paladin" : L"Auto: Melee";
        return w;
    }

    BuildStatWeights w = MakeProfilePreset(CompareBuildProfile::Caster, false, reagentCaster);
    w.Label = L"Auto: Caster";
    return w;
}

bool IsSkillRelevantToArchetype(int skillIdx, CompareBuildProfile archetype, bool isPaladin)
{
    if (skillIdx < 0)
    {
        return true;
    }
    switch (skillIdx)
    {
        case kSkillMagicResist:
        case kSkillHealing:
        case kSkillFocus:
        case kSkillParry:
            return true;
        case kSkillSwords:
        case kSkillMacing:
        case kSkillFencing:
        case kSkillArchery:
        case kSkillThrowing:
        case kSkillTactics:
        case kSkillAnatomy:
        case kSkillBushido:
        case kSkillNinjitsu:
        case kSkillLumberjacking:
            return archetype != CompareBuildProfile::Caster;
        case kSkillChivalry:
            return archetype != CompareBuildProfile::Caster || isPaladin;
        case kSkillMagery:
        case kSkillEvalInt:
        case kSkillMeditation:
        case kSkillNecromancy:
        case kSkillSpiritSpeak:
        case kSkillSpellweaving:
        case kSkillMysticism:
        case kSkillInscribe:
            return archetype == CompareBuildProfile::Caster ||
                   archetype == CompareBuildProfile::Hybrid;
        case kSkillAnimalTaming:
        case kSkillAnimalLore:
        case kSkillVeterinary:
        case kSkillMusicianship:
        case kSkillProvocation:
        case kSkillPeacemaking:
        case kSkillDiscordance:
            return archetype == CompareBuildProfile::Hybrid ||
                   archetype == CompareBuildProfile::Caster;
        default:
            return false;
    }
}

double EffectiveSkillBonusSum(
    const ParsedEquipmentStats &stats,
    const BuildStatWeights &weights,
    const CharacterCompareContext *ctx)
{
    if (stats.SkillBonuses.empty())
    {
        return 0.0;
    }

    const bool useCharacterSkills = (ctx != nullptr && ctx->Valid && ctx->HasSkills);
    const bool useArchetypeFilter = (ctx != nullptr && ctx->Valid);

    double total = 0.0;
    for (const auto &entry : stats.SkillBonuses)
    {
        const int idx = SkillClilocToIndex(entry.SkillCliloc);
        if (!useArchetypeFilter || idx < 0)
        {
            total += entry.Value;
            continue;
        }

        if (useCharacterSkills)
        {
            const double playerSkill = SkillVal(ctx, idx);
            if (playerSkill >= 30.0)
            {
                total += entry.Value;
            }
            else if (IsSkillRelevantToArchetype(idx, weights.EffectiveArchetype, weights.IsPaladin))
            {
                total += entry.Value * 0.50;
            }
            else
            {
                total += entry.Value * kIrrelevantSkillFactor;
            }
        }
        else
        {
            if (IsSkillRelevantToArchetype(idx, weights.EffectiveArchetype, weights.IsPaladin))
            {
                total += entry.Value;
            }
            else
            {
                total += entry.Value * kIrrelevantSkillFactor;
            }
        }
    }
    return total;
}

OtherGearBaseline MakeBaselineWithout(
    const CharacterCompareContext *ctx,
    const ParsedEquipmentStats &removed,
    const BuildStatWeights &weights)
{
    OtherGearBaseline b;
    if (ctx == nullptr || !ctx->Valid || !ctx->HasStatusTotals)
    {
        return b;
    }

    b.Active = true;
    b.MaxPhys = ctx->MaxPhysicalResist > 0 ? ctx->MaxPhysicalResist : 70;
    b.MaxFire = ctx->MaxFireResist > 0 ? ctx->MaxFireResist : 70;
    b.MaxCold = ctx->MaxColdResist > 0 ? ctx->MaxColdResist : 70;
    b.MaxPoison = ctx->MaxPoisonResist > 0 ? ctx->MaxPoisonResist : 70;
    b.MaxEnergy = ctx->MaxEnergyResist > 0 ? ctx->MaxEnergyResist : 70;

    auto resolveOtherResist = [](int statusTotal, int removedVal, bool hasOther, int otherScanned) {
        const int fromStatus = std::max(0, statusTotal - removedVal);
        return hasOther ? std::max(fromStatus, std::max(0, otherScanned)) : fromStatus;
    };

    b.Phys = resolveOtherResist(
        ctx->PhysicalResist, removed.PhysicalResist, ctx->HasOtherEquippedResists,
        ctx->OtherEquippedPhysicalResist);
    b.Fire = resolveOtherResist(
        ctx->FireResist, removed.FireResist, ctx->HasOtherEquippedResists,
        ctx->OtherEquippedFireResist);
    b.Cold = resolveOtherResist(
        ctx->ColdResist, removed.ColdResist, ctx->HasOtherEquippedResists,
        ctx->OtherEquippedColdResist);
    b.Poison = resolveOtherResist(
        ctx->PoisonResist, removed.PoisonResist, ctx->HasOtherEquippedResists,
        ctx->OtherEquippedPoisonResist);
    b.Energy = resolveOtherResist(
        ctx->EnergyResist, removed.EnergyResist, ctx->HasOtherEquippedResists,
        ctx->OtherEquippedEnergyResist);

    b.MaxDci = ctx->MaxDefenseChance > 0 ? ctx->MaxDefenseChance : 45;
    b.MaxHci = ctx->MaxAttackChance > 0 ? ctx->MaxAttackChance : 45;
    b.MaxSsi = ctx->MaxWeaponSpeed > 0 ? ctx->MaxWeaponSpeed : 60;
    b.MaxDi = ctx->MaxWeaponDamage > 0 ? ctx->MaxWeaponDamage : 100;
    b.MaxLrc = ctx->MaxLowerReagentCost > 0 ? ctx->MaxLowerReagentCost : 100;
    b.MaxLmc = ctx->MaxLowerManaCost > 0 ? ctx->MaxLowerManaCost : 40;
    b.MaxFc = ctx->MaxCastSpeed > 0 ? ctx->MaxCastSpeed : (weights.IsPaladin ? 4 : 2);
    if (weights.IsPaladin && b.MaxFc < 4)
    {
        b.MaxFc = 4;
    }
    b.MaxFcr = ctx->MaxCastRecovery > 0 ? ctx->MaxCastRecovery : 6;

    b.Dci = std::max(0, ctx->DefenseChance - removed.DefenseChanceIncrease);
    b.Hci = std::max(0, ctx->AttackChance - removed.HitChanceIncrease);
    b.Ssi = std::max(0, ctx->WeaponSpeed - removed.SwingSpeedIncrease);
    b.Di = std::max(0, ctx->WeaponDamage - removed.DamageIncrease);
    b.Lrc = std::max(0, ctx->LowerReagentCost - removed.LowerReagentCost);
    b.Lmc = std::max(0, ctx->LowerManaCost - removed.LowerManaCost);
    b.Fc = std::max(0, ctx->CastSpeed - removed.FasterCasting);
    b.Fcr = std::max(0, ctx->CastRecovery - removed.FasterCastRecovery);
    return b;
}

OtherGearBaseline AdvanceBaseline(const OtherGearBaseline &base, const ParsedEquipmentStats &added)
{
    OtherGearBaseline next = base;
    if (!next.Active)
    {
        return next;
    }
    next.Phys += added.PhysicalResist;
    next.Fire += added.FireResist;
    next.Cold += added.ColdResist;
    next.Poison += added.PoisonResist;
    next.Energy += added.EnergyResist;
    next.Dci += added.DefenseChanceIncrease;
    next.Hci += added.HitChanceIncrease;
    next.Ssi += added.SwingSpeedIncrease;
    next.Di += added.DamageIncrease;
    next.Lrc += added.LowerReagentCost;
    next.Lmc += added.LowerManaCost;
    next.Fc += added.FasterCasting;
    next.Fcr += added.FasterCastRecovery;
    return next;
}

double ScoreCappedAttr(int otherVal, int itemVal, int cap, double weight, bool baselineActive)
{
    if (itemVal == 0 || weight == 0.0)
    {
        return 0.0;
    }
    if (!baselineActive || cap <= 0)
    {
        return static_cast<double>(itemVal) * weight;
    }

    const int effBase = std::max(0, std::min(otherVal, cap));
    const int effWith = std::max(0, std::min(otherVal + itemVal, cap));
    const int effContrib = effWith - effBase;
    const int overcapContrib = itemVal - effContrib;
    return static_cast<double>(effContrib) * weight +
           static_cast<double>(overcapContrib) * (weight * kOvercapBufferFactor);
}

// Cumulative EHP-aware resistance value curve V(x, cap):
// Marginal slope rises smoothly from 1.0x at 0% to 1.55x at cap, then drops to 0.10x overcap.
double ResistCurveValue(int level, int cap)
{
    if (level <= 0 || cap <= 0)
    {
        return static_cast<double>(level);
    }
    const double c = static_cast<double>(cap);
    if (level <= cap)
    {
        const double x = static_cast<double>(level);
        return x + 0.55 * (x * x) / (2.0 * c);
    }
    const double atCap = c + 0.55 * c * 0.5;
    return atCap + kOvercapBufferFactor * static_cast<double>(level - cap);
}

double ScoreResistsOnBaseline(
    const ParsedEquipmentStats &stats,
    const OtherGearBaseline &base,
    double resistWeight)
{
    if (!base.Active)
    {
        return static_cast<double>(stats.TotalResists()) * resistWeight;
    }

    const double avgOther =
        static_cast<double>(base.Phys + base.Fire + base.Cold + base.Poison + base.Energy) / 5.0;
    auto scoreOne = [&](int other, int itemVal, int cap) -> double {
        if (itemVal == 0)
        {
            return 0.0;
        }
        double holeMult = 1.0;
        if (avgOther >= 20.0 && static_cast<double>(other) < avgOther - 10.0)
        {
            const double deficit =
                ClampDouble((avgOther - static_cast<double>(other)) / std::max(20.0, avgOther), 0.0, 1.0);
            holeMult = 1.0 + 0.35 * deficit;
        }
        const double deltaV = ResistCurveValue(other + itemVal, cap) - ResistCurveValue(other, cap);
        return deltaV * resistWeight * holeMult;
    };

    return scoreOne(base.Phys, stats.PhysicalResist, base.MaxPhys) +
           scoreOne(base.Fire, stats.FireResist, base.MaxFire) +
           scoreOne(base.Cold, stats.ColdResist, base.MaxCold) +
           scoreOne(base.Poison, stats.PoisonResist, base.MaxPoison) +
           scoreOne(base.Energy, stats.EnergyResist, base.MaxEnergy);
}

double EffectiveSecondsOnBaseline(
    const ParsedEquipmentStats &weapon,
    const OtherGearBaseline &base,
    const CharacterCompareContext *ctx)
{
    if (!weapon.HasWeaponSpeed || weapon.WeaponSpeed <= 0.0)
    {
        return 0.0;
    }

    const int maxSsi = base.Active ? base.MaxSsi : 60;
    const int totalSsi = std::max(
        -50, std::min(maxSsi, (base.Active ? base.Ssi : 0) + weapon.SwingSpeedIncrease));

    if (weapon.SpeedIsMlSeconds)
    {
        if (ctx != nullptr && ctx->Valid && (ctx->Stam > 0 || ctx->Dex > 0))
        {
            const int stam = ctx->Stam > 0 ? ctx->Stam : ctx->Dex;
            const int stamTicks = std::max(0, stam / 30);
            const double baseTicks = weapon.WeaponSpeed * 4.0;
            const double scaledTicks =
                std::floor((baseTicks - static_cast<double>(stamTicks)) * (100.0 / (100.0 + totalSsi)));
            return std::max(5.0, scaledTicks) * 0.25;
        }
        return std::max(1.25, weapon.WeaponSpeed * (100.0 / (100.0 + static_cast<double>(totalSsi))));
    }

    const double baseSec = std::max(1.25, 400.0 / std::max(1.0, weapon.WeaponSpeed));
    return std::max(1.25, baseSec * (100.0 / (100.0 + static_cast<double>(totalSsi))));
}

double EffectiveDpsOnBaseline(
    const ParsedEquipmentStats &weapon,
    const OtherGearBaseline &base,
    const CharacterCompareContext *ctx)
{
    if (!weapon.HasWeaponDamage)
    {
        return 0.0;
    }
    const double effSec = EffectiveSecondsOnBaseline(weapon, base, ctx);
    if (effSec <= 0.0)
    {
        return 0.0;
    }
    const int maxDi = base.Active ? base.MaxDi : 100;
    const int totalDi =
        std::max(0, std::min(maxDi, (base.Active ? base.Di : 0) + weapon.DamageIncrease));
    const double diMult = 1.0 + static_cast<double>(totalDi) / 100.0;
    return (weapon.AverageDamage() * diMult) / effSec;
}

double WeaponSkillUsabilityFactor(
    const ParsedEquipmentStats &weapon,
    const CharacterCompareContext *ctx,
    wstr_t *warningOut = nullptr)
{
    if (ctx == nullptr || !ctx->Valid || !ctx->HasSkills || !weapon.HasWeaponDamage)
    {
        return 1.0;
    }
    if (weapon.UseBestSkill || weapon.WeaponSkillCliloc == 0u)
    {
        return 1.0;
    }
    if (weapon.HasMageWeapon && SkillVal(ctx, kSkillMagery) >= 50.0)
    {
        return 1.0;
    }
    const int reqIdx = WeaponSkillClilocToIndex(weapon.WeaponSkillCliloc);
    if (reqIdx < 0)
    {
        return 1.0;
    }

    const double reqSkill = SkillVal(ctx, reqIdx);
    const double bestSkill = BestWeaponSkillValue(ctx);
    if (bestSkill >= 50.0 && reqSkill < 20.0)
    {
        if (warningOut != nullptr)
        {
            *warningOut = L"Low skill: " + wstr_t(WeaponSkillDisplayName(weapon.WeaponSkillCliloc)) +
                          L" (" + FormatFloat1(reqSkill, false) + L")";
        }
        return 0.45;
    }
    return 1.0;
}

CapImpact EvaluateCappedSwap(int otherVal, int cap, int equippedVal, int hoveredVal)
{
    CapImpact c;
    c.RawDelta = hoveredVal - equippedVal;
    c.Cap = cap;
    c.EffBefore = std::max(0, std::min(otherVal + equippedVal, cap));
    c.EffAfter = std::max(0, std::min(otherVal + hoveredVal, cap));
    c.EffDelta = c.EffAfter - c.EffBefore;
    return c;
}

void AnnotateCappedDeltas(
    std::vector<EquipmentStatDelta> &deltas,
    const ParsedEquipmentStats &hovered,
    const ParsedEquipmentStats &equipped,
    const OtherGearBaseline &base)
{
    if (!base.Active)
    {
        return;
    }

    auto applyCap = [&](const wchar_t *label, int otherVal, int cap, int eqVal, int hovVal, bool isPct) {
        if (cap <= 0 || eqVal == hovVal)
        {
            return;
        }
        const CapImpact imp = EvaluateCappedSwap(otherVal, cap, eqVal, hovVal);
        if (imp.EffDelta == imp.RawDelta)
        {
            return; // Completely uncapped: no annotation needed
        }

        for (auto &d : deltas)
        {
            if (d.Label != label)
            {
                continue;
            }
            if (imp.EffDelta == 0)
            {
                d.MutedByCap = true;
                d.CapNote = (imp.RawDelta > 0) ? L"(capped)" : L"(still capped)";
            }
            else
            {
                d.CapNote = L"(" + FormatInt(imp.EffDelta, true) + (isPct ? L"% eff)" : L" eff)");
            }
            break;
        }
    };

    applyCap(L"Phys Resist", base.Phys, base.MaxPhys, equipped.PhysicalResist, hovered.PhysicalResist, true);
    applyCap(L"Fire Resist", base.Fire, base.MaxFire, equipped.FireResist, hovered.FireResist, true);
    applyCap(L"Cold Resist", base.Cold, base.MaxCold, equipped.ColdResist, hovered.ColdResist, true);
    applyCap(L"Poison Resist", base.Poison, base.MaxPoison, equipped.PoisonResist, hovered.PoisonResist, true);
    applyCap(L"Energy Resist", base.Energy, base.MaxEnergy, equipped.EnergyResist, hovered.EnergyResist, true);

    applyCap(L"Def Chance", base.Dci, base.MaxDci, equipped.DefenseChanceIncrease, hovered.DefenseChanceIncrease, true);
    applyCap(L"Hit Chance", base.Hci, base.MaxHci, equipped.HitChanceIncrease, hovered.HitChanceIncrease, true);
    applyCap(L"Swing Speed", base.Ssi, base.MaxSsi, equipped.SwingSpeedIncrease, hovered.SwingSpeedIncrease, true);
    applyCap(L"Damage Inc", base.Di, base.MaxDi, equipped.DamageIncrease, hovered.DamageIncrease, true);
    applyCap(L"Lower Reag", base.Lrc, base.MaxLrc, equipped.LowerReagentCost, hovered.LowerReagentCost, true);
    applyCap(L"Lower Mana", base.Lmc, base.MaxLmc, equipped.LowerManaCost, hovered.LowerManaCost, true);
    applyCap(L"Faster Cast", base.Fc, base.MaxFc, equipped.FasterCasting, hovered.FasterCasting, false);
    applyCap(L"Cast Recovery", base.Fcr, base.MaxFcr, equipped.FasterCastRecovery, hovered.FasterCastRecovery, false);

    // Also annotate Total Resists when effective resist change differs from raw sum
    const int rawResDelta = hovered.TotalResists() - equipped.TotalResists();
    if (rawResDelta != 0)
    {
        const int effResDelta =
            EvaluateCappedSwap(base.Phys, base.MaxPhys, equipped.PhysicalResist, hovered.PhysicalResist).EffDelta +
            EvaluateCappedSwap(base.Fire, base.MaxFire, equipped.FireResist, hovered.FireResist).EffDelta +
            EvaluateCappedSwap(base.Cold, base.MaxCold, equipped.ColdResist, hovered.ColdResist).EffDelta +
            EvaluateCappedSwap(base.Poison, base.MaxPoison, equipped.PoisonResist, hovered.PoisonResist).EffDelta +
            EvaluateCappedSwap(base.Energy, base.MaxEnergy, equipped.EnergyResist, hovered.EnergyResist).EffDelta;
        if (effResDelta != rawResDelta)
        {
            for (auto &d : deltas)
            {
                if (d.Label == L"Total Resists")
                {
                    if (effResDelta == 0)
                    {
                        d.MutedByCap = true;
                        d.CapNote = (rawResDelta > 0) ? L"(capped)" : L"(still capped)";
                    }
                    else
                    {
                        d.CapNote = L"(" + FormatInt(effResDelta, true) + L"% eff)";
                    }
                    break;
                }
            }
        }
    }
}

void CollectUsabilityWarnings(
    std::vector<wstr_t> &warnings,
    const ParsedEquipmentStats &hovered,
    const CharacterCompareContext *ctx)
{
    if (ctx != nullptr && ctx->Valid)
    {
        if (ctx->Str > 0 && hovered.StrengthReq > ctx->Str)
        {
            warnings.push_back(
                L"Needs " + FormatInt(hovered.StrengthReq, false) + L" Str (have " +
                FormatInt(ctx->Str, false) + L")");
        }

        wstr_t skillWarn;
        WeaponSkillUsabilityFactor(hovered, ctx, &skillWarn);
        if (!skillWarn.empty())
        {
            warnings.push_back(std::move(skillWarn));
        }
    }

    if (hovered.HasDurability && hovered.DurabilityMax > 0 &&
        hovered.DurabilityCur * 4 < hovered.DurabilityMax)
    {
        warnings.push_back(
            L"Low durability (" + FormatInt(hovered.DurabilityCur, false) + L"/" +
            FormatInt(hovered.DurabilityMax, false) + L")");
    }
}

double ScoreEquipmentOnBaseline(
    const ParsedEquipmentStats &stats,
    EquipCategory category,
    const BuildStatWeights &w,
    const OtherGearBaseline &base,
    const CharacterCompareContext *ctx)
{
    EquipCategory effectiveCat = category;
    if (effectiveCat == EquipCategory::Unknown)
    {
        effectiveCat = stats.HasWeaponIdentity() ? EquipCategory::Weapon : EquipCategory::JewelryOrUtility;
    }

    double score = 0.0;
    const double skillSum = EffectiveSkillBonusSum(stats, w, ctx);
    const bool capActive = base.Active;

    if (effectiveCat == EquipCategory::Weapon)
    {
        const double usability = WeaponSkillUsabilityFactor(stats, ctx);
        const double effDps = EffectiveDpsOnBaseline(stats, base, ctx);
        score += effDps * w.WeaponDps * usability;
        score += ScoreCappedAttr(base.Di, stats.DamageIncrease, base.MaxDi, w.WeaponDi, capActive);
        score += ScoreCappedAttr(base.Ssi, stats.SwingSpeedIncrease, base.MaxSsi, w.WeaponSsi, capActive);
        score += ScoreCappedAttr(base.Hci, stats.HitChanceIncrease, base.MaxHci, w.WeaponHci, capActive);
        score += ScoreCappedAttr(base.Dci, stats.DefenseChanceIncrease, base.MaxDci, w.WeaponDci, capActive);
        score += static_cast<double>(stats.Velocity) * w.WeaponVelocity;
        score += stats.Balanced ? w.WeaponBalanced : 0.0;

        score += static_cast<double>(stats.HitLifeLeech + stats.HitManaLeech) * w.WeaponLeech;
        score += static_cast<double>(stats.HitStamLeech) * w.WeaponStamLeech;
        score += static_cast<double>(stats.HitLowerDefense) * w.WeaponHld;
        score += static_cast<double>(stats.HitLowerAttack) * w.WeaponHla;
        score += static_cast<double>(stats.HitLightning + stats.HitFireball) * w.WeaponSpellProc;
        score += static_cast<double>(stats.HitHarm + stats.HitMagicArrow + stats.HitDispel) * w.WeaponMinorProc;
        score += static_cast<double>(stats.TotalHitArea()) * w.WeaponHitArea;

        score += stats.SpellChanneling ? w.WeaponSpellChanneling : 0.0;
        score += stats.UseBestSkill ? w.WeaponUseBestSkill : 0.0;
        score += static_cast<double>(stats.SlayerCount()) * w.WeaponSlayer;
        score += static_cast<double>(stats.SpellDamageIncrease) * w.WeaponSdi;
        score += ScoreCappedAttr(base.Lmc, stats.LowerManaCost, base.MaxLmc, w.WeaponLmc, capActive);
        score += ScoreCappedAttr(base.Lrc, stats.LowerReagentCost, base.MaxLrc, w.WeaponLrc, capActive);
        score += ScoreCappedAttr(base.Fc, stats.FasterCasting, base.MaxFc, w.WeaponFc, capActive);
        score += ScoreCappedAttr(base.Fcr, stats.FasterCastRecovery, base.MaxFcr, w.WeaponFcr, capActive);
        score += ScoreResistsOnBaseline(stats, base, w.WeaponResist);
        score += static_cast<double>(stats.BonusStr) * w.WeaponStr +
                 static_cast<double>(stats.BonusDex) * w.WeaponDex +
                 static_cast<double>(stats.BonusInt) * w.WeaponInt;
        score += static_cast<double>(stats.BonusHits) * w.WeaponHits +
                 static_cast<double>(stats.BonusStam) * w.WeaponStam +
                 static_cast<double>(stats.BonusMana) * w.WeaponMana;
        score += static_cast<double>(stats.RegenHits) * w.WeaponRegenHits +
                 static_cast<double>(stats.RegenMana) * w.WeaponRegenMana +
                 static_cast<double>(stats.RegenStam) * w.WeaponRegenStam;
        score += static_cast<double>(stats.Luck) * w.WeaponLuck;
        score += static_cast<double>(stats.SelfRepair) * w.WeaponSelfRepair;
        score += skillSum * w.WeaponSkill;
        score += static_cast<double>(stats.WeaponLevel) * w.WeaponLevel;
        if (stats.HasMageWeapon)
        {
            const double mageWepMult =
                (w.EffectiveArchetype == CompareBuildProfile::Caster) ? 0.55 : 0.30;
            score += static_cast<double>(std::max(0, 30 - stats.MageWeaponPenalty)) * mageWepMult;
        }
        score += static_cast<double>(stats.ReflectPhysical) * w.WeaponReflect;
        score += static_cast<double>(stats.EnhancePotions) * w.WeaponEnhancePotions;
    }
    else if (effectiveCat == EquipCategory::ArmorOrShield)
    {
        score += ScoreResistsOnBaseline(stats, base, w.ArmorResist);
        score += ScoreCappedAttr(base.Lmc, stats.LowerManaCost, base.MaxLmc, w.ArmorLmc, capActive);
        score += ScoreCappedAttr(base.Lrc, stats.LowerReagentCost, base.MaxLrc, w.ArmorLrc, capActive);
        score += static_cast<double>(stats.RegenHits) * w.ArmorRegenHits;
        score += static_cast<double>(stats.RegenMana) * w.ArmorRegenMana;
        score += static_cast<double>(stats.RegenStam) * w.ArmorRegenStam;
        score += ScoreCappedAttr(base.Dci, stats.DefenseChanceIncrease, base.MaxDci, w.ArmorDci, capActive);
        score += ScoreCappedAttr(base.Hci, stats.HitChanceIncrease, base.MaxHci, w.ArmorHci, capActive);
        score += ScoreCappedAttr(base.Di, stats.DamageIncrease, base.MaxDi, w.ArmorDi, capActive);
        score += ScoreCappedAttr(base.Ssi, stats.SwingSpeedIncrease, base.MaxSsi, w.ArmorSsi, capActive);
        score += static_cast<double>(stats.SpellDamageIncrease) * w.ArmorSdi;
        score += ScoreCappedAttr(base.Fc, stats.FasterCasting, base.MaxFc, w.ArmorFc, capActive);
        score += ScoreCappedAttr(base.Fcr, stats.FasterCastRecovery, base.MaxFcr, w.ArmorFcr, capActive);
        score += static_cast<double>(stats.BonusStr) * w.ArmorStr +
                 static_cast<double>(stats.BonusDex) * w.ArmorDex +
                 static_cast<double>(stats.BonusInt) * w.ArmorInt;
        score += static_cast<double>(stats.BonusHits) * w.ArmorHits +
                 static_cast<double>(stats.BonusStam) * w.ArmorStam +
                 static_cast<double>(stats.BonusMana) * w.ArmorMana;
        score += static_cast<double>(stats.ReflectPhysical) * w.ArmorReflect;
        score += static_cast<double>(stats.Luck) * w.ArmorLuck;
        score += skillSum * w.ArmorSkill;
        score += stats.MageArmor ? w.ArmorMageArmor : 0.0;
        score += stats.SpellChanneling ? w.ArmorSpellChanneling : 0.0;
        score += static_cast<double>(stats.SelfRepair) * w.ArmorSelfRepair;
        score += static_cast<double>(stats.EnhancePotions) * w.ArmorEnhancePotions;
        score += stats.NightSight ? w.ArmorNightSight : 0.0;
        score += static_cast<double>(stats.SlayerCount()) * w.ArmorSlayer;
    }
    else
    {
        // JewelryOrUtility
        score += ScoreCappedAttr(base.Di, stats.DamageIncrease, base.MaxDi, w.JewelDi, capActive);
        score += static_cast<double>(stats.SpellDamageIncrease) * w.JewelSdi;
        score += ScoreCappedAttr(base.Hci, stats.HitChanceIncrease, base.MaxHci, w.JewelHci, capActive);
        score += ScoreCappedAttr(base.Dci, stats.DefenseChanceIncrease, base.MaxDci, w.JewelDci, capActive);
        score += ScoreCappedAttr(base.Ssi, stats.SwingSpeedIncrease, base.MaxSsi, w.JewelSsi, capActive);
        score += ScoreCappedAttr(base.Fc, stats.FasterCasting, base.MaxFc, w.JewelFc, capActive);
        score += ScoreCappedAttr(base.Fcr, stats.FasterCastRecovery, base.MaxFcr, w.JewelFcr, capActive);
        score += ScoreCappedAttr(base.Lmc, stats.LowerManaCost, base.MaxLmc, w.JewelLmc, capActive);
        score += ScoreCappedAttr(base.Lrc, stats.LowerReagentCost, base.MaxLrc, w.JewelLrc, capActive);
        score += ScoreResistsOnBaseline(stats, base, w.JewelResist);
        score += static_cast<double>(stats.BonusStr) * w.JewelStr +
                 static_cast<double>(stats.BonusDex) * w.JewelDex +
                 static_cast<double>(stats.BonusInt) * w.JewelInt;
        score += static_cast<double>(stats.BonusHits) * w.JewelHits +
                 static_cast<double>(stats.BonusStam) * w.JewelStam +
                 static_cast<double>(stats.BonusMana) * w.JewelMana;
        score += static_cast<double>(stats.RegenHits) * w.JewelRegenHits +
                 static_cast<double>(stats.RegenMana) * w.JewelRegenMana +
                 static_cast<double>(stats.RegenStam) * w.JewelRegenStam;
        score += skillSum * w.JewelSkill;
        score += static_cast<double>(stats.Luck) * w.JewelLuck;
        score += static_cast<double>(stats.EnhancePotions) * w.JewelEnhancePotions;
        score += static_cast<double>(stats.ReflectPhysical) * w.JewelReflect;
        score += stats.SpellChanneling ? w.JewelSpellChanneling : 0.0;
        score += static_cast<double>(stats.SlayerCount()) * w.JewelSlayer;
        score += static_cast<double>(stats.TalismanKiller + stats.TalismanProtection) * w.JewelTalismanCombat;
        score += static_cast<double>(stats.TalismanExceptional + stats.TalismanSuccess) * w.JewelTalismanCraft;
        score += static_cast<double>(stats.QuiverDamageMod) * w.JewelQuiverDmg;
        score += stats.NightSight ? w.JewelNightSight : 0.0;
        score += static_cast<double>(stats.SelfRepair) * w.JewelSelfRepair;
    }

    score -= static_cast<double>(stats.IncreasedKarmaLoss) * 0.5;
    return score;
}

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
            auto bucket = [](const EquipmentStatDelta &d) {
                if (d.IsInformational)
                {
                    return 3;
                }
                if (d.MutedByCap)
                {
                    return 2;
                }
                return IsDeltaGain(d) ? 0 : 1;
            };
            const int bucketA = bucket(a);
            const int bucketB = bucket(b);
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
    if (equipped.HasWeaponIdentity())
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
        if (d.MutedByCap)
        {
            color = kColorMuted;
        }
        else if (IsDeltaGain(d))
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
    if (!d.CapNote.empty())
    {
        line += wstr_t(kColorMuted) + L" " + SanitizeAsciiLabel(d.CapNote);
    }
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

    for (const wstr_t &warn : entry.Warnings)
    {
        const bool isHardReq = (warn.find(L"Needs ") == 0u);
        html += wstr_t(isHardReq ? kColorRed : kColorGold) + L"* " + SanitizeAsciiLabel(warn) +
                kColorWhite + L"\n";
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
    if (stats.LowerReagentCost > 0)
    {
        addStat(L"Lower Reag", FormatInt(stats.LowerReagentCost, false) + L"%");
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

bool ParsedEquipmentStats::HasWeaponIdentity() const
{
    return HasWeaponDamage || HasWeaponSpeed || IsOneHanded || IsTwoHanded ||
           WeaponSkillCliloc != 0u;
}

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
    if (stats.HasWeaponIdentity())
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

    if (tileLayer == OL_NECKLACE)
    {
        // Gorgets carry armor resists/durability; necklaces behave as jewelry
        if (stats.TotalResists() > 0 || stats.MageArmor || stats.HasDurability)
        {
            return EquipCategory::ArmorOrShield;
        }
        return EquipCategory::JewelryOrUtility;
    }

    // Non-hand armor layers: UO tiledata.mul sets TileFlag.Weapon (0x02) on most
    // armor/jewelry graphics, so never treat a non-hand armor layer as a Weapon.
    switch (tileLayer)
    {
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

    if (tileLayer == OL_1_HAND)
    {
        if (isWeapon)
        {
            return EquipCategory::Weapon;
        }
        // Non-weapon in 1H slot (e.g., Spellbook)
        return EquipCategory::JewelryOrUtility;
    }

    if (tileLayer == OL_2_HAND)
    {
        // Shields on OL_2_HAND also carry TileFlag.Weapon in tiledata.mul, but lack
        // weapon OPL clilocs and carry armor resists/durability/DCI instead.
        if (stats.TotalResists() > 0 || stats.MageArmor || stats.HasDurability ||
            stats.DefenseChanceIncrease > 0 || stats.ReflectPhysical > 0)
        {
            return EquipCategory::ArmorOrShield;
        }
        if (isWeapon)
        {
            return EquipCategory::Weapon;
        }
        return EquipCategory::ArmorOrShield;
    }

    if (isWeapon)
    {
        return EquipCategory::Weapon;
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

int SkillClilocToIndex(uint32_t skillCliloc)
{
    switch (skillCliloc)
    {
        case 1002070u:
            return kSkillEvalInt;
        case 1002078u:
            return 19; // Forensics
        case 1002097u:
            return 24; // Lockpicking
        default:
            break;
    }
    if (skillCliloc >= 1044060u && skillCliloc < 1044060u + static_cast<uint32_t>(kCompareSkillCount))
    {
        return static_cast<int>(skillCliloc - 1044060u);
    }
    return -1;
}

wstr_t ResolveBuildProfileLabel(const CharacterCompareContext *ctx)
{
    return ResolveBuildWeights(ctx).Label;
}

double ScoreEquipment(
    const ParsedEquipmentStats &stats,
    EquipCategory category,
    const CharacterCompareContext *ctx)
{
    const BuildStatWeights weights = ResolveBuildWeights(ctx);
    const ParsedEquipmentStats emptyRemoved;
    const OtherGearBaseline base = MakeBaselineWithout(ctx, emptyRemoved, weights);
    return ScoreEquipmentOnBaseline(stats, category, weights, base, ctx);
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
    const wstr_t &targetLabel,
    const CharacterCompareContext *ctx)
{
    EquipmentComparisonEntry entry;
    entry.TargetLabel = SanitizeAsciiLabel(targetLabel);

    const BuildStatWeights weights = ResolveBuildWeights(ctx);
    entry.ProfileLabel = weights.Label;
    const OtherGearBaseline base = MakeBaselineWithout(ctx, equipped, weights);

    entry.HoveredScore = ScoreEquipmentOnBaseline(hovered, category, weights, base, ctx);

    const EquipCategory eqCategory = InferEquippedCategory(equipped, category);

    entry.EquippedScore = ScoreEquipmentOnBaseline(equipped, eqCategory, weights, base, ctx);
    entry.ScoreDelta = entry.HoveredScore - entry.EquippedScore;
    entry.ScoreDeltaPct = ComputeScoreDeltaPct(entry.ScoreDelta, entry.EquippedScore);
    entry.Verdict = ClassifyVerdict(entry.ScoreDelta, entry.EquippedScore);

    CollectUsabilityWarnings(entry.Warnings, hovered, ctx);

    // Weapon derived & core deltas
    if (hovered.HasWeaponDamage || equipped.HasWeaponDamage)
    {
        const double hovEffDps = EffectiveDpsOnBaseline(hovered, base, ctx);
        const double eqEffDps = EffectiveDpsOnBaseline(equipped, base, ctx);
        AddFloatDelta(entry.Deltas, L"Eff DPS", hovEffDps, eqEffDps, false);
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
    AnnotateCappedDeltas(entry.Deltas, hovered, equipped, base);
    SortDeltasForDisplay(entry.Deltas);
    return entry;
}

EquipmentComparisonEntry CompareAgainstCombinedStats(
    const ParsedEquipmentStats &hovered,
    const ParsedEquipmentStats &eqA,
    const ParsedEquipmentStats &eqB,
    EquipCategory category,
    const wstr_t &targetLabel,
    const CharacterCompareContext *ctx)
{
    EquipmentComparisonEntry entry;
    entry.TargetLabel = SanitizeAsciiLabel(targetLabel);

    const BuildStatWeights weights = ResolveBuildWeights(ctx);
    entry.ProfileLabel = weights.Label;

    const ParsedEquipmentStats combined = CombineStatsForComparison(eqA, eqB);
    const OtherGearBaseline base = MakeBaselineWithout(ctx, combined, weights);

    entry.HoveredScore = ScoreEquipmentOnBaseline(hovered, category, weights, base, ctx);

    const EquipCategory catA = InferEquippedCategory(eqA, EquipCategory::Weapon);
    const EquipCategory catB = InferEquippedCategory(eqB, EquipCategory::Weapon);
    const OtherGearBaseline baseAfterA = AdvanceBaseline(base, eqA);
    entry.EquippedScore = ScoreEquipmentOnBaseline(eqA, catA, weights, base, ctx) +
                          ScoreEquipmentOnBaseline(eqB, catB, weights, baseAfterA, ctx);
    entry.ScoreDelta = entry.HoveredScore - entry.EquippedScore;
    entry.ScoreDeltaPct = ComputeScoreDeltaPct(entry.ScoreDelta, entry.EquippedScore);
    entry.Verdict = ClassifyVerdict(entry.ScoreDelta, entry.EquippedScore);

    CollectUsabilityWarnings(entry.Warnings, hovered, ctx);

    // Combined independent-hand DPS (dual-wield swings both hands; 1H+shield swings 1H only)
    const double eqEffDps = (eqA.HasWeaponDamage ? EffectiveDpsOnBaseline(eqA, base, ctx) : 0.0) +
                            (eqB.HasWeaponDamage ? EffectiveDpsOnBaseline(eqB, baseAfterA, ctx) : 0.0);
    const double eqBaseDps = (eqA.HasWeaponDamage ? eqA.BaseDps() : 0.0) +
                             (eqB.HasWeaponDamage ? eqB.BaseDps() : 0.0);

    if (hovered.HasWeaponDamage || eqA.HasWeaponDamage || eqB.HasWeaponDamage)
    {
        AddFloatDelta(
            entry.Deltas, L"Eff DPS", EffectiveDpsOnBaseline(hovered, base, ctx), eqEffDps, false);
        AddFloatDelta(entry.Deltas, L"Base DPS", hovered.BaseDps(), eqBaseDps, false);
    }

    CollectSharedAttributeDeltas(entry.Deltas, hovered, combined);
    AnnotateCappedDeltas(entry.Deltas, hovered, combined, base);
    SortDeltasForDisplay(entry.Deltas);
    return entry;
}

EquipmentCompareResult BuildEquipmentComparison(
    const EquipmentCompareItemInput &hovered,
    const std::vector<EquipmentCompareItemInput> &equippedTargets,
    int totalSlotsInFamily,
    int occupiedSlotsInFamily,
    const CharacterCompareContext *ctx)
{
    EquipmentCompareResult result;
    if (!hovered.PropertiesLoaded)
    {
        return result;
    }

    const ParsedEquipmentStats hoveredStats = ParseEquipmentOpl(hovered.Properties);
    const EquipCategory hoveredCat =
        ClassifyEquipmentCategory(hoveredStats, hovered.Layer, hovered.IsWeapon);
    const bool hoveredIsWeapon = (hoveredCat == EquipCategory::Weapon);
    const BuildStatWeights weights = ResolveBuildWeights(ctx);
    const wstr_t headerTitle = (ctx != nullptr && ctx->Valid)
                                   ? (L"--- COMPARE [" + SanitizeAsciiLabel(weights.Label) + L"] ---")
                                   : L"--- COMPARE ---";

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
        const double hoveredScore = ScoreEquipment(hoveredStats, hoveredCat, ctx);
        const UpgradeVerdict emptyVerdict = ClassifyVerdict(hoveredScore, 0.0);
        EquipmentComparisonEntry emptyEntry;
        emptyEntry.TargetLabel = L"Empty Slot";
        emptyEntry.ProfileLabel = weights.Label;
        emptyEntry.HoveredScore = hoveredScore;
        emptyEntry.EquippedScore = 0.0;
        emptyEntry.ScoreDelta = hoveredScore;
        emptyEntry.ScoreDeltaPct = ComputeScoreDeltaPct(hoveredScore, 0.0);
        emptyEntry.Verdict = emptyVerdict;
        CollectUsabilityWarnings(emptyEntry.Warnings, hoveredStats, ctx);
        result.Comparisons.push_back(emptyEntry);

        ComparisonCardOutput card;
        card.AlignLeft = true;
        card.MaxWidth = 260;
        card.BorderColorARGB = BorderColorForVerdict(emptyVerdict);

        wstr_t html;
        html += wstr_t(kColorGold) + headerTitle + kColorWhite + L"\n";
        html += FormatVerdictBanner(emptyVerdict, emptyEntry.ScoreDeltaPct, false) + L"\n";
        if (!openSlotBanner.empty())
        {
            html += wstr_t(kColorBlue) + L"* " + openSlotBanner + kColorWhite + L"\n";
        }
        for (const wstr_t &warn : emptyEntry.Warnings)
        {
            const bool isHardReq = (warn.find(L"Needs ") == 0u);
            html += wstr_t(isHardReq ? kColorRed : kColorGold) + L"* " + SanitizeAsciiLabel(warn) +
                    kColorWhite + L"\n";
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
    html += wstr_t(kColorGold) + headerTitle + kColorWhite + L"\n";
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
            const bool aIsWeapon =
                ClassifyEquipmentCategory(
                    statsA, equippedTargets[0].Layer, equippedTargets[0].IsWeapon) ==
                EquipCategory::Weapon;
            const bool bIsWeapon =
                ClassifyEquipmentCategory(
                    statsB, equippedTargets[1].Layer, equippedTargets[1].IsWeapon) ==
                EquipCategory::Weapon;
            const wstr_t combinedLabel =
                (aIsWeapon && bIsWeapon) ? L"Combined (MH + OH)" : L"Combined (1H + Shield)";

            EquipmentComparisonEntry combined = CompareAgainstCombinedStats(
                hoveredStats, statsA, statsB, hoveredCat, combinedLabel, ctx);
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
                ClassifyEquipmentCategory(eqStats, eq.Layer, eq.IsWeapon) == EquipCategory::Weapon)
            {
                note = L"Replaces Off-Hand / 2H";
            }

            result.Comparisons.push_back(
                CompareSingleStats(hoveredStats, eqStats, hoveredCat, targetLabel, ctx));
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
