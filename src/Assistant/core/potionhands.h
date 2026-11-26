// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <common/str.h>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace xuoassist {

using PotionHandsPropertyRows = std::vector<std::pair<uint32_t, wstr_t>>;

// Game thread only. Feed complete outgoing gameplay sends and localized replies
// emitted to the active player. The normal use request is always sent first.
void PotionHands_OnGameSend(const uint8_t *data, size_t size);
void PotionHands_OnCliloc(uint32_t cliloc);
void PotionHands_OnMoveRejected();
void PotionHands_OnPropertyList(
    uint32_t serial, uint32_t revision, const PotionHandsPropertyRows &properties);
bool PotionHands_BeginPotionUse(uint32_t serial);
bool PotionHands_BeginSpellCast(int spellIndex, uint32_t bookSerial = 0);
bool PotionHands_BeginTargetedSpellCast(int spellIndex, uint32_t targetSerial);
void PotionHands_OnTargetCursor(uint32_t cursorId, uint8_t type);
void PotionHands_OnTargetCursorEnd(uint32_t cursorId);
void PotionHands_Tick();
void PotionHands_ResetSession();
void PotionHands_Draw();
bool PotionHands_Busy();
bool PotionHands_BlocksItemMove();

} // namespace xuoassist
