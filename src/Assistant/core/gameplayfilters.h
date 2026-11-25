// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

namespace xuoassist {

bool GameplayFilters_AllowSpeech(
    uint32_t serial, bool system, bool assistant, bool mobile, const std::wstring &text);
void GameplayFilters_ReportSound(uint16_t index, bool blocked);
void GameplayFilters_DrawSoundLogging();
uint16_t GameplayFilters_AnimationBody(uint32_t serial, uint16_t graphic);
bool GameplayFilters_IgnoreTarget(uint32_t serial);
bool GameplayFilters_ShowFieldLabels();
bool GameplayFilters_BlockTradeOffers();
bool GameplayFilters_AllowFriendTrades();
void GameplayFilters_ReportTradeDeclined(const std::string &opponentName);

void GameplayFilters_BeginCast(int spell);
void GameplayFilters_ReceiveCursor(uint32_t cursorId, uint8_t type, uint8_t flags);
void GameplayFilters_EndCursor(uint32_t cursorId);
bool GameplayFilters_AllowHealTarget(uint32_t cursorId, uint32_t serial);
bool GameplayFilters_AllowSpellTarget(int spell, uint32_t serial);

void GameplayFilters_ResetSession();
void GameplayFilters_Draw();

} // namespace xuoassist
