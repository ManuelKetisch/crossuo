// SPDX-License-Identifier: MIT

#include "PaperdollSlots_rules.h"
#include <cassert>
#include <cstring>
#include <set>

int main()
{
    // 1. Wire layer constants must match ModernUO JewelrySlots exactly.
    static_assert(OL_RING == 0x08, "OL_RING wire value");
    static_assert(OL_BRACELET == 0x0E, "OL_BRACELET wire value");
    static_assert(OL_EARRINGS == 0x12, "OL_EARRINGS wire value");
    static_assert(OL_RING_2 == 0x1E, "OL_RING_2 wire value");
    static_assert(OL_RING_3 == 0x1F, "OL_RING_3 wire value");
    static_assert(OL_RING_4 == 0x20, "OL_RING_4 wire value");
    static_assert(OL_RING_5 == 0x21, "OL_RING_5 wire value");
    static_assert(OL_RING_6 == 0x22, "OL_RING_6 wire value");
    static_assert(OL_EARRINGS_2 == 0x23, "OL_EARRINGS_2 wire value");
    static_assert(OL_BRACELET_2 == 0x24, "OL_BRACELET_2 wire value");
    static_assert(OL_EXTENDED_COUNT == 37, "OL_EXTENDED_COUNT");

    // 2. Predicates and canonical mapping.
    assert(kPaperdollRingLayers.size() == 6);
    assert(kPaperdollEarringLayers.size() == 2);
    assert(kPaperdollBraceletLayers.size() == 2);
    for (int layer : kPaperdollRingLayers)
    {
        assert(IsRingLayer(layer));
        assert(!IsEarringsLayer(layer));
        assert(!IsBraceletLayer(layer));
        assert(IsPaperdollEquipmentLayer(layer));
        assert(CanonicalJewelryLayer(layer) == OL_RING);
    }
    for (int layer : kPaperdollEarringLayers)
    {
        assert(IsEarringsLayer(layer));
        assert(!IsRingLayer(layer));
        assert(!IsBraceletLayer(layer));
        assert(IsPaperdollEquipmentLayer(layer));
        assert(CanonicalJewelryLayer(layer) == OL_EARRINGS);
    }
    for (int layer : kPaperdollBraceletLayers)
    {
        assert(IsBraceletLayer(layer));
        assert(!IsRingLayer(layer));
        assert(!IsEarringsLayer(layer));
        assert(IsPaperdollEquipmentLayer(layer));
        assert(CanonicalJewelryLayer(layer) == OL_BRACELET);
    }
    assert(!IsExtendedJewelryLayer(OL_RING));
    assert(!IsExtendedJewelryLayer(OL_EARRINGS));
    assert(!IsExtendedJewelryLayer(OL_NECKLACE));
    assert(!IsExtendedJewelryLayer(OL_BRACELET));
    assert(!IsExtendedJewelryLayer(OL_BANK));
    assert(IsExtendedJewelryLayer(OL_RING_2));
    assert(IsExtendedJewelryLayer(OL_RING_6));
    assert(IsExtendedJewelryLayer(OL_EARRINGS_2));
    assert(IsExtendedJewelryLayer(OL_BRACELET_2));
    assert(!IsExtendedJewelryLayer(OL_EXTENDED_COUNT));
    assert(CanonicalJewelryLayer(OL_NECKLACE) == OL_NECKLACE);

    // 3. Layout geometry: 3 left equip slots (helmet, necklace, tunic) + 10 dock slots (2 bracelets, 2 earrings, 6 rings).
    assert(kPaperdollLeftEquipSlots.size() == 3);
    assert(kPaperdollJewelryDockSlots.size() == 10);
    {
        std::set<int> allLayers;
        for (const auto &slot : kPaperdollLeftEquipSlots)
        {
            assert(allLayers.insert(slot.Layer).second);
            assert(slot.EmptyTooltip != nullptr && slot.EmptyTooltip[0] != L'\0');
        }
        assert(allLayers.count(OL_HELMET) == 1);
        assert(allLayers.count(OL_NECKLACE) == 1);
        assert(allLayers.count(OL_TUNIC) == 1);
        assert(allLayers.count(OL_BRACELET) == 0);

        int prevRight = kPaperdollJewelryDockX;
        for (size_t i = 0; i < kPaperdollJewelryDockSlots.size(); ++i)
        {
            const auto &slot = kPaperdollJewelryDockSlots[i];
            assert(allLayers.insert(slot.Layer).second);
            assert(slot.X >= prevRight);
            assert(slot.X + 20 <= kPaperdollJewelryDockX + kPaperdollJewelryDockWidth);
            assert(slot.Y >= kPaperdollJewelryDockY);
            assert(slot.Y + 21 <= kPaperdollJewelryDockY + kPaperdollJewelryDockHeight);
            assert(slot.EmptyTooltip != nullptr && slot.EmptyTooltip[0] != L'\0');
            if (i < 2)
            {
                assert(IsBraceletLayer(slot.Layer));
                assert(slot.X >= kPaperdollJewelryDockX + 6);
                assert(slot.X + 20 <= kPaperdollJewelryDivider1X - 6);
            }
            else if (i < 4)
            {
                assert(IsEarringsLayer(slot.Layer));
                assert(slot.X >= kPaperdollJewelryDivider1X + 1 + 6);
                assert(slot.X + 20 <= kPaperdollJewelryDivider2X - 6);
            }
            else
            {
                assert(IsRingLayer(slot.Layer));
                assert(slot.X >= kPaperdollJewelryDivider2X + 1 + 6);
                assert(slot.X + 20 <= kPaperdollJewelryDockX + kPaperdollJewelryDockWidth - 6);
            }
            prevRight = slot.X + 20;
        }
    }

    // 4. Empty slot tooltips cover every interactive slot.
    assert(PaperdollEmptySlotTooltip(OL_HELMET)[0] != L'\0');
    assert(PaperdollEmptySlotTooltip(OL_NECKLACE)[0] != L'\0');
    assert(PaperdollEmptySlotTooltip(OL_TUNIC)[0] != L'\0');
    assert(PaperdollEmptySlotTooltip(OL_1_HAND)[0] != L'\0');
    assert(PaperdollEmptySlotTooltip(OL_2_HAND)[0] != L'\0');
    for (int layer : kPaperdollBraceletLayers)
    {
        assert(PaperdollEmptySlotTooltip(layer)[0] != L'\0');
    }
    for (int layer : kPaperdollEarringLayers)
    {
        assert(PaperdollEmptySlotTooltip(layer)[0] != L'\0');
    }
    for (int layer : kPaperdollRingLayers)
    {
        assert(PaperdollEmptySlotTooltip(layer)[0] != L'\0');
    }
    assert(PaperdollEmptySlotTooltip(OL_BACKPACK)[0] == L'\0');

    // 5. Ring drop resolution: targeted empty slot, auto first-free, occupied slot fallback, full rejection.
    {
        std::set<int> occupied;
        auto isOccupied = [&occupied](int layer) { return occupied.count(layer) != 0; };

        // Drop directly onto Ring slot 5 -> equips into Ring 5.
        assert(ResolvePaperdollDropLayer(OL_RING, false, true, OL_RING_5, false, isOccupied) == OL_RING_5);
        occupied.insert(OL_RING_5);

        // Drop onto body (-1) -> picks first free (Ring 1).
        assert(ResolvePaperdollDropLayer(OL_RING, false, true, -1, false, isOccupied) == OL_RING);
        occupied.insert(OL_RING);

        // Drop onto occupied Ring 1 -> falls back to next free (Ring 2).
        assert(ResolvePaperdollDropLayer(OL_RING, false, true, OL_RING, false, isOccupied) == OL_RING_2);
        occupied.insert(OL_RING_2);

        // Drop onto mismatched Earring or Bracelet slot -> ignores other families, picks next free ring (Ring 3).
        assert(ResolvePaperdollDropLayer(OL_RING, false, true, OL_EARRINGS_2, false, isOccupied) == OL_RING_3);
        occupied.insert(OL_RING_3);

        // Fill remaining slots 4 and 6.
        assert(ResolvePaperdollDropLayer(OL_RING, false, true, OL_BRACELET_2, false, isOccupied) == OL_RING_4);
        occupied.insert(OL_RING_4);
        assert(ResolvePaperdollDropLayer(OL_RING, false, true, -1, false, isOccupied) == OL_RING_6);
        occupied.insert(OL_RING_6);

        // 7th ring rejected cleanly.
        assert(ResolvePaperdollDropLayer(OL_RING, false, true, -1, false, isOccupied) == OL_NONE);
        assert(ResolvePaperdollDropLayer(OL_RING, false, true, OL_RING_3, false, isOccupied) == OL_NONE);

        // Removing Ring 3 frees Ring 3.
        occupied.erase(OL_RING_3);
        assert(ResolvePaperdollDropLayer(OL_RING, false, true, -1, false, isOccupied) == OL_RING_3);
    }

    // 6. Earring drop resolution: 2 slots, targeted or first-free, 3rd rejected.
    {
        std::set<int> occupied;
        auto isOccupied = [&occupied](int layer) { return occupied.count(layer) != 0; };

        // Drop directly onto Right Earring -> equips into OL_EARRINGS_2.
        assert(ResolvePaperdollDropLayer(OL_EARRINGS, false, true, OL_EARRINGS_2, false, isOccupied) == OL_EARRINGS_2);
        occupied.insert(OL_EARRINGS_2);

        // Drop onto body -> fills Left Earring (OL_EARRINGS).
        assert(ResolvePaperdollDropLayer(OL_EARRINGS, false, true, -1, false, isOccupied) == OL_EARRINGS);
        occupied.insert(OL_EARRINGS);

        // 3rd earring rejected.
        assert(ResolvePaperdollDropLayer(OL_EARRINGS, false, true, -1, false, isOccupied) == OL_NONE);
        assert(ResolvePaperdollDropLayer(OL_EARRINGS, false, true, OL_EARRINGS, false, isOccupied) == OL_NONE);
    }

    // 7. Bracelet drop resolution: 2 slots, targeted or first-free, cross-family isolation, 3rd rejected.
    {
        std::set<int> occupied;
        auto isOccupied = [&occupied](int layer) { return occupied.count(layer) != 0; };

        // Drop directly onto Right Bracelet -> equips into OL_BRACELET_2.
        assert(ResolvePaperdollDropLayer(OL_BRACELET, false, true, OL_BRACELET_2, false, isOccupied) == OL_BRACELET_2);
        occupied.insert(OL_BRACELET_2);

        // Drop onto mismatched Ring or Earring slot -> ignores other families, fills Left Bracelet (OL_BRACELET).
        assert(ResolvePaperdollDropLayer(OL_BRACELET, false, true, OL_RING_2, false, isOccupied) == OL_BRACELET);
        assert(ResolvePaperdollDropLayer(OL_BRACELET, false, true, OL_EARRINGS_2, false, isOccupied) == OL_BRACELET);
        assert(ResolvePaperdollDropLayer(OL_BRACELET, false, true, -1, false, isOccupied) == OL_BRACELET);
        occupied.insert(OL_BRACELET);

        // 3rd bracelet rejected.
        assert(ResolvePaperdollDropLayer(OL_BRACELET, false, true, -1, false, isOccupied) == OL_NONE);
        assert(ResolvePaperdollDropLayer(OL_BRACELET, false, true, OL_BRACELET, false, isOccupied) == OL_NONE);
        assert(ResolvePaperdollDropLayer(OL_BRACELET, false, true, OL_BRACELET_2, false, isOccupied) == OL_NONE);

        // Removing Left Bracelet frees OL_BRACELET; dropping onto occupied OL_BRACELET_2 falls back to OL_BRACELET.
        occupied.erase(OL_BRACELET);
        assert(ResolvePaperdollDropLayer(OL_BRACELET, false, true, OL_BRACELET_2, false, isOccupied) == OL_BRACELET);
    }

    // 8. Single necklace remains 1-slot; non-player stays 1-slot for all jewelry; dual-wield preserved.
    {
        std::set<int> occupied = { OL_NECKLACE, OL_BRACELET, OL_RING, OL_EARRINGS, OL_1_HAND };
        auto isOccupied = [&occupied](int layer) { return occupied.count(layer) != 0; };

        assert(ResolvePaperdollDropLayer(OL_NECKLACE, false, true, -1, false, isOccupied) == OL_NONE);

        // Non-player mobile remains single ring / single earring / single bracelet.
        assert(ResolvePaperdollDropLayer(OL_RING, false, false, OL_RING_2, false, isOccupied) == OL_NONE);
        assert(ResolvePaperdollDropLayer(OL_EARRINGS, false, false, OL_EARRINGS_2, false, isOccupied) == OL_NONE);
        assert(ResolvePaperdollDropLayer(OL_BRACELET, false, false, OL_BRACELET_2, false, isOccupied) == OL_NONE);

        // Non-player with free primary bracelet equips into OL_BRACELET even if dropped on OL_BRACELET_2.
        occupied.erase(OL_BRACELET);
        assert(ResolvePaperdollDropLayer(OL_BRACELET, false, false, OL_BRACELET_2, false, isOccupied) == OL_BRACELET);

        // Dual-wield one-handed weapon still promotes to OL_2_HAND.
        assert(ResolvePaperdollDropLayer(OL_1_HAND, true, true, -1, true, isOccupied) == OL_2_HAND);
        assert(ResolvePaperdollDropLayer(OL_1_HAND, true, true, -1, false, isOccupied) == OL_NONE);
    }

    return 0;
}
