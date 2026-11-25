// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace xuoassist {

constexpr uint32_t BandageActionInterval = 1200;
constexpr uint32_t BandageCastQuietPeriod = 6000;

struct BandageTimer
{
    uint32_t Start = 0;
    uint32_t Duration = 0;

    uint32_t Remaining(uint32_t now) const
    {
        const auto elapsed = now - Start;
        return elapsed < Duration ? Duration - elapsed : 0;
    }

    void Extend(uint32_t now, uint32_t duration)
    {
        if (duration > Remaining(now))
        {
            Start = now;
            Duration = duration;
        }
    }
};

enum class BandagePatientMode : int
{
    Self = 0,
    SelectedFriend = 1,
    LowestPartyOrFriend = 2,
};

struct BandagePatientCandidate
{
    uint32_t Serial = 0;
    int Hits = 0;
    int MaxHits = 0;
    int Distance = 0;
    bool IsSelf = false;
    bool Poisoned = false;
};

inline bool BandageNeeded(int hits, int maxHits, int threshold, bool poisoned, bool treatPoison)
{
    return maxHits > 0 && hits > 0 && (!poisoned || treatPoison) &&
        (poisoned || (hits < maxHits && hits * 100 <= maxHits * threshold));
}

// Returns true when candidate `a` should be healed before candidate `b`
// (both assumed eligible via BandageNeeded).
inline bool BandagePatientBetter(
    const BandagePatientCandidate &a, const BandagePatientCandidate &b, bool treatPoison)
{
    const bool aUrgentPoison = treatPoison && a.Poisoned;
    const bool bUrgentPoison = treatPoison && b.Poisoned;
    if (aUrgentPoison != bUrgentPoison)
    {
        return aUrgentPoison;
    }

    const int64_t aCross = static_cast<int64_t>(a.Hits) * static_cast<int64_t>(b.MaxHits);
    const int64_t bCross = static_cast<int64_t>(b.Hits) * static_cast<int64_t>(a.MaxHits);
    if (aCross != bCross)
    {
        return aCross < bCross;
    }
    if (a.IsSelf != b.IsSelf)
    {
        return a.IsSelf;
    }
    if (a.Distance != b.Distance)
    {
        return a.Distance < b.Distance;
    }
    return a.Serial < b.Serial;
}

inline uint32_t BandageEstimatedInterval(int dex, bool self, bool creature)
{
    dex = std::max(0, dex);
    double seconds = 0.7;
    if (self)
    {
        seconds = 5.0 + 0.05 * (120 - dex);
    }
    else if (creature)
    {
        seconds = 2.0;
    }
    else if (dex < 204)
    {
        seconds = 3.2 - std::sin(dex / 130.0) * 2.5;
    }
    return static_cast<uint32_t>(std::max(seconds * 1000.0 + 250.0, double(BandageActionInterval)));
}

inline bool BandageTargetedUse(const uint8_t *data, std::size_t size)
{
    return data != nullptr && size == 13 && data[0] == 0xBF && data[1] == 0 &&
        data[2] == 13 && data[3] == 0 && data[4] == 0x2C;
}

inline uint32_t BandageCompetingAction(const uint8_t *data, std::size_t size)
{
    if (data == nullptr || size == 0)
    {
        return 0;
    }
    switch (data[0])
    {
        case 0x06: // use
        case 0x07: // lift
        case 0x08: // drop
        case 0x13: // equip
        case 0x6C: // target response
            return BandageActionInterval;
        case 0x12: // skill, spell, or door command
            if (size > 3)
            {
                switch (data[3])
                {
                    case 0x24: // use skill
                    case 0x27: // cast from spellbook
                    case 0x56: // cast from macro
                        return BandageCastQuietPeriod;
                    case 0x58: // open door
                        return BandageActionInterval;
                }
            }
            break;
        case 0xBF:
            if (size > 4 && data[3] == 0)
            {
                switch (data[4])
                {
                    case 0x1C: // cast spell
                    case 0x2D: // targeted spell
                    case 0x2E: // targeted skill
                        return BandageCastQuietPeriod;
                    case 0x2C: // targeted bandage
                    case 0x30: // resource macro
                        return BandageActionInterval;
                }
            }
            break;
    }
    return 0;
}

} // namespace xuoassist
