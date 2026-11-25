// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016 Hotride

#pragma once

#include <algorithm>
#include "BaseQueue.h"
#include <xuocore/enumlist.h>

class CBaseProfession : public CBaseQueueItem
{
public:
    astr_t Name = "";
    astr_t TrueName = "";
    wstr_t Description = {};
    uint32_t NameClilocID = 0;
    uint32_t DescriptionClilocID = 0;
    PROFESSION_TYPE Type = PT_NO_PROF;
    uint16_t Gump = 0;
    bool TopLevel = false;
    int DescriptionIndex = 0;

    CBaseProfession();
    virtual ~CBaseProfession() override;

    bool AddDescription(int desc, const astr_t &name, const char *val);
};

class CProfessionCategory : public CBaseProfession
{
public:
    astr_t Childrens = "|";

    CProfessionCategory();
    virtual ~CProfessionCategory() override;

    void AddChildren(const astr_t &child);
};

class CProfession : public CBaseProfession
{
public:
    uint8_t Str = 0;
    uint8_t Int = 0;
    uint8_t Dex = 0;

private:
    uint8_t m_SkillIndex[4];
    uint8_t m_SkillValue[4];

public:
    static constexpr int MinStartingStat = 10;
    static constexpr int MaxStartingSkillPoints = 50;

    CProfession();
    virtual ~CProfession() override;

    void SetSkillIndex(int index, uint8_t val)
    {
        if (index >= 0 && index < 4)
            m_SkillIndex[index] = val;
    }
    void SetSkillValue(int index, uint8_t val)
    {
        if (index >= 0 && index < 4)
            m_SkillValue[index] = val;
    }

    uint8_t GetSkillIndex(int index) const
    {
        if (index >= 0 && index < 4)
            return m_SkillIndex[index];
        else
            return 0xFF;
    }
    uint8_t GetSkillValue(int index) const
    {
        if (index >= 0 && index < 4)
            return m_SkillValue[index];
        else
            return 0xFF;
    }

    int GetStatPointsLeft(int budget) const
    {
        return budget - Str - Dex - Int;
    }

    void AllocateStatPoints(int slot, int requested, int budget, int maximum)
    {
        if (slot < 0 || slot >= 3)
        {
            return;
        }
        uint8_t *const stats[] = { &Str, &Dex, &Int };
        const int available = *stats[slot] + GetStatPointsLeft(budget);
        const int value = std::min({ requested, available, maximum });
        *stats[slot] = std::max(int{ MinStartingStat }, value);
    }

    int GetSkillPointsLeft(int slots) const
    {
        int remaining = slots == 4 ? 120 : 100;
        for (int slot = 0; slot < slots; ++slot)
        {
            remaining -= GetSkillValue(slot);
        }
        return remaining;
    }

    void AllocateSkillPoints(int slot, int requested, int slots)
    {
        if (slot < 0 || slot >= slots)
        {
            return;
        }
        const int available = GetSkillValue(slot) + GetSkillPointsLeft(slots);
        const int value = std::min({ requested, available, MaxStartingSkillPoints });
        SetSkillValue(slot, std::max(0, value));
    }

    bool CanChooseStartingSkill(int slot, uint8_t skill, int slots) const
    {
        if (skill == 0xFF)
        {
            return false;
        }
        for (int other = 0; other < slots; ++other)
        {
            if (other != slot && GetSkillIndex(other) == skill)
            {
                return false;
            }
        }
        return true;
    }

    int GetUnnamedSkillWithPoints(int slots) const
    {
        for (int slot = 0; slot < slots; ++slot)
        {
            if (GetSkillValue(slot) > 0 && GetSkillIndex(slot) == 0xFF)
            {
                return slot;
            }
        }
        return -1;
    }
};
