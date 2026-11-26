// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <xuocore/enumlist.h>

struct PaperdollPointOffset
{
    int X = 0;
    int Y = 0;
};

// Per-body visual alignment offset for horizontally mirrored off-hand weapons.
// Paperdoll body art holds the character's left hand (viewer-right) lower than
// the right hand (viewer-left), and asymmetric silhouettes (such as Gargoyle
// wings) shift the full-body horizontal center slightly off the hands. Applied
// on top of the mirrored baseline (2 * bodyCenterX - 8 - sprite->Width, 19).
inline PaperdollPointOffset PaperdollOffHandWeaponOffset(uint16_t bodyGraphic)
{
    switch (bodyGraphic)
    {
        case 0x000D: // Human Female
        case 0x000F: // Elf Female
            return { 1, 10 };
        case 0x029A: // Gargoyle Male
            return { 2, 13 };
        case 0x0299: // Gargoyle Female
            return { 4, 12 };
        case 0x000C: // Human Male
        case 0x000E: // Elf Male
        default:
            return { 1, 11 };
    }
}

struct PaperdollSlotSpec
{
    int X = 0;
    int Y = 0;
    int Layer = OL_NONE;
    const wchar_t *EmptyTooltip = L"";
};

constexpr std::array<int, 6> kPaperdollRingLayers = {
    OL_RING,
    OL_RING_2,
    OL_RING_3,
    OL_RING_4,
    OL_RING_5,
    OL_RING_6,
};

constexpr std::array<int, 2> kPaperdollEarringLayers = {
    OL_EARRINGS,
    OL_EARRINGS_2,
};

constexpr std::array<int, 2> kPaperdollBraceletLayers = {
    OL_BRACELET,
    OL_BRACELET_2,
};

// Integrated jewelry dock geometry centered beneath the 262px paperdoll frame.
constexpr int kPaperdollJewelryDockX = 3;
constexpr int kPaperdollJewelryDockY = 322;
constexpr int kPaperdollJewelryDockWidth = 256;
constexpr int kPaperdollJewelryDockHeight = 42;
constexpr int kPaperdollJewelrySlotY = 338;
constexpr int kPaperdollJewelryDivider1X = 61;
constexpr int kPaperdollJewelryDivider2X = 118;
constexpr int kPaperdollJewelryLabelY = 325;

constexpr std::array<PaperdollSlotSpec, 3> kPaperdollLeftEquipSlots = { {
    { 1, 75, OL_HELMET, L"Helmet slot" },
    { 1, 96, OL_NECKLACE, L"Necklace slot" },
    { 1, 117, OL_TUNIC, L"Tunic slot" },
} };

constexpr std::array<PaperdollSlotSpec, 10> kPaperdollJewelryDockSlots = { {
    { 12, kPaperdollJewelrySlotY, OL_BRACELET, L"Left bracelet slot" },
    { 33, kPaperdollJewelrySlotY, OL_BRACELET_2, L"Right bracelet slot" },
    { 69, kPaperdollJewelrySlotY, OL_EARRINGS, L"Left earring slot" },
    { 90, kPaperdollJewelrySlotY, OL_EARRINGS_2, L"Right earring slot" },
    { 125, kPaperdollJewelrySlotY, OL_RING, L"Ring slot 1" },
    { 146, kPaperdollJewelrySlotY, OL_RING_2, L"Ring slot 2" },
    { 167, kPaperdollJewelrySlotY, OL_RING_3, L"Ring slot 3" },
    { 188, kPaperdollJewelrySlotY, OL_RING_4, L"Ring slot 4" },
    { 209, kPaperdollJewelrySlotY, OL_RING_5, L"Ring slot 5" },
    { 230, kPaperdollJewelrySlotY, OL_RING_6, L"Ring slot 6" },
} };

inline bool IsRingLayer(int layer)
{
    for (int candidate : kPaperdollRingLayers)
    {
        if (layer == candidate)
        {
            return true;
        }
    }
    return false;
}

inline bool IsEarringsLayer(int layer)
{
    for (int candidate : kPaperdollEarringLayers)
    {
        if (layer == candidate)
        {
            return true;
        }
    }
    return false;
}

inline bool IsBraceletLayer(int layer)
{
    for (int candidate : kPaperdollBraceletLayers)
    {
        if (layer == candidate)
        {
            return true;
        }
    }
    return false;
}

inline bool IsExtendedJewelryLayer(int layer)
{
    return layer >= OL_RING_2 && layer <= OL_BRACELET_2;
}

inline bool IsPaperdollEquipmentLayer(int layer)
{
    return (layer > OL_NONE && layer < OL_MOUNT) || IsExtendedJewelryLayer(layer);
}

inline int CanonicalJewelryLayer(int layer)
{
    if (IsRingLayer(layer))
    {
        return OL_RING;
    }
    if (IsEarringsLayer(layer))
    {
        return OL_EARRINGS;
    }
    if (IsBraceletLayer(layer))
    {
        return OL_BRACELET;
    }
    return layer;
}

inline const wchar_t *PaperdollEmptySlotTooltip(int layer)
{
    for (const auto &slot : kPaperdollLeftEquipSlots)
    {
        if (slot.Layer == layer)
        {
            return slot.EmptyTooltip;
        }
    }
    for (const auto &slot : kPaperdollJewelryDockSlots)
    {
        if (slot.Layer == layer)
        {
            return slot.EmptyTooltip;
        }
    }
    if (layer == OL_1_HAND)
    {
        return L"Main hand slot";
    }
    if (layer == OL_2_HAND)
    {
        return L"Off hand slot";
    }
    return L"";
}

// Resolve which equipment layer a dragged wearable item should equip into when
// dropped on a character's paperdoll. Returns OL_NONE if no matching slot is free.
template <typename LayerOccupiedFn>
inline int ResolvePaperdollDropLayer(
    int itemTileLayer,
    bool isWeapon,
    bool isPlayer,
    int droppedOnLayer,
    bool primaryIsWeapon,
    LayerOccupiedFn isOccupied)
{
    if (itemTileLayer == OL_NONE)
    {
        return OL_NONE;
    }

    if (IsRingLayer(itemTileLayer))
    {
        if (!isPlayer)
        {
            return !isOccupied(OL_RING) ? OL_RING : OL_NONE;
        }
        if (IsRingLayer(droppedOnLayer) && !isOccupied(droppedOnLayer))
        {
            return droppedOnLayer;
        }
        for (int layer : kPaperdollRingLayers)
        {
            if (!isOccupied(layer))
            {
                return layer;
            }
        }
        return OL_NONE;
    }

    if (IsEarringsLayer(itemTileLayer))
    {
        if (!isPlayer)
        {
            return !isOccupied(OL_EARRINGS) ? OL_EARRINGS : OL_NONE;
        }
        if (IsEarringsLayer(droppedOnLayer) && !isOccupied(droppedOnLayer))
        {
            return droppedOnLayer;
        }
        for (int layer : kPaperdollEarringLayers)
        {
            if (!isOccupied(layer))
            {
                return layer;
            }
        }
        return OL_NONE;
    }

    if (IsBraceletLayer(itemTileLayer))
    {
        if (!isPlayer)
        {
            return !isOccupied(OL_BRACELET) ? OL_BRACELET : OL_NONE;
        }
        if (IsBraceletLayer(droppedOnLayer) && !isOccupied(droppedOnLayer))
        {
            return droppedOnLayer;
        }
        for (int layer : kPaperdollBraceletLayers)
        {
            if (!isOccupied(layer))
            {
                return layer;
            }
        }
        return OL_NONE;
    }

    if (isPlayer && itemTileLayer == OL_1_HAND && isWeapon &&
        isOccupied(OL_1_HAND) && primaryIsWeapon && !isOccupied(OL_2_HAND))
    {
        return OL_2_HAND;
    }

    return !isOccupied(itemTileLayer) ? itemTileLayer : OL_NONE;
}
