// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace xuoassist {

struct DamageTotals
{
    uint64_t Amount = 0;
    uint64_t Hits = 0;
    int Largest = 0;

    void Add(int damage)
    {
        Amount += damage;
        ++Hits;
        Largest = std::max(Largest, damage);
    }

    double PerSecond(uint64_t milliseconds) const
    {
        return milliseconds != 0 ? static_cast<double>(Amount) * 1000.0 / milliseconds : 0.0;
    }
};

struct DamageTarget
{
    std::string Name;
    DamageTotals Totals;
};

class DamageMeterState
{
public:
    enum class Status { Idle, Running, Paused, Stopped };

    void Start(uint32_t now, uint32_t player)
    {
        *this = {};
        m_Player = player;
        m_Started = now;
        m_Status = Status::Running;
    }

    void Pause(uint32_t now)
    {
        if (m_Status == Status::Running)
        {
            m_Elapsed = Elapsed(now);
            m_Status = Status::Paused;
        }
    }

    void Resume(uint32_t now)
    {
        if (m_Status == Status::Paused)
        {
            m_Started = now;
            m_Status = Status::Running;
        }
    }

    void Stop(uint32_t now)
    {
        if (m_Status == Status::Running || m_Status == Status::Paused)
        {
            m_Elapsed = Elapsed(now);
            m_Status = Status::Stopped;
        }
    }

    void Record(uint32_t serial, const std::string &name, int damage)
    {
        if (m_Status != Status::Running || serial == 0 || damage <= 0)
        {
            return;
        }
        if (serial == m_Player)
        {
            m_Taken.Add(damage);
            return;
        }

        m_Others.Add(damage);
        auto target = m_Targets.find(serial);
        if (target == m_Targets.end())
        {
            if (m_Targets.size() >= MaxTargets)
            {
                m_Unlisted.Add(damage);
                return;
            }
            target = m_Targets.emplace(serial, DamageTarget{}).first;
        }
        if (!name.empty())
        {
            target->second.Name = name;
        }
        target->second.Totals.Add(damage);
    }

    uint64_t Elapsed(uint32_t now) const
    {
        const uint32_t running = m_Status == Status::Running ? now - m_Started : 0;
        return m_Elapsed + running;
    }

    uint64_t DamageFor(uint32_t serial) const
    {
        if (serial == m_Player && serial != 0)
        {
            return m_Taken.Amount;
        }
        const auto target = m_Targets.find(serial);
        return target != m_Targets.end() ? target->second.Totals.Amount : 0;
    }

    Status GetStatus() const { return m_Status; }
    const auto &Targets() const { return m_Targets; }
    const DamageTotals &Taken() const { return m_Taken; }
    const DamageTotals &Others() const { return m_Others; }
    const DamageTotals &Unlisted() const { return m_Unlisted; }

    static constexpr size_t MaxTargets = 256;

private:
    Status m_Status = Status::Idle;
    uint32_t m_Player = 0;
    uint32_t m_Started = 0;
    uint64_t m_Elapsed = 0;
    DamageTotals m_Taken;
    DamageTotals m_Others;
    DamageTotals m_Unlisted;
    std::unordered_map<uint32_t, DamageTarget> m_Targets;
};

} // namespace xuoassist
