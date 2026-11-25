// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

class CGameObject;

namespace xuoassist {

enum class TargetAssistSelection
{
    None,
    Self,
    Last
};

// One-shot selection using the current profile. Zero means no target was applied.
uint32_t TargetAssist_RunPreset(const std::string &name, bool useCursor);
void TargetAssist_DrawOptions();
void TargetAssist_ResetSession();
bool TargetAssist_AllowObject(uint8_t cursor, uint32_t serial);
bool TargetAssist_AllowLastRange(uint32_t serial);
void TargetAssist_Record(uint8_t cursor, uint32_t serial);
uint32_t TargetAssist_LastObject(uint8_t cursor, uint32_t fallback);
using TargetAssistFilter = bool (*)(const CGameObject *);
TargetAssistFilter TargetAssist_CycleFilter();
bool TargetAssist_OnlyFriend(const CGameObject *object);
bool TargetAssist_BlockDismount(uint32_t serial);
void TargetAssist_SendLast();
void TargetAssist_Queue(TargetAssistSelection selection);
TargetAssistSelection TargetAssist_TakeQueue();

} // namespace xuoassist
