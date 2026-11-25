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

    return 0;
}
