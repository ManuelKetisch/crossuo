// SPDX-License-Identifier: AGPL-3.0-or-later

#include "src/Assistant/core/damagemeter_state.h"
#include <cassert>

int main()
{
    xuoassist::DamageMeterState meter;
    meter.Record(2, "Mongbat", 99);
    assert(meter.DamageFor(2) == 0);

    meter.Start(UINT32_MAX - 499, 1);
    meter.Record(0, "Invalid", 4);
    meter.Record(2, "", 10);
    meter.Record(2, "Mongbat", 15);
    meter.Record(2, "", 0);
    meter.Record(1, "Player", 5);
    meter.Pause(500);
    meter.Record(2, "Mongbat", 99);
    assert(meter.Elapsed(2000) == 1000 && meter.DamageFor(2) == 25 && meter.DamageFor(1) == 5);
    assert(meter.Targets().at(2).Name == "Mongbat" && meter.Targets().at(2).Totals.Largest == 15);
    assert(meter.Taken().Amount == 5 && meter.Others().Hits == 2);

    meter.Resume(2000);
    meter.Record(3, "Rat", 25);
    meter.Stop(3000);
    meter.Record(3, "Rat", 99);
    assert(meter.Elapsed(4000) == 2000 && meter.Others().PerSecond(meter.Elapsed(4000)) == 25.0);
    assert(meter.DamageFor(3) == 25);
    meter.Start(5000, 6);
    assert(meter.DamageFor(2) == 0 && meter.Taken().Amount == 0 && meter.Targets().empty());
    for (uint32_t id = 1000; id < 1300; ++id)
    {
        meter.Record(id, "", 1);
    }
    assert(meter.Targets().size() == 256 && meter.Others().Amount == 300 && meter.Unlisted().Amount == 44);
}
