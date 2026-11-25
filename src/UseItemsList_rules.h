// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 CrossUO Contributors

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace xuo_container_restore {

// ModernUO applies Mobile.ActionDelay (500ms) to all 0x06 UseReq packets
// (including paperdoll requests) for non-staff players. Pace queued restore
// opens safely above that cooldown so login restoration is not rejected.
constexpr uint32_t ActionDelayMs = 650;
constexpr uint32_t RetryDelayMs = 1200;
constexpr uint32_t WaitForObjectMs = 2500;
constexpr uint8_t MaxAttempts = 2;
constexpr size_t MaxSavedContainerPositions = 256;

enum class StepAction
{
    Idle,
    Wait,
    SendPaperdoll,
    SendDoubleClick
};

struct StepResult
{
    StepAction Action = StepAction::Idle;
    uint32_t Serial = 0;
};

struct PendingUseItem
{
    uint32_t Serial = 0;
    uint32_t WaitingSince = 0;
    bool WaitingInitialized = false;
    uint32_t LastSentAt = 0;
    uint8_t Attempts = 0;
};

struct SavedContainerPosition
{
    uint32_t Serial = 0;
    short X = 0;
    short Y = 0;
    short MinimizedX = 0;
    short MinimizedY = 0;
    bool LockMoving = false;
};

inline void UpsertSavedContainerPosition(
    std::vector<SavedContainerPosition> &positions,
    const SavedContainerPosition &entry)
{
    if (entry.Serial == 0)
    {
        return;
    }
    for (auto it = positions.begin(); it != positions.end(); ++it)
    {
        if (it->Serial == entry.Serial)
        {
            positions.erase(it);
            break;
        }
    }
    if (positions.size() >= MaxSavedContainerPositions)
    {
        positions.erase(positions.begin());
    }
    positions.push_back(entry);
}

inline const SavedContainerPosition *FindSavedContainerPositionIn(
    const std::vector<SavedContainerPosition> &positions,
    uint32_t serial)
{
    if (serial == 0)
    {
        return nullptr;
    }
    for (const auto &entry : positions)
    {
        if (entry.Serial == serial)
        {
            return &entry;
        }
    }
    return nullptr;
}

class UseItemQueue
{
public:
    void Add(uint32_t serial)
    {
        if (serial == 0)
        {
            return;
        }
        for (const auto &item : m_List)
        {
            if (item.Serial == serial)
            {
                return;
            }
        }
        PendingUseItem entry;
        entry.Serial = serial;
        m_List.push_back(entry);
    }

    void Clear()
    {
        m_List.clear();
        m_HasActionSent = false;
        m_LastActionSentAt = 0;
    }

    bool Empty() const { return m_List.empty(); }

    void NoteActionSent(uint32_t now)
    {
        m_HasActionSent = true;
        m_LastActionSentAt = now;
    }

    template <typename ExistsFn, typename AlreadyOpenFn>
    StepResult Step(uint32_t now, ExistsFn &&existsInWorld, AlreadyOpenFn &&alreadyOpen)
    {
        while (!m_List.empty())
        {
            auto &front = m_List.front();
            if (alreadyOpen(front.Serial))
            {
                m_List.pop_front();
                continue;
            }

            if (front.Attempts > 0)
            {
                if (static_cast<uint32_t>(now - front.LastSentAt) < RetryDelayMs)
                {
                    return { StepAction::Wait, front.Serial };
                }
                if (front.Attempts >= MaxAttempts)
                {
                    m_List.pop_front();
                    continue;
                }
            }

            if (!existsInWorld(front.Serial))
            {
                // If an earlier saved container is missing or waiting on a parent
                // listed later, promote the first ready queued container ahead so
                // stale serials never stall restoration of existing bags.
                if (front.Attempts == 0)
                {
                    size_t readyIndex = m_List.size();
                    for (size_t i = 1; i < m_List.size(); ++i)
                    {
                        const uint32_t candidate = m_List[i].Serial;
                        if (!alreadyOpen(candidate) && existsInWorld(candidate))
                        {
                            readyIndex = i;
                            break;
                        }
                    }
                    if (readyIndex < m_List.size())
                    {
                        PendingUseItem promoted = m_List[readyIndex];
                        m_List.erase(m_List.begin() + static_cast<std::ptrdiff_t>(readyIndex));
                        m_List.push_front(promoted);
                        continue;
                    }
                }

                if (!front.WaitingInitialized)
                {
                    front.WaitingInitialized = true;
                    front.WaitingSince = now;
                    return { StepAction::Wait, front.Serial };
                }
                if (static_cast<uint32_t>(now - front.WaitingSince) < WaitForObjectMs)
                {
                    return { StepAction::Wait, front.Serial };
                }
                m_List.pop_front();
                continue;
            }

            if (m_HasActionSent && static_cast<uint32_t>(now - m_LastActionSentAt) < ActionDelayMs)
            {
                return { StepAction::Wait, front.Serial };
            }

            const uint32_t serial = front.Serial;
            front.Attempts++;
            front.LastSentAt = now;
            m_HasActionSent = true;
            m_LastActionSentAt = now;
            if (serial < 0x40000000u)
            {
                m_List.pop_front();
                return { StepAction::SendPaperdoll, serial };
            }
            return { StepAction::SendDoubleClick, serial };
        }

        return { StepAction::Idle, 0 };
    }

private:
    std::deque<PendingUseItem> m_List;
    bool m_HasActionSent = false;
    uint32_t m_LastActionSentAt = 0;
};

} // namespace xuo_container_restore
