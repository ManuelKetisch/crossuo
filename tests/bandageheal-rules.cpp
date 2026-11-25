// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Assistant/core/bandageheal_rules.h"
#include <cassert>
#include <limits>

int main()
{
    using namespace xuoassist;

    constexpr int Limit = 85;
    assert(BandageNeeded(51, 60, Limit, false, false));
    assert(!BandageNeeded(52, 60, Limit, false, false));
    assert(!BandageNeeded(100, 100, 100, false, true));
    assert(!BandageNeeded(2, 3, Limit, true, false));
    assert(BandageNeeded(100, 100, Limit, true, true));
    assert(!BandageNeeded(0, 100, Limit, true, true));
    assert(!BandageNeeded(1, 0, Limit, false, true));

    assert(BandageEstimatedInterval(120, true, false) == 5250);
    assert(BandageEstimatedInterval(70, true, false) == 7750);
    assert(BandageEstimatedInterval(120, false, true) == 2250);
    assert(BandageEstimatedInterval(204, false, false) == BandageActionInterval);
    assert(BandageEstimatedInterval(4096, true, false) == BandageActionInterval);

    BandageTimer timer;
    const auto nearWrap = std::numeric_limits<uint32_t>::max() - 9;
    timer.Extend(nearWrap, BandageCastQuietPeriod);
    assert(timer.Remaining(5) == BandageCastQuietPeriod - 15);
    timer.Extend(5, BandageActionInterval);
    assert(timer.Remaining(5) == BandageCastQuietPeriod - 15);
    assert(timer.Remaining(5990) == 0);
    timer.Extend(5990, BandageActionInterval);
    assert(timer.Remaining(6000) == BandageActionInterval - 10);

    const uint8_t colorAndTarget[] = { 0xBF, 0, 13, 0, 0x2C, 0x40, 0, 0, 1, 0, 0, 0, 2 };
    const uint8_t castFromBook[] = { 0xBF, 0, 9, 0, 0x1C, 0, 2, 0, 17 };
    const uint8_t oldCast[] = { 0x12, 0, 5, 0x27, 0 };
    assert(BandageTargetedUse(colorAndTarget, sizeof(colorAndTarget)));
    assert(!BandageTargetedUse(colorAndTarget, 5));
    assert(!BandageTargetedUse(castFromBook, sizeof(castFromBook)));
    assert(BandageCompetingAction(colorAndTarget, sizeof(colorAndTarget)) == BandageActionInterval);
    assert(BandageCompetingAction(castFromBook, sizeof(castFromBook)) == BandageCastQuietPeriod);
    assert(BandageCompetingAction(oldCast, sizeof(oldCast)) == BandageCastQuietPeriod);
    assert(BandageCompetingAction(oldCast, 3) == 0);
    assert(BandageCompetingAction(nullptr, 0) == 0);

    // LowestPartyOrFriend patient priority rules.
    const BandagePatientCandidate selfHurt{ 1, 70, 100, 0, true, false };
    const BandagePatientCandidate friendLower{ 2, 40, 100, 2, false, false };
    const BandagePatientCandidate friendPoisoned{ 3, 95, 100, 2, false, true };
    const BandagePatientCandidate friendSamePctFarther{ 4, 35, 50, 2, false, false };
    const BandagePatientCandidate friendSamePctCloser{ 5, 35, 50, 1, false, false };
    const BandagePatientCandidate friendSamePctHigherSerial{ 9, 35, 50, 1, false, false };

    // Lowest HP% wins when neither is urgent poison.
    assert(BandagePatientBetter(friendLower, selfHurt, true));
    assert(!BandagePatientBetter(selfHurt, friendLower, true));

    // Poisoned patient wins when TreatPoison is enabled, even at higher HP%.
    assert(BandagePatientBetter(friendPoisoned, friendLower, true));
    assert(!BandagePatientBetter(friendLower, friendPoisoned, true));
    // When TreatPoison is disabled, lowest HP% still wins.
    assert(BandagePatientBetter(friendLower, friendPoisoned, false));

    // Exact percentage tie (70/100 == 35/50): self beats friend, closer friend beats farther,
    // and lower serial provides deterministic tie-breaking.
    assert(BandagePatientBetter(selfHurt, friendSamePctCloser, true));
    assert(!BandagePatientBetter(friendSamePctCloser, selfHurt, true));
    assert(BandagePatientBetter(friendSamePctCloser, friendSamePctFarther, true));
    assert(BandagePatientBetter(friendSamePctCloser, friendSamePctHigherSerial, true));
    assert(!BandagePatientBetter(friendSamePctHigherSerial, friendSamePctCloser, true));
}
