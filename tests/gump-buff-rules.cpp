// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Gumps/GumpBuff_rules.h"
#include <cassert>
#include <cmath>

int main()
{
    using namespace xuobuff;

    // 1. Indefinite vs timed detection and wrap-safe remaining milliseconds.
    assert(!HasTimer(kBuffIndefiniteTimer));
    assert(HasTimer(1000u));
    assert(ComputeRemainingMs(kBuffIndefiniteTimer, 500u) == 0u);
    assert(ComputeRemainingMs(15000u, 5000u) == 10000u);
    assert(ComputeRemainingMs(5000u, 5000u) == 0u);
    assert(ComputeRemainingMs(4000u, 5000u) == 0u);
    // Unsigned 32-bit wrap-around across 0xFFFFFFFF -> 0.
    assert(ComputeRemainingMs(100u, 0xFFFFFF9Cu) == 200u);

    // 2. Ceiling seconds calculation: 20m00s starts at 1200s, hits 0 only when expired.
    assert(RemainingSecondsCeil(0u) == 0u);
    assert(RemainingSecondsCeil(1u) == 1u);
    assert(RemainingSecondsCeil(999u) == 1u);
    assert(RemainingSecondsCeil(1000u) == 1u);
    assert(RemainingSecondsCeil(1001u) == 2u);
    assert(RemainingSecondsCeil(1200000u) == 1200u);

    // 3. WoW-style on-icon exact timer formatting.
    assert(FormatBarTimer(0u, false) == "ON");
    assert(FormatBarTimer(0u, true) == "0s");
    assert(FormatBarTimer(9u, true) == "9s");
    assert(FormatBarTimer(59u, true) == "59s");
    assert(FormatBarTimer(60u, true) == "1:00");
    assert(FormatBarTimer(65u, true) == "1:05");
    assert(FormatBarTimer(1458u, true) == "24:18");
    assert(FormatBarTimer(3599u, true) == "59:59");
    assert(FormatBarTimer(3600u, true) == "1:00:00");
    assert(FormatBarTimer(4230u, true) == "1:10:30");

    // 4. Tooltip duration strings.
    assert(FormatTooltipDuration(0u, false) == L"Duration: Active until canceled");
    assert(FormatTooltipDuration(45u, true) == L"Time remaining: 45s");
    assert(FormatTooltipDuration(1458u, true) == L"Time remaining: 24m 18s");
    assert(FormatTooltipDuration(4230u, true) == L"Time remaining: 1h 10m 30s");

    // 5. Urgency tiers.
    assert(ClassifyUrgency(0u, false) == BuffUrgency::Indefinite);
    assert(ClassifyUrgency(61u, true) == BuffUrgency::Normal);
    assert(ClassifyUrgency(60u, true) == BuffUrgency::Warning);
    assert(ClassifyUrgency(16u, true) == BuffUrgency::Warning);
    assert(ClassifyUrgency(15u, true) == BuffUrgency::Critical);
    assert(ClassifyUrgency(0u, true) == BuffUrgency::Critical);

    // 6. Progress bar fraction.
    assert(std::fabs(ComputeProgressFraction(0u, 0u, false) - 1.0f) < 1e-5f);
    assert(std::fabs(ComputeProgressFraction(600000u, 1200000u, true) - 0.5f) < 1e-5f);
    assert(std::fabs(ComputeProgressFraction(1500000u, 1200000u, true) - 1.0f) < 1e-5f);
    assert(std::fabs(ComputeProgressFraction(0u, 1200000u, true) - 0.0f) < 1e-5f);

    // 7. Tooltip HTML assembly (no broken POSIX swprintf %s, inserts inside <left>...</left>).
    {
        const std::wstring raw = L"<left>Strength\nIncreases Strength by 20%.</left>";
        const std::wstring tip = BuildTooltipText(raw, 1458u, true);
        assert(tip.find(L"<left>Strength\nIncreases Strength by 20%.") == 0);
        assert(tip.find(L"Time remaining: 24m 18s") != std::wstring::npos);
        assert(tip.find(L"#8CE99A") != std::wstring::npos);
        assert(tip.rfind(L"</left>") == tip.size() - 7);
    }
    {
        const std::wstring tipWarn = BuildTooltipText(L"Agility\n+20 DEX", 42u, true);
        assert(tipWarn.find(L"#FFD166") != std::wstring::npos);
        assert(tipWarn.find(L"Time remaining: 42s") != std::wstring::npos);
    }
    {
        const std::wstring tipCrit = BuildTooltipText(L"<left>Night Sight</left>", 10u, true);
        assert(tipCrit.find(L"#FF6B6B") != std::wstring::npos);
        assert(tipCrit.find(L"Time remaining: 10s") != std::wstring::npos);
    }
    {
        const std::wstring tipIndef = BuildTooltipText(L"<left>Protection</left>", 0u, false);
        assert(tipIndef.find(L"#A5D8FF") != std::wstring::npos);
        assert(tipIndef.find(L"Duration: Active until canceled") != std::wstring::npos);
    }
    {
        const std::wstring tipEmpty = BuildTooltipText(L"", 30u, true);
        assert(tipEmpty.find(L"Active buff") != std::wstring::npos);
        assert(tipEmpty.find(L"Time remaining: 30s") != std::wstring::npos);
    }

    // 8. Slot geometry ensures room for timer pill & duration bar beneath 28x28 icons.
    {
        const BuffSlotSize slot = ComputeSlotSize(28, 28);
        assert(slot.Width >= 44);
        assert(slot.Height >= 46);
    }

    return 0;
}
