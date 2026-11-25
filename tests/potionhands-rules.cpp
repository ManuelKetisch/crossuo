// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Assistant/core/potionhands_rules.h"
#include <cassert>

namespace {

using namespace xuoassist::potionhands;
using namespace xuoassist::tradefilter;

void test_baseline_potion_rules()
{
    assert(StandardDrink(0x0F0C, 0));
    assert(StandardDrink(0x0F06, 0));
    assert(StandardDrink(0x0F0B, 0));
    assert(!StandardDrink(0x0F06, 0x489)); // Conflagration
    assert(!StandardDrink(0x0F06, 0x48D)); // Confusion Blast
    assert(!StandardDrink(0x0F0D, 0)); // Explosion
    assert(!StandardDrink(0x0F0A, 0)); // Poison

    const uint8_t targetedUse[] = { 0xBF, 0, 13, 0, 0x2C };
    const uint8_t extCast[] = { 0xBF, 0, 9, 0, 0x1C, 0, 2, 0, 29 };
    const uint8_t targetCast[] = { 0xBF, 0, 11, 0, 0x2D, 0, 29, 0, 0, 0, 1 };
    const uint8_t other[] = { 0xBF, 0, 5, 0, 0x06 };
    assert(ActionPacket(targetedUse, sizeof(targetedUse)));
    assert(ActionPacket(extCast, sizeof(extCast)));
    assert(ActionPacket(targetCast, sizeof(targetCast)));
    assert(!ActionPacket(targetedUse, 4));
    assert(!ActionPacket(other, sizeof(other)));
    assert(!ActionPacket(nullptr, 0));

    const uint8_t itemUse[] = { 0x06, 0x40, 0x12, 0x34, 0x56 };
    const uint8_t paperdoll[] = { 0x06, 0x80, 0x00, 0x12, 0x34 };
    assert(ItemUseSerial(itemUse, sizeof(itemUse)) == 0x40123456);
    assert(ItemUseSerial(itemUse, 4) == 0);
    assert(ItemUseSerial(paperdoll, sizeof(paperdoll)) == 0);

    assert(ObserveDrink(true, true, true, 5, 5) == DrinkResult::Pending);
    assert(ObserveDrink(true, true, true, 5, 4) == DrinkResult::Consumed);
    assert(ObserveDrink(true, true, false, 5, 4) == DrinkResult::Changed);
    assert(ObserveDrink(true, true, true, 5, 6) == DrinkResult::Changed);
    assert(ObserveDrink(true, true, true, 5, 2) == DrinkResult::Changed);
    assert(ObserveDrink(true, false, false, 5, 0) == DrinkResult::Changed);
    assert(ObserveDrink(true, false, false, 1, 0) == DrinkResult::Consumed);
    assert(ObserveDrink(false, false, false, 1, 0) == DrinkResult::Changed);

    assert(DrinkRefused(1049547)); // Already full health
    assert(DrinkRefused(FreeHandMessage));
    assert(DrinkRefused(500235)); // Heal cooldown
    assert(!DrinkRefused(500231)); // Cure success is confirmed by inventory
    assert(!DeadlineReached(100, UINT32_MAX - 1199, 1301));
    assert(DeadlineReached(101, UINT32_MAX - 1199, 1301));
}

void test_spell_channeling_detection()
{
    const uint32_t withSc[] = { 1060413, SpellChannelingCliloc, 1060441 };
    const uint32_t withoutSc[] = { 1060413, 1060441, 1060483 };
    assert(HasSpellChannelingCliloc(withSc, 3));
    assert(!HasSpellChannelingCliloc(withoutSc, 3));
    assert(!HasSpellChannelingCliloc(nullptr, 0));

    assert(HasSpellChannelingText("Weight: 5 stones\nSpell Channeling\nFaster Casting -1"));
    assert(HasSpellChannelingText("  spell channeling  \r\nSwing Speed Increase 25%"));
    assert(!HasSpellChannelingText("Spell Channeling Sword\nWeight: 5 stones"));
    assert(!HasSpellChannelingText("Weight: 5 stones\nFaster Casting 1"));
    assert(!HasSpellChannelingText(nullptr));
    assert(!HasSpellChannelingText(""));
    // Non-English localized OPL line support
    assert(HasSpellChannelingText("Gewicht: 5\nZauberkanalisierung\nSchaden 10%", "Zauberkanalisierung"));
    assert(!HasSpellChannelingText("Gewicht: 5\nSchaden 10%", "Zauberkanalisierung"));

    // Spellbooks and runebooks allow equipped casting without cliloc 1060482
    assert(IsSpellbookGraphic(0x0EFA)); // Magery spellbook
    assert(IsSpellbookGraphic(0x2253)); // Necromancer spellbook
    assert(IsSpellbookGraphic(0x2252)); // Book of Chivalry
    assert(IsSpellbookGraphic(0x22C5)); // Runebook
    assert(!IsSpellbookGraphic(0x13B9)); // Viking sword
    assert(!IsSpellbookGraphic(0x1B76)); // Heater shield
}

void test_dual_wield_and_hand_plans()
{
    const HandItem empty{};
    const HandItem sword1{ 0x40000001, HandLayerOne, HandLayerOne, 0x13B9, 0, true, false };
    const HandItem sword1Sc{ 0x40000001, HandLayerOne, HandLayerOne, 0x13B9, 0, true, true };
    const HandItem shield{ 0x40000002, HandLayerTwo, HandLayerTwo, 0x1B76, 0x0482, false, false };
    const HandItem shieldSc{ 0x40000002, HandLayerTwo, HandLayerTwo, 0x1B76, 0x0482, false, true };
    const HandItem halberd2H{ 0x40000003, HandLayerTwo, HandLayerTwo, 0x143E, 0, true, false };
    const HandItem halberd2HSc{ 0x40000003, HandLayerTwo, HandLayerTwo, 0x143E, 0, true, true };
    // Custom dual-wield: off-hand weapon has base layer HandLayerOne, equipped on HandLayerTwo
    const HandItem offSwordDW{ 0x40000004, HandLayerTwo, HandLayerOne, 0x0F5E, 0x0022, true, false };

    // Potion plans:
    // 1. Single 2H weapon -> stow 2H, restore 2H
    {
        const HandPlan p = PlanPotionHands(empty, halberd2H);
        assert(p.Count == 1);
        assert(p.StowItem(0).Serial == halberd2H.Serial);
        assert(p.StowItem(0).Layer == HandLayerTwo);
        assert(p.RestoreItem(0).Serial == halberd2H.Serial);
        assert(p.KeptOneHand == 0 && p.KeptTwoHand == 0);
    }
    // 2. 1H + shield -> stow shield only, keep 1H equipped
    {
        const HandPlan p = PlanPotionHands(sword1, shield);
        assert(p.Count == 1);
        assert(p.StowItem(0).Serial == shield.Serial);
        assert(p.StowItem(0).Layer == HandLayerTwo);
        assert(p.KeptOneHand == sword1.Serial && p.KeptTwoHand == 0);
    }
    // 3. Custom dual-wield (1H + 1H on OL_2_HAND) -> stow off-hand, keep main-hand equipped
    {
        const HandPlan p = PlanPotionHands(sword1, offSwordDW);
        assert(p.Count == 1);
        assert(p.StowItem(0).Serial == offSwordDW.Serial);
        assert(p.StowItem(0).Layer == HandLayerTwo);
        assert(p.KeptOneHand == sword1.Serial && p.KeptTwoHand == 0);
    }
    // 4. Single 1H or single shield -> already has a free hand
    {
        assert(PlanPotionHands(sword1, empty).Empty());
        assert(PlanPotionHands(empty, shield).Empty());
        assert(PlanPotionHands(empty, empty).Empty());
    }

    // Spell plans:
    // 1. Single 2H non-SC -> stow 2H; SC -> skip
    {
        const HandPlan p = PlanSpellHands(empty, halberd2H);
        assert(p.Count == 1);
        assert(p.StowItem(0).Serial == halberd2H.Serial);
        assert(p.RestoreItem(0).Serial == halberd2H.Serial);

        const HandPlan sc = PlanSpellHands(empty, halberd2HSc);
        assert(sc.Empty());
        assert(sc.KeptTwoHand == halberd2HSc.Serial);
    }
    // 2. 1H + shield (neither SC) -> stow shield then sword; restore sword (OL_1_HAND) then shield (OL_2_HAND)
    {
        const HandPlan p = PlanSpellHands(sword1, shield);
        assert(p.Count == 2);
        assert(p.StowItem(0).Serial == shield.Serial && p.StowItem(0).Layer == HandLayerTwo);
        assert(p.StowItem(1).Serial == sword1.Serial && p.StowItem(1).Layer == HandLayerOne);
        assert(p.RestoreItem(0).Serial == sword1.Serial && p.RestoreItem(0).Layer == HandLayerOne);
        assert(p.RestoreItem(1).Serial == shield.Serial && p.RestoreItem(1).Layer == HandLayerTwo);
        assert(p.KeptOneHand == 0 && p.KeptTwoHand == 0);
    }
    // 3. Custom dual-wield (neither SC) -> stow off-hand then main-hand; restore main-hand FIRST then off-hand SECOND
    {
        const HandPlan p = PlanSpellHands(sword1, offSwordDW);
        assert(p.Count == 2);
        assert(p.StowItem(0).Serial == offSwordDW.Serial && p.StowItem(0).Layer == HandLayerTwo);
        assert(p.StowItem(1).Serial == sword1.Serial && p.StowItem(1).Layer == HandLayerOne);
        assert(p.RestoreItem(0).Serial == sword1.Serial && p.RestoreItem(0).Layer == HandLayerOne);
        assert(p.RestoreItem(1).Serial == offSwordDW.Serial && p.RestoreItem(1).Layer == HandLayerTwo);
    }
    // 4. Mixed Spell Channeling: SC main-hand + non-SC shield -> stow shield only, keep main-hand
    {
        const HandPlan p = PlanSpellHands(sword1Sc, shield);
        assert(p.Count == 1);
        assert(p.StowItem(0).Serial == shield.Serial);
        assert(p.KeptOneHand == sword1Sc.Serial && p.KeptTwoHand == 0);
    }
    // 5. Mixed Spell Channeling: non-SC main-hand + SC shield -> stow main-hand only, keep shield
    {
        const HandPlan p = PlanSpellHands(sword1, shieldSc);
        assert(p.Count == 1);
        assert(p.StowItem(0).Serial == sword1.Serial);
        assert(p.KeptOneHand == 0 && p.KeptTwoHand == shieldSc.Serial);
    }
    // 6. Both Spell Channeling -> skip completely
    {
        const HandPlan p = PlanSpellHands(sword1Sc, shieldSc);
        assert(p.Empty());
        assert(p.KeptOneHand == sword1Sc.Serial && p.KeptTwoHand == shieldSc.Serial);
    }
}

void test_dual_wield_lifecycle_and_manual_interrupts()
{
    const HandItem mainWep{ 0x40000101, HandLayerOne, HandLayerOne, 0x13B9, 0x0010, true, false };
    const HandItem offWep{ 0x40000102, HandLayerTwo, HandLayerOne, 0x0F5E, 0x0022, true, false };
    const HandPlan plan = PlanSpellHands(mainWep, offWep);
    assert(plan.Count == 2);

    // Initial state: both equipped
    ObservedHands obs{};
    obs.OneHandSerial = mainWep.Serial;
    obs.OneHandGraphic = mainWep.Graphic;
    obs.OneHandHue = mainWep.Hue;
    obs.TwoHandSerial = offWep.Serial;
    obs.TwoHandGraphic = offWep.Graphic;
    obs.TwoHandHue = offWep.Hue;
    assert(VerifyStowState(plan, 0, obs));
    assert(VerifyStowInFlight(plan, 0, obs));

    // In-flight stow[0] (off-hand cleared before pack update arrives)
    obs.TwoHandSerial = 0;
    obs.TwoHandGraphic = 0;
    obs.TwoHandHue = 0;
    assert(!VerifyStowState(plan, 1, obs));
    assert(VerifyStowInFlight(plan, 0, obs));

    // Confirmed stow[0] (off-hand in pack, main-hand still equipped)
    obs.StowedInPack[0] = true;
    assert(VerifyStowState(plan, 1, obs));

    // Manual interference during stow[1]: player equips another weapon into off-hand!
    {
        ObservedHands tampered = obs;
        tampered.TwoHandSerial = 0x40000999;
        tampered.TwoHandGraphic = 0x1B76;
        assert(!VerifyStowState(plan, 1, tampered));
        assert(!VerifyStowInFlight(plan, 1, tampered));
    }

    // Confirmed stow[1] (both hands empty, both items in pack)
    obs.OneHandSerial = 0;
    obs.OneHandGraphic = 0;
    obs.OneHandHue = 0;
    obs.StowedInPack[1] = true;
    assert(VerifyStowState(plan, 2, obs));
    assert(VerifyRestoreState(plan, 0, obs));

    // Manual interference during cast wait: stowed off-hand dropped/removed from backpack!
    {
        ObservedHands tampered = obs;
        tampered.StowedInPack[0] = false;
        assert(!VerifyStowState(plan, 2, tampered));
        assert(!VerifyRestoreState(plan, 0, tampered));
    }

    // In-flight restore[0] (main-hand left backpack, not yet shown on layer 1)
    obs.StowedInPack[1] = false;
    assert(VerifyRestoreInFlight(plan, 0, obs));
    assert(!VerifyRestoreState(plan, 1, obs));

    // Confirmed restore[0] (main-hand back on HandLayerOne, off-hand still in pack)
    obs.OneHandSerial = mainWep.Serial;
    obs.OneHandGraphic = mainWep.Graphic;
    obs.OneHandHue = mainWep.Hue;
    assert(VerifyRestoreState(plan, 1, obs));

    // Manual interference during restore[1]: main-hand swapped to different serial!
    {
        ObservedHands tampered = obs;
        tampered.OneHandSerial = 0x40000777;
        assert(!VerifyRestoreState(plan, 1, tampered));
        assert(!VerifyRestoreInFlight(plan, 1, tampered));
    }

    // In-flight restore[1] (off-hand left backpack, in transit to HandLayerTwo)
    obs.StowedInPack[0] = false;
    assert(VerifyRestoreInFlight(plan, 1, obs));

    // Confirmed restore[1] (both exact saved serials back in exact layers!)
    obs.TwoHandSerial = offWep.Serial;
    obs.TwoHandGraphic = offWep.Graphic;
    obs.TwoHandHue = offWep.Hue;
    assert(VerifyRestoreState(plan, 2, obs));
}

void test_spell_wait_and_packet_interrupts()
{
    // Target response (0x6C, 19 bytes) completes a targeted spell and must NOT abort spell wait.
    uint8_t targetResp[19]{};
    targetResp[0] = 0x6C;
    assert(ActionPacket(targetResp, sizeof(targetResp)));
    assert(TargetResponsePacket(targetResp, sizeof(targetResp)));
    assert(!InterruptsSpellWait(targetResp, sizeof(targetResp)));

    // Manual lift/equip/use/new cast MUST interrupt spell wait.
    const uint8_t lift[] = { 0x07, 0x40, 0, 0, 1, 0, 1 };
    const uint8_t equip[] = { 0x13, 0x40, 0, 0, 1, 1, 0, 0, 0, 1 };
    const uint8_t useItem[] = { 0x06, 0x40, 0, 0, 1 };
    const uint8_t newCast[] = { 0xBF, 0, 9, 0, 0x1C, 0, 2, 0, 4 };
    assert(InterruptsSpellWait(lift, sizeof(lift)));
    assert(InterruptsSpellWait(equip, sizeof(equip)));
    assert(InterruptsSpellWait(useItem, sizeof(useItem)));
    assert(InterruptsSpellWait(newCast, sizeof(newCast)));

    // Fizzle / disturb / refusal clilocs
    assert(SpellFinishedOrFailedCliloc(502632)); // Fizzles
    assert(SpellFinishedOrFailedCliloc(500641)); // Concentration disturbed
    assert(SpellFinishedOrFailedCliloc(502625)); // Insufficient mana
    assert(SpellFinishedOrFailedCliloc(502630)); // More reagents needed
    assert(SpellFinishedOrFailedCliloc(502644)); // Not yet recovered
    assert(!SpellFinishedOrFailedCliloc(502172)); // Potion free hand

    // 1. Immediate fizzle -> RestoreNow
    assert(EvaluateSpellWait(true, false, false, false, false, false, 1500, 1000, 0, 0) ==
           SpellWaitOutcome::RestoreNow);

    // 2. Targeted spell: cursor active -> Pending; cursor timeout -> Abort; cursor ended -> RestoreNow
    assert(EvaluateSpellWait(false, true, true, false, true, false, 2000, 1000, 1500, 1400) ==
           SpellWaitOutcome::Pending);
    assert(EvaluateSpellWait(false, true, true, false, true, false, 1500 + SpellCursorTimeout, 1000, 1500, 1400) ==
           SpellWaitOutcome::Abort);
    assert(EvaluateSpellWait(false, false, true, true, true, false, 2100, 1000, 1500, 1400) ==
           SpellWaitOutcome::RestoreNow);

    // 3. Untargeted spell: post-cast observed waits for SpellPostCastGrace so a targeted cursor cannot race it
    assert(EvaluateSpellWait(false, false, false, false, true, false, 1700, 1000, 0, 1500) ==
           SpellWaitOutcome::Pending);
    assert(EvaluateSpellWait(false, false, false, false, true, false, 1500 + SpellPostCastGrace, 1000, 0, 1500) ==
           SpellWaitOutcome::RestoreNow);
    // Still cast-frozen -> stay Pending even after grace
    assert(EvaluateSpellWait(false, false, false, false, true, true, 1500 + SpellPostCastGrace + 100, 1000, 0, 1500) ==
           SpellWaitOutcome::Pending);

    // 4. Cast timeout fallback -> RestoreNow (handles tick wraparound)
    assert(EvaluateSpellWait(false, false, false, false, false, false, 1000 + SpellCastTimeout - 1, 1000, 0, 0) ==
           SpellWaitOutcome::Pending);
    assert(EvaluateSpellWait(false, false, false, false, false, false, 1000 + SpellCastTimeout, 1000, 0, 0) ==
           SpellWaitOutcome::RestoreNow);
    assert(EvaluateSpellWait(false, false, false, false, false, false, 500, UINT32_MAX - (SpellCastTimeout - 501), 0, 0) ==
           SpellWaitOutcome::RestoreNow);
}

void test_trade_filter_rules()
{
    // DecideTradeWatch:
    // Filter disabled -> Ignore
    assert(DecideTradeWatch(false, false, false, false) == TradeWatchDecision::Ignore);
    // Initiated by us -> Ignore
    assert(DecideTradeWatch(true, false, true, false) == TradeWatchDecision::Ignore);
    // Unsolicited non-friend -> WatchFirstOffer
    assert(DecideTradeWatch(true, false, false, false) == TradeWatchDecision::WatchFirstOffer);
    assert(DecideTradeWatch(true, true, false, false) == TradeWatchDecision::WatchFirstOffer);
    // Unsolicited friend: allowed only when allowFriends is enabled
    assert(DecideTradeWatch(true, true, false, true) == TradeWatchDecision::Ignore);
    assert(DecideTradeWatch(true, false, false, true) == TradeWatchDecision::WatchFirstOffer);

    // DecideFirstOffer:
    // Unsolicited item in opponent's box within opening window -> Decline
    assert(DecideFirstOffer(true, true, true, false, 0x0EED, false, 1200, 1000) ==
           FirstOfferDecision::Decline);
    // Item in our own box -> Allow (stops watching)
    assert(DecideFirstOffer(true, true, false, true, 0x0EED, false, 1200, 1000) ==
           FirstOfferDecision::Allow);
    // Empty trade window: item offered only after opening window elapsed -> Allow
    assert(DecideFirstOffer(true, true, true, false, 0x0EED, false, 1000 + FirstOfferWindow, 1000) ==
           FirstOfferDecision::Allow);
    // Tick wraparound inside opening window -> Decline
    assert(DecideFirstOffer(true, true, true, false, 0x0EED, false, 200, UINT32_MAX - 100) ==
           FirstOfferDecision::Decline);
    // Legacy virtual currency placeholder (0x14F0) on pre-7.0.45.65 -> KeepWatching
    assert(DecideFirstOffer(true, true, true, false, VirtualCurrencyCheck, true, 1100, 1000) ==
           FirstOfferDecision::KeepWatching);
    // Real bank check (0x14F0) on modern protocol (>= 7.0.45.65) in opponent box within window -> Decline
    assert(DecideFirstOffer(true, true, true, false, VirtualCurrencyCheck, false, 1100, 1000) ==
           FirstOfferDecision::Decline);
    // Unrelated container -> KeepWatching
    assert(DecideFirstOffer(true, true, false, false, 0x0EED, false, 1100, 1000) ==
           FirstOfferDecision::KeepWatching);
    // Not watching or filter off -> Allow
    assert(DecideFirstOffer(false, true, true, false, 0x0EED, false, 1100, 1000) ==
           FirstOfferDecision::Allow);
    assert(DecideFirstOffer(true, false, true, false, 0x0EED, false, 1100, 1000) ==
           FirstOfferDecision::Allow);
}

} // namespace

int main()
{
    test_baseline_potion_rules();
    test_spell_channeling_detection();
    test_dual_wield_and_hand_plans();
    test_dual_wield_lifecycle_and_manual_interrupts();
    test_spell_wait_and_packet_interrupts();
    test_trade_filter_rules();
    return 0;
}
