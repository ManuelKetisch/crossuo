// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 CrossUO Contributors

#include "UseItemsList_rules.h"
#include <cassert>
#include <set>

int main()
{
    using namespace xuo_container_restore;

    // 1. Pacing after PaperdollReq: must wait ActionDelayMs before opening the backpack
    //    so ModernUO's 500ms Mobile.ActionDelay does not reject the container open.
    {
        UseItemQueue queue;
        const uint32_t backpack = 0x40069668u;
        const uint32_t subBag = 0x40069680u;
        const uint32_t deepBag = 0x40069690u;

        std::set<uint32_t> world = { backpack };
        std::set<uint32_t> opened;
        auto exists = [&](uint32_t s) { return world.count(s) != 0; };
        auto isOpen = [&](uint32_t s) { return opened.count(s) != 0; };

        queue.NoteActionSent(1000); // PaperdollReq sent during GumpManager::Load
        queue.Add(backpack);
        queue.Add(subBag);
        queue.Add(deepBag);
        queue.Add(backpack); // duplicate ignored

        // Immediately after PaperdollReq -> must wait.
        auto step = queue.Step(1016, exists, isOpen);
        assert(step.Action == StepAction::Wait);
        assert(step.Serial == backpack);

        step = queue.Step(1000 + ActionDelayMs - 1, exists, isOpen);
        assert(step.Action == StepAction::Wait);

        // Once ActionDelayMs elapses -> sends backpack double-click.
        step = queue.Step(1000 + ActionDelayMs, exists, isOpen);
        assert(step.Action == StepAction::SendDoubleClick);
        assert(step.Serial == backpack);

        // Server opens backpack and sends its contents (subBag now exists in world).
        opened.insert(backpack);
        world.insert(subBag);

        // Before next ActionDelayMs elapses -> advances to subBag and waits.
        step = queue.Step(1000 + ActionDelayMs + 100, exists, isOpen);
        assert(step.Action == StepAction::Wait);
        assert(step.Serial == subBag);

        // After ActionDelayMs -> opens subBag.
        const uint32_t tSub = 1000 + ActionDelayMs * 2;
        step = queue.Step(tSub, exists, isOpen);
        assert(step.Action == StepAction::SendDoubleClick);
        assert(step.Serial == subBag);

        // Server opens subBag, revealing deepBag.
        opened.insert(subBag);
        world.insert(deepBag);

        const uint32_t tDeep = tSub + ActionDelayMs;
        step = queue.Step(tDeep, exists, isOpen);
        assert(step.Action == StepAction::SendDoubleClick);
        assert(step.Serial == deepBag);

        opened.insert(deepBag);
        step = queue.Step(tDeep + 10, exists, isOpen);
        assert(step.Action == StepAction::Idle);
        assert(queue.Empty());
    }

    // 2. Out-of-order saved bags + stale serial: ready backpack is promoted immediately
    //    ahead of a missing/nested entry, opens first, unlocks nested bag, and stale
    //    serial expires cleanly after WaitForObjectMs.
    {
        UseItemQueue queue;
        const uint32_t stale = 0x4006A368u;
        const uint32_t subBag = 0x40069680u;
        const uint32_t backpack = 0x40069668u;

        std::set<uint32_t> world = { backpack };
        std::set<uint32_t> opened;
        auto exists = [&](uint32_t s) { return world.count(s) != 0; };
        auto isOpen = [&](uint32_t s) { return opened.count(s) != 0; };

        queue.Add(stale);
        queue.Add(subBag);
        queue.Add(backpack);

        // Backpack is promoted immediately and opened without stalling on stale.
        auto step = queue.Step(5000, exists, isOpen);
        assert(step.Action == StepAction::SendDoubleClick);
        assert(step.Serial == backpack);

        opened.insert(backpack);
        world.insert(subBag);

        // Now subBag exists -> promoted ahead of stale and opened after ActionDelayMs.
        step = queue.Step(5000 + ActionDelayMs, exists, isOpen);
        assert(step.Action == StepAction::SendDoubleClick);
        assert(step.Serial == subBag);

        opened.insert(subBag);

        // Only stale remains -> waits up to WaitForObjectMs, then drops cleanly.
        const uint32_t waitStart = 5000 + ActionDelayMs + 50;
        step = queue.Step(waitStart, exists, isOpen);
        assert(step.Action == StepAction::Wait);
        assert(step.Serial == stale);

        step = queue.Step(waitStart + WaitForObjectMs - 1, exists, isOpen);
        assert(step.Action == StepAction::Wait);
        assert(!queue.Empty());

        step = queue.Step(waitStart + WaitForObjectMs, exists, isOpen);
        assert(step.Action == StepAction::Idle);
        assert(queue.Empty());
    }

    // 3. Retry once if an open request gets lost/rejected, then drop after MaxAttempts.
    {
        UseItemQueue queue;
        const uint32_t bag = 0x40061234u;
        std::set<uint32_t> world = { bag };
        std::set<uint32_t> opened;
        auto exists = [&](uint32_t s) { return world.count(s) != 0; };
        auto isOpen = [&](uint32_t s) { return opened.count(s) != 0; };

        queue.Add(bag);

        auto step = queue.Step(1000, exists, isOpen);
        assert(step.Action == StepAction::SendDoubleClick);
        assert(step.Serial == bag);

        // Still waiting for reply before RetryDelayMs.
        step = queue.Step(1000 + RetryDelayMs - 1, exists, isOpen);
        assert(step.Action == StepAction::Wait);

        // Retry #2 fires at RetryDelayMs.
        const uint32_t tRetry = 1000 + RetryDelayMs;
        step = queue.Step(tRetry, exists, isOpen);
        assert(step.Action == StepAction::SendDoubleClick);
        assert(step.Serial == bag);

        // Still never opened after second RetryDelayMs -> drops cleanly.
        step = queue.Step(tRetry + RetryDelayMs, exists, isOpen);
        assert(step.Action == StepAction::Idle);
        assert(queue.Empty());
    }

    // 4. SavedContainerPosition upsert, negative coordinate preservation, update, and cap.
    {
        std::vector<SavedContainerPosition> positions;
        UpsertSavedContainerPosition(positions, { 0x40066E4Eu, -30, 853, 12, 34, true });
        const auto *found = FindSavedContainerPositionIn(positions, 0x40066E4Eu);
        assert(found != nullptr);
        assert(found->X == -30);
        assert(found->Y == 853);
        assert(found->MinimizedX == 12);
        assert(found->MinimizedY == 34);
        assert(found->LockMoving);

        // Updating an existing serial replaces in-place without duplicating.
        UpsertSavedContainerPosition(positions, { 0x40066E4Eu, 1496, 401, 0, 0, false });
        assert(positions.size() == 1);
        found = FindSavedContainerPositionIn(positions, 0x40066E4Eu);
        assert(found != nullptr);
        assert(found->X == 1496 && found->Y == 401 && !found->LockMoving);

        // Bounded at MaxSavedContainerPositions (oldest entry evicted).
        for (uint32_t i = 0; i < MaxSavedContainerPositions + 10; ++i)
        {
            UpsertSavedContainerPosition(positions, { 0x40070000u + i, static_cast<short>(i), 100, 0, 0, false });
        }
        assert(positions.size() == MaxSavedContainerPositions);
        assert(FindSavedContainerPositionIn(positions, 0x40066E4Eu) == nullptr);
        assert(FindSavedContainerPositionIn(positions, 0x40070000u + MaxSavedContainerPositions + 9) != nullptr);
    }

    return 0;
}
