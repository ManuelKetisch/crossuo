// SPDX-License-Identifier: MIT

#include <common/str.h>
#include "src/Profession.h"
#include <cassert>

int main()
{
    CProfession advanced;
    for (int slot = 0; slot < 4; ++slot)
    {
        advanced.SetSkillIndex(slot, 0xFF);
        assert(advanced.GetSkillValue(slot) == 0);
    }
    assert(advanced.GetSkillPointsLeft(4) == 120);
    assert(advanced.GetUnnamedSkillWithPoints(4) == -1);

    advanced.SetSkillIndex(0, 17);
    advanced.AllocateSkillPoints(0, 50, 4);
    assert(advanced.CanChooseStartingSkill(0, 17, 4));
    assert(!advanced.CanChooseStartingSkill(1, 17, 4));
    assert(!advanced.CanChooseStartingSkill(1, 0xFF, 4));

    advanced.SetSkillIndex(1, 25);
    advanced.AllocateSkillPoints(1, 50, 4);
    advanced.AllocateSkillPoints(3, 50, 4);
    assert(advanced.GetSkillValue(0) == 50);
    assert(advanced.GetSkillValue(1) == 50);
    assert(advanced.GetSkillValue(2) == 0);
    assert(advanced.GetSkillValue(3) == 20);
    assert(advanced.GetSkillPointsLeft(4) == 0);
    assert(advanced.GetUnnamedSkillWithPoints(4) == 3);
    advanced.SetSkillIndex(3, 40);
    assert(advanced.GetUnnamedSkillWithPoints(4) == -1);

    // Picking a different skill for the fourth row preserves its allocation.
    advanced.SetSkillIndex(3, 41);
    assert(advanced.GetSkillValue(3) == 20);
    advanced.AllocateSkillPoints(2, 1, 4);
    assert(advanced.GetSkillValue(2) == 0);
    advanced.AllocateSkillPoints(1, 43, 4);
    assert(advanced.GetSkillPointsLeft(4) == 7);
    advanced.AllocateSkillPoints(2, 10, 4);
    assert(advanced.GetSkillValue(2) == 7);
    assert(advanced.GetUnnamedSkillWithPoints(4) == 2);
    advanced.SetSkillIndex(2, 27);
    assert(advanced.GetUnnamedSkillWithPoints(4) == -1);
    assert(advanced.GetSkillValue(1) == 43);
    assert(advanced.GetSkillValue(3) == 20);
    assert(advanced.GetSkillPointsLeft(4) == 0);

    CProfession older;
    assert(older.GetSkillPointsLeft(3) == 100);
    older.AllocateSkillPoints(0, 52, 3);
    older.AllocateSkillPoints(1, 35, 3);
    older.AllocateSkillPoints(2, 50, 3);
    assert(older.GetSkillValue(0) == 50);
    assert(older.GetSkillValue(1) == 35);
    assert(older.GetSkillValue(2) == 15);
    older.AllocateSkillPoints(2, -5, 3);
    assert(older.GetSkillValue(2) == 0);
    assert(older.GetSkillPointsLeft(3) == 15);
    older.AllocateSkillPoints(3, 5, 3);
    assert(older.GetSkillValue(3) == 0);

    CProfession stats;
    stats.Str = stats.Dex = stats.Int = CProfession::MinStartingStat;
    assert(stats.GetStatPointsLeft(90) == 60);
    stats.AllocateStatPoints(0, 45, 90, 60);
    stats.AllocateStatPoints(1, 30, 90, 60);
    assert(stats.Str == 45 && stats.Dex == 30 && stats.Int == 10);
    assert(stats.GetStatPointsLeft(90) == 5);
    stats.AllocateStatPoints(2, 60, 90, 60);
    assert(stats.Str == 45 && stats.Dex == 30 && stats.Int == 15);
    assert(stats.GetStatPointsLeft(90) == 0);
    stats.AllocateStatPoints(0, 0, 90, 60);
    assert(stats.Str == 10 && stats.Dex == 30 && stats.Int == 15);
    stats.AllocateStatPoints(0, 100, 90, 60);
    assert(stats.Str == 45 && stats.Dex == 30 && stats.Int == 15);

    CProfession olderStats;
    olderStats.Str = olderStats.Dex = olderStats.Int = CProfession::MinStartingStat;
    olderStats.AllocateStatPoints(0, 70, 80, 60);
    assert(olderStats.Str == 60 && olderStats.Dex == 10 && olderStats.Int == 10);
    assert(olderStats.GetStatPointsLeft(80) == 0);
}
