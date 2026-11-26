// SPDX-License-Identifier: MIT

#include "EquipmentCompare.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace
{
bool ApproxEq(double a, double b, double eps = 1e-3)
{
    return std::fabs(a - b) <= eps;
}

bool Contains(const wstr_t &haystack, const wchar_t *needle)
{
    return haystack.find(needle) != wstr_t::npos;
}

bool IsHexDigit(wchar_t ch)
{
    return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F');
}

void AssertStrictAsciiAndSixDigitColors(const wstr_t &html)
{
    // 1. Zero non-ASCII characters (no Unicode triangles/tofu glyphs in UO bitmap fonts).
    for (wchar_t ch : html)
    {
        assert(ch == L'\n' || (ch >= 0x20 && ch <= 0x7E));
    }

    // 2. Every <basefont color="..."> tag must use exact 6-digit #RRGGBB.
    const wstr_t needle = L"<basefont color=\"";
    size_t pos = 0;
    int tagCount = 0;
    while ((pos = html.find(needle, pos)) != wstr_t::npos)
    {
        ++tagCount;
        const size_t valStart = pos + needle.size();
        const size_t valEnd = html.find(L'"', valStart);
        assert(valEnd != wstr_t::npos);
        const wstr_t colorVal = html.substr(valStart, valEnd - valStart);
        assert(colorVal.size() == 7u);
        assert(colorVal[0] == L'#');
        for (size_t i = 1; i < 7u; ++i)
        {
            assert(IsHexDigit(colorVal[i]));
        }
        pos = valEnd + 1;
    }
    assert(tagCount > 0);
}

const EquipmentStatDelta *FindDelta(
    const std::vector<EquipmentStatDelta> &deltas,
    const wchar_t *label)
{
    for (const auto &d : deltas)
    {
        if (d.Label == label)
        {
            return &d;
        }
    }
    return nullptr;
}

void TestParseAllClilocGroups()
{
    OplPropertyList props = {
        // Weapon core & ranged
        { 1061168u, L"15\t19" },
        { 1061167u, L"2.5s" },
        { 1061824u, L"" },
        { 1061169u, L"10" },
        { 1042971u, L"Min Throw Range\t4" },
        { 1061175u, L"" }, // Archery
        { 1060403u, L"20" },
        { 1060404u, L"15" },
        { 1060405u, L"25" },
        { 1060406u, L"10" },
        { 1060407u, L"10" },
        { 1072846u, L"10" },
        { 1079978u, L"10" },
        { 1072792u, L"" },
        { 1072793u, L"35" },
        { 1062414u, L"12" },

        // Shared AosAttributes
        { 1060401u, L"50" },
        { 1060486u, L"30" },
        { 1060415u, L"15" },
        { 1060408u, L"12" },
        { 1060482u, L"" },
        { 1060483u, L"18" },
        { 1060433u, L"8" },
        { 1060434u, L"20" },
        { 1060413u, L"1" },
        { 1060412u, L"3" },
        { 1060485u, L"8" },
        { 1060409u, L"6" },
        { 1060432u, L"7" },
        { 1060431u, L"5" },
        { 1060439u, L"10" },
        { 1060484u, L"9" },
        { 1060444u, L"2" },
        { 1060440u, L"3" },
        { 1060443u, L"4" },
        { 1060436u, L"140" },
        { 1060442u, L"15" },
        { 1060411u, L"25" },
        { 1060441u, L"" },
        { 1075210u, L"10" },

        // Weapon / Armor attrs
        { 1060400u, L"" },
        { 1060438u, L"5" }, // -5 skill
        { 1060422u, L"40" },
        { 1060427u, L"45" },
        { 1060430u, L"30" },
        { 1060423u, L"50" },
        { 1060420u, L"25" },
        { 1060421u, L"20" },
        { 1060426u, L"15" },
        { 1060424u, L"35" },
        { 1060425u, L"45" },
        { 1060417u, L"10" },
        { 1060416u, L"10" },
        { 1060418u, L"12" },
        { 1060419u, L"14" },
        { 1060428u, L"16" },
        { 1060429u, L"18" },
        { 1060450u, L"5" },
        { 1060437u, L"" },

        // Resists, Skills, Slayers, Durability, Misc
        { 1060448u, L"11" },
        { 1060447u, L"12" },
        { 1060445u, L"13" },
        { 1060449u, L"14" },
        { 1060446u, L"15" },
        { 1060451u, L"#1044100\t15" },
        { 1060452u, L"#1044101\t10.5" },
        { 1060479u, L"" }, // Undead Slayer
        { 1070855u, L"" }, // Fey Slayer
        { 1017396u, L"" }, // Pre-AOS Slayer
        { 1072506u, L"" }, // Talisman Slayer
        { 1060639u, L"210\t255" },
        { 1060410u, L"50" },
        { 1060435u, L"40" },
        { 1061170u, L"45" },
        { 1061078u, L"11" },
        { 1074762u, L"10" },
        { 1072388u, L"Dragon\t25" },
        { 1072387u, L"Daemon\t20" },
        { 1072395u, L"Blacksmithy\t15" },
        { 1072394u, L"Blacksmithy\t10" },

        // Custom passthrough clilocs
        { 1042971u, L"Material\tPlatinum" },
        { 1070722u, L"Weapon level\t42" },
    };

    const ParsedEquipmentStats s = ParseEquipmentOpl(props);

    assert(s.HasWeaponDamage);
    assert(s.MinDamage == 15 && s.MaxDamage == 19);
    assert(ApproxEq(s.AverageDamage(), 17.0));
    assert(s.HasWeaponSpeed && s.SpeedIsMlSeconds && ApproxEq(s.WeaponSpeed, 2.5));
    assert(s.IsOneHanded && !s.IsTwoHanded);
    assert(s.MaxRange == 10 && s.MinThrowRange == 4);
    assert(s.WeaponSkillCliloc == 1061175u);
    assert(s.PhysDamagePct == 20 && s.ColdDamagePct == 15 && s.FireDamagePct == 25);
    assert(s.PoisonDamagePct == 10 && s.EnergyDamagePct == 10);
    assert(s.ChaosDamagePct == 10 && s.DirectDamagePct == 10);
    assert(s.Balanced && s.Velocity == 35 && s.PoisonCharges == 12);

    assert(s.DamageIncrease == 50 && s.SwingSpeedIncrease == 30);
    assert(s.HitChanceIncrease == 15 && s.DefenseChanceIncrease == 12);
    assert(s.SpellChanneling && s.SpellDamageIncrease == 18);
    assert(s.LowerManaCost == 8 && s.LowerReagentCost == 20);
    assert(s.FasterCasting == 1 && s.FasterCastRecovery == 3);
    assert(s.BonusStr == 8 && s.BonusDex == 6 && s.BonusInt == 7);
    assert(s.BonusHits == 5 && s.BonusMana == 10 && s.BonusStam == 9);
    assert(s.RegenHits == 2 && s.RegenMana == 3 && s.RegenStam == 4);
    assert(s.Luck == 140 && s.ReflectPhysical == 15 && s.EnhancePotions == 25);
    assert(s.NightSight && s.IncreasedKarmaLoss == 10);

    assert(s.UseBestSkill && s.HasMageWeapon && s.MageWeaponPenalty == 5);
    assert(s.HitLifeLeech == 40 && s.HitManaLeech == 45 && s.HitStamLeech == 30);
    assert(s.HitLightning == 50 && s.HitFireball == 25 && s.HitHarm == 20 && s.HitMagicArrow == 15);
    assert(s.HitLowerAttack == 35 && s.HitLowerDefense == 45 && s.HitDispel == 10);
    assert(s.HitColdArea == 10 && s.HitEnergyArea == 12 && s.HitFireArea == 14);
    assert(s.HitPhysicalArea == 16 && s.HitPoisonArea == 18 && s.TotalHitArea() == 70);
    assert(s.SelfRepair == 5 && s.MageArmor);

    assert(s.PhysicalResist == 11 && s.FireResist == 12 && s.ColdResist == 13);
    assert(s.PoisonResist == 14 && s.EnergyResist == 15 && s.TotalResists() == 65);
    assert(s.SkillBonuses.size() == 2u);
    assert(s.SkillBonuses[0].SkillCliloc == 1044100u && ApproxEq(s.SkillBonuses[0].Value, 15.0));
    assert(s.SkillBonuses[1].SkillCliloc == 1044101u && ApproxEq(s.SkillBonuses[1].Value, 10.5));
    assert(ApproxEq(s.TotalSkillBonuses(), 25.5));
    assert(s.SlayerCount() == 4);
    assert(s.HasDurability && s.DurabilityCur == 210 && s.DurabilityMax == 255);
    assert(s.DurabilityBonus == 50 && s.LowerStatReq == 40 && s.StrengthReq == 45);
    assert(s.ArtifactRarity == 11 && s.QuiverDamageMod == 10);
    assert(s.TalismanKiller == 25 && s.TalismanProtection == 20);
    assert(s.TalismanExceptional == 15 && s.TalismanSuccess == 10);

    assert(s.MaterialGrade == L"Platinum");
    assert(s.WeaponLevel == 42);

    // Check remaining passthrough clilocs (1114057, 1114778, 1114779)
    OplPropertyList passProps = {
        { 1114057u, L"Material\tBlaze" },
        { 1114778u, L"Weapon level\t100" },
    };
    const ParsedEquipmentStats p2 = ParseEquipmentOpl(passProps);
    assert(p2.MaterialGrade == L"Blaze");
    assert(p2.WeaponLevel == 100);

    OplPropertyList passProps3 = {
        { 1114779u, L"Material\tElectrum" },
    };
    assert(ParseEquipmentOpl(passProps3).MaterialGrade == L"Electrum");
}

void TestWeaponSpeedAndSsiCap()
{
    // ML seconds: 2.0s + 60% SSI -> 2.0 * (100/160) = 1.25s exact cap
    const ParsedEquipmentStats mlCap60 = ParseEquipmentOpl({
        { 1061168u, L"10\t20" }, // avg 15
        { 1061167u, L"2.0s" },
        { 1060401u, L"50" }, // 1.5x -> 22.5 dmg/swing
        { 1060486u, L"60" },
    });
    assert(mlCap60.SpeedIsMlSeconds);
    assert(ApproxEq(mlCap60.BaseSeconds(), 2.0));
    assert(ApproxEq(mlCap60.EffectiveSeconds(), 1.25));
    assert(ApproxEq(mlCap60.BaseDps(), 7.5));
    assert(ApproxEq(mlCap60.EffectiveDps(), 22.5 / 1.25)); // 18.0

    // SSI above 60% must still clamp to 60% / 1.25s cap
    const ParsedEquipmentStats mlOverCap = ParseEquipmentOpl({
        { 1061168u, L"10\t20" },
        { 1061167u, L"2.0s" },
        { 1060401u, L"50" },
        { 1060486u, L"90" },
    });
    assert(ApproxEq(mlOverCap.EffectiveSeconds(), 1.25));
    assert(ApproxEq(mlOverCap.EffectiveDps(), mlCap60.EffectiveDps()));

    // Very fast 1.5s weapon + 30% SSI -> 1.1538s clamped up to 1.25s
    const ParsedEquipmentStats mlFast = ParseEquipmentOpl({
        { 1061168u, L"10\t10" },
        { 1061167u, L"1.5s" },
        { 1060486u, L"30" },
    });
    assert(ApproxEq(mlFast.EffectiveSeconds(), 1.25));
    assert(ApproxEq(mlFast.EffectiveDps(), 10.0 / 1.25));

    // Pre-ML speed (no trailing 's'): higher number = faster!
    const ParsedEquipmentStats preMlSlow = ParseEquipmentOpl({
        { 1061168u, L"10\t20" },
        { 1061167u, L"40" }, // BaseSec = 400/40 = 10.0s
    });
    const ParsedEquipmentStats preMlFast = ParseEquipmentOpl({
        { 1061168u, L"10\t20" },
        { 1061167u, L"50" }, // BaseSec = 400/50 = 8.0s
    });
    assert(!preMlSlow.SpeedIsMlSeconds && !preMlFast.SpeedIsMlSeconds);
    assert(ApproxEq(preMlSlow.BaseSeconds(), 10.0));
    assert(ApproxEq(preMlFast.BaseSeconds(), 8.0));
    assert(preMlFast.EffectiveDps() > preMlSlow.EffectiveDps());

    // Pre-ML speed delta: higher is better (LowerIsBetter == false)
    const EquipmentComparisonEntry preMlCmp =
        CompareSingleStats(preMlFast, preMlSlow, EquipCategory::Weapon, L"Main Hand");
    assert(preMlCmp.Verdict == UpgradeVerdict::Upgrade);
    const EquipmentStatDelta *preMlSpd = FindDelta(preMlCmp.Deltas, L"Speed");
    assert(preMlSpd != nullptr && !preMlSpd->LowerIsBetter && ApproxEq(preMlSpd->Delta, 10.0));

    // ML speed delta: lower is better (LowerIsBetter == true)
    const ParsedEquipmentStats mlSlow = ParseEquipmentOpl({
        { 1061168u, L"10\t20" },
        { 1061167u, L"3.0s" },
    });
    const ParsedEquipmentStats mlFaster = ParseEquipmentOpl({
        { 1061168u, L"10\t20" },
        { 1061167u, L"2.5s" },
    });
    const EquipmentComparisonEntry mlCmp =
        CompareSingleStats(mlFaster, mlSlow, EquipCategory::Weapon, L"Main Hand");
    assert(mlCmp.Verdict == UpgradeVerdict::Upgrade);
    const EquipmentStatDelta *mlSpd = FindDelta(mlCmp.Deltas, L"Speed");
    assert(mlSpd != nullptr && mlSpd->LowerIsBetter && ApproxEq(mlSpd->Delta, -0.5));
}

void TestMageWeaponLowerIsBetter()
{
    const ParsedEquipmentStats mageBest = ParseEquipmentOpl({
        { 1061168u, L"12\t14" },
        { 1061167u, L"2.5s" },
        { 1060438u, L"0" }, // -0 skill (best!)
    });
    const ParsedEquipmentStats mageWorst = ParseEquipmentOpl({
        { 1061168u, L"12\t14" },
        { 1061167u, L"2.5s" },
        { 1060438u, L"29" }, // -29 skill (worst!)
    });

    assert(mageBest.HasMageWeapon && mageBest.MageWeaponPenalty == 0);
    assert(mageWorst.HasMageWeapon && mageWorst.MageWeaponPenalty == 29);
    assert(
        ScoreEquipment(mageBest, EquipCategory::Weapon) >
        ScoreEquipment(mageWorst, EquipCategory::Weapon));

    const EquipmentComparisonEntry cmp =
        CompareSingleStats(mageBest, mageWorst, EquipCategory::Weapon, L"Main Hand");
    assert(cmp.Verdict == UpgradeVerdict::Upgrade);
    const EquipmentStatDelta *mw = FindDelta(cmp.Deltas, L"Mage Weapon");
    assert(mw != nullptr);
    assert(mw->LowerIsBetter);
    assert(ApproxEq(mw->Delta, -29.0));
}

void TestCategoryClassification()
{
    const ParsedEquipmentStats sword = ParseEquipmentOpl({ { 1061168u, L"11\t15" } });
    assert(ClassifyEquipmentCategory(sword, OL_1_HAND, true) == EquipCategory::Weapon);

    const ParsedEquipmentStats shield = ParseEquipmentOpl({ { 1060448u, L"5" }, { 1060408u, L"15" } });
    assert(ClassifyEquipmentCategory(shield, OL_2_HAND, false) == EquipCategory::ArmorOrShield);

    const ParsedEquipmentStats spellbook = ParseEquipmentOpl({ { 1060483u, L"12" }, { 1060413u, L"1" } });
    assert(ClassifyEquipmentCategory(spellbook, OL_1_HAND, false) == EquipCategory::JewelryOrUtility);

    const ParsedEquipmentStats gorget = ParseEquipmentOpl({ { 1060448u, L"15" }, { 1060447u, L"12" } });
    assert(ClassifyEquipmentCategory(gorget, OL_NECKLACE, false) == EquipCategory::ArmorOrShield);

    const ParsedEquipmentStats necklace = ParseEquipmentOpl({ { 1060436u, L"100" }, { 1060401u, L"15" } });
    assert(ClassifyEquipmentCategory(necklace, OL_NECKLACE, false) == EquipCategory::JewelryOrUtility);

    const ParsedEquipmentStats quiver = ParseEquipmentOpl({ { 1074762u, L"10" } });
    assert(ClassifyEquipmentCategory(quiver, OL_CLOAK, false) == EquipCategory::JewelryOrUtility);

    const ParsedEquipmentStats ring = ParseEquipmentOpl({ { 1060401u, L"25" } });
    assert(ClassifyEquipmentCategory(ring, OL_RING_4, false) == EquipCategory::JewelryOrUtility);
    assert(ClassifyEquipmentCategory(ring, OL_BRACELET_2, false) == EquipCategory::JewelryOrUtility);
    assert(ClassifyEquipmentCategory(ring, OL_EARRINGS_2, false) == EquipCategory::JewelryOrUtility);
}

void TestOneHandedVsDualWield()
{
    EquipmentCompareItemInput hovered;
    hovered.Serial = 0x40000010u;
    hovered.Layer = OL_1_HAND;
    hovered.IsWeapon = true;
    hovered.PropertiesLoaded = true;
    hovered.Name = L"Blaze Katana";
    hovered.Properties = {
        { 1061168u, L"14\t18" },
        { 1061167u, L"2.5s" },
        { 1061824u, L"" },
        { 1060401u, L"45" },
        { 1060486u, L"25" },
        { 1060422u, L"40" },
        { 1042971u, L"Material\tBlaze" },
        { 1070722u, L"Weapon level\t35" },
    };

    EquipmentCompareItemInput mh;
    mh.Serial = 0x40000001u;
    mh.Layer = OL_1_HAND;
    mh.IsWeapon = true;
    mh.PropertiesLoaded = true;
    mh.SlotLabel = L"Main Hand";
    mh.FormattedTooltip = L"Platinum Katana\nWeapon Damage 15 - 19";
    mh.Properties = {
        { 1061168u, L"15\t19" },
        { 1061167u, L"2.5s" },
        { 1061824u, L"" },
        { 1060401u, L"50" },
        { 1060486u, L"30" },
        { 1060422u, L"50" },
    };

    EquipmentCompareItemInput oh;
    oh.Serial = 0x40000002u;
    oh.Layer = OL_2_HAND;
    oh.IsWeapon = true;
    oh.IsOffHandWeapon = true;
    oh.PropertiesLoaded = true;
    oh.SlotLabel = L"Off Hand";
    oh.FormattedTooltip = L"Iron Kryss\nWeapon Damage 10 - 12";
    oh.Properties = {
        { 1061168u, L"10\t12" },
        { 1061167u, L"2.5s" },
        { 1061171u, L"" }, // Off-hand quirk emits Two-Handed cliloc
        { 1060401u, L"15" },
    };

    const EquipmentCompareResult res = BuildEquipmentComparison(hovered, { mh, oh }, 2, 2);
    assert(res.Valid && !res.LoadingEquipped);
    assert(res.Comparisons.size() == 2u);
    assert(res.Comparisons[0].TargetLabel == L"Main Hand");
    assert(res.Comparisons[0].Verdict == UpgradeVerdict::Downgrade);
    assert(res.Comparisons[1].TargetLabel == L"Off Hand");
    assert(res.Comparisons[1].Verdict == UpgradeVerdict::Upgrade);

    // 3 side cards: Compare card + Main Hand + Off Hand
    assert(res.SideCards.size() == 3u);
    assert(res.SideCards[0].AlignLeft && res.SideCards[0].MaxWidth == 260);
    assert(res.SideCards[1].AlignLeft == false && res.SideCards[1].MaxWidth == 270);
    assert(res.SideCards[1].BorderColorARGB == 0xFFD8B058u);
    assert(res.SideCards[2].BorderColorARGB == 0xFFD8B058u);

    AssertStrictAsciiAndSixDigitColors(res.SideCards[0].HtmlText);
    AssertStrictAsciiAndSixDigitColors(res.SideCards[1].HtmlText);
    AssertStrictAsciiAndSixDigitColors(res.SideCards[2].HtmlText);

    assert(Contains(res.SideCards[0].HtmlText, L"Best swap:"));
    assert(Contains(res.SideCards[0].HtmlText, L"Off Hand"));
    assert(Contains(res.SideCards[0].HtmlText, L"vs Main Hand"));
    assert(Contains(res.SideCards[0].HtmlText, L"vs Off Hand"));
    assert(Contains(res.SideCards[1].HtmlText, L"[EQUIPPED: Main Hand]"));
    assert(Contains(res.SideCards[2].HtmlText, L"[EQUIPPED: Off Hand]"));
}

void TestTwoHandedVsDualWieldCombined()
{
    EquipmentCompareItemInput twoHand;
    twoHand.Serial = 0x40000020u;
    twoHand.Layer = OL_2_HAND;
    twoHand.IsWeapon = true;
    twoHand.PropertiesLoaded = true;
    twoHand.Properties = {
        { 1061168u, L"18\t22" }, // avg 20
        { 1061167u, L"3.0s" },
        { 1061171u, L"" },
        { 1060401u, L"50" }, // 30 / 3.0 = 10.0 Eff DPS
    };

    EquipmentCompareItemInput mh;
    mh.Serial = 0x40000001u;
    mh.Layer = OL_1_HAND;
    mh.IsWeapon = true;
    mh.PropertiesLoaded = true;
    mh.SlotLabel = L"Main Hand";
    mh.FormattedTooltip = L"Main Katana";
    mh.Properties = {
        { 1061168u, L"14\t18" }, // avg 16
        { 1061167u, L"2.5s" },
        { 1060401u, L"40" }, // 22.4 / 2.5 = 8.96 Eff DPS
        { 1060415u, L"15" },
    };

    EquipmentCompareItemInput oh;
    oh.Serial = 0x40000002u;
    oh.Layer = OL_2_HAND;
    oh.IsWeapon = true;
    oh.IsOffHandWeapon = true;
    oh.PropertiesLoaded = true;
    oh.SlotLabel = L"Off Hand";
    oh.FormattedTooltip = L"Off Katana";
    oh.Properties = {
        { 1061168u, L"12\t16" }, // avg 14
        { 1061167u, L"2.5s" },
        { 1060401u, L"30" }, // 18.2 / 2.5 = 7.28 Eff DPS
        { 1060415u, L"10" },
    };

    const EquipmentCompareResult res = BuildEquipmentComparison(twoHand, { mh, oh }, 2, 2);
    assert(res.Valid);
    assert(res.Comparisons.size() == 1u);
    assert(res.Comparisons[0].TargetLabel == L"Combined (MH + OH)");
    assert(res.Comparisons[0].Verdict == UpgradeVerdict::Downgrade);

    // Combined Eff DPS is 8.96 + 7.28 = 16.24 vs 2H's 10.0 -> -6.24 Eff DPS
    const EquipmentStatDelta *dps = FindDelta(res.Comparisons[0].Deltas, L"Eff DPS");
    assert(dps != nullptr && ApproxEq(dps->Delta, 10.0 - 16.24, 0.05));

    // Combined HCI is 15 + 10 = 25 -> 2H has 0 -> -25% Hit Chance
    const EquipmentStatDelta *hci = FindDelta(res.Comparisons[0].Deltas, L"Hit Chance");
    assert(hci != nullptr && ApproxEq(hci->Delta, -25.0));

    assert(res.SideCards.size() == 3u);
    AssertStrictAsciiAndSixDigitColors(res.SideCards[0].HtmlText);
    assert(Contains(res.SideCards[0].HtmlText, L"vs Combined (MH + OH)"));
    assert(Contains(res.SideCards[0].HtmlText, L"Replaces both hands"));
}

void TestTwoHandedVsOneHandAndShield()
{
    EquipmentCompareItemInput halberd;
    halberd.Serial = 0x40000030u;
    halberd.Layer = OL_2_HAND;
    halberd.IsWeapon = true;
    halberd.PropertiesLoaded = true;
    halberd.Properties = {
        { 1061168u, L"20\t24" }, // avg 22
        { 1061167u, L"3.0s" },
        { 1061171u, L"" },
        { 1060401u, L"50" }, // 33 / 3.0 = 11.0 Eff DPS
    };

    EquipmentCompareItemInput sword;
    sword.Serial = 0x40000031u;
    sword.Layer = OL_1_HAND;
    sword.IsWeapon = true;
    sword.PropertiesLoaded = true;
    sword.SlotLabel = L"Main Hand";
    sword.FormattedTooltip = L"Broad Sword";
    sword.Properties = {
        { 1061168u, L"10\t14" }, // avg 12
        { 1061167u, L"2.5s" },
        { 1060401u, L"20" }, // 14.4 / 2.5 = 5.76 Eff DPS
    };

    EquipmentCompareItemInput shield;
    shield.Serial = 0x40000032u;
    shield.Layer = OL_2_HAND;
    shield.IsWeapon = false;
    shield.PropertiesLoaded = true;
    shield.SlotLabel = L"Shield / Off Hand";
    shield.FormattedTooltip = L"Order Shield";
    shield.Properties = {
        { 1060448u, L"15" },
        { 1060447u, L"15" },
        { 1060445u, L"15" },
        { 1060408u, L"15" },
        { 1060482u, L"" }, // Spell Channeling
    };

    const EquipmentCompareResult res = BuildEquipmentComparison(halberd, { sword, shield }, 2, 2);
    assert(res.Valid);
    assert(res.Comparisons.size() == 1u);
    assert(res.Comparisons[0].TargetLabel == L"Combined (1H + Shield)");

    // Gained DPS (+5.24), Lost Total Resists (-45%), Lost Def Chance (-15%), Lost Spell Channeling
    const EquipmentStatDelta *dps = FindDelta(res.Comparisons[0].Deltas, L"Eff DPS");
    assert(dps != nullptr && dps->Delta > 5.0);
    const EquipmentStatDelta *resists = FindDelta(res.Comparisons[0].Deltas, L"Total Resists");
    assert(resists != nullptr && ApproxEq(resists->Delta, -45.0));
    const EquipmentStatDelta *dci = FindDelta(res.Comparisons[0].Deltas, L"Def Chance");
    assert(dci != nullptr && ApproxEq(dci->Delta, -15.0));
    const EquipmentStatDelta *sc = FindDelta(res.Comparisons[0].Deltas, L"Spell Channeling");
    assert(sc != nullptr && sc->IsBooleanFlag && sc->Delta < 0.0);

    assert(res.SideCards.size() == 3u);
    AssertStrictAsciiAndSixDigitColors(res.SideCards[0].HtmlText);
    assert(Contains(res.SideCards[0].HtmlText, L"vs Combined (1H + Shield)"));
}

void TestArmorResistsLmcAndRegen()
{
    EquipmentCompareItemInput newTunic;
    newTunic.Serial = 0x40000040u;
    newTunic.Layer = OL_TORSO;
    newTunic.PropertiesLoaded = true;
    newTunic.Properties = {
        { 1060448u, L"18" },
        { 1060447u, L"16" },
        { 1060445u, L"15" },
        { 1060449u, L"14" },
        { 1060446u, L"17" }, // Total 80
        { 1060433u, L"8" },  // LMC 8%
        { 1060440u, L"2" },  // Mana Regen 2
        { 1060444u, L"2" },  // HP Regen 2
        { 1060437u, L"" },   // Mage Armor
        { 1060450u, L"5" },  // Self Repair 5
        { 1061170u, L"60" }, // Str Req 60
        { 1042971u, L"Material\tValorite" },
    };

    EquipmentCompareItemInput oldTunic;
    oldTunic.Serial = 0x40000041u;
    oldTunic.Layer = OL_TORSO;
    oldTunic.PropertiesLoaded = true;
    oldTunic.SlotLabel = L"Chest";
    oldTunic.FormattedTooltip = L"Iron Plate Chest";
    oldTunic.Properties = {
        { 1060448u, L"12" },
        { 1060447u, L"10" },
        { 1060445u, L"10" },
        { 1060449u, L"10" },
        { 1060446u, L"10" }, // Total 52
        { 1060433u, L"4" },  // LMC 4%
        { 1061170u, L"95" }, // Str Req 95
        { 1042971u, L"Material\tIron" },
    };

    const EquipmentCompareResult res = BuildEquipmentComparison(newTunic, { oldTunic }, 1, 1);
    assert(res.Valid);
    assert(res.Comparisons.size() == 1u);
    assert(res.Comparisons[0].Verdict == UpgradeVerdict::Upgrade);
    assert(res.SideCards.size() == 2u);
    assert(res.SideCards[0].BorderColorARGB == 0xFF44CC66u);

    const EquipmentStatDelta *totRes = FindDelta(res.Comparisons[0].Deltas, L"Total Resists");
    assert(totRes != nullptr && ApproxEq(totRes->Delta, 28.0));
    const EquipmentStatDelta *lmc = FindDelta(res.Comparisons[0].Deltas, L"Lower Mana");
    assert(lmc != nullptr && ApproxEq(lmc->Delta, 4.0));
    const EquipmentStatDelta *mr = FindDelta(res.Comparisons[0].Deltas, L"Mana Regen");
    assert(mr != nullptr && ApproxEq(mr->Delta, 2.0));
    const EquipmentStatDelta *strReq = FindDelta(res.Comparisons[0].Deltas, L"Str Req");
    assert(strReq != nullptr && strReq->IsInformational && !strReq->LowerIsBetter &&
           ApproxEq(strReq->Delta, -35.0));

    // Also verify Str Req emits 0-vs-95 diff (e.g. Leather vs Plate)
    const ParsedEquipmentStats leather = ParseEquipmentOpl({ { 1060448u, L"10" } });
    const ParsedEquipmentStats plate = ParseEquipmentOpl({ { 1060448u, L"10" }, { 1061170u, L"95" } });
    const EquipmentComparisonEntry strCmp =
        CompareSingleStats(leather, plate, EquipCategory::ArmorOrShield, L"Chest");
    const EquipmentStatDelta *zeroVsPlate = FindDelta(strCmp.Deltas, L"Str Req");
    assert(zeroVsPlate != nullptr && zeroVsPlate->IsInformational &&
           ApproxEq(zeroVsPlate->Delta, -95.0));

    AssertStrictAsciiAndSixDigitColors(res.SideCards[0].HtmlText);
    assert(Contains(res.SideCards[0].HtmlText, L"#44FF66"));
    assert(Contains(res.SideCards[0].HtmlText, L"Valorite (was Iron)"));
}

void TestMultiSlotJewelryBraceletsEarringsRings()
{
    // 1. 2 Bracelets with 1 open slot (1/2)
    EquipmentCompareItemInput bracNew;
    bracNew.Serial = 0x40000050u;
    bracNew.Layer = OL_BRACELET;
    bracNew.PropertiesLoaded = true;
    bracNew.Properties = {
        { 1060401u, L"25" },
        { 1060415u, L"15" },
        { 1060413u, L"1" },
        { 1060412u, L"3" },
    };

    EquipmentCompareItemInput bracLeft;
    bracLeft.Serial = 0x40000051u;
    bracLeft.Layer = OL_BRACELET;
    bracLeft.PropertiesLoaded = true;
    bracLeft.SlotLabel = L"Left Bracelet";
    bracLeft.FormattedTooltip = L"Gold Bracelet";
    bracLeft.Properties = {
        { 1060401u, L"15" },
        { 1060415u, L"10" },
    };

    const EquipmentCompareResult brac1 = BuildEquipmentComparison(bracNew, { bracLeft }, 2, 1);
    assert(brac1.Valid && brac1.Comparisons.size() == 1u);
    AssertStrictAsciiAndSixDigitColors(brac1.SideCards[0].HtmlText);
    assert(Contains(brac1.SideCards[0].HtmlText, L"Open slot available (1/2 bracelets)"));

    // 2. 2 Earrings both occupied (2/2) -> no open slot banner, Best swap shown
    EquipmentCompareItemInput earNew;
    earNew.Serial = 0x40000060u;
    earNew.Layer = OL_EARRINGS;
    earNew.PropertiesLoaded = true;
    earNew.Properties = { { 1060483u, L"12" }, { 1060433u, L"8" } };

    EquipmentCompareItemInput earL;
    earL.Serial = 0x40000061u;
    earL.Layer = OL_EARRINGS;
    earL.PropertiesLoaded = true;
    earL.SlotLabel = L"Left Earring";
    earL.FormattedTooltip = L"Left Earring";
    earL.Properties = { { 1060483u, L"15" }, { 1060433u, L"8" } };

    EquipmentCompareItemInput earR;
    earR.Serial = 0x40000062u;
    earR.Layer = OL_EARRINGS_2;
    earR.PropertiesLoaded = true;
    earR.SlotLabel = L"Right Earring";
    earR.FormattedTooltip = L"Right Earring";
    earR.Properties = { { 1060483u, L"5" } };

    const EquipmentCompareResult ear2 = BuildEquipmentComparison(earNew, { earL, earR }, 2, 2);
    assert(ear2.Valid && ear2.Comparisons.size() == 2u);
    assert(ear2.SideCards.size() == 3u);
    AssertStrictAsciiAndSixDigitColors(ear2.SideCards[0].HtmlText);
    assert(!Contains(ear2.SideCards[0].HtmlText, L"Open slot available"));
    assert(Contains(ear2.SideCards[0].HtmlText, L"Best swap:"));
    assert(Contains(ear2.SideCards[0].HtmlText, L"Right Earring"));

    // 3. 6 Rings (4/6 occupied -> open slot hint + Weakest Ring (4/6 equipped))
    EquipmentCompareItemInput ringNew;
    ringNew.Serial = 0x40000070u;
    ringNew.Layer = OL_RING;
    ringNew.PropertiesLoaded = true;
    ringNew.Properties = {
        { 1060401u, L"25" },
        { 1060486u, L"10" },
        { 1060451u, L"#1044095\t15" },
    };

    EquipmentCompareItemInput weakRing;
    weakRing.Serial = 0x40000071u;
    weakRing.Layer = OL_RING_3;
    weakRing.PropertiesLoaded = true;
    weakRing.SlotLabel = L"Weakest Ring";
    weakRing.FormattedTooltip = L"Silver Ring";
    weakRing.Properties = { { 1060436u, L"50" } };

    const EquipmentCompareResult ring4 = BuildEquipmentComparison(ringNew, { weakRing }, 6, 4);
    assert(ring4.Valid && ring4.Comparisons.size() == 1u);
    assert(ring4.Comparisons[0].TargetLabel == L"Weakest Ring (4/6 equipped)");
    AssertStrictAsciiAndSixDigitColors(ring4.SideCards[0].HtmlText);
    assert(Contains(ring4.SideCards[0].HtmlText, L"Open slot available (4/6 rings)"));
    assert(Contains(ring4.SideCards[0].HtmlText, L"vs Weakest Ring (4/6 equipped)"));

    // 6/6 full rings -> no open slot banner
    const EquipmentCompareResult ring6 = BuildEquipmentComparison(ringNew, { weakRing }, 6, 6);
    AssertStrictAsciiAndSixDigitColors(ring6.SideCards[0].HtmlText);
    assert(!Contains(ring6.SideCards[0].HtmlText, L"Open slot available"));
    assert(Contains(ring6.SideCards[0].HtmlText, L"vs Weakest Ring (6/6 equipped)"));
}

void TestEmptySlotAndLoadingStates()
{
    // Empty slot
    EquipmentCompareItemInput hovered;
    hovered.Serial = 0x40000080u;
    hovered.Layer = OL_HELMET;
    hovered.PropertiesLoaded = true;
    hovered.Properties = {
        { 1060448u, L"15" },
        { 1060447u, L"10" },
        { 1060433u, L"8" },
        { 1060436u, L"100" },
    };

    const EquipmentCompareResult emptyRes = BuildEquipmentComparison(hovered, {}, 1, 0);
    assert(emptyRes.Valid && !emptyRes.LoadingEquipped);
    assert(emptyRes.Comparisons.size() == 1u);
    assert(emptyRes.Comparisons[0].Verdict == UpgradeVerdict::Upgrade);
    assert(emptyRes.SideCards.size() == 1u);
    AssertStrictAsciiAndSixDigitColors(emptyRes.SideCards[0].HtmlText);
    assert(Contains(emptyRes.SideCards[0].HtmlText, L"UPGRADE (Empty Slot)"));
    assert(Contains(emptyRes.SideCards[0].HtmlText, L"Total Resists"));
    assert(Contains(emptyRes.SideCards[0].HtmlText, L"Lower Mana"));

    // Unloaded equipped item
    EquipmentCompareItemInput unloadedEq;
    unloadedEq.Serial = 0x40000081u;
    unloadedEq.Layer = OL_HELMET;
    unloadedEq.PropertiesLoaded = false;
    unloadedEq.SlotLabel = L"Helmet";

    const EquipmentCompareResult loadingRes = BuildEquipmentComparison(hovered, { unloadedEq }, 1, 1);
    assert(loadingRes.Valid && loadingRes.LoadingEquipped);
    assert(loadingRes.Comparisons.empty());
    assert(loadingRes.SideCards.size() == 2u);
    AssertStrictAsciiAndSixDigitColors(loadingRes.SideCards[0].HtmlText);
    AssertStrictAsciiAndSixDigitColors(loadingRes.SideCards[1].HtmlText);
    assert(Contains(loadingRes.SideCards[0].HtmlText, L"Loading equipped stats..."));
    assert(Contains(loadingRes.SideCards[1].HtmlText, L"Loading equipped stats..."));

    // Unloaded hovered item -> invalid result
    EquipmentCompareItemInput unloadedHover;
    unloadedHover.PropertiesLoaded = false;
    const EquipmentCompareResult invRes = BuildEquipmentComparison(unloadedHover, { unloadedEq }, 1, 1);
    assert(!invRes.Valid);
}

void TestKarmaLossLowerIsBetterAndSidegradeThreshold()
{
    const ParsedEquipmentStats lowKarma = ParseEquipmentOpl({
        { 1060448u, L"15" },
        { 1060447u, L"15" },
        { 1075210u, L"5" },
    });
    const ParsedEquipmentStats highKarma = ParseEquipmentOpl({
        { 1060448u, L"15" },
        { 1060447u, L"15" },
        { 1075210u, L"25" },
    });

    const EquipmentComparisonEntry cmp =
        CompareSingleStats(lowKarma, highKarma, EquipCategory::ArmorOrShield, L"Chest");
    assert(cmp.Verdict == UpgradeVerdict::Upgrade);
    const EquipmentStatDelta *karma = FindDelta(cmp.Deltas, L"Karma Loss");
    assert(karma != nullptr && karma->LowerIsBetter && ApproxEq(karma->Delta, -20.0));

    // Sidegrade within 2% threshold
    assert(ClassifyVerdict(1.5, 100.0) == UpgradeVerdict::Sidegrade);
    assert(ClassifyVerdict(-1.5, 100.0) == UpgradeVerdict::Sidegrade);
    assert(ClassifyVerdict(2.5, 100.0) == UpgradeVerdict::Upgrade);
    assert(ClassifyVerdict(-2.5, 100.0) == UpgradeVerdict::Downgrade);
}

void TestHandSwapNotes()
{
    // 1H weapon hovering when player wears a 2H weapon -> "Replaces 2H weapon"
    EquipmentCompareItemInput oneHand;
    oneHand.Serial = 0x40000090u;
    oneHand.Layer = OL_1_HAND;
    oneHand.IsWeapon = true;
    oneHand.PropertiesLoaded = true;
    oneHand.Properties = {
        { 1061168u, L"13\t17" },
        { 1061167u, L"2.5s" },
        { 1061824u, L"" },
        { 1060401u, L"40" },
    };

    EquipmentCompareItemInput twoHandEq;
    twoHandEq.Serial = 0x40000091u;
    twoHandEq.Layer = OL_2_HAND;
    twoHandEq.IsWeapon = true;
    twoHandEq.PropertiesLoaded = true;
    twoHandEq.SlotLabel = L"Two-Handed";
    twoHandEq.FormattedTooltip = L"Halberd";
    twoHandEq.Properties = {
        { 1061168u, L"18\t22" },
        { 1061167u, L"3.5s" },
        { 1061171u, L"" },
        { 1060401u, L"25" },
    };

    const EquipmentCompareResult r1 = BuildEquipmentComparison(oneHand, { twoHandEq }, 1, 1);
    assert(r1.Valid && r1.SideCards.size() == 2u);
    AssertStrictAsciiAndSixDigitColors(r1.SideCards[0].HtmlText);
    assert(Contains(r1.SideCards[0].HtmlText, L"Replaces 2H weapon"));

    // Shield hovering when player wears a 2H weapon -> "Replaces Off-Hand / 2H"
    EquipmentCompareItemInput shield;
    shield.Serial = 0x40000092u;
    shield.Layer = OL_2_HAND;
    shield.IsWeapon = false;
    shield.PropertiesLoaded = true;
    shield.Properties = {
        { 1060448u, L"10" },
        { 1060408u, L"15" },
        { 1060482u, L"" },
    };

    const EquipmentCompareResult r2 = BuildEquipmentComparison(shield, { twoHandEq }, 1, 1);
    assert(r2.Valid && r2.SideCards.size() == 2u);
    AssertStrictAsciiAndSixDigitColors(r2.SideCards[0].HtmlText);
    assert(Contains(r2.SideCards[0].HtmlText, L"Replaces Off-Hand / 2H"));
}

void TestMalformedAndEdgeOplInputs()
{
    // Build a 400-digit float overflow string to verify ParseDoubleSafe rejects infinity cleanly
    wstr_t hugeFloat(400u, L'9');
    hugeFloat += L"s";

    const ParsedEquipmentStats malformed = ParseEquipmentOpl({
        { 1061168u, L"" },
        { 1061168u, L"15\t" },
        { 1061168u, L"abc\tdef" },
        { 1061167u, L"notanumber" },
        { 1061167u, hugeFloat },
        { 1060401u, L"9999999999999999999999" },
        { 1060486u, L"-15" },
        { 1060451u, L"#bad\t10" },
        { 1060452u, L"#1044100\t" + hugeFloat },
        { 1042971u, L"Material\t   " },
        { 1070722u, L"Weapon level\t-5" },
        { 1060639u, L"100" },
    });

    assert(!malformed.HasWeaponDamage);
    assert(!malformed.HasWeaponSpeed);
    assert(malformed.DamageIncrease == 1000000000); // safely clamped
    assert(malformed.SwingSpeedIncrease == -15);
    assert(malformed.SkillBonuses.empty());
    assert(malformed.MaterialGrade.empty());
    assert(malformed.WeaponLevel == 0);
    assert(!malformed.HasDurability);

    // Comma decimal locale format ("2,5s") parses cleanly
    const ParsedEquipmentStats commaSpeed = ParseEquipmentOpl({
        { 1061168u, L"10\t20" },
        { 1061167u, L"2,5s" },
    });
    assert(commaSpeed.HasWeaponSpeed && commaSpeed.SpeedIsMlSeconds);
    assert(ApproxEq(commaSpeed.WeaponSpeed, 2.5));
}

void TestNegativeAndTinyBaselinesAndEmptySlotNegative()
{
    // Negative equippedScore (-10.0) with +0.1 delta must NOT trigger Upgrade
    assert(ClassifyVerdict(0.1, -10.0) == UpgradeVerdict::Sidegrade);
    assert(ClassifyVerdict(1.5, -10.0) == UpgradeVerdict::Upgrade);
    assert(ClassifyVerdict(-1.5, -10.0) == UpgradeVerdict::Downgrade);

    // Tiny positive baseline (Luck 1 -> score 0.12) vs Luck 5 -> score 0.60 (delta +0.48)
    // Verdict is Sidegrade; ScoreDeltaPct uses floored denom max(1.0, |eq|) so it's +48%, NOT +400%
    const ParsedEquipmentStats tinyEq = ParseEquipmentOpl({ { 1060436u, L"1" } });
    const ParsedEquipmentStats tinyHov = ParseEquipmentOpl({ { 1060436u, L"5" } });
    const EquipmentComparisonEntry tinyCmp =
        CompareSingleStats(tinyHov, tinyEq, EquipCategory::JewelryOrUtility, L"Ring");
    assert(tinyCmp.Verdict == UpgradeVerdict::Sidegrade);
    assert(tinyCmp.ScoreDeltaPct < 100.0 && tinyCmp.ScoreDeltaPct > 0.0);

    // Negative-score item (pure IncreasedKarmaLoss) in an Empty Slot must be Downgrade, not Upgrade
    EquipmentCompareItemInput cursed;
    cursed.Serial = 0x40000100u;
    cursed.Layer = OL_RING;
    cursed.PropertiesLoaded = true;
    cursed.Properties = { { 1075210u, L"20" } }; // score = -10.0

    const EquipmentCompareResult emptyNeg = BuildEquipmentComparison(cursed, {}, 6, 0);
    assert(emptyNeg.Valid);
    assert(emptyNeg.Comparisons.size() == 1u);
    assert(emptyNeg.Comparisons[0].Verdict == UpgradeVerdict::Downgrade);
    assert(emptyNeg.SideCards[0].BorderColorARGB == 0xFFCC4444u);
    AssertStrictAsciiAndSixDigitColors(emptyNeg.SideCards[0].HtmlText);
    assert(Contains(emptyNeg.SideCards[0].HtmlText, L"DOWNGRADE (Empty Slot)"));
    assert(Contains(emptyNeg.SideCards[0].HtmlText, L"Karma Loss"));
}

void TestMixedMlAndPreMlSpeedDelta()
{
    const ParsedEquipmentStats mlWeapon = ParseEquipmentOpl({
        { 1061168u, L"12\t16" },
        { 1061167u, L"2.5s" }, // BaseSeconds = 2.5s
    });
    const ParsedEquipmentStats preMlWeapon = ParseEquipmentOpl({
        { 1061168u, L"12\t16" },
        { 1061167u, L"40" }, // BaseSeconds = 400/40 = 10.0s
    });

    const EquipmentComparisonEntry cmp =
        CompareSingleStats(mlWeapon, preMlWeapon, EquipCategory::Weapon, L"Main Hand");
    assert(cmp.Verdict == UpgradeVerdict::Upgrade);
    const EquipmentStatDelta *spd = FindDelta(cmp.Deltas, L"Speed");
    assert(spd != nullptr && spd->LowerIsBetter && spd->DetailText == L"s");
    assert(ApproxEq(spd->Delta, 2.5 - 10.0));
}

void TestPartialTwoTargetLoading()
{
    // 1. Independent 2-target (dual-wield) with MH loaded and OH still loading
    EquipmentCompareItemInput hov1h;
    hov1h.Serial = 0x40000110u;
    hov1h.Layer = OL_1_HAND;
    hov1h.IsWeapon = true;
    hov1h.PropertiesLoaded = true;
    hov1h.Properties = { { 1061168u, L"14\t18" }, { 1061167u, L"2.5s" } };

    EquipmentCompareItemInput mhLoaded;
    mhLoaded.Serial = 0x40000111u;
    mhLoaded.Layer = OL_1_HAND;
    mhLoaded.IsWeapon = true;
    mhLoaded.PropertiesLoaded = true;
    mhLoaded.SlotLabel = L"Main Hand";
    mhLoaded.FormattedTooltip = L"Main Sword";
    mhLoaded.Properties = { { 1061168u, L"10\t12" }, { 1061167u, L"2.5s" } };

    EquipmentCompareItemInput ohUnloaded;
    ohUnloaded.Serial = 0x40000112u;
    ohUnloaded.Layer = OL_2_HAND;
    ohUnloaded.IsWeapon = true;
    ohUnloaded.IsOffHandWeapon = true;
    ohUnloaded.PropertiesLoaded = false;
    ohUnloaded.SlotLabel = L"Off Hand";

    const EquipmentCompareResult partialDual =
        BuildEquipmentComparison(hov1h, { mhLoaded, ohUnloaded }, 2, 2);
    assert(partialDual.Valid && partialDual.LoadingEquipped);
    assert(partialDual.Comparisons.size() == 1u);
    assert(partialDual.SideCards.size() == 3u);
    AssertStrictAsciiAndSixDigitColors(partialDual.SideCards[0].HtmlText);
    assert(Contains(partialDual.SideCards[0].HtmlText, L"Loading other slot..."));
    assert(Contains(partialDual.SideCards[2].HtmlText, L"Loading equipped stats..."));

    // 2. Combined 2H with 1 of 2 targets unloaded must wait for both before computing combined delta
    EquipmentCompareItemInput hov2h;
    hov2h.Serial = 0x40000113u;
    hov2h.Layer = OL_2_HAND;
    hov2h.IsWeapon = true;
    hov2h.PropertiesLoaded = true;
    hov2h.Properties = { { 1061168u, L"18\t22" }, { 1061167u, L"3.0s" } };

    const EquipmentCompareResult partialCombined =
        BuildEquipmentComparison(hov2h, { mhLoaded, ohUnloaded }, 2, 2);
    assert(partialCombined.Valid && partialCombined.LoadingEquipped);
    assert(partialCombined.Comparisons.empty());
    assert(partialCombined.SideCards.size() == 3u);
    AssertStrictAsciiAndSixDigitColors(partialCombined.SideCards[0].HtmlText);
    assert(Contains(partialCombined.SideCards[0].HtmlText, L"Loading equipped stats..."));
}

void TestCrossCategorySpellbookVsWeapon()
{
    const ParsedEquipmentStats spellbook = ParseEquipmentOpl({
        { 1060483u, L"12" }, // SDI 12% -> 21.6
        { 1060413u, L"1" },  // FC 1 -> 14.0
        { 1060412u, L"2" },  // FCR 2 -> 12.0
    }); // JewelryOrUtility score = 47.6

    const ParsedEquipmentStats strongSword = ParseEquipmentOpl({
        { 1061168u, L"15\t19" }, // avg 17
        { 1061167u, L"2.5s" },
        { 1060401u, L"50" }, // EffDPS = 10.2 -> 61.2 + DI 17.5 + Leech 42.5 = 121.2
        { 1060422u, L"50" },
    });

    // Direction 1: Hovering Spellbook (JewelryOrUtility) while wielding strong Weapon
    const EquipmentComparisonEntry bookVsSword = CompareSingleStats(
        spellbook, strongSword, EquipCategory::JewelryOrUtility, L"Main Hand");
    assert(bookVsSword.EquippedScore > 100.0); // Weapon DPS + leech properly scored!
    assert(bookVsSword.Verdict == UpgradeVerdict::Downgrade);

    // Direction 2: Hovering Weapon while holding Spellbook
    const EquipmentComparisonEntry swordVsBook = CompareSingleStats(
        strongSword, spellbook, EquipCategory::Weapon, L"Main Hand");
    assert(ApproxEq(swordVsBook.EquippedScore, ScoreEquipment(spellbook, EquipCategory::JewelryOrUtility)));
    assert(swordVsBook.Verdict == UpgradeVerdict::Upgrade);

    // Direction 3: Hovering 2H Weapon against Spellbook + Shield combined
    const ParsedEquipmentStats shield = ParseEquipmentOpl({
        { 1060448u, L"15" },
        { 1060447u, L"15" },
        { 1060408u, L"15" },
    });
    const EquipmentComparisonEntry twoHVsBookShield = CompareAgainstCombinedStats(
        strongSword, spellbook, shield, EquipCategory::Weapon, L"Combined (1H + Shield)");
    const double expectedCombinedScore =
        ScoreEquipment(spellbook, EquipCategory::JewelryOrUtility) +
        ScoreEquipment(shield, EquipCategory::ArmorOrShield);
    assert(ApproxEq(twoHVsBookShield.EquippedScore, expectedCombinedScore));
}

void TestIdenticalStatsAndTruncation()
{
    // 1. Identical stats -> "= Identical stats"
    EquipmentCompareItemInput itemA;
    itemA.Serial = 0x40000120u;
    itemA.Layer = OL_HELMET;
    itemA.PropertiesLoaded = true;
    itemA.SlotLabel = L"Helmet";
    itemA.FormattedTooltip = L"Plate Helm";
    itemA.Properties = { { 1060448u, L"15" }, { 1060433u, L"8" } };

    const EquipmentCompareResult identical = BuildEquipmentComparison(itemA, { itemA }, 1, 1);
    assert(identical.Valid);
    assert(identical.Comparisons.size() == 1u);
    assert(identical.Comparisons[0].Verdict == UpgradeVerdict::Sidegrade);
    assert(identical.Comparisons[0].Deltas.empty());
    AssertStrictAsciiAndSixDigitColors(identical.SideCards[0].HtmlText);
    assert(Contains(identical.SideCards[0].HtmlText, L"= Identical stats"));

    // 2. >16 deltas triggers "... +N more" truncation cleanly
    EquipmentCompareItemInput megaItem;
    megaItem.Serial = 0x40000121u;
    megaItem.Layer = OL_TORSO;
    megaItem.PropertiesLoaded = true;
    megaItem.Properties = {
        { 1060448u, L"15" }, { 1060447u, L"15" }, { 1060445u, L"15" }, { 1060449u, L"15" },
        { 1060446u, L"15" }, { 1060401u, L"20" }, { 1060486u, L"15" }, { 1060415u, L"15" },
        { 1060408u, L"15" }, { 1060483u, L"12" }, { 1060433u, L"8" },  { 1060434u, L"20" },
        { 1060413u, L"1" },  { 1060412u, L"3" },  { 1060485u, L"8" },  { 1060409u, L"8" },
        { 1060432u, L"8" },  { 1060431u, L"5" },  { 1060439u, L"5" },  { 1060484u, L"5" },
        { 1060444u, L"2" },  { 1060440u, L"2" },
    };

    EquipmentCompareItemInput plainItem;
    plainItem.Serial = 0x40000122u;
    plainItem.Layer = OL_TORSO;
    plainItem.PropertiesLoaded = true;
    plainItem.SlotLabel = L"Chest";
    plainItem.FormattedTooltip = L"Plain Tunic";
    plainItem.Properties = {};

    const EquipmentCompareResult trunc = BuildEquipmentComparison(megaItem, { plainItem }, 1, 1);
    assert(trunc.Valid);
    assert(trunc.Comparisons[0].Deltas.size() > 16u);
    AssertStrictAsciiAndSixDigitColors(trunc.SideCards[0].HtmlText);
    assert(Contains(trunc.SideCards[0].HtmlText, L"... +"));
    assert(Contains(trunc.SideCards[0].HtmlText, L" more"));
}

void TestTiledataWeaponFlagRegression()
{
    // UO tiledata.mul sets TileFlag.Weapon (0x02) on armor (ringmail/plate/etc.),
    // shields, and jewelry. Armor/jewelry/shields must NEVER be scored as Weapon.
    EquipmentCompareItemInput lootedRingmail;
    lootedRingmail.Serial = 0x40000130u;
    lootedRingmail.Layer = OL_TORSO;
    lootedRingmail.IsWeapon = true; // Simulate TileFlag.Weapon set on 0x13EC
    lootedRingmail.PropertiesLoaded = true;
    lootedRingmail.Name = L"Ringmail Tunic";
    lootedRingmail.Properties = {
        { 1060434u, L"10" },    // Lower Reagent Cost 10%
        { 1060448u, L"3" },     // Physical Resist 3%
        { 1060447u, L"3" },     // Fire Resist 3%
        { 1060445u, L"1" },     // Cold Resist 1%
        { 1060449u, L"5" },     // Poison Resist 5%
        { 1060446u, L"3" },     // Energy Resist 3%
        { 1061170u, L"40" },    // Strength Requirement 40
        { 1060639u, L"46\t46" }, // Durability 46 / 46
    };

    EquipmentCompareItemInput baseRingmail;
    baseRingmail.Serial = 0x40000131u;
    baseRingmail.Layer = OL_TORSO;
    baseRingmail.IsWeapon = true; // Simulate TileFlag.Weapon set on 0x13EC
    baseRingmail.PropertiesLoaded = true;
    baseRingmail.SlotLabel = L"Chest";
    baseRingmail.FormattedTooltip = L"Ringmail Tunic";
    baseRingmail.Properties = {
        { 1060448u, L"3" },     // Physical Resist 3%
        { 1060447u, L"3" },     // Fire Resist 3%
        { 1060445u, L"1" },     // Cold Resist 1%
        { 1060449u, L"5" },     // Poison Resist 5%
        { 1060446u, L"3" },     // Energy Resist 3%
        { 1061170u, L"40" },    // Strength Requirement 40
        { 1060639u, L"33\t42" }, // Durability 33 / 42
    };

    const EquipmentCompareResult ringmailRes =
        BuildEquipmentComparison(lootedRingmail, { baseRingmail }, 1, 1);
    assert(ringmailRes.Valid);
    assert(ringmailRes.Comparisons.size() == 1u);
    const auto &cmp = ringmailRes.Comparisons[0];
    // Balanced default: 15 resists * 1.65 = 24.75 vs +10% LRC (*0.65) = 31.25 (+26.3%)
    assert(ApproxEq(cmp.EquippedScore, 24.75));
    assert(ApproxEq(cmp.HoveredScore, 31.25));
    assert(ApproxEq(cmp.ScoreDelta, 6.5));
    assert(ApproxEq(cmp.ScoreDeltaPct, (6.5 / 24.75) * 100.0, 0.05));
    assert(cmp.Verdict == UpgradeVerdict::Upgrade);
    assert(ringmailRes.SideCards[0].BorderColorARGB == 0xFF44CC66u);
    const EquipmentStatDelta *lrc = FindDelta(cmp.Deltas, L"Lower Reag");
    assert(lrc != nullptr && ApproxEq(lrc->Delta, 10.0));
    const EquipmentStatDelta *dur = FindDelta(cmp.Deltas, L"Max Durability");
    assert(dur != nullptr && dur->IsInformational && ApproxEq(dur->Delta, 4.0));
    AssertStrictAsciiAndSixDigitColors(ringmailRes.SideCards[0].HtmlText);
    assert(Contains(ringmailRes.SideCards[0].HtmlText, L"^ UPGRADE (+26.3%)"));

    // Verify all other armor, jewelry, and shield layers ignore stray IsWeapon = true
    const ParsedEquipmentStats armorStats = ParseEquipmentOpl({
        { 1060448u, L"5" },
        { 1060639u, L"40\t40" },
    });
    assert(ClassifyEquipmentCategory(armorStats, OL_HELMET, true) == EquipCategory::ArmorOrShield);
    assert(ClassifyEquipmentCategory(armorStats, OL_GLOVES, true) == EquipCategory::ArmorOrShield);
    assert(ClassifyEquipmentCategory(armorStats, OL_ARMS, true) == EquipCategory::ArmorOrShield);
    assert(ClassifyEquipmentCategory(armorStats, OL_LEGS, true) == EquipCategory::ArmorOrShield);
    assert(ClassifyEquipmentCategory(armorStats, OL_PANTS, true) == EquipCategory::ArmorOrShield);
    assert(ClassifyEquipmentCategory(armorStats, OL_NECKLACE, true) == EquipCategory::ArmorOrShield);
    assert(ClassifyEquipmentCategory(armorStats, OL_2_HAND, true) == EquipCategory::ArmorOrShield);

    const ParsedEquipmentStats jewelStats = ParseEquipmentOpl({
        { 1060434u, L"10" },
        { 1060436u, L"50" },
    });
    assert(ClassifyEquipmentCategory(jewelStats, OL_RING, true) == EquipCategory::JewelryOrUtility);
    assert(ClassifyEquipmentCategory(jewelStats, OL_BRACELET, true) == EquipCategory::JewelryOrUtility);
    assert(ClassifyEquipmentCategory(jewelStats, OL_EARRINGS, true) == EquipCategory::JewelryOrUtility);
    assert(ClassifyEquipmentCategory(jewelStats, OL_NECKLACE, true) == EquipCategory::JewelryOrUtility);

    // Shield (OL_2_HAND with IsWeapon=true from tiledata) hovered while wielding 1H + Shield
    // must NOT be treated as a 2H weapon replacing both hands.
    EquipmentCompareItemInput betterShield;
    betterShield.Serial = 0x40000132u;
    betterShield.Layer = OL_2_HAND;
    betterShield.IsWeapon = true; // Simulate TileFlag.Weapon set on 0x1B76
    betterShield.PropertiesLoaded = true;
    betterShield.Properties = {
        { 1060448u, L"8" },
        { 1060408u, L"15" },
        { 1060639u, L"50\t50" },
    };

    EquipmentCompareItemInput oldShield;
    oldShield.Serial = 0x40000133u;
    oldShield.Layer = OL_2_HAND;
    oldShield.IsWeapon = true;
    oldShield.PropertiesLoaded = true;
    oldShield.SlotLabel = L"Shield";
    oldShield.FormattedTooltip = L"Heater Shield";
    oldShield.Properties = {
        { 1060448u, L"5" },
        { 1060639u, L"45\t45" },
    };

    const EquipmentCompareResult shieldRes =
        BuildEquipmentComparison(betterShield, { oldShield }, 1, 1);
    assert(shieldRes.Valid && shieldRes.Comparisons.size() == 1u);
    assert(shieldRes.Comparisons[0].Verdict == UpgradeVerdict::Upgrade);
    assert(!Contains(shieldRes.SideCards[0].HtmlText, L"Replaces Off-Hand / 2H"));
}

void TestSotaBuildProfilesAndStatWorth()
{
    // Compare identical armor with +10% Damage Increase vs +10% Lower Reagent Cost
    const ParsedEquipmentStats armorDi = ParseEquipmentOpl({
        { 1060448u, L"10" },
        { 1060401u, L"10" }, // +10% DI
    });
    const ParsedEquipmentStats armorLrc = ParseEquipmentOpl({
        { 1060448u, L"10" },
        { 1060434u, L"10" }, // +10% LRC
    });

    // 1. Default Balanced (no context): +10% DI clearly beats +10% LRC
    const EquipmentComparisonEntry balancedCmp =
        CompareSingleStats(armorDi, armorLrc, EquipCategory::ArmorOrShield, L"Chest");
    assert(balancedCmp.Verdict == UpgradeVerdict::Upgrade);
    assert(balancedCmp.ScoreDelta > 7.0);

    // 2. Auto-detected Melee swordsman: +10% DI overwhelmingly beats +10% LRC
    CharacterCompareContext meleeCtx;
    meleeCtx.Valid = true;
    meleeCtx.Profile = CompareBuildProfile::Auto;
    meleeCtx.HasSkills = true;
    meleeCtx.Skills[40] = 100.0; // Swordsmanship
    meleeCtx.Skills[27] = 100.0; // Tactics
    meleeCtx.Skills[1] = 100.0;  // Anatomy
    assert(ResolveBuildProfileLabel(&meleeCtx) == L"Auto: Melee");
    const EquipmentComparisonEntry meleeCmp =
        CompareSingleStats(armorDi, armorLrc, EquipCategory::ArmorOrShield, L"Chest", &meleeCtx);
    assert(meleeCmp.Verdict == UpgradeVerdict::Upgrade);
    assert(meleeCmp.ScoreDelta > 15.0);

    // 3. Auto-detected Paladin (Swords + Chivalry, no Magery): DI beats LRC, FC/LMC valued
    CharacterCompareContext paladinCtx = meleeCtx;
    paladinCtx.Skills[51] = 85.0; // Chivalry
    assert(ResolveBuildProfileLabel(&paladinCtx) == L"Auto: Paladin");
    const EquipmentComparisonEntry palCmp =
        CompareSingleStats(armorDi, armorLrc, EquipCategory::ArmorOrShield, L"Chest", &paladinCtx);
    assert(palCmp.Verdict == UpgradeVerdict::Upgrade);

    // 4. Auto-detected Pure Magery Caster: +10% LRC (uncapped) beats +10% DI
    CharacterCompareContext casterCtx;
    casterCtx.Valid = true;
    casterCtx.Profile = CompareBuildProfile::Auto;
    casterCtx.HasSkills = true;
    casterCtx.Skills[25] = 100.0; // Magery
    casterCtx.Skills[16] = 100.0; // EvalInt
    casterCtx.Skills[46] = 100.0; // Meditation
    assert(ResolveBuildProfileLabel(&casterCtx) == L"Auto: Caster");
    const EquipmentComparisonEntry casterCmp =
        CompareSingleStats(armorDi, armorLrc, EquipCategory::ArmorOrShield, L"Chest", &casterCtx);
    assert(casterCmp.Verdict == UpgradeVerdict::Downgrade);

    // 5. Skill relevance: +15 Swordsmanship beats +15 Begging on a Melee character
    assert(SkillClilocToIndex(1002070u) == 16); // EvalInt special cliloc
    assert(SkillClilocToIndex(1044060u + 40u) == 40); // Swordsmanship
    const ParsedEquipmentStats ringSwords = ParseEquipmentOpl({ { 1060451u, L"#1044100\t15" } }); // Swords
    const ParsedEquipmentStats ringBegging = ParseEquipmentOpl({ { 1060451u, L"#1044066\t15" } }); // Begging
    const EquipmentComparisonEntry skillCmp = CompareSingleStats(
        ringSwords, ringBegging, EquipCategory::JewelryOrUtility, L"Ring", &meleeCtx);
    assert(skillCmp.Verdict == UpgradeVerdict::Upgrade);
    assert(skillCmp.HoveredScore > skillCmp.EquippedScore * 5.0);

    // 6. Tank & Luck manual overrides
    CharacterCompareContext luckCtx;
    luckCtx.Valid = true;
    luckCtx.Profile = CompareBuildProfile::Luck;
    const ParsedEquipmentStats luckyArmor = ParseEquipmentOpl({ { 1060436u, L"100" } }); // +100 Luck
    const ParsedEquipmentStats resistArmor = ParseEquipmentOpl({ { 1060448u, L"10" } }); // +10 Phys
    assert(
        CompareSingleStats(luckyArmor, resistArmor, EquipCategory::ArmorOrShield, L"Chest", &luckCtx)
            .Verdict == UpgradeVerdict::Upgrade);
    assert(
        CompareSingleStats(luckyArmor, resistArmor, EquipCategory::ArmorOrShield, L"Chest", &meleeCtx)
            .Verdict == UpgradeVerdict::Downgrade);
}

void TestSotaCapAwareScoringResistsDpsAndWarnings()
{
    // 1. Caster ALREADY at 100% LRC: +15% LRC overcap must be marked (capped) and NOT dominate +8% SDI
    CharacterCompareContext capCaster;
    capCaster.Valid = true;
    capCaster.Profile = CompareBuildProfile::Caster;
    capCaster.HasStatusTotals = true;
    capCaster.LowerReagentCost = 100;
    capCaster.MaxLowerReagentCost = 100;
    capCaster.LowerManaCost = 38;
    capCaster.MaxLowerManaCost = 40;

    EquipmentCompareItemInput lrcRing;
    lrcRing.Serial = 0x40000201u;
    lrcRing.Layer = OL_RING;
    lrcRing.PropertiesLoaded = true;
    lrcRing.Properties = {
        { 1060434u, L"15" }, // +15% LRC (completely over cap!)
        { 1060433u, L"8" },  // +8% LMC (only +2% effective!)
    };

    EquipmentCompareItemInput sdiRing;
    sdiRing.Serial = 0x40000202u;
    sdiRing.Layer = OL_RING;
    sdiRing.PropertiesLoaded = true;
    sdiRing.SlotLabel = L"Ring";
    sdiRing.FormattedTooltip = L"SDI Ring";
    sdiRing.Properties = {
        { 1060483u, L"10" }, // +10% SDI
    };

    const EquipmentCompareResult capRes =
        BuildEquipmentComparison(lrcRing, { sdiRing }, 6, 1, &capCaster);
    assert(capRes.Valid && capRes.Comparisons.size() == 1u);
    assert(capRes.Comparisons[0].Verdict == UpgradeVerdict::Downgrade);
    const EquipmentStatDelta *lrcD = FindDelta(capRes.Comparisons[0].Deltas, L"Lower Reag");
    assert(lrcD != nullptr && lrcD->MutedByCap && lrcD->CapNote == L"(capped)");
    const EquipmentStatDelta *lmcD = FindDelta(capRes.Comparisons[0].Deltas, L"Lower Mana");
    assert(lmcD != nullptr && !lmcD->MutedByCap && lmcD->CapNote == L"(+2% eff)");
    AssertStrictAsciiAndSixDigitColors(capRes.SideCards[0].HtmlText);
    assert(Contains(capRes.SideCards[0].HtmlText, L"--- COMPARE [Caster] ---"));
    assert(Contains(capRes.SideCards[0].HtmlText, L"(capped)"));
    assert(Contains(capRes.SideCards[0].HtmlText, L"(+2% eff)"));

    // 2. Overcap Fire Resist (95/85 -> 90/85) traded for plugging a Cold Resist hole (40/85 -> 45/85)
    // Total raw resists delta == 0, yet true cap-aware + EHP hole value MUST be a strong UPGRADE!
    CharacterCompareContext resistCtx;
    resistCtx.Valid = true;
    resistCtx.Profile = CompareBuildProfile::Tank;
    resistCtx.HasStatusTotals = true;
    resistCtx.PhysicalResist = 80;
    resistCtx.FireResist = 85; // clamped in 0x11
    resistCtx.ColdResist = 40;
    resistCtx.PoisonResist = 80;
    resistCtx.EnergyResist = 80;
    resistCtx.MaxPhysicalResist = 85;
    resistCtx.MaxFireResist = 85;
    resistCtx.MaxColdResist = 85;
    resistCtx.MaxPoisonResist = 85;
    resistCtx.MaxEnergyResist = 85;
    resistCtx.HasOtherEquippedResists = true;
    resistCtx.OtherEquippedPhysicalResist = 70;
    resistCtx.OtherEquippedFireResist = 85; // Other worn gear alone already caps Fire at 85!
    resistCtx.OtherEquippedColdResist = 40;
    resistCtx.OtherEquippedPoisonResist = 70;
    resistCtx.OtherEquippedEnergyResist = 70;

    const ParsedEquipmentStats oldChest = ParseEquipmentOpl({
        { 1060448u, L"10" },
        { 1060447u, L"10" }, // Fire 10 (all overcap!)
        { 1060445u, L"0" },
    });
    const ParsedEquipmentStats newChest = ParseEquipmentOpl({
        { 1060448u, L"10" },
        { 1060447u, L"5" }, // Lose 5 Fire (still 90 >= 85!)
        { 1060445u, L"5" }, // Gain 5 Cold in a gaping 40/85 hole!
    });
    const EquipmentComparisonEntry holeCmp =
        CompareSingleStats(newChest, oldChest, EquipCategory::ArmorOrShield, L"Chest", &resistCtx);
    assert(holeCmp.Verdict == UpgradeVerdict::Upgrade);
    assert(holeCmp.ScoreDelta > 10.0);
    const EquipmentStatDelta *fireD = FindDelta(holeCmp.Deltas, L"Fire Resist");
    assert(fireD != nullptr && fireD->MutedByCap && fireD->CapNote == L"(still capped)");

    // 3. ModernUO ML Stamina + Suit SSI/DI cap + Usability warnings (Str Req, Wrong Skill, Low Dur)
    CharacterCompareContext warCtx;
    warCtx.Valid = true;
    warCtx.Profile = CompareBuildProfile::Auto;
    warCtx.Str = 75;
    warCtx.Stam = 150; // 5 stam ticks -> 2.5s (10 ticks) drops to 5 ticks = 1.25s cap even at 0% SSI!
    warCtx.HasSkills = true;
    warCtx.Skills[40] = 100.0; // Swordsmanship
    warCtx.Skills[27] = 100.0; // Tactics
    warCtx.Skills[42] = 0.0;   // Fencing untrained!
    warCtx.HasStatusTotals = true;
    warCtx.WeaponSpeed = 60;
    warCtx.MaxWeaponSpeed = 60;

    EquipmentCompareItemInput wrongSkillWep;
    wrongSkillWep.Serial = 0x40000210u;
    wrongSkillWep.Layer = OL_1_HAND;
    wrongSkillWep.IsWeapon = true;
    wrongSkillWep.PropertiesLoaded = true;
    wrongSkillWep.Properties = {
        { 1061168u, L"14\t18" },
        { 1061167u, L"2.5s" },
        { 1061174u, L"" },      // Skill required: Fencing (0.0!)
        { 1061170u, L"95" },    // Str Req 95 > 75
        { 1060639u, L"6\t42" }, // Low durability (< 25%)
    };

    EquipmentCompareItemInput rightSkillWep;
    rightSkillWep.Serial = 0x40000211u;
    rightSkillWep.Layer = OL_1_HAND;
    rightSkillWep.IsWeapon = true;
    rightSkillWep.PropertiesLoaded = true;
    rightSkillWep.SlotLabel = L"Main Hand";
    rightSkillWep.FormattedTooltip = L"Katana";
    rightSkillWep.Properties = {
        { 1061168u, L"14\t18" },
        { 1061167u, L"2.5s" },
        { 1061172u, L"" }, // Skill required: Swordsmanship (100.0!)
        { 1061170u, L"40" },
        { 1060639u, L"40\t42" },
    };

    const EquipmentCompareResult warnRes =
        BuildEquipmentComparison(wrongSkillWep, { rightSkillWep }, 1, 1, &warCtx);
    assert(warnRes.Valid && warnRes.Comparisons.size() == 1u);
    assert(warnRes.Comparisons[0].Verdict == UpgradeVerdict::Downgrade);
    assert(warnRes.Comparisons[0].Warnings.size() == 3u);
    AssertStrictAsciiAndSixDigitColors(warnRes.SideCards[0].HtmlText);
    assert(Contains(warnRes.SideCards[0].HtmlText, L"Needs 95 Str (have 75)"));
    assert(Contains(warnRes.SideCards[0].HtmlText, L"Low skill: Fencing (0.0)"));
    assert(Contains(warnRes.SideCards[0].HtmlText, L"Low durability (6/42)"));
}

void TestParseSAToLAttributesAndIgnoredMetadata()
{
    // 1. Parse all verified SA/HS/ToL cliloc IDs + Publish 81 rarity tiers + Imbued tag
    const ParsedEquipmentStats s = ParseEquipmentOpl({
        { 1151495u, L"" },    // Legendary Artifact (ignored)
        { 1080418u, L"" },    // Imbued (ignored)
        { 1113696u, L"6" },   // Casting Focus 6%
        { 1113598u, L"5" },   // Damage Eater 5%
        { 1113597u, L"10" },  // Kinetic Eater 10%
        { 1113593u, L"11" },  // Fire Eater 11%
        { 1113594u, L"12" },  // Cold Eater 12%
        { 1113595u, L"13" },  // Poison Eater 13%
        { 1113596u, L"14" },  // Energy Eater 14%
        { 1113630u, L"25" },  // Soul Charge 25%
        { 1112364u, L"" },    // reactive paralyze
        { 1116158u, L"" },    // Mana Phase
        { 1113695u, L"15" },  // Kinetic Resonance 15%
        { 1113691u, L"16" },  // Fire Resonance 16%
        { 1113692u, L"17" },  // Cold Resonance 17%
        { 1113693u, L"18" },  // Poison Resonance 18%
        { 1113694u, L"19" },  // Energy Resonance 19%
        { 1113710u, L"" },    // Battle Lust
        { 1112857u, L"25" },  // splintering weapon 25%
        { 1113712u, L"35" },  // Hit Curse 35%
        { 1113700u, L"40" },  // Hit Fatigue 40%
        { 1113699u, L"45" },  // Hit Mana Drain 45%
        { 1113591u, L"" },    // Blood Drinker
        { 1154671u, L"" },    // Bane
    });

    assert(s.CastingFocus == 6);
    assert(s.DamageEater == 5);
    assert(s.KineticEater == 10 && s.FireEater == 11 && s.ColdEater == 12);
    assert(s.PoisonEater == 13 && s.EnergyEater == 14 && s.TotalElementalEaters() == 60);
    assert(s.SoulCharge == 25);
    assert(s.ReactiveParalyze && s.ManaPhase);
    assert(s.KineticResonance == 15 && s.FireResonance == 16 && s.ColdResonance == 17);
    assert(s.PoisonResonance == 18 && s.EnergyResonance == 19 && s.TotalResonances() == 85);
    assert(s.BattleLust && s.SplinteringWeapon == 25);
    assert(s.HitCurse == 35 && s.HitFatigue == 40 && s.HitManaDrain == 45);
    assert(s.BloodDrinker && s.Bane);

    // Alternate Reactive Paralyze cliloc 1154660
    const ParsedEquipmentStats altRp = ParseEquipmentOpl({ { 1154660u, L"" } });
    assert(altRp.ReactiveParalyze);

    // 2. Case-insensitive passthrough text fallback parsing (cliloc 1042971 / 1070722 / 1114057)
    const ParsedEquipmentStats txt = ParseEquipmentOpl({
        { 1042971u, L"Minor Magic Item" },
        { 1042971u, L"Legendary Artifact" },
        { 1070722u, L"Imbued" },
        { 1042971u, L"casting focus 4%" },
        { 1070722u, L"Damage Eater\t6%" },
        { 1114057u, L"KINETIC EATER 12%" },
        { 1042971u, L"fire eater 8%" },
        { 1042971u, L"Cold Eater 9%" },
        { 1042971u, L"Poison Eater 7%" },
        { 1042971u, L"Energy Eater 6%" },
        { 1042971u, L"soul charge 20%" },
        { 1042971u, L"reactive paralyze" },
        { 1042971u, L"mana phase" },
        { 1042971u, L"Kinetic Resonance 10%" },
        { 1042971u, L"fire resonance 11%" },
        { 1042971u, L"Cold Resonance 12%" },
        { 1042971u, L"Poison Resonance 13%" },
        { 1042971u, L"Energy Resonance 14%" },
        { 1042971u, L"battle lust" },
        { 1042971u, L"splintering weapon 30%" },
        { 1042971u, L"hit curse 25%" },
        { 1042971u, L"hit fatigue 20%" },
        { 1042971u, L"hit mana drain 15%" },
        { 1042971u, L"blood drinker" },
        { 1042971u, L"bane" },
        { 1042971u, L"balanced" },
        { 1042971u, L"Velocity 40%" },
    });

    assert(txt.CastingFocus == 4 && txt.DamageEater == 6);
    assert(txt.KineticEater == 12 && txt.FireEater == 8 && txt.ColdEater == 9);
    assert(txt.PoisonEater == 7 && txt.EnergyEater == 6);
    assert(txt.SoulCharge == 20 && txt.ReactiveParalyze && txt.ManaPhase);
    assert(txt.KineticResonance == 10 && txt.FireResonance == 11 && txt.ColdResonance == 12);
    assert(txt.PoisonResonance == 13 && txt.EnergyResonance == 14);
    assert(txt.BattleLust && txt.SplinteringWeapon == 30);
    assert(txt.HitCurse == 25 && txt.HitFatigue == 20 && txt.HitManaDrain == 15);
    assert(txt.BloodDrinker && txt.Bane && txt.Balanced && txt.Velocity == 40);
    assert(txt.MaterialGrade.empty() && txt.WeaponLevel == 0);

    // 3. All 8 Publish 81 rarity tiers + Imbued must have ZERO score and ZERO deltas
    const ParsedEquipmentStats onlyMetadata = ParseEquipmentOpl({
        { 1151488u, L"" }, // Minor Magic Item
        { 1151489u, L"" }, // Lesser Magic Item
        { 1151490u, L"" }, // Greater Magic Item
        { 1151491u, L"" }, // Major Magic Item
        { 1151492u, L"" }, // Lesser Artifact
        { 1151493u, L"" }, // Greater Artifact
        { 1151494u, L"" }, // Major Artifact
        { 1151495u, L"" }, // Legendary Artifact
        { 1080418u, L"" }, // Imbued
        { 1042971u, L"Greater Magic Item" },
        { 1042971u, L"Major Artifact" },
        { 1042971u, L"Lesser Artifact" },
        { 1042971u, L"Imbued" },
    });
    assert(ApproxEq(ScoreEquipment(onlyMetadata, EquipCategory::ArmorOrShield), 0.0));
    assert(ApproxEq(ScoreEquipment(onlyMetadata, EquipCategory::Weapon), 0.0));
    assert(ApproxEq(ScoreEquipment(onlyMetadata, EquipCategory::JewelryOrUtility), 0.0));

    EquipmentCompareItemInput imbuedLegend;
    imbuedLegend.Serial = 0x40000301u;
    imbuedLegend.Layer = OL_TORSO;
    imbuedLegend.PropertiesLoaded = true;
    imbuedLegend.Properties = {
        { 1151495u, L"" }, // Legendary Artifact
        { 1080418u, L"" }, // Imbued
        { 1060448u, L"15" },
        { 1060433u, L"8" },
    };

    EquipmentCompareItemInput plainMinor;
    plainMinor.Serial = 0x40000302u;
    plainMinor.Layer = OL_TORSO;
    plainMinor.PropertiesLoaded = true;
    plainMinor.SlotLabel = L"Chest";
    plainMinor.FormattedTooltip = L"Plate Chest";
    plainMinor.Properties = {
        { 1151488u, L"" }, // Minor Magic Item
        { 1060448u, L"15" },
        { 1060433u, L"8" },
    };

    const EquipmentCompareResult metaRes =
        BuildEquipmentComparison(imbuedLegend, { plainMinor }, 1, 1);
    assert(metaRes.Valid && metaRes.Comparisons.size() == 1u);
    assert(metaRes.Comparisons[0].Verdict == UpgradeVerdict::Sidegrade);
    assert(ApproxEq(metaRes.Comparisons[0].ScoreDelta, 0.0));
    assert(metaRes.Comparisons[0].Deltas.empty());
    AssertStrictAsciiAndSixDigitColors(metaRes.SideCards[0].HtmlText);
    assert(Contains(metaRes.SideCards[0].HtmlText, L"= Identical stats"));
}

void TestSAToLProfileScoringRangedAndDeltas()
{
    CharacterCompareContext casterCtx;
    casterCtx.Valid = true;
    casterCtx.Profile = CompareBuildProfile::Caster;

    CharacterCompareContext meleeCtx;
    meleeCtx.Valid = true;
    meleeCtx.Profile = CompareBuildProfile::Melee;

    CharacterCompareContext tankCtx;
    tankCtx.Valid = true;
    tankCtx.Profile = CompareBuildProfile::Tank;

    // 1. Casting Focus + Fire Resonance + Mana Phase: huge value for Caster, beats DI on Caster, loses to DI on Melee
    const ParsedEquipmentStats cfArmor = ParseEquipmentOpl({
        { 1060448u, L"10" },
        { 1113696u, L"6" },  // Casting Focus 6%
        { 1113691u, L"15" }, // Fire Resonance 15%
        { 1116158u, L"" },   // Mana Phase
    });
    const ParsedEquipmentStats diArmor = ParseEquipmentOpl({
        { 1060448u, L"10" },
        { 1060401u, L"20" }, // Damage Increase 20%
    });
    const EquipmentComparisonEntry cfVsDiCaster =
        CompareSingleStats(cfArmor, diArmor, EquipCategory::ArmorOrShield, L"Chest", &casterCtx);
    assert(cfVsDiCaster.Verdict == UpgradeVerdict::Upgrade);
    assert(cfVsDiCaster.ScoreDelta > 45.0);
    assert(FindDelta(cfVsDiCaster.Deltas, L"Casting Focus") != nullptr);
    assert(FindDelta(cfVsDiCaster.Deltas, L"Fire Resonance") != nullptr);
    assert(FindDelta(cfVsDiCaster.Deltas, L"Mana Phase") != nullptr);

    const EquipmentComparisonEntry cfVsDiMelee =
        CompareSingleStats(cfArmor, diArmor, EquipCategory::ArmorOrShield, L"Chest", &meleeCtx);
    assert(cfVsDiMelee.Verdict == UpgradeVerdict::Downgrade);

    // 2. Universal Damage Eater 6% beats single-element Fire Eater 15% across all archetypes,
    // and Tank values Damage Eater + Reactive Paralyze highest!
    const ParsedEquipmentStats dmgEaterChest = ParseEquipmentOpl({
        { 1060448u, L"10" },
        { 1113598u, L"6" }, // Damage Eater 6%
    });
    const ParsedEquipmentStats fireEaterChest = ParseEquipmentOpl({
        { 1060448u, L"10" },
        { 1113593u, L"15" }, // Fire Eater 15%
    });
    assert(
        CompareSingleStats(dmgEaterChest, fireEaterChest, EquipCategory::ArmorOrShield, L"Chest", &tankCtx)
            .Verdict == UpgradeVerdict::Upgrade);
    assert(
        CompareSingleStats(dmgEaterChest, fireEaterChest, EquipCategory::ArmorOrShield, L"Chest", &meleeCtx)
            .Verdict == UpgradeVerdict::Upgrade);
    assert(
        CompareSingleStats(dmgEaterChest, fireEaterChest, EquipCategory::ArmorOrShield, L"Chest", &casterCtx)
            .Verdict == UpgradeVerdict::Upgrade);
    assert(
        ScoreEquipment(dmgEaterChest, EquipCategory::ArmorOrShield, &tankCtx) >
        ScoreEquipment(dmgEaterChest, EquipCategory::ArmorOrShield, &meleeCtx));

    // Shield with SA armor attributes classifies as ArmorOrShield even with TileFlag.Weapon
    const ParsedEquipmentStats saShield = ParseEquipmentOpl({
        { 1113630u, L"25" }, // Soul Charge 25%
        { 1112364u, L"" },   // Reactive Paralyze
        { 1113598u, L"5" },  // Damage Eater 5%
    });
    assert(ClassifyEquipmentCategory(saShield, OL_2_HAND, true) == EquipCategory::ArmorOrShield);
    const ParsedEquipmentStats plainShield = ParseEquipmentOpl({
        { 1060448u, L"5" },
    });
    const EquipmentComparisonEntry shieldCmp =
        CompareSingleStats(saShield, plainShield, EquipCategory::ArmorOrShield, L"Shield", &tankCtx);
    assert(shieldCmp.Verdict == UpgradeVerdict::Upgrade);
    assert(FindDelta(shieldCmp.Deltas, L"Soul Charge") != nullptr);
    assert(FindDelta(shieldCmp.Deltas, L"Reactive Paralyze") != nullptr);
    assert(FindDelta(shieldCmp.Deltas, L"Damage Eater") != nullptr);

    // 3. SA/ToL weapon procs (Battle Lust, Splintering, Hit Curse/Fatigue/Mana Drain, Blood Drinker, Bane)
    EquipmentCompareItemInput tolSword;
    tolSword.Serial = 0x40000310u;
    tolSword.Layer = OL_1_HAND;
    tolSword.IsWeapon = true;
    tolSword.PropertiesLoaded = true;
    tolSword.Properties = {
        { 1151495u, L"" },    // Legendary Artifact (ignored)
        { 1080418u, L"" },    // Imbued (ignored)
        { 1061168u, L"15\t19" },
        { 1061167u, L"2.5s" },
        { 1061824u, L"" },
        { 1061172u, L"" },    // Swordsmanship
        { 1060401u, L"40" },
        { 1113710u, L"" },    // Battle Lust
        { 1112857u, L"25" },  // Splintering Weapon 25%
        { 1113712u, L"35" },  // Hit Curse 35%
        { 1113700u, L"30" },  // Hit Fatigue 30%
        { 1113699u, L"30" },  // Hit Mana Drain 30%
        { 1113591u, L"" },    // Blood Drinker
        { 1154671u, L"" },    // Bane
    };

    EquipmentCompareItemInput baseSword;
    baseSword.Serial = 0x40000311u;
    baseSword.Layer = OL_1_HAND;
    baseSword.IsWeapon = true;
    baseSword.PropertiesLoaded = true;
    baseSword.SlotLabel = L"Main Hand";
    baseSword.FormattedTooltip = L"Broad Sword";
    baseSword.Properties = {
        { 1061168u, L"15\t19" },
        { 1061167u, L"2.5s" },
        { 1061824u, L"" },
        { 1061172u, L"" },
        { 1060401u, L"40" },
    };

    const EquipmentCompareResult tolWepMelee =
        BuildEquipmentComparison(tolSword, { baseSword }, 1, 1, &meleeCtx);
    const EquipmentCompareResult tolWepCaster =
        BuildEquipmentComparison(tolSword, { baseSword }, 1, 1, &casterCtx);
    assert(tolWepMelee.Valid && tolWepMelee.Comparisons.size() == 1u);
    assert(tolWepMelee.Comparisons[0].Verdict == UpgradeVerdict::Upgrade);
    // Martial weapon procs must add far more score for Melee than pure Caster
    assert(tolWepMelee.Comparisons[0].ScoreDelta > 140.0);
    assert(tolWepMelee.Comparisons[0].ScoreDelta > tolWepCaster.Comparisons[0].ScoreDelta * 3.0);
    assert(FindDelta(tolWepMelee.Comparisons[0].Deltas, L"Battle Lust") != nullptr);
    assert(FindDelta(tolWepMelee.Comparisons[0].Deltas, L"Splintering") != nullptr);
    assert(FindDelta(tolWepMelee.Comparisons[0].Deltas, L"Hit Curse") != nullptr);
    assert(FindDelta(tolWepMelee.Comparisons[0].Deltas, L"Hit Fatigue") != nullptr);
    assert(FindDelta(tolWepMelee.Comparisons[0].Deltas, L"Hit Mana Drain") != nullptr);
    assert(FindDelta(tolWepMelee.Comparisons[0].Deltas, L"Blood Drinker") != nullptr);
    assert(FindDelta(tolWepMelee.Comparisons[0].Deltas, L"Bane") != nullptr);
    AssertStrictAsciiAndSixDigitColors(tolWepMelee.SideCards[0].HtmlText);

    // 4. Ranged weapons: Balanced (1072792) + Velocity (1072793) scored strongly for ranged/melee
    EquipmentCompareItemInput rangedGood;
    rangedGood.Serial = 0x40000320u;
    rangedGood.Layer = OL_2_HAND;
    rangedGood.IsWeapon = true;
    rangedGood.PropertiesLoaded = true;
    rangedGood.Properties = {
        { 1151492u, L"" },   // Lesser Artifact (ignored)
        { 1061168u, L"16\t20" },
        { 1061167u, L"3.0s" },
        { 1061171u, L"" },   // Two-Handed
        { 1061175u, L"" },   // Archery
        { 1061169u, L"10" }, // Max Range 10
        { 1072792u, L"" },   // Balanced
        { 1072793u, L"45" }, // Velocity 45%
    };

    EquipmentCompareItemInput rangedPlain;
    rangedPlain.Serial = 0x40000321u;
    rangedPlain.Layer = OL_2_HAND;
    rangedPlain.IsWeapon = true;
    rangedPlain.PropertiesLoaded = true;
    rangedPlain.SlotLabel = L"Two-Handed";
    rangedPlain.FormattedTooltip = L"Composite Bow";
    rangedPlain.Properties = {
        { 1061168u, L"16\t20" },
        { 1061167u, L"3.0s" },
        { 1061171u, L"" },
        { 1061175u, L"" },
        { 1061169u, L"10" },
    };

    const EquipmentCompareResult bowMelee =
        BuildEquipmentComparison(rangedGood, { rangedPlain }, 1, 1, &meleeCtx);
    const EquipmentCompareResult bowCaster =
        BuildEquipmentComparison(rangedGood, { rangedPlain }, 1, 1, &casterCtx);
    assert(bowMelee.Valid && bowMelee.Comparisons.size() == 1u);
    assert(bowMelee.Comparisons[0].Verdict == UpgradeVerdict::Upgrade);
    assert(bowMelee.Comparisons[0].ScoreDelta > 40.0); // Balanced (10) + Velocity 45 * 0.75 (33.75) = 43.75
    assert(bowMelee.Comparisons[0].ScoreDelta > bowCaster.Comparisons[0].ScoreDelta * 2.5);
    const EquipmentStatDelta *balD = FindDelta(bowMelee.Comparisons[0].Deltas, L"Balanced");
    const EquipmentStatDelta *velD = FindDelta(bowMelee.Comparisons[0].Deltas, L"Velocity");
    assert(balD != nullptr && balD->IsBooleanFlag && balD->Delta > 0.0);
    assert(velD != nullptr && velD->IsPercent && ApproxEq(velD->Delta, 45.0));
    AssertStrictAsciiAndSixDigitColors(bowMelee.SideCards[0].HtmlText);
    assert(Contains(bowMelee.SideCards[0].HtmlText, L"+ Balanced"));
    assert(Contains(bowMelee.SideCards[0].HtmlText, L"+45% Velocity"));

    // Empty-slot summary renders SA/ToL & ranged stats cleanly
    const EquipmentCompareResult bowEmpty = BuildEquipmentComparison(rangedGood, {}, 1, 0, &meleeCtx);
    AssertStrictAsciiAndSixDigitColors(bowEmpty.SideCards[0].HtmlText);
    assert(Contains(bowEmpty.SideCards[0].HtmlText, L"+ Balanced"));
    assert(Contains(bowEmpty.SideCards[0].HtmlText, L"+ 45% Velocity"));

    const EquipmentCompareResult tolEmpty = BuildEquipmentComparison(tolSword, {}, 1, 0, &meleeCtx);
    AssertStrictAsciiAndSixDigitColors(tolEmpty.SideCards[0].HtmlText);
    assert(Contains(tolEmpty.SideCards[0].HtmlText, L"+ Battle Lust"));
    assert(Contains(tolEmpty.SideCards[0].HtmlText, L"+ 25% Splintering"));
    assert(Contains(tolEmpty.SideCards[0].HtmlText, L"+ Blood Drinker"));
    assert(Contains(tolEmpty.SideCards[0].HtmlText, L"+ Bane"));
}

void TestSAToLCapsAndCombinedComparison()
{
    // 1. Casting Focus cap (12%): partial cap (+2% eff) and full overcap ((capped))
    CharacterCompareContext cfPartialCtx;
    cfPartialCtx.Valid = true;
    cfPartialCtx.Profile = CompareBuildProfile::Caster;
    cfPartialCtx.HasOtherEquippedSAStats = true;
    cfPartialCtx.OtherEquippedCastingFocus = 10; // Only 2% left under 12% cap!

    const ParsedEquipmentStats cfRing6 = ParseEquipmentOpl({ { 1113696u, L"6" } });
    const ParsedEquipmentStats emptyRing = ParseEquipmentOpl({});
    const EquipmentComparisonEntry cfPartialCmp = CompareSingleStats(
        cfRing6, emptyRing, EquipCategory::JewelryOrUtility, L"Ring", &cfPartialCtx);
    const EquipmentStatDelta *cfPartD = FindDelta(cfPartialCmp.Deltas, L"Casting Focus");
    assert(cfPartD != nullptr && !cfPartD->MutedByCap && cfPartD->CapNote == L"(+2% eff)");

    CharacterCompareContext cfCappedCtx = cfPartialCtx;
    cfCappedCtx.OtherEquippedCastingFocus = 12; // Already at 12% cap!
    const ParsedEquipmentStats sdiRing = ParseEquipmentOpl({ { 1060483u, L"10" } }); // +10% SDI = 24.0
    const EquipmentComparisonEntry cfCappedCmp = CompareSingleStats(
        cfRing6, sdiRing, EquipCategory::JewelryOrUtility, L"Ring", &cfCappedCtx);
    assert(cfCappedCmp.Verdict == UpgradeVerdict::Downgrade);
    const EquipmentStatDelta *cfCapD = FindDelta(cfCappedCmp.Deltas, L"Casting Focus");
    assert(cfCapD != nullptr && cfCapD->MutedByCap && cfCapD->CapNote == L"(capped)");

    // 2. Damage Eater cap (18%) + shared Elemental Eater cap (30% combined) + Soul Charge (30%) +
    //    Resonance (20%) + non-stacking boolean procs (Mana Phase, Reactive Paralyze)
    CharacterCompareContext eaterCtx;
    eaterCtx.Valid = true;
    eaterCtx.Profile = CompareBuildProfile::Tank;
    eaterCtx.HasOtherEquippedSAStats = true;
    eaterCtx.OtherEquippedDamageEater = 18;    // At 18% Damage Eater cap
    eaterCtx.OtherEquippedFireEater = 0;       // 30 - 18 = 12% Fire Eater room left
    eaterCtx.OtherEquippedColdEater = 12;      // 18 + 12 = 30% Cold Eater already capped!
    eaterCtx.OtherEquippedSoulCharge = 25;     // 5% Soul Charge room left
    eaterCtx.OtherEquippedFireResonance = 20;  // Fire Resonance capped at 20%
    eaterCtx.OtherEquippedManaPhase = true;    // Already wearing Mana Phase
    eaterCtx.OtherEquippedReactiveParalyze = true;

    EquipmentCompareItemInput overcapArmor;
    overcapArmor.Serial = 0x40000330u;
    overcapArmor.Layer = OL_TORSO;
    overcapArmor.PropertiesLoaded = true;
    overcapArmor.Properties = {
        { 1113598u, L"6" },  // Damage Eater +6% (fully capped!)
        { 1113593u, L"15" }, // Fire Eater +15% (+12% eff under 30% total)
        { 1113594u, L"10" }, // Cold Eater +10% (fully capped at 30%!)
        { 1113630u, L"15" }, // Soul Charge +15% (+5% eff under 30%)
        { 1113691u, L"10" }, // Fire Resonance +10% (fully capped at 20%)
        { 1116158u, L"" },   // Mana Phase (duplicate -> capped!)
        { 1112364u, L"" },   // Reactive Paralyze (duplicate -> capped!)
    };

    EquipmentCompareItemInput bareArmor;
    bareArmor.Serial = 0x40000331u;
    bareArmor.Layer = OL_TORSO;
    bareArmor.PropertiesLoaded = true;
    bareArmor.SlotLabel = L"Chest";
    bareArmor.FormattedTooltip = L"Bare Chest";
    bareArmor.Properties = {};

    const EquipmentCompareResult capRes =
        BuildEquipmentComparison(overcapArmor, { bareArmor }, 1, 1, &eaterCtx);
    assert(capRes.Valid && capRes.Comparisons.size() == 1u);
    const auto &deltas = capRes.Comparisons[0].Deltas;

    const EquipmentStatDelta *deD = FindDelta(deltas, L"Damage Eater");
    assert(deD != nullptr && deD->MutedByCap && deD->CapNote == L"(capped)");
    const EquipmentStatDelta *feD = FindDelta(deltas, L"Fire Eater");
    assert(feD != nullptr && !feD->MutedByCap && feD->CapNote == L"(+12% eff)");
    const EquipmentStatDelta *ceD = FindDelta(deltas, L"Cold Eater");
    assert(ceD != nullptr && ceD->MutedByCap && ceD->CapNote == L"(capped)");
    const EquipmentStatDelta *scD = FindDelta(deltas, L"Soul Charge");
    assert(scD != nullptr && !scD->MutedByCap && scD->CapNote == L"(+5% eff)");
    const EquipmentStatDelta *frD = FindDelta(deltas, L"Fire Resonance");
    assert(frD != nullptr && frD->MutedByCap && frD->CapNote == L"(capped)");
    const EquipmentStatDelta *mpD = FindDelta(deltas, L"Mana Phase");
    assert(mpD != nullptr && mpD->IsBooleanFlag && mpD->MutedByCap && mpD->CapNote == L"(capped)");
    const EquipmentStatDelta *rpD = FindDelta(deltas, L"Reactive Paralyze");
    assert(rpD != nullptr && rpD->IsBooleanFlag && rpD->MutedByCap && rpD->CapNote == L"(capped)");

    AssertStrictAsciiAndSixDigitColors(capRes.SideCards[0].HtmlText);
    assert(Contains(capRes.SideCards[0].HtmlText, L"(+12% eff)"));
    assert(Contains(capRes.SideCards[0].HtmlText, L"(+5% eff)"));
    assert(Contains(capRes.SideCards[0].HtmlText, L"(capped)"));

    // 3. Combined 2H weapon vs (1H weapon + SA Shield) propagates all SA/ToL deltas cleanly
    const ParsedEquipmentStats twoHand = ParseEquipmentOpl({
        { 1061168u, L"18\t22" },
        { 1061167u, L"3.0s" },
        { 1061171u, L"" },
        { 1112857u, L"30" }, // Splintering 30%
        { 1113591u, L"" },   // Blood Drinker
    });
    const ParsedEquipmentStats oneHand = ParseEquipmentOpl({
        { 1061168u, L"12\t16" },
        { 1061167u, L"2.5s" },
        { 1061824u, L"" },
        { 1113710u, L"" }, // Battle Lust
        { 1154671u, L"" }, // Bane
    });
    const ParsedEquipmentStats shield = ParseEquipmentOpl({
        { 1060448u, L"10" },
        { 1113598u, L"6" },  // Damage Eater 6%
        { 1113630u, L"20" }, // Soul Charge 20%
        { 1112364u, L"" },   // Reactive Paralyze
    });

    const EquipmentComparisonEntry combinedCmp = CompareAgainstCombinedStats(
        twoHand, oneHand, shield, EquipCategory::Weapon, L"Combined (1H + Shield)");
    assert(FindDelta(combinedCmp.Deltas, L"Splintering") != nullptr);
    assert(FindDelta(combinedCmp.Deltas, L"Blood Drinker") != nullptr);
    const EquipmentStatDelta *lostBl = FindDelta(combinedCmp.Deltas, L"Battle Lust");
    assert(lostBl != nullptr && lostBl->Delta < 0.0);
    const EquipmentStatDelta *lostBane = FindDelta(combinedCmp.Deltas, L"Bane");
    assert(lostBane != nullptr && lostBane->Delta < 0.0);
    const EquipmentStatDelta *lostDe = FindDelta(combinedCmp.Deltas, L"Damage Eater");
    assert(lostDe != nullptr && ApproxEq(lostDe->Delta, -6.0));
    const EquipmentStatDelta *lostSc = FindDelta(combinedCmp.Deltas, L"Soul Charge");
    assert(lostSc != nullptr && ApproxEq(lostSc->Delta, -20.0));
    const EquipmentStatDelta *lostRp = FindDelta(combinedCmp.Deltas, L"Reactive Paralyze");
    assert(lostRp != nullptr && lostRp->Delta < 0.0);
}

void TestAuditRegressionFixes()
{
    // Fix #1: Solo HCI cap parity (60% vs 45%)
    {
        CharacterCompareContext soloCtx;
        soloCtx.Valid = true;
        soloCtx.Profile = CompareBuildProfile::Melee;
        soloCtx.HasStatusTotals = true;
        soloCtx.AttackChance = 45;
        soloCtx.MaxAttackChance = 60;
        soloCtx.MaxDefenseChance = 85;
        soloCtx.MaxPhysicalResist = 85;

        const ParsedEquipmentStats hci15 = ParseEquipmentOpl({ { 1060415u, L"15" } });
        const ParsedEquipmentStats hci20 = ParseEquipmentOpl({ { 1060415u, L"20" } });
        const ParsedEquipmentStats plain = ParseEquipmentOpl({});

        // 45 -> 60 HCI: completely uncapped under solo cap 60!
        const EquipmentComparisonEntry c60 =
            CompareSingleStats(hci15, plain, EquipCategory::JewelryOrUtility, L"Ring", &soloCtx);
        const EquipmentStatDelta *hciD60 = FindDelta(c60.Deltas, L"Hit Chance");
        assert(hciD60 != nullptr && !hciD60->MutedByCap && hciD60->CapNote.empty());
        assert(ApproxEq(c60.ScoreDelta, 15.0 * 2.40));

        // 45 -> 65 HCI: +15% eff under solo cap 60!
        const EquipmentComparisonEntry c65 =
            CompareSingleStats(hci20, plain, EquipCategory::JewelryOrUtility, L"Ring", &soloCtx);
        const EquipmentStatDelta *hciD65 = FindDelta(c65.Deltas, L"Hit Chance");
        assert(hciD65 != nullptr && !hciD65->MutedByCap && hciD65->CapNote == L"(+15% eff)");
    }

    // Fix #4: Multi-slot independent comparison keeps sibling target worn
    {
        CharacterCompareContext bracCtx;
        bracCtx.Valid = true;
        bracCtx.Profile = CompareBuildProfile::Caster;
        bracCtx.HasOtherEquippedSAStats = true;
        bracCtx.OtherEquippedCastingFocus = 0; // Other non-bracelet pieces have 0 CF

        EquipmentCompareItemInput hovBrac;
        hovBrac.Serial = 0x40000400u;
        hovBrac.Layer = OL_BRACELET;
        hovBrac.PropertiesLoaded = true;
        hovBrac.Properties = { { 1113696u, L"6" } }; // +6% Casting Focus

        EquipmentCompareItemInput leftBrac;
        leftBrac.Serial = 0x40000401u;
        leftBrac.Layer = OL_BRACELET;
        leftBrac.PropertiesLoaded = true;
        leftBrac.SlotLabel = L"Left Bracelet";
        leftBrac.FormattedTooltip = L"Plain Bracelet";
        leftBrac.Properties = {};

        EquipmentCompareItemInput rightBrac;
        rightBrac.Serial = 0x40000402u;
        rightBrac.Layer = OL_BRACELET_2;
        rightBrac.PropertiesLoaded = true;
        rightBrac.SlotLabel = L"Right Bracelet";
        rightBrac.FormattedTooltip = L"CF10 Bracelet";
        rightBrac.Properties = { { 1113696u, L"10" } }; // Sibling holds 10% CF!

        const EquipmentCompareResult res =
            BuildEquipmentComparison(hovBrac, { leftBrac, rightBrac }, 2, 2, &bracCtx);
        assert(res.Valid && res.Comparisons.size() == 2u);
        // Replacing Left keeps Right's 10% CF worn -> only +2% eff under 12% cap!
        const EquipmentStatDelta *leftCf = FindDelta(res.Comparisons[0].Deltas, L"Casting Focus");
        assert(leftCf != nullptr && !leftCf->MutedByCap && leftCf->CapNote == L"(+2% eff)");
        // Replacing Right removes Right's 10% CF -> -4% CF uncapped!
        const EquipmentStatDelta *rightCf = FindDelta(res.Comparisons[1].Deltas, L"Casting Focus");
        assert(rightCf != nullptr && !rightCf->MutedByCap && rightCf->CapNote.empty() &&
               ApproxEq(rightCf->Delta, -4.0));
    }

    // Fix #6 & #15: Negative FasterCasting (-1 from Spell Channeling) retains full penalty,
    // is NOT muted as "(still capped)", and empty-slot summary shows -1 Faster Cast in red & ctx Eff DPS
    {
        CharacterCompareContext casterCtx;
        casterCtx.Valid = true;
        casterCtx.Profile = CompareBuildProfile::Caster;
        casterCtx.HasStatusTotals = true;
        casterCtx.CastSpeed = 0;
        casterCtx.MaxCastSpeed = 2;
        casterCtx.Stam = 150;
        casterCtx.WeaponDamage = 50;

        const ParsedEquipmentStats scNegFc = ParseEquipmentOpl({
            { 1061168u, L"10\t14" },
            { 1061167u, L"2.5s" },
            { 1060482u, L"" },   // Spell Channeling (+16.0)
            { 1060413u, L"-1" }, // Faster Casting -1 (-14.0 full penalty, NOT -1.4!)
        });
        const ParsedEquipmentStats scClean = ParseEquipmentOpl({
            { 1061168u, L"10\t14" },
            { 1061167u, L"2.5s" },
            { 1060482u, L"" },
        });
        const EquipmentComparisonEntry fcCmp = CompareSingleStats(
            scNegFc, scClean, EquipCategory::Weapon, L"Main Hand", &casterCtx);
        assert(fcCmp.Verdict == UpgradeVerdict::Downgrade);
        assert(ApproxEq(fcCmp.ScoreDelta, -14.0));
        const EquipmentStatDelta *fcD = FindDelta(fcCmp.Deltas, L"Faster Cast");
        assert(fcD != nullptr && !fcD->MutedByCap && fcD->CapNote.empty() &&
               ApproxEq(fcD->Delta, -1.0));

        // Empty-slot summary uses ctx Stam=150 (1.25s cap) & suit DI 50% (12 * 1.5 / 1.25 = 14.4 Eff DPS)
        // and renders "-1 Faster Cast" in red (#FF5555)
        EquipmentCompareItemInput emptyWep;
        emptyWep.Serial = 0x40000410u;
        emptyWep.Layer = OL_1_HAND;
        emptyWep.IsWeapon = true;
        emptyWep.PropertiesLoaded = true;
        emptyWep.Properties = {
            { 1061168u, L"10\t14" }, // avg 12
            { 1061167u, L"2.5s" },   // with 150 stam -> 1.25s; with 50% suit DI -> 18/1.25 = 14.4 Eff DPS
            { 1060482u, L"" },
            { 1060413u, L"-1" },
        };
        const EquipmentCompareResult emptyRes =
            BuildEquipmentComparison(emptyWep, {}, 1, 0, &casterCtx);
        AssertStrictAsciiAndSixDigitColors(emptyRes.SideCards[0].HtmlText);
        assert(Contains(emptyRes.SideCards[0].HtmlText, L"+ 14.4 Eff DPS"));
        assert(Contains(emptyRes.SideCards[0].HtmlText, L"<basefont color=\"#FF5555\">-1 Faster Cast"));
    }

    // Fix #7: Elemental Eater cap note must NEVER invert sign when Damage Eater also changes
    {
        CharacterCompareContext ctx;
        ctx.Valid = true;
        ctx.Profile = CompareBuildProfile::Tank;
        ctx.HasOtherEquippedSAStats = true;
        ctx.OtherEquippedDamageEater = 10;
        ctx.OtherEquippedFireEater = 10;

        const ParsedEquipmentStats eq = ParseEquipmentOpl({ { 1113593u, L"10" } });
        const ParsedEquipmentStats hov =
            ParseEquipmentOpl({ { 1113598u, L"6" }, { 1113593u, L"15" } });

        // Forward swap: +5% Fire Eater, post-swap DE=16 + other FE=10 -> capped (NOT "-6% eff"!)
        const EquipmentComparisonEntry fwd =
            CompareSingleStats(hov, eq, EquipCategory::ArmorOrShield, L"Chest", &ctx);
        const EquipmentStatDelta *fwdFe = FindDelta(fwd.Deltas, L"Fire Eater");
        assert(fwdFe != nullptr && ApproxEq(fwdFe->Delta, 5.0));
        assert(fwdFe->MutedByCap && fwdFe->CapNote == L"(capped)");

        // Reverse swap: -5% Fire Eater -> still capped (NOT "+6% eff"!)
        const EquipmentComparisonEntry rev =
            CompareSingleStats(eq, hov, EquipCategory::ArmorOrShield, L"Chest", &ctx);
        const EquipmentStatDelta *revFe = FindDelta(rev.Deltas, L"Fire Eater");
        assert(revFe != nullptr && ApproxEq(revFe->Delta, -5.0));
        assert(revFe->MutedByCap && revFe->CapNote == L"(still capped)");
    }

    // Fix #8: Core 2H weapons with tiledata Layer == OL_1_HAND + cliloc 1061171 use Combined 2H comparison
    {
        EquipmentCompareItemInput bow;
        bow.Serial = 0x40000420u;
        bow.Layer = OL_1_HAND; // Simulates tiledata.mul Layer == 1 on Bow/Crossbow/BattleAxe/WarHammer
        bow.IsWeapon = true;
        bow.PropertiesLoaded = true;
        bow.Properties = {
            { 1061168u, L"16\t20" },
            { 1061167u, L"3.0s" },
            { 1061171u, L"" }, // two-handed weapon!
            { 1061175u, L"" }, // Archery
        };

        EquipmentCompareItemInput mh;
        mh.Serial = 0x40000421u;
        mh.Layer = OL_1_HAND;
        mh.IsWeapon = true;
        mh.PropertiesLoaded = true;
        mh.SlotLabel = L"Main Hand";
        mh.FormattedTooltip = L"Katana";
        mh.Properties = { { 1061168u, L"12\t16" }, { 1061167u, L"2.5s" }, { 1061824u, L"" } };

        EquipmentCompareItemInput shield;
        shield.Serial = 0x40000422u;
        shield.Layer = OL_2_HAND;
        shield.IsWeapon = false;
        shield.PropertiesLoaded = true;
        shield.SlotLabel = L"Shield / Off Hand";
        shield.FormattedTooltip = L"Heater Shield";
        shield.Properties = { { 1060448u, L"10" }, { 1060408u, L"15" } };

        const EquipmentCompareResult bowRes = BuildEquipmentComparison(bow, { mh, shield }, 2, 2);
        assert(bowRes.Valid && bowRes.Comparisons.size() == 1u);
        assert(bowRes.Comparisons[0].TargetLabel == L"Combined (1H + Shield)");
        assert(Contains(bowRes.SideCards[0].HtmlText, L"Replaces both hands"));
    }

    // Fix #9 & #14: Ship-decay cliloc 1149791 NOT parsed as MinThrowRange; passthrough parsed; "(Imbued)" ignored
    {
        const ParsedEquipmentStats shipMsg = ParseEquipmentOpl({ { 1149791u, L"4" } });
        assert(shipMsg.MinThrowRange == 0);

        const ParsedEquipmentStats thrownTab =
            ParseEquipmentOpl({ { 1042971u, L"Min Throw Range\t4" }, { 1042971u, L"(Imbued)" } });
        assert(thrownTab.MinThrowRange == 4);
        assert(ApproxEq(ScoreEquipment(thrownTab, EquipCategory::Weapon), 0.0));

        const ParsedEquipmentStats thrownSpace =
            ParseEquipmentOpl({ { 1070722u, L"min throw range 3" } });
        assert(thrownSpace.MinThrowRange == 3);
    }

    // Fix #10: Weakest Ring replacement scoring uses MakeBaselineWithout (does not punish BiS ring holding cap)
    {
        CharacterCompareContext capCtx;
        capCtx.Valid = true;
        capCtx.Profile = CompareBuildProfile::Caster;
        capCtx.HasStatusTotals = true;
        capCtx.LowerReagentCost = 100;
        capCtx.MaxLowerReagentCost = 100;
        capCtx.LowerManaCost = 40;
        capCtx.MaxLowerManaCost = 40;
        capCtx.CastSpeed = 2;
        capCtx.MaxCastSpeed = 2;

        const ParsedEquipmentStats bisRing = ParseEquipmentOpl({
            { 1060434u, L"20" }, // 20% LRC (keeps player at 100%!)
            { 1060433u, L"8" },  // 8% LMC (keeps player at 40%!)
            { 1060413u, L"1" },  // 1 FC (keeps player at 2!)
        });
        const ParsedEquipmentStats junkRing = ParseEquipmentOpl({
            { 1060483u, L"4" }, // 4% SDI (~9.6 pts)
        });

        // Naive ScoreEquipment (without subtracting ring) slashes capped stats to 0.10x (~6.16 < 9.6)!
        assert(
            ScoreEquipment(bisRing, EquipCategory::JewelryOrUtility, &capCtx) <
            ScoreEquipment(junkRing, EquipCategory::JewelryOrUtility, &capCtx));
        // Replacement scoring subtracts the ring first -> BiS ring scores ~61.6 >> 9.6!
        assert(
            ScoreEquippedItemForReplacement(bisRing, EquipCategory::JewelryOrUtility, &capCtx) >
            ScoreEquippedItemForReplacement(junkRing, EquipCategory::JewelryOrUtility, &capCtx) * 5.0);
    }

    // Fix #11: EnhancePotions 50% cap, FasterCasting Magery>=70 & SW/Myst parity, Dual-Wield Splintering 35% cap
    {
        // EnhancePotions 50% cap
        CharacterCompareContext epCtx;
        epCtx.Valid = true;
        epCtx.Profile = CompareBuildProfile::Hybrid;
        epCtx.HasOtherEquippedSAStats = true;
        epCtx.OtherEquippedEnhancePotions = 45;
        const ParsedEquipmentStats ep15 = ParseEquipmentOpl({ { 1060411u, L"15" } });
        const ParsedEquipmentStats none = ParseEquipmentOpl({});
        const EquipmentComparisonEntry epCmp =
            CompareSingleStats(ep15, none, EquipCategory::JewelryOrUtility, L"Ring", &epCtx);
        const EquipmentStatDelta *epD = FindDelta(epCmp.Deltas, L"Enhance Potions");
        assert(epD != nullptr && !epD->MutedByCap && epD->CapNote == L"(+5% eff)");

        epCtx.OtherEquippedEnhancePotions = 50;
        const EquipmentComparisonEntry epCapCmp =
            CompareSingleStats(ep15, none, EquipCategory::JewelryOrUtility, L"Ring", &epCtx);
        const EquipmentStatDelta *epCapD = FindDelta(epCapCmp.Deltas, L"Enhance Potions");
        assert(epCapD != nullptr && epCapD->MutedByCap && epCapD->CapNote == L"(capped)");

        // FasterCasting parity:
        // (a) Hybrid Paladin with Magery 100 + Chivalry 85 -> MaxFc = 2 (FC 2 -> 3 is capped!)
        CharacterCompareContext hybPal;
        hybPal.Valid = true;
        hybPal.Profile = CompareBuildProfile::Auto;
        hybPal.HasSkills = true;
        hybPal.Skills[25] = 100.0; // Magery >= 70
        hybPal.Skills[51] = 85.0;  // Chivalry
        hybPal.Skills[40] = 100.0; // Swords
        hybPal.HasStatusTotals = true;
        hybPal.CastSpeed = 2;
        const ParsedEquipmentStats fc1 = ParseEquipmentOpl({ { 1060413u, L"1" } });
        const EquipmentComparisonEntry hybPalCmp =
            CompareSingleStats(fc1, none, EquipCategory::JewelryOrUtility, L"Ring", &hybPal);
        const EquipmentStatDelta *hybFcD = FindDelta(hybPalCmp.Deltas, L"Faster Cast");
        assert(hybFcD != nullptr && hybFcD->MutedByCap && hybFcD->CapNote == L"(capped)");

        // (b) Pure Paladin (Chiv 85, Magery 0) -> MaxFc = 4 (FC 2 -> 3 is uncapped!)
        CharacterCompareContext purePal = hybPal;
        purePal.Skills[25] = 0.0;
        const EquipmentComparisonEntry purePalCmp =
            CompareSingleStats(fc1, none, EquipCategory::JewelryOrUtility, L"Ring", &purePal);
        const EquipmentStatDelta *palFcD = FindDelta(purePalCmp.Deltas, L"Faster Cast");
        assert(palFcD != nullptr && !palFcD->MutedByCap && palFcD->CapNote.empty());

        // (c) Pure Spellweaver (Spellweaving 100, Magery 0, Necro 0) -> MaxFc = 4 (FC 2 -> 3 uncapped!)
        CharacterCompareContext swCtx;
        swCtx.Valid = true;
        swCtx.Profile = CompareBuildProfile::Auto;
        swCtx.HasSkills = true;
        swCtx.Skills[54] = 100.0; // Spellweaving
        swCtx.HasStatusTotals = true;
        swCtx.CastSpeed = 2;
        const EquipmentComparisonEntry swCmp =
            CompareSingleStats(fc1, none, EquipCategory::JewelryOrUtility, L"Ring", &swCtx);
        const EquipmentStatDelta *swFcD = FindDelta(swCmp.Deltas, L"Faster Cast");
        assert(swFcD != nullptr && !swFcD->MutedByCap && swFcD->CapNote.empty());

        // (d) Pure Mystic (Mysticism 100) -> MaxFc = 4 (FC 2 -> 3 uncapped!)
        CharacterCompareContext mystCtx = swCtx;
        mystCtx.Skills[54] = 0.0;
        mystCtx.Skills[55] = 100.0; // Mysticism
        const EquipmentComparisonEntry mystCmp =
            CompareSingleStats(fc1, none, EquipCategory::JewelryOrUtility, L"Ring", &mystCtx);
        const EquipmentStatDelta *mystFcD = FindDelta(mystCmp.Deltas, L"Faster Cast");
        assert(mystFcD != nullptr && !mystFcD->MutedByCap && mystFcD->CapNote.empty());

        // Dual-Wield SplinteringWeapon 35% cap
        CharacterCompareContext dwCtx;
        dwCtx.Valid = true;
        dwCtx.Profile = CompareBuildProfile::Melee;
        dwCtx.HasOtherEquippedSAStats = true;
        dwCtx.OtherEquippedSplinteringWeapon = 25;
        const ParsedEquipmentStats spl20 = ParseEquipmentOpl({
            { 1061168u, L"12\t16" },
            { 1061167u, L"2.5s" },
            { 1112857u, L"20" },
        });
        const ParsedEquipmentStats spl0 = ParseEquipmentOpl({
            { 1061168u, L"12\t16" },
            { 1061167u, L"2.5s" },
        });
        const EquipmentComparisonEntry splCmp =
            CompareSingleStats(spl20, spl0, EquipCategory::Weapon, L"Main Hand", &dwCtx);
        const EquipmentStatDelta *splD = FindDelta(splCmp.Deltas, L"Splintering");
        assert(splD != nullptr && !splD->MutedByCap && splD->CapNote == L"(+10% eff)");
    }

    // Fix #13: Combined 2H DPS baseline includes off-hand SSI/DI on main hand
    {
        CharacterCompareContext ctx;
        ctx.Valid = true;
        ctx.Profile = CompareBuildProfile::Melee;
        ctx.HasStatusTotals = true;
        ctx.WeaponDamage = 50; // 1H has 0 DI, Shield has 50 DI -> total 50
        ctx.WeaponSpeed = 0;

        const ParsedEquipmentStats twoH = ParseEquipmentOpl({
            { 1061168u, L"15\t15" }, // avg 15, 2.5s, 0 DI -> 6.0 Eff DPS
            { 1061167u, L"2.5s" },
            { 1061171u, L"" },
        });
        const ParsedEquipmentStats oneH = ParseEquipmentOpl({
            { 1061168u, L"15\t15" }, // avg 15, 2.5s, 0 DI, WITH Shield's +50% DI -> 9.0 Eff DPS!
            { 1061167u, L"2.5s" },
            { 1061824u, L"" },
        });
        const ParsedEquipmentStats diShield = ParseEquipmentOpl({
            { 1060448u, L"10" },
            { 1060401u, L"50" }, // +50% DI on shield!
        });
        const EquipmentComparisonEntry cmp = CompareAgainstCombinedStats(
            twoH, oneH, diShield, EquipCategory::Weapon, L"Combined (1H + Shield)", &ctx);
        const EquipmentStatDelta *dpsD = FindDelta(cmp.Deltas, L"Eff DPS");
        assert(dpsD != nullptr && ApproxEq(dpsD->Delta, 6.0 - 9.0, 0.05));
    }

    // Fix #16: OL_15 (Face) classifies as ArmorOrShield even with TileFlag.Weapon, labeled "Face"
    {
        const ParsedEquipmentStats mask = ParseEquipmentOpl({ { 1060448u, L"5" } });
        assert(ClassifyEquipmentCategory(mask, OL_15, true) == EquipCategory::ArmorOrShield);

        EquipmentCompareItemInput faceHov;
        faceHov.Serial = 0x40000430u;
        faceHov.Layer = OL_15;
        faceHov.IsWeapon = true;
        faceHov.PropertiesLoaded = true;
        faceHov.Properties = { { 1060448u, L"8" } };

        EquipmentCompareItemInput faceEq;
        faceEq.Serial = 0x40000431u;
        faceEq.Layer = OL_15;
        faceEq.IsWeapon = true;
        faceEq.PropertiesLoaded = true;
        faceEq.FormattedTooltip = L"Face Mask";
        faceEq.Properties = { { 1060448u, L"5" } };

        const EquipmentCompareResult faceRes = BuildEquipmentComparison(faceHov, { faceEq }, 1, 1);
        assert(faceRes.Valid && faceRes.Comparisons.size() == 1u);
        assert(faceRes.Comparisons[0].TargetLabel == L"Face");
        assert(faceRes.Comparisons[0].Verdict == UpgradeVerdict::Upgrade);
        assert(Contains(faceRes.SideCards[1].HtmlText, L"[EQUIPPED: Face]"));
    }

    // Polish #5: Dual-wield SplinteringWeapon does NOT stack across sibling weapons (only non-weapon helm like ElvenGlasses)
    {
        CharacterCompareContext ctx;
        ctx.Valid = true;
        ctx.Profile = CompareBuildProfile::Melee;
        ctx.HasOtherEquippedSAStats = true;
        ctx.OtherEquippedSplinteringWeapon = 0; // no ElvenGlasses

        EquipmentCompareItemInput hov;
        hov.Serial = 0x40000501u;
        hov.Layer = OL_1_HAND;
        hov.IsWeapon = true;
        hov.PropertiesLoaded = true;
        hov.Properties = {
            { 1061168u, L"14\t16" },
            { 1061167u, L"2.5s" },
            { 1061824u, L"" },
            { 1112857u, L"30" }, // splintering weapon 30%
        };

        EquipmentCompareItemInput mh;
        mh.Serial = 0x40000502u;
        mh.Layer = OL_1_HAND;
        mh.IsWeapon = true;
        mh.PropertiesLoaded = true;
        mh.FormattedTooltip = L"Main Hand Sword";
        mh.Properties = {
            { 1061168u, L"14\t16" },
            { 1061167u, L"2.5s" },
            { 1061824u, L"" },
            { 1112857u, L"10" },
        };

        EquipmentCompareItemInput oh;
        oh.Serial = 0x40000503u;
        oh.Layer = OL_2_HAND;
        oh.IsWeapon = true;
        oh.IsOffHandWeapon = true;
        oh.PropertiesLoaded = true;
        oh.FormattedTooltip = L"Off Hand Sword";
        oh.Properties = {
            { 1061168u, L"14\t16" },
            { 1061167u, L"2.5s" },
            { 1061824u, L"" },
            { 1112857u, L"30" }, // 30% on OH must NOT cap MH comparison!
        };

        const EquipmentCompareResult dwRes = BuildEquipmentComparison(hov, { mh, oh }, 2, 2, &ctx);
        assert(dwRes.Valid && dwRes.Comparisons.size() == 2u);
        const EquipmentStatDelta *mhSplint = FindDelta(dwRes.Comparisons[0].Deltas, L"Splintering");
        assert(mhSplint != nullptr && ApproxEq(mhSplint->Delta, 20.0));
        // Must NOT be marked capped because OH's 30% does not stack onto MH swings.
        assert(!mhSplint->MutedByCap && mhSplint->CapNote.empty());
    }

    // Polish #6: Quiver with Lower Ammo Cost (1075208) and 0 QuiverDamageMod classifies as JewelryOrUtility & shows delta
    {
        const ParsedEquipmentStats elvenQuiver = ParseEquipmentOpl({
            { 1075208u, L"30" }, // Lower Ammo Cost 30%
        });
        assert(elvenQuiver.QuiverLowerAmmoCost == 30);
        assert(ClassifyEquipmentCategory(elvenQuiver, OL_CLOAK, false) == EquipCategory::JewelryOrUtility);

        const ParsedEquipmentStats plainQuiver = ParseEquipmentOpl({
            { 1075208u, L"10" },
        });
        const EquipmentComparisonEntry cmp = CompareSingleStats(
            elvenQuiver, plainQuiver, EquipCategory::JewelryOrUtility, L"Cloak");
        assert(cmp.Verdict == UpgradeVerdict::Upgrade);
        const EquipmentStatDelta *ammoD = FindDelta(cmp.Deltas, L"Lower Ammo");
        assert(ammoD != nullptr && ApproxEq(ammoD->Delta, 20.0));
    }
}
} // namespace

int main()
{
    TestParseAllClilocGroups();
    TestWeaponSpeedAndSsiCap();
    TestMageWeaponLowerIsBetter();
    TestCategoryClassification();
    TestOneHandedVsDualWield();
    TestTwoHandedVsDualWieldCombined();
    TestTwoHandedVsOneHandAndShield();
    TestArmorResistsLmcAndRegen();
    TestMultiSlotJewelryBraceletsEarringsRings();
    TestEmptySlotAndLoadingStates();
    TestKarmaLossLowerIsBetterAndSidegradeThreshold();
    TestHandSwapNotes();
    TestMalformedAndEdgeOplInputs();
    TestNegativeAndTinyBaselinesAndEmptySlotNegative();
    TestMixedMlAndPreMlSpeedDelta();
    TestPartialTwoTargetLoading();
    TestCrossCategorySpellbookVsWeapon();
    TestIdenticalStatsAndTruncation();
    TestTiledataWeaponFlagRegression();
    TestSotaBuildProfilesAndStatWorth();
    TestSotaCapAwareScoringResistsDpsAndWarnings();
    TestParseSAToLAttributesAndIgnoredMetadata();
    TestSAToLProfileScoringRangedAndDeltas();
    TestSAToLCapsAndCombinedComparison();
    TestAuditRegressionFixes();

    std::printf("equipment-compare-rules: all tests passed!\n");
    return 0;
}
