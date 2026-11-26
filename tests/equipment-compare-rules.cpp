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
        { 1149791u, L"4" },
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

    std::printf("equipment-compare-rules: all tests passed!\n");
    return 0;
}
