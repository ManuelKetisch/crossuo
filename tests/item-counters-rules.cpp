// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Assistant/core/itemcounters_rules.h"
#include <cassert>

int main()
{
    using namespace xuoassist;

    // 1. Tiny 14x14 reagent centered inside a 44x44 padded texture upscales to fill a 32x32 box.
    {
        const SupplyIconLayout layout = ItemCounters_ComputeIconLayout(
            10, 20, 32, 32,
            44, 44,
            15, 15, 14, 14);
        assert(layout.VisibleWidth == 32);
        assert(layout.VisibleHeight == 32);
        assert(layout.DrawWidth > 44);
        assert(layout.DrawHeight > 44);
        // Visible artwork start should be centered in [10, 42) x [20, 52).
        const double scale = 32.0 / 14.0;
        const int scaledOffX = static_cast<int>(std::lround(15 * scale));
        const int scaledOffY = static_cast<int>(std::lround(15 * scale));
        assert(layout.DrawX + scaledOffX == 10);
        assert(layout.DrawY + scaledOffY == 20);
    }

    // 2. Very small 8x8 sprite caps upscale at maxScaleNum/maxScaleDen (5/2 = 2.5x -> 20x20) and centers.
    {
        const SupplyIconLayout layout = ItemCounters_ComputeIconLayout(
            0, 0, 32, 32,
            44, 44,
            18, 18, 8, 8);
        assert(layout.VisibleWidth == 20);
        assert(layout.VisibleHeight == 20);
        assert(layout.DrawWidth == 110);
        assert(layout.DrawHeight == 110);
        assert(layout.DrawX == (32 - 20) / 2 - 45);
        assert(layout.DrawY == (32 - 20) / 2 - 45);
    }

    // 3. Oversized 64x40 sprite downscales cleanly to fit a 32x32 box preserving aspect ratio.
    {
        const SupplyIconLayout layout = ItemCounters_ComputeIconLayout(
            100, 50, 32, 32,
            64, 40,
            0, 0, 64, 40);
        assert(layout.VisibleWidth == 32);
        assert(layout.VisibleHeight == 20);
        assert(layout.DrawWidth == 32);
        assert(layout.DrawHeight == 20);
        assert(layout.DrawX == 100);
        assert(layout.DrawY == 56);
    }

    // 4. Zero or missing ImageWidth/ImageHeight falls back to full spriteWidth/spriteHeight with zero offset.
    {
        const SupplyIconLayout layout = ItemCounters_ComputeIconLayout(
            4, 8, 32, 32,
            16, 16,
            9, 7, 0, 0);
        assert(layout.VisibleWidth == 32);
        assert(layout.VisibleHeight == 32);
        assert(layout.DrawWidth == 32);
        assert(layout.DrawHeight == 32);
        assert(layout.DrawX == 4);
        assert(layout.DrawY == 8);
    }

    // 5. Invalid/zero sprite or box dimensions return a zeroed layout safely.
    {
        const SupplyIconLayout zeroSprite = ItemCounters_ComputeIconLayout(
            10, 20, 32, 32, 0, 44, 0, 0, 0, 0);
        assert(zeroSprite.DrawX == 0 && zeroSprite.DrawY == 0);
        assert(zeroSprite.DrawWidth == 0 && zeroSprite.DrawHeight == 0);
        assert(zeroSprite.VisibleWidth == 0 && zeroSprite.VisibleHeight == 0);

        const SupplyIconLayout zeroBox = ItemCounters_ComputeIconLayout(
            10, 20, 0, 32, 44, 44, 15, 15, 14, 14);
        assert(zeroBox.DrawWidth == 0 && zeroBox.DrawHeight == 0);
        assert(zeroBox.VisibleWidth == 0 && zeroBox.VisibleHeight == 0);
    }

    // 5b. Backpack-native icon layout: never upscales small sprites, accounts for
    // stackable +5,+5 double-draw centering, and only downscales oversized art.
    {
        // 14x14 reagent inside 44x44 texture with stack double-draw inside 44x32 box.
        const SupplyIconLayout stacked = ItemCounters_ComputeBackpackIconLayout(
            10, 20, 44, 32,
            44, 44,
            15, 15, 14, 14,
            true);
        assert(!stacked.Downscaled);
        assert(stacked.DoubleDraw);
        assert(stacked.DrawWidth == 44);
        assert(stacked.DrawHeight == 44);
        assert(stacked.VisibleWidth == 19);
        assert(stacked.VisibleHeight == 19);
        // Centered visible span [10 + (44-19)/2, ...] = 22 -> DrawX = 22 - 15 = 7.
        assert(stacked.DrawX == 10 + (44 - 19) / 2 - 15);
        assert(stacked.DrawY == 20 + (32 - 19) / 2 - 15);

        // Single-count reagent (no double draw) stays 1:1 centered.
        const SupplyIconLayout single = ItemCounters_ComputeBackpackIconLayout(
            10, 20, 44, 32,
            44, 44,
            15, 15, 14, 14,
            false);
        assert(!single.Downscaled);
        assert(!single.DoubleDraw);
        assert(single.DrawWidth == 44);
        assert(single.DrawHeight == 44);
        assert(single.VisibleWidth == 14);
        assert(single.VisibleHeight == 14);
        assert(single.DrawX == 10 + (44 - 14) / 2 - 15);
        assert(single.DrawY == 20 + (32 - 14) / 2 - 15);

        // Sprite that fits 1:1 (42x30) but not with +5 shift drops double-draw instead of downscaling.
        const SupplyIconLayout tightFit = ItemCounters_ComputeBackpackIconLayout(
            0, 0, 44, 32,
            44, 44,
            1, 7, 42, 30,
            true);
        assert(!tightFit.Downscaled);
        assert(!tightFit.DoubleDraw);
        assert(tightFit.DrawWidth == 44);
        assert(tightFit.DrawHeight == 44);
        assert(tightFit.VisibleWidth == 42);
        assert(tightFit.VisibleHeight == 30);

        // Oversized 64x40 sprite downscales cleanly without double-draw.
        const SupplyIconLayout oversized = ItemCounters_ComputeBackpackIconLayout(
            100, 50, 44, 32,
            64, 40,
            0, 0, 64, 40,
            true);
        assert(oversized.Downscaled);
        assert(!oversized.DoubleDraw);
        assert(oversized.VisibleWidth == 44);
        assert(oversized.VisibleHeight == 28);
        assert(oversized.DrawX == 100);
        assert(oversized.DrawY == 52);
    }

    // 6. Pouch and non-bag container graphic helper checks.
    assert(ItemCounters_PouchGraphic == 0x0E79);
    assert(ItemCounters_ShouldSkipAutoOpenBag(0x0E79, true));
    assert(!ItemCounters_ShouldSkipAutoOpenBag(0x0E79, false));
    assert(!ItemCounters_ShouldSkipAutoOpenBag(0x0E76, true));
    assert(!ItemCounters_IsNonBagContainerGraphic(0x0E76)); // bag
    assert(!ItemCounters_IsNonBagContainerGraphic(0x0E75)); // backpack
    assert(!ItemCounters_IsNonBagContainerGraphic(0x09B2)); // backpack
    assert(!ItemCounters_IsNonBagContainerGraphic(0x0E7C)); // chest
    assert(!ItemCounters_IsNonBagContainerGraphic(0x2FB7)); // quiver

    // Spellbooks (including Book of Chivalry 0x2252), runebooks, BOD books,
    // key rings, potion kegs, game boards, and corpses carry TileFlag::Container
    // in UO tiledata but must never be auto-opened or recounted as bags.
    const uint16_t nonBagGraphics[] = {
        0x0E3B, 0x0EFA, 0x2252, 0x2253, 0x2254, 0x225A, 0x225B, 0x238C, 0x23A0,
        0x2D50, 0x2D9D, 0x2259, 0x22C5, 0x9C16, 0x9C17, 0x9A95, 0xA266, 0x1011,
        0x1769, 0x1940, 0x0FA6, 0x1E5E, 0x2006
    };
    for (uint16_t graphic : nonBagGraphics)
    {
        assert(ItemCounters_IsNonBagContainerGraphic(graphic));
        assert(ItemCounters_ShouldSkipAutoOpenBag(graphic, true));
        assert(ItemCounters_ShouldSkipAutoOpenBag(graphic, false));
    }

    // Recount run gate: manual recount always runs; auto-recount requires an
    // active counter consumer and yields while desktop container restore runs.
    assert(ItemCounters_ShouldRunRecount(false, true, false, true));
    assert(!ItemCounters_ShouldRunRecount(true, false, false, false));
    assert(!ItemCounters_ShouldRunRecount(true, false, true, true));
    assert(!ItemCounters_ShouldRunRecount(false, false, true, false));
    assert(ItemCounters_ShouldRunRecount(true, false, true, false));

    // 7. Container change decision rules.
    {
        // Outside player backpack -> Ignore.
        ContainerChangeInput input;
        input.InsidePlayerBackpack = false;
        input.ContainerOpened = false;
        input.IsNewSubcontainer = true;
        input.MatchesTrackedCounter = true;
        input.AutoRecountEnabled = true;
        input.SkipPouches = false;
        input.TargetBagGraphic = 0x0E76;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::Ignore);
    }

    {
        // Opened bag inside backpack -> RefreshCountsOnly (never re-open).
        ContainerChangeInput input;
        input.InsidePlayerBackpack = true;
        input.ContainerOpened = true;
        input.IsNewSubcontainer = false;
        input.MatchesTrackedCounter = true;
        input.AutoRecountEnabled = true;
        input.SkipPouches = true;
        input.TargetBagGraphic = 0x0E76;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::RefreshCountsOnly);
    }

    {
        // Tracked item entering unopened non-pouch bag -> RefreshAndQueueBagOpen.
        ContainerChangeInput input;
        input.InsidePlayerBackpack = true;
        input.ContainerOpened = false;
        input.IsNewSubcontainer = false;
        input.MatchesTrackedCounter = true;
        input.AutoRecountEnabled = true;
        input.SkipPouches = true;
        input.TargetBagGraphic = 0x0E76;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::RefreshAndQueueBagOpen);
    }

    {
        // Untracked item entering unopened bag -> RefreshCountsOnly.
        ContainerChangeInput input;
        input.InsidePlayerBackpack = true;
        input.ContainerOpened = false;
        input.IsNewSubcontainer = false;
        input.MatchesTrackedCounter = false;
        input.AutoRecountEnabled = true;
        input.SkipPouches = true;
        input.TargetBagGraphic = 0x0E76;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::RefreshCountsOnly);
    }

    {
        // New unopened non-pouch sub-bag entering backpack -> RefreshAndQueueBagOpen.
        ContainerChangeInput input;
        input.InsidePlayerBackpack = true;
        input.ContainerOpened = false;
        input.IsNewSubcontainer = true;
        input.MatchesTrackedCounter = false;
        input.AutoRecountEnabled = true;
        input.SkipPouches = true;
        input.TargetBagGraphic = 0x0E76;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::RefreshAndQueueBagOpen);
    }

    {
        // Pouch (0x0E79) skipped when SkipPouches == true, queued when SkipPouches == false.
        ContainerChangeInput input;
        input.InsidePlayerBackpack = true;
        input.ContainerOpened = false;
        input.IsNewSubcontainer = true;
        input.MatchesTrackedCounter = false;
        input.AutoRecountEnabled = true;
        input.SkipPouches = true;
        input.TargetBagGraphic = ItemCounters_PouchGraphic;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::RefreshCountsOnly);

        input.SkipPouches = false;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::RefreshAndQueueBagOpen);
    }

    {
        // Book of Chivalry (0x2252) and other spellbooks never queue bag open,
        // even when SkipPouches is false and AutoRecountEnabled is true.
        ContainerChangeInput input;
        input.InsidePlayerBackpack = true;
        input.ContainerOpened = false;
        input.IsNewSubcontainer = true;
        input.MatchesTrackedCounter = false;
        input.AutoRecountEnabled = true;
        input.SkipPouches = false;
        input.TargetBagGraphic = 0x2252;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::RefreshCountsOnly);

        input.TargetBagGraphic = 0x0EFA;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::RefreshCountsOnly);

        input.TargetBagGraphic = 0x1011;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::RefreshCountsOnly);
    }

    {
        // AutoRecountEnabled == false -> RefreshCountsOnly even for unopened non-pouch bags.
        ContainerChangeInput input;
        input.InsidePlayerBackpack = true;
        input.ContainerOpened = false;
        input.IsNewSubcontainer = true;
        input.MatchesTrackedCounter = true;
        input.AutoRecountEnabled = false;
        input.SkipPouches = false;
        input.TargetBagGraphic = 0x0E76;
        assert(ItemCounters_DecideContainerChange(input) == ContainerChangeAction::RefreshCountsOnly);
    }

    // 8. Stat threshold evaluation: HigherIsBetter (hp/mana/stam) with default 33/66 thresholds.
    {
        assert(ItemCounters_EvaluateThreshold(0, 100, 33, 66, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Low);
        assert(ItemCounters_EvaluateThreshold(33, 100, 33, 66, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Low);
        assert(ItemCounters_EvaluateThreshold(34, 100, 33, 66, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(66, 100, 33, 66, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(67, 100, 33, 66, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Good);
        assert(ItemCounters_EvaluateThreshold(100, 100, 33, 66, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Good);

        // Over-100% and negative values clamp cleanly into [0, 100].
        assert(ItemCounters_EvaluateThreshold(140, 100, 33, 66, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Good);
        assert(ItemCounters_EvaluateThreshold(-10, 100, 33, 66, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Low);

        // Custom thresholds and single-threshold configurations.
        assert(ItemCounters_EvaluateThreshold(20, 100, 20, 50, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Low);
        assert(ItemCounters_EvaluateThreshold(21, 100, 20, 50, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(50, 100, 20, 50, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(51, 100, 20, 50, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Good);
        assert(ItemCounters_EvaluateThreshold(25, 100, 25, 0, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Low);
        assert(ItemCounters_EvaluateThreshold(26, 100, 25, 0, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Good);
        assert(ItemCounters_EvaluateThreshold(50, 100, 0, 50, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(51, 100, 0, 50, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Good);

        // maxValue == 0 falls back to raw value (e.g. STR/INT/DEX with min=10, mid=30).
        assert(ItemCounters_EvaluateThreshold(10, 0, 10, 30, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Low);
        assert(ItemCounters_EvaluateThreshold(11, 0, 10, 30, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(30, 0, 10, 30, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(31, 0, 10, 30, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Good);

        // Zero thresholds (0, 0) return Default.
        assert(ItemCounters_EvaluateThreshold(10, 100, 0, 0, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Default);
        assert(ItemCounters_EvaluateThreshold(0, 100, -1, 0, StatThresholdPolarity::HigherIsBetter) ==
               StatThresholdBand::Default);
    }

    // 9. Stat threshold evaluation: LowerIsBetter (weight) with default 75/90 thresholds.
    {
        assert(ItemCounters_EvaluateThreshold(0, 400, 75, 90, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Good);
        assert(ItemCounters_EvaluateThreshold(299, 400, 75, 90, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Good); // 74%
        assert(ItemCounters_EvaluateThreshold(300, 400, 75, 90, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Mid); // 75%
        assert(ItemCounters_EvaluateThreshold(359, 400, 75, 90, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Mid); // 89%
        assert(ItemCounters_EvaluateThreshold(360, 400, 75, 90, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Low); // 90%
        assert(ItemCounters_EvaluateThreshold(399, 400, 75, 90, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Low); // 99%

        // Overencumbered (value >= maxValue) is always Low even if thresholds exceed 100.
        assert(ItemCounters_EvaluateThreshold(400, 400, 105, 110, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Low);
        assert(ItemCounters_EvaluateThreshold(450, 400, 105, 110, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Low);

        // Legacy 33/66 and reversed 90/75 thresholds both work cleanly.
        assert(ItemCounters_EvaluateThreshold(32, 100, 33, 66, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Good);
        assert(ItemCounters_EvaluateThreshold(33, 100, 33, 66, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(66, 100, 33, 66, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Low);
        assert(ItemCounters_EvaluateThreshold(74, 100, 90, 75, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Good);
        assert(ItemCounters_EvaluateThreshold(75, 100, 90, 75, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(90, 100, 90, 75, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Low);

        // Single-threshold, raw fallback, and disabled (0, 0) for LowerIsBetter.
        assert(ItemCounters_EvaluateThreshold(75, 100, 75, 0, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(74, 100, 75, 0, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Good);
        assert(ItemCounters_EvaluateThreshold(90, 100, 0, 90, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Low);
        assert(ItemCounters_EvaluateThreshold(89, 100, 0, 90, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Good);
        assert(ItemCounters_EvaluateThreshold(80, 0, 75, 90, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Mid);
        assert(ItemCounters_EvaluateThreshold(450, 400, 0, 0, StatThresholdPolarity::LowerIsBetter) ==
               StatThresholdBand::Default);
    }

    // 10. Threshold color mapping.
    {
        const uint32_t defColor = ItemCounters_ThresholdColor(StatThresholdBand::Default);
        const uint32_t goodColor = ItemCounters_ThresholdColor(StatThresholdBand::Good);
        const uint32_t midColor = ItemCounters_ThresholdColor(StatThresholdBand::Mid);
        const uint32_t lowColor = ItemCounters_ThresholdColor(StatThresholdBand::Low);

        assert(defColor == 0u);
        assert(lowColor == ItemCounters_ThresholdLowColor);
        assert(midColor == ItemCounters_ThresholdMidColor);
        assert(goodColor == ItemCounters_ThresholdGoodColor);
        assert((lowColor >> 24) == 0xFFu);
        assert((midColor >> 24) == 0xFFu);
        assert((goodColor >> 24) == 0xFFu);
        assert(lowColor != midColor && midColor != goodColor && lowColor != goodColor);
    }

    // 11. Built-in grouped supply catalog integrity, ModernUO IDs/hues, and uniqueness.
    {
        const auto &catalog = ItemCounters_GetDefaultCatalog();
        assert(catalog.size() == 47);

        std::array<int, ItemCounters_CategoryCount> counts{};
        for (size_t i = 0; i < catalog.size(); ++i)
        {
            const auto &spec = catalog[i];
            assert(spec.Name != nullptr && spec.Name[0] != '\0');
            assert(spec.Key != nullptr && spec.Key[0] != '\0');
            assert(spec.Graphic > 0 && spec.Graphic < ItemCounters_AnyHue);
            assert(spec.Hue == ItemCounters_AnyHue || spec.Hue <= 0x3FFFu);
            assert(spec.Category != SupplyCategory::Custom);
            counts[static_cast<size_t>(spec.Category)] += 1;

            // Every built-in key must be unique and not collide with reserved DisplayManager stats.
            assert(!ItemCounters_EqualsIgnoreCaseAscii(spec.Key, "str"));
            assert(!ItemCounters_EqualsIgnoreCaseAscii(spec.Key, "dex"));
            assert(!ItemCounters_EqualsIgnoreCaseAscii(spec.Key, "int"));
            assert(!ItemCounters_EqualsIgnoreCaseAscii(spec.Key, "gold"));
            assert(!ItemCounters_EqualsIgnoreCaseAscii(spec.Key, "bandage"));
            for (size_t j = i + 1; j < catalog.size(); ++j)
            {
                assert(!ItemCounters_EqualsIgnoreCaseAscii(spec.Key, catalog[j].Key));
            }
        }

        assert(counts[static_cast<size_t>(SupplyCategory::Magery)] == 8);
        assert(counts[static_cast<size_t>(SupplyCategory::Necromancy)] == 5);
        assert(counts[static_cast<size_t>(SupplyCategory::Mysticism)] == 5);
        assert(counts[static_cast<size_t>(SupplyCategory::Potions)] == 13);
        assert(counts[static_cast<size_t>(SupplyCategory::Combat)] == 8);
        assert(counts[static_cast<size_t>(SupplyCategory::Utility)] == 8);
        assert(counts[static_cast<size_t>(SupplyCategory::Custom)] == 0);

        // Verify shared-graphic potions use exact hues so they do not double-count.
        assert(ItemCounters_HueMatches(0x0000, 0x0000));
        assert(!ItemCounters_HueMatches(0x0000, 0x0489)); // Night Sight vs Conflagration (0x0F06)
        assert(!ItemCounters_HueMatches(0x0000, 0x048D)); // Poison vs Invisibility (0x0F0A)
        assert(ItemCounters_HueMatches(0x048D, static_cast<uint16_t>(0x848D))); // partial hue flag bit
        assert(ItemCounters_HueMatches(ItemCounters_AnyHue, 0x048E));

        // Category inference for legacy v1 counters & shared potion graphics.
        assert(ItemCounters_InferCategory(0x0F7A, ItemCounters_AnyHue) == SupplyCategory::Magery);
        assert(ItemCounters_InferCategory(0x0F78, ItemCounters_AnyHue) == SupplyCategory::Necromancy);
        assert(ItemCounters_InferCategory(0x0F8F, ItemCounters_AnyHue) == SupplyCategory::Necromancy);
        assert(ItemCounters_InferCategory(0x0F8E, ItemCounters_AnyHue) == SupplyCategory::Necromancy);
        assert(ItemCounters_InferCategory(0x0F8A, ItemCounters_AnyHue) == SupplyCategory::Necromancy);
        assert(ItemCounters_InferCategory(0x4077, ItemCounters_AnyHue) == SupplyCategory::Mysticism);
        assert(ItemCounters_InferCategory(0x0F0C, 0x0000) == SupplyCategory::Potions);
        assert(ItemCounters_InferCategory(0x0F06, 0x0489) == SupplyCategory::Potions);
        assert(ItemCounters_InferCategory(0x0E21, ItemCounters_AnyHue) == SupplyCategory::Combat);
        assert(ItemCounters_InferCategory(0x0E24, 0x048E) == SupplyCategory::Utility);
        assert(ItemCounters_InferCategory(0x1234, 0x0000) == SupplyCategory::Custom);

        // Category INI parsing & round-tripping.
        for (size_t c = 0; c < ItemCounters_CategoryCount; ++c)
        {
            const auto cat = static_cast<SupplyCategory>(c);
            SupplyCategory parsed = SupplyCategory::Custom;
            assert(ItemCounters_ParseCategory(ItemCounters_CategoryId(cat), parsed));
            assert(parsed == cat);
            assert(ItemCounters_ParseCategoryFilter(ItemCounters_CategoryFilterId(static_cast<int>(c))) ==
                   static_cast<int>(c));
        }
        assert(ItemCounters_ParseCategoryFilter("all") == -1);
        assert(ItemCounters_ParseCategoryFilter("unknown") == -1);
        assert(ItemCounters_EqualsIgnoreCaseAscii(ItemCounters_CategoryFilterId(-1), "all"));

        // Migration deduplication: existing user Bat Wing (hue 0) covers AnyHue default,
        // while existing Poison Potion (0x0F0A, hue 0) does NOT suppress Invisibility Potion (0x0F0A, hue 0x048D).
        const DefaultSupplySpec batWingSpec{ "Bat wing", "bw", 0x0F78, ItemCounters_AnyHue, 5, SupplyCategory::Necromancy };
        const DefaultSupplySpec invisSpec{ "Invisibility potion", "invis_pot", 0x0F0A, 0x048D, 0, SupplyCategory::Potions };
        assert(ItemCounters_DefaultMatchesExisting("my_bats", 0x0F78, 0x0000, batWingSpec));
        assert(!ItemCounters_DefaultMatchesExisting("poison_pot", 0x0F0A, 0x0000, invisSpec));
        assert(ItemCounters_DefaultMatchesExisting("invis_pot", 0x0F0A, 0x048D, invisSpec));
    }

    // 12. Grouped status-bar layout: All view fits 6 groups on 1 page at 8 columns,
    // supports category filtering, collapsible sections, In-bag filtering, and narrow pagination.
    {
        const auto &catalog = ItemCounters_GetDefaultCatalog();
        std::vector<SupplyLayoutEntryInput> allEntries;
        allEntries.reserve(catalog.size());
        for (size_t i = 0; i < catalog.size(); ++i)
        {
            const auto &spec = catalog[i];
            // Stocked items: Black pearl, Bat wing, Heal potion, Bandages.
            const int count = (spec.Graphic == 0x0F7A || spec.Graphic == 0x0F78 ||
                               spec.Graphic == 0x0F0C || spec.Graphic == 0x0E21)
                ? 12
                : 0;
            const bool highlighted = spec.Graphic == 0x0F8F; // Grave dust cast highlight
            allEntries.push_back({ i, spec.Category, count, highlighted });
        }

        // Standard 8-column All view fits all 6 built-in groups on 1 page cleanly.
        const auto fullLayout = ItemCounters_BuildGroupedSupplyLayout(
            allEntries, -1, false, 0u, 8, 44, 17, 4, 460);
        assert(fullLayout.Pages.size() == 1);
        assert(fullLayout.Pages[0].Sections.size() == 6);
        assert(fullLayout.Pages[0].Sections[0].Category == SupplyCategory::Magery);
        assert(fullLayout.Pages[0].Sections[0].ShowHeader);
        assert(fullLayout.Pages[0].Sections[0].StockedEntries == 1);
        assert(fullLayout.Pages[0].Sections[0].TotalEntries == 8);
        assert(fullLayout.Pages[0].Sections[1].Category == SupplyCategory::Necromancy);
        assert(fullLayout.Pages[0].Sections[3].Category == SupplyCategory::Potions);
        assert(fullLayout.Pages[0].Sections[3].SourceIndices.size() == 13);

        // Single-category tab (Potions) omits group header and returns only Potions.
        const auto potionsLayout = ItemCounters_BuildGroupedSupplyLayout(
            allEntries, static_cast<int>(SupplyCategory::Potions), false, 0u, 8, 44, 17, 4, 460);
        assert(potionsLayout.Pages.size() == 1);
        assert(potionsLayout.Pages[0].Sections.size() == 1);
        assert(!potionsLayout.Pages[0].Sections[0].ShowHeader);
        assert(potionsLayout.Pages[0].Sections[0].SourceIndices.size() == 13);

        // In-bag filter keeps stocked items and active-cast highlighted reagents, hides empty groups.
        const auto inBagLayout = ItemCounters_BuildGroupedSupplyLayout(
            allEntries, -1, true, 0u, 8, 44, 17, 4, 460);
        assert(inBagLayout.Pages.size() == 1);
        assert(inBagLayout.Pages[0].Sections.size() == 4); // Magery, Necro (Bat wing + highlighted Grave dust), Potions, Combat
        assert(inBagLayout.Pages[0].Sections[1].Category == SupplyCategory::Necromancy);
        assert(inBagLayout.Pages[0].Sections[1].SourceIndices.size() == 2);

        // Collapsing a category in All view shrinks its section to header-only with zero tiles.
        const uint32_t collapsedMask = ItemCounters_ToggleCategoryCollapsed(0u, SupplyCategory::Potions);
        assert(ItemCounters_IsCategoryCollapsed(collapsedMask, SupplyCategory::Potions));
        const auto collapsedLayout = ItemCounters_BuildGroupedSupplyLayout(
            allEntries, -1, false, collapsedMask, 8, 44, 17, 4, 460);
        assert(collapsedLayout.Pages.size() == 1);
        assert(collapsedLayout.Pages[0].Sections[3].Collapsed);
        assert(collapsedLayout.Pages[0].Sections[3].SourceIndices.empty());
        assert(collapsedLayout.Pages[0].Height < fullLayout.Pages[0].Height);

        // Narrow classic statusbar (4 columns) cleanly paginates across multiple pages.
        const auto narrowLayout = ItemCounters_BuildGroupedSupplyLayout(
            allEntries, -1, false, 0u, 4, 44, 17, 4, 300);
        assert(narrowLayout.Pages.size() > 1);
        size_t totalTiles = 0;
        for (const auto &page : narrowLayout.Pages)
        {
            assert(page.Height <= 300);
            for (const auto &sec : page.Sections)
            {
                totalTiles += sec.SourceIndices.size();
            }
        }
        assert(totalTiles == catalog.size());
    }

    // 13. Supply unit weight catalog parity with ModernUO ItemWeights, OPL parsing,
    // stack weight estimation, statusbar weight hues, and category weight breakdowns.
    {
        const auto &catalog = ItemCounters_GetDefaultCatalog();
        for (const auto &spec : catalog)
        {
            assert(spec.UnitWeightMilliStones > 0);
            if (spec.Category == SupplyCategory::Magery ||
                spec.Category == SupplyCategory::Necromancy ||
                spec.Category == SupplyCategory::Mysticism)
            {
                assert(spec.UnitWeightMilliStones == 10);
            }
            else if (spec.Category == SupplyCategory::Potions)
            {
                assert(spec.UnitWeightMilliStones == 200);
            }
        }

        // Exact graphic/hue lookups & custom potion fallback.
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x0F7A, ItemCounters_AnyHue) == 10); // Black pearl
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x0F81, ItemCounters_AnyHue) == 10); // Fertile dirt
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x0E21, ItemCounters_AnyHue) == 10); // Bandages
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x0F3F, ItemCounters_AnyHue) == 10); // Arrows
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x0EF3, 0x0000) == 20);              // Blank scrolls
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x1F4C, 0x0000) == 20);              // Recall scrolls
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x0F0E, 0x0000) == 50);              // Empty bottles
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x14FC, 0x0000) == 50);              // Lockpicks
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x0E24, 0x048E) == 50);              // Pet ointment
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x0F0C, 0x0000) == 200);             // Heal potion
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x0F0D, 0x0999) == 200);             // Custom hued potion
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x26AC, 0x0000) == 4000);            // Bolas
        assert(ItemCounters_DefaultUnitWeightMilliStones(0x1234, 0x0000) == 0);               // Unknown item

        // Exact ceiling stack estimation matching ModernUO Item.PileWeight.
        assert(ItemCounters_EstimateStackWeightStones(0, 10) == 0);
        assert(ItemCounters_EstimateStackWeightStones(-5, 10) == 0);
        assert(ItemCounters_EstimateStackWeightStones(50, 0) == 0);
        assert(ItemCounters_EstimateStackWeightStones(1, 10) == 1);
        assert(ItemCounters_EstimateStackWeightStones(100, 10) == 1);
        assert(ItemCounters_EstimateStackWeightStones(101, 10) == 2);
        assert(ItemCounters_EstimateStackWeightStones(500, 10) == 5);
        assert(ItemCounters_EstimateStackWeightStones(60000, 10) == 600);
        assert(ItemCounters_EstimateStackWeightStones(50, 20) == 1);
        assert(ItemCounters_EstimateStackWeightStones(51, 20) == 2);
        assert(ItemCounters_EstimateStackWeightStones(20, 50) == 1);
        assert(ItemCounters_EstimateStackWeightStones(25, 200) == 5);
        assert(ItemCounters_EstimateStackWeightStones(3, 4000) == 12);

        // OPL weight cliloc parsing (1072788 singular / 1072789 plural).
        const std::vector<std::pair<uint32_t, std::wstring>> singleStone = {
            { 1042971u, L"Black Pearl" },
            { 1072788u, L"1" },
        };
        assert(ItemCounters_ParseOplWeightStones(singleStone) == 1);

        const std::vector<std::pair<uint32_t, std::wstring>> multiStone = {
            { 1072789u, L" 1,250 " },
        };
        assert(ItemCounters_ParseOplWeightStones(multiStone) == 1250);

        const std::vector<std::pair<uint32_t, std::wstring>> zeroStone = {
            { 1072789u, L"0" },
        };
        assert(ItemCounters_ParseOplWeightStones(zeroStone) == 0);

        const std::vector<std::pair<uint32_t, std::wstring>> noWeight = {
            { 1042971u, L"Blessed" },
            { 1072789u, L"invalid" },
        };
        assert(ItemCounters_ParseOplWeightStones(noWeight) == -1);

        // Human-readable unit weight strings.
        assert(ItemCounters_FormatUnitWeight(0).empty());
        assert(ItemCounters_FormatUnitWeight(10) == "0.01 st/ea (100/st)");
        assert(ItemCounters_FormatUnitWeight(20) == "0.02 st/ea (50/st)");
        assert(ItemCounters_FormatUnitWeight(50) == "0.05 st/ea (20/st)");
        assert(ItemCounters_FormatUnitWeight(200) == "0.2 st/ea (5/st)");
        assert(ItemCounters_FormatUnitWeight(4000) == "4 st/ea");

        // Statusbar weight hue thresholds (75% warn, 90%/overloaded alert).
        constexpr uint16_t kNorm = 0x0386;
        constexpr uint16_t kWarn = 0x002B;
        constexpr uint16_t kCrit = 0x0021;
        assert(ItemCounters_WeightStatusHue(200, 400, kNorm, kWarn, kCrit) == kNorm);
        assert(ItemCounters_WeightStatusHue(299, 400, kNorm, kWarn, kCrit) == kNorm);
        assert(ItemCounters_WeightStatusHue(300, 400, kNorm, kWarn, kCrit) == kWarn);
        assert(ItemCounters_WeightStatusHue(359, 400, kNorm, kWarn, kCrit) == kWarn);
        assert(ItemCounters_WeightStatusHue(360, 400, kNorm, kWarn, kCrit) == kCrit);
        assert(ItemCounters_WeightStatusHue(400, 400, kNorm, kWarn, kCrit) == kCrit);
        assert(ItemCounters_WeightStatusHue(450, 400, kNorm, kWarn, kCrit) == kCrit);
        assert(ItemCounters_WeightStatusHue(450, 0, kNorm, kWarn, kCrit) == kNorm);

        // Supply weight summary aggregation & descending heavy-category sort.
        const std::vector<SupplyWeightInput> weightInputs = {
            { SupplyCategory::Magery, 200, 2, false },
            { SupplyCategory::Magery, 300, 3, false },
            { SupplyCategory::Potions, 40, 8, true },
            { SupplyCategory::Combat, 500, 5, false },
            { SupplyCategory::Utility, 0, 0, false },
            { SupplyCategory::Necromancy, -1, -1, false },
        };
        const auto summary = ItemCounters_SummarizeSupplyWeights(weightInputs);
        assert(summary.HasKnownWeight);
        assert(summary.AnyEstimated);
        assert(summary.TotalWeightStones == 18);
        assert(summary.CategoryWeightStones[static_cast<size_t>(SupplyCategory::Magery)] == 5);
        assert(!summary.CategoryEstimated[static_cast<size_t>(SupplyCategory::Magery)]);
        assert(summary.CategoryWeightStones[static_cast<size_t>(SupplyCategory::Potions)] == 8);
        assert(summary.CategoryEstimated[static_cast<size_t>(SupplyCategory::Potions)]);
        assert(summary.HeavyCategories.size() == 3);
        assert(summary.HeavyCategories[0].Category == SupplyCategory::Potions);
        assert(summary.HeavyCategories[0].WeightStones == 8);
        assert(summary.HeavyCategories[1].Category == SupplyCategory::Magery);
        assert(summary.HeavyCategories[1].WeightStones == 5);
        assert(summary.HeavyCategories[2].Category == SupplyCategory::Combat);
        assert(summary.HeavyCategories[2].WeightStones == 5);

        // Grouped layout propagates category weight totals to section headers.
        const std::vector<SupplyLayoutEntryInput> weightedEntries = {
            { 0, SupplyCategory::Magery, 200, false, 2, false },
            { 1, SupplyCategory::Magery, 300, false, 3, true },
            { 2, SupplyCategory::Combat, 500, false, 5, false },
        };
        const auto weightedLayout = ItemCounters_BuildGroupedSupplyLayout(
            weightedEntries, -1, false, 0u, 8, 44, 17, 4, 460);
        assert(weightedLayout.Pages.size() == 1);
        assert(weightedLayout.Pages[0].Sections.size() == 2);
        assert(weightedLayout.Pages[0].Sections[0].HasKnownWeight);
        assert(weightedLayout.Pages[0].Sections[0].WeightStones == 5);
        assert(weightedLayout.Pages[0].Sections[0].WeightEstimated);
        assert(weightedLayout.Pages[0].Sections[1].HasKnownWeight);
        assert(weightedLayout.Pages[0].Sections[1].WeightStones == 5);
        assert(!weightedLayout.Pages[0].Sections[1].WeightEstimated);
    }

    return 0;
}
