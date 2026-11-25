// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace xuoassist {
namespace potionhands {

constexpr uint32_t FreeHandMessage = 502172;
constexpr uint32_t SpellChannelingCliloc = 1060482;
constexpr uint32_t MinActionDelay = 1200;
constexpr uint32_t InitialReplyTimeout = 2200;
constexpr uint32_t MoveReplyTimeout = 4500;
constexpr uint32_t DrinkReplyTimeout = 3500;
constexpr uint32_t SpellCastTimeout = 6000;
constexpr uint32_t SpellCursorTimeout = 15000;
constexpr uint32_t SpellPostCastGrace = 350;

constexpr uint8_t HandLayerOne = 1; // OL_1_HAND
constexpr uint8_t HandLayerTwo = 2; // OL_2_HAND

inline bool StandardDrink(uint16_t graphic, uint16_t hue)
{
    // On ModernUO the undyed graphics below are untargeted drinks. Throwables
    // sharing the night-sight graphic use distinct nonzero hues.
    if (hue != 0)
    {
        return false;
    }
    switch (graphic)
    {
        case 0x0F06: // Night Sight
        case 0x0F07: // Cure
        case 0x0F08: // Agility
        case 0x0F09: // Strength
        case 0x0F0B: // Refresh
        case 0x0F0C: // Heal
            return true;
        default:
            return false;
    }
}

inline bool IsSpellbookGraphic(uint16_t graphic)
{
    switch (graphic)
    {
        case 0x0EFA: // Magery spellbook
        case 0x2252: // Book of Chivalry
        case 0x2253: // Necromancer spellbook
        case 0x225A: // Book of Masteries
        case 0x238C: // Book of Bushido
        case 0x23A0: // Book of Ninjitsu
        case 0x2D50: // Spellweaving spellbook
        case 0x2D9D: // Mysticism spellbook
        case 0x0E3B: // Runebook (alt)
        case 0x22C5: // Runebook
            return true;
        default:
            return false;
    }
}

inline bool ActionPacket(const uint8_t *data, size_t size)
{
    if (data == nullptr || size == 0)
    {
        return false;
    }
    switch (data[0])
    {
        case 0x06: // Use
        case 0x07: // Lift
        case 0x08: // Drop
        case 0x12: // Spell or skill action
        case 0x13: // Equip
        case 0x6C: // Target response
        case 0xEC: // Equip macro
        case 0xED: // Unequip macro
            return true;
        case 0xBF:
            return size >= 5 && data[3] == 0 &&
                (data[4] == 0x1C || // Extended cast spell
                 data[4] == 0x2C || // Use object on target
                 data[4] == 0x2D);  // Target cast spell
        default:
            return false;
    }
}

inline bool TargetResponsePacket(const uint8_t *data, size_t size)
{
    return data != nullptr && size == 19 && data[0] == 0x6C;
}

inline bool InterruptsSpellWait(const uint8_t *data, size_t size)
{
    return ActionPacket(data, size) && !TargetResponsePacket(data, size);
}

inline uint32_t ItemUseSerial(const uint8_t *data, size_t size)
{
    if (data == nullptr || size != 5 || data[0] != 0x06)
    {
        return 0;
    }
    const uint32_t serial = (uint32_t(data[1]) << 24) | (uint32_t(data[2]) << 16) |
        (uint32_t(data[3]) << 8) | data[4];
    return (serial & 0xC0000000) == 0x40000000 ? serial : 0;
}

inline bool DeadlineReached(uint32_t now, uint32_t started, uint32_t period)
{
    return now - started >= period;
}

enum class DrinkResult { Pending, Consumed, Changed };

inline DrinkResult ObserveDrink(bool parentAccessible, bool itemExists, bool sameItemAndParent,
    uint32_t previousCount, uint32_t currentCount)
{
    if (!parentAccessible || previousCount == 0)
    {
        return DrinkResult::Changed;
    }
    if (!itemExists)
    {
        return previousCount == 1 ? DrinkResult::Consumed : DrinkResult::Changed;
    }
    if (!sameItemAndParent || currentCount == 0 || currentCount > previousCount)
    {
        return DrinkResult::Changed;
    }
    if (currentCount == previousCount)
    {
        return DrinkResult::Pending;
    }
    return previousCount - currentCount == 1 ? DrinkResult::Consumed : DrinkResult::Changed;
}

inline bool DrinkRefused(uint32_t message)
{
    switch (message)
    {
        case FreeHandMessage:
        case 502138:  // Out of range
        case 502173:  // Similar potion effect already active
        case 500119:  // Action delay
        case 500235:  // Heal cooldown
        case 1005000: // Poison or Mortal Strike prevents healing
        case 1049547: // Full health
        case 1042000: // No poison to cure
        case 1061652: // Vampire form prevents Cure
            return true;
        default:
            return false;
    }
}

inline bool SpellFinishedOrFailedCliloc(uint32_t message)
{
    switch (message)
    {
        case 500641:  // Your concentration is disturbed, thus ruining thy spell
        case 502625:  // Insufficient mana for this spell
        case 502626:  // Your hands must be free to cast spells
        case 502630:  // More reagents are needed for this spell
        case 502632:  // The spell fizzles
        case 502642:  // You are already casting a spell
        case 502643:  // You cannot cast a spell while frozen
        case 502644:  // You have not yet recovered from casting a spell
        case 502645:  // You cannot concentrate on that spell
        case 502646:  // You cannot cast a spell while frozen (sequence)
        case 1061091: // You cannot cast that spell in this form
        case 1072060: // You cannot cast a spell while calmed
            return true;
        default:
            return false;
    }
}

inline bool HasSpellChannelingCliloc(const uint32_t *clilocs, size_t count)
{
    if (clilocs == nullptr)
    {
        return false;
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (clilocs[i] == SpellChannelingCliloc)
        {
            return true;
        }
    }
    return false;
}

inline bool EqualsIgnoreAsciiCase(const char *a, size_t aLen, const char *b)
{
    if (b == nullptr)
    {
        return false;
    }
    const size_t bLen = std::strlen(b);
    if (aLen != bLen)
    {
        return false;
    }
    for (size_t i = 0; i < aLen; ++i)
    {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
        {
            return false;
        }
    }
    return true;
}

inline bool HasSpellChannelingText(const char *oplData, const char *localizedLine = nullptr)
{
    if (oplData == nullptr || oplData[0] == '\0')
    {
        return false;
    }
    const bool hasLocalized = localizedLine != nullptr && localizedLine[0] != '\0';
    const char *p = oplData;
    while (*p != '\0')
    {
        const char *lineStart = p;
        while (*p != '\0' && *p != '\n' && *p != '\r')
        {
            ++p;
        }
        const char *lineEnd = p;
        while (lineStart < lineEnd && (*lineStart == ' ' || *lineStart == '\t'))
        {
            ++lineStart;
        }
        while (lineEnd > lineStart && (lineEnd[-1] == ' ' || lineEnd[-1] == '\t'))
        {
            --lineEnd;
        }
        const size_t len = static_cast<size_t>(lineEnd - lineStart);
        if (len > 0)
        {
            if (EqualsIgnoreAsciiCase(lineStart, len, "spell channeling") ||
                (hasLocalized && EqualsIgnoreAsciiCase(lineStart, len, localizedLine)))
            {
                return true;
            }
        }
        while (*p == '\n' || *p == '\r')
        {
            ++p;
        }
    }
    return false;
}

struct HandItem
{
    uint32_t Serial = 0;
    uint8_t Layer = 0;     // Equipped layer: HandLayerOne (1) or HandLayerTwo (2)
    uint8_t BaseLayer = 0; // Tiledata static layer
    uint16_t Graphic = 0;
    uint16_t Hue = 0;
    bool IsWeapon = false;
    bool SpellChanneling = false;
};

struct HandPlan
{
    // Items to stow, in stow order (off-hand first, then main-hand).
    // Restore runs in reverse order (main-hand first, then off-hand), so
    // 1H+shield and custom dual-wield layouts always re-equip in exact layer order.
    HandItem Stow[2]{};
    size_t Count = 0;
    uint32_t KeptOneHand = 0;
    uint32_t KeptTwoHand = 0;

    bool Empty() const { return Count == 0; }
    const HandItem &StowItem(size_t index) const { return Stow[index]; }
    const HandItem &RestoreItem(size_t index) const { return Stow[Count - 1 - index]; }
    size_t RestoreStowIndex(size_t restoreIndex) const { return Count - 1 - restoreIndex; }
};

inline HandPlan PlanPotionHands(const HandItem &oneHand, const HandItem &twoHand)
{
    HandPlan plan;
    if (oneHand.Serial != 0 && twoHand.Serial != 0)
    {
        // Both hands occupied (1H+shield or custom dual-wield): stowing the
        // off-hand frees a hand while keeping the main-hand weapon in place so
        // restoring the off-hand lands back in HandLayerTwo.
        plan.Stow[0] = twoHand;
        plan.Stow[0].Layer = HandLayerTwo;
        plan.Count = 1;
        plan.KeptOneHand = oneHand.Serial;
        return plan;
    }
    if (oneHand.Serial == 0 && twoHand.Serial != 0 && twoHand.IsWeapon)
    {
        // Single two-handed weapon.
        plan.Stow[0] = twoHand;
        plan.Stow[0].Layer = HandLayerTwo;
        plan.Count = 1;
        return plan;
    }
    if (oneHand.Serial != 0 && twoHand.Serial == 0 && oneHand.BaseLayer == HandLayerTwo)
    {
        // Custom two-handed item equipped on the main-hand layer.
        plan.Stow[0] = oneHand;
        plan.Stow[0].Layer = HandLayerOne;
        plan.Count = 1;
        return plan;
    }
    return plan;
}

inline HandPlan PlanSpellHands(const HandItem &oneHand, const HandItem &twoHand)
{
    HandPlan plan;
    // Stow off-hand first, then main-hand, so restore equips main-hand first
    // and off-hand second (preserving exact layers on custom dual-wield shards).
    if (twoHand.Serial != 0)
    {
        if (twoHand.SpellChanneling)
        {
            plan.KeptTwoHand = twoHand.Serial;
        }
        else
        {
            plan.Stow[plan.Count] = twoHand;
            plan.Stow[plan.Count].Layer = HandLayerTwo;
            ++plan.Count;
        }
    }
    if (oneHand.Serial != 0)
    {
        if (oneHand.SpellChanneling)
        {
            plan.KeptOneHand = oneHand.Serial;
        }
        else
        {
            plan.Stow[plan.Count] = oneHand;
            plan.Stow[plan.Count].Layer = HandLayerOne;
            ++plan.Count;
        }
    }
    return plan;
}

struct ObservedHands
{
    uint32_t OneHandSerial = 0;
    uint16_t OneHandGraphic = 0;
    uint16_t OneHandHue = 0;
    uint32_t TwoHandSerial = 0;
    uint16_t TwoHandGraphic = 0;
    uint16_t TwoHandHue = 0;
    bool StowedInPack[2] = { false, false }; // Indexed by HandPlan::Stow[i]
};

inline bool HandMatches(const HandItem &expected, uint32_t serial, uint16_t graphic, uint16_t hue)
{
    return serial == expected.Serial && graphic == expected.Graphic && hue == expected.Hue;
}

inline bool ObservedLayerMatches(const HandItem &expected, const ObservedHands &obs)
{
    if (expected.Layer == HandLayerOne)
    {
        return HandMatches(expected, obs.OneHandSerial, obs.OneHandGraphic, obs.OneHandHue);
    }
    if (expected.Layer == HandLayerTwo)
    {
        return HandMatches(expected, obs.TwoHandSerial, obs.TwoHandGraphic, obs.TwoHandHue);
    }
    return false;
}

inline uint32_t ObservedLayerSerial(uint8_t layer, const ObservedHands &obs)
{
    if (layer == HandLayerOne)
    {
        return obs.OneHandSerial;
    }
    if (layer == HandLayerTwo)
    {
        return obs.TwoHandSerial;
    }
    return 0;
}

inline bool VerifyStowState(const HandPlan &plan, size_t stowedCount, const ObservedHands &obs)
{
    if (stowedCount > plan.Count)
    {
        return false;
    }
    bool oneAccounted = false;
    bool twoAccounted = false;
    if (plan.KeptOneHand != 0)
    {
        if (obs.OneHandSerial != plan.KeptOneHand)
        {
            return false;
        }
        oneAccounted = true;
    }
    if (plan.KeptTwoHand != 0)
    {
        if (obs.TwoHandSerial != plan.KeptTwoHand)
        {
            return false;
        }
        twoAccounted = true;
    }

    for (size_t i = 0; i < plan.Count; ++i)
    {
        const HandItem &item = plan.Stow[i];
        if (i < stowedCount)
        {
            if (!obs.StowedInPack[i] || ObservedLayerSerial(item.Layer, obs) != 0)
            {
                return false;
            }
        }
        else
        {
            if (!ObservedLayerMatches(item, obs))
            {
                return false;
            }
            if (item.Layer == HandLayerOne)
            {
                oneAccounted = true;
            }
            else if (item.Layer == HandLayerTwo)
            {
                twoAccounted = true;
            }
        }
    }

    if (!oneAccounted && obs.OneHandSerial != 0)
    {
        return false;
    }
    if (!twoAccounted && obs.TwoHandSerial != 0)
    {
        return false;
    }
    return true;
}

inline bool VerifyStowInFlight(const HandPlan &plan, size_t activeIndex, const ObservedHands &obs)
{
    if (activeIndex >= plan.Count)
    {
        return false;
    }
    bool oneAccounted = false;
    bool twoAccounted = false;
    if (plan.KeptOneHand != 0)
    {
        if (obs.OneHandSerial != plan.KeptOneHand)
        {
            return false;
        }
        oneAccounted = true;
    }
    if (plan.KeptTwoHand != 0)
    {
        if (obs.TwoHandSerial != plan.KeptTwoHand)
        {
            return false;
        }
        twoAccounted = true;
    }

    for (size_t i = 0; i < plan.Count; ++i)
    {
        const HandItem &item = plan.Stow[i];
        if (i < activeIndex)
        {
            if (!obs.StowedInPack[i] || ObservedLayerSerial(item.Layer, obs) != 0)
            {
                return false;
            }
        }
        else if (i == activeIndex)
        {
            // While lift/drop is in flight, the active item's layer may still
            // hold that same item or already be empty, but never a different item.
            const uint32_t current = ObservedLayerSerial(item.Layer, obs);
            if (current != 0 && !ObservedLayerMatches(item, obs))
            {
                return false;
            }
            if (item.Layer == HandLayerOne)
            {
                oneAccounted = true;
            }
            else if (item.Layer == HandLayerTwo)
            {
                twoAccounted = true;
            }
        }
        else
        {
            if (!ObservedLayerMatches(item, obs))
            {
                return false;
            }
            if (item.Layer == HandLayerOne)
            {
                oneAccounted = true;
            }
            else if (item.Layer == HandLayerTwo)
            {
                twoAccounted = true;
            }
        }
    }

    if (!oneAccounted && obs.OneHandSerial != 0)
    {
        return false;
    }
    if (!twoAccounted && obs.TwoHandSerial != 0)
    {
        return false;
    }
    return true;
}

inline bool VerifyRestoreState(const HandPlan &plan, size_t restoredCount, const ObservedHands &obs)
{
    if (restoredCount > plan.Count)
    {
        return false;
    }
    bool oneAccounted = false;
    bool twoAccounted = false;
    if (plan.KeptOneHand != 0)
    {
        if (obs.OneHandSerial != plan.KeptOneHand)
        {
            return false;
        }
        oneAccounted = true;
    }
    if (plan.KeptTwoHand != 0)
    {
        if (obs.TwoHandSerial != plan.KeptTwoHand)
        {
            return false;
        }
        twoAccounted = true;
    }

    for (size_t r = 0; r < plan.Count; ++r)
    {
        const size_t stowIdx = plan.RestoreStowIndex(r);
        const HandItem &item = plan.Stow[stowIdx];
        if (r < restoredCount)
        {
            if (!ObservedLayerMatches(item, obs))
            {
                return false;
            }
            if (item.Layer == HandLayerOne)
            {
                oneAccounted = true;
            }
            else if (item.Layer == HandLayerTwo)
            {
                twoAccounted = true;
            }
        }
        else
        {
            if (!obs.StowedInPack[stowIdx] || ObservedLayerSerial(item.Layer, obs) != 0)
            {
                return false;
            }
        }
    }

    if (!oneAccounted && obs.OneHandSerial != 0)
    {
        return false;
    }
    if (!twoAccounted && obs.TwoHandSerial != 0)
    {
        return false;
    }
    return true;
}

inline bool VerifyRestoreInFlight(const HandPlan &plan, size_t activeRestoreIndex, const ObservedHands &obs)
{
    if (activeRestoreIndex >= plan.Count)
    {
        return false;
    }
    bool oneAccounted = false;
    bool twoAccounted = false;
    if (plan.KeptOneHand != 0)
    {
        if (obs.OneHandSerial != plan.KeptOneHand)
        {
            return false;
        }
        oneAccounted = true;
    }
    if (plan.KeptTwoHand != 0)
    {
        if (obs.TwoHandSerial != plan.KeptTwoHand)
        {
            return false;
        }
        twoAccounted = true;
    }

    for (size_t r = 0; r < plan.Count; ++r)
    {
        const size_t stowIdx = plan.RestoreStowIndex(r);
        const HandItem &item = plan.Stow[stowIdx];
        if (r < activeRestoreIndex)
        {
            if (!ObservedLayerMatches(item, obs))
            {
                return false;
            }
            if (item.Layer == HandLayerOne)
            {
                oneAccounted = true;
            }
            else if (item.Layer == HandLayerTwo)
            {
                twoAccounted = true;
            }
        }
        else if (r == activeRestoreIndex)
        {
            // While lift/equip is in flight, the target layer may still be empty
            // or already hold the restored item, but never a different item.
            const uint32_t current = ObservedLayerSerial(item.Layer, obs);
            if (current != 0 && !ObservedLayerMatches(item, obs))
            {
                return false;
            }
            if (item.Layer == HandLayerOne)
            {
                oneAccounted = true;
            }
            else if (item.Layer == HandLayerTwo)
            {
                twoAccounted = true;
            }
        }
        else
        {
            if (!obs.StowedInPack[stowIdx] || ObservedLayerSerial(item.Layer, obs) != 0)
            {
                return false;
            }
        }
    }

    if (!oneAccounted && obs.OneHandSerial != 0)
    {
        return false;
    }
    if (!twoAccounted && obs.TwoHandSerial != 0)
    {
        return false;
    }
    return true;
}

enum class SpellWaitOutcome
{
    Pending,
    RestoreNow,
    Abort
};

inline SpellWaitOutcome EvaluateSpellWait(
    bool fizzledOrRefused,
    bool cursorActive,
    bool sawCursor,
    bool cursorEnded,
    bool postCastObserved,
    bool castFrozenNow,
    uint32_t now,
    uint32_t castSentAt,
    uint32_t cursorSince,
    uint32_t postCastSince)
{
    if (fizzledOrRefused && !cursorActive)
    {
        return SpellWaitOutcome::RestoreNow;
    }
    if (cursorEnded)
    {
        return SpellWaitOutcome::RestoreNow;
    }
    if (cursorActive)
    {
        if (DeadlineReached(now, cursorSince, SpellCursorTimeout))
        {
            return SpellWaitOutcome::Abort;
        }
        return SpellWaitOutcome::Pending;
    }
    if (sawCursor)
    {
        return SpellWaitOutcome::RestoreNow;
    }
    if (postCastObserved && !castFrozenNow &&
        DeadlineReached(now, postCastSince, SpellPostCastGrace))
    {
        return SpellWaitOutcome::RestoreNow;
    }
    if (DeadlineReached(now, castSentAt, SpellCastTimeout))
    {
        return SpellWaitOutcome::RestoreNow;
    }
    return SpellWaitOutcome::Pending;
}

} // namespace potionhands

namespace tradefilter {

// An item dropped to start a trade is delivered as part of its opening sequence.
constexpr uint32_t FirstOfferWindow = 1500;
constexpr uint16_t VirtualCurrencyCheck = 0x14F0;

enum class TradeWatchDecision
{
    Ignore,
    WatchFirstOffer
};

inline TradeWatchDecision DecideTradeWatch(
    bool blockTradeOffers, bool allowFriends, bool initiatedByUs, bool opponentIsFriend)
{
    if (!blockTradeOffers || initiatedByUs)
    {
        return TradeWatchDecision::Ignore;
    }
    if (allowFriends && opponentIsFriend)
    {
        return TradeWatchDecision::Ignore;
    }
    return TradeWatchDecision::WatchFirstOffer;
}

enum class FirstOfferDecision
{
    KeepWatching,
    Allow,
    Decline
};

inline FirstOfferDecision DecideFirstOffer(
    bool blockTradeOffers,
    bool watchFirstOffer,
    bool isOpponentContainer,
    bool isOwnContainer,
    uint16_t graphic,
    bool legacyVirtualCurrencyProtocol,
    uint32_t now,
    uint32_t openedAt)
{
    if (!blockTradeOffers || !watchFirstOffer)
    {
        return FirstOfferDecision::Allow;
    }
    if (!isOpponentContainer && !isOwnContainer)
    {
        return FirstOfferDecision::KeepWatching;
    }
    if (graphic == VirtualCurrencyCheck && legacyVirtualCurrencyProtocol)
    {
        return FirstOfferDecision::KeepWatching;
    }
    if (isOpponentContainer && (now - openedAt) < FirstOfferWindow)
    {
        return FirstOfferDecision::Decline;
    }
    return FirstOfferDecision::Allow;
}

} // namespace tradefilter
} // namespace xuoassist
