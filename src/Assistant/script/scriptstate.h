// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

#pragma once

// Main-thread script-state stores:
//   - globals: the SetGlobal/GetGlobal/ClearGlobals map. ASSISTANT-GLOBAL
//     (shared by every running script, session lifetime, survives script exit);
//   - timers: the TimerExists/SetTimer/Timer/RemoveTimer/ClearTimers family
//     (g_Ticks-based);
//   - buffs: the BuffExists name mirror (the packet layer surfaces buffs as
//     events OnBuffAdd/OnBuffRemove).

#include "../core/global.h"

namespace xuoassist {

// --- globals ---------------------------------------------------------------

void ScriptGlobal_Set(const astr_t &name, const astr_t &value);
// Returns "" when the name is not set.
astr_t ScriptGlobal_Get(const astr_t &name);
void ScriptGlobal_Clear();

// --- timers ----------------------------------------------------------------

void ScriptTimer_Set(const astr_t &name, int value);
// -1 when the timer does not exist (OnCommandTimer).
int ScriptTimer_Get(const astr_t &name);
bool ScriptTimer_Exists(const astr_t &name);
void ScriptTimer_Remove(const astr_t &name);
void ScriptTimer_Clear();

// --- buffs -----------------------------------------------------------------

void ScriptBuffs_Add(uint16_t graphic, uint32_t timer, const astr_t &name);
void ScriptBuffs_Remove(uint16_t graphic);
const astr_t &ScriptBuffs_GetName(uint16_t graphic);
astr_t ScriptBuffs_List();
void ScriptBuffs_Clear();
// Case-insensitive name match (trims + lowercases the script
bool ScriptBuffs_Exists(const astr_t &name);

// --- Razor Enhanced countdown timers (main-thread owned, steady_clock) ------

void ScriptCountdownTimer_Create(const astr_t &name, int delayMs);
// True while the named timer exists and has remaining time > 0.
bool ScriptCountdownTimer_Check(const astr_t &name);
// Remaining milliseconds (0 when expired or missing).
int ScriptCountdownTimer_Remaining(const astr_t &name);
bool ScriptCountdownTimer_Remove(const astr_t &name);
void ScriptCountdownTimer_Clear();

// --- Razor Enhanced shared values (main-thread owned, JSON payloads) --------

void ScriptSharedValue_Set(const astr_t &name, const astr_t &jsonValue);
bool ScriptSharedValue_Get(const astr_t &name, astr_t &outJsonValue);
bool ScriptSharedValue_Exists(const astr_t &name);
bool ScriptSharedValue_Remove(const astr_t &name);
vector<astr_t> ScriptSharedValue_Keys();
void ScriptSharedValue_Clear();

// --- Razor Enhanced ignore set (main-thread owned) --------------------------

void ScriptIgnore_Add(uint32_t serial);
void ScriptIgnore_Remove(uint32_t serial);
bool ScriptIgnore_Contains(uint32_t serial);
void ScriptIgnore_Clear();

// --- Razor Enhanced active agent preset memory (main-thread owned) ----------

enum XA_SCRIPT_AGENT_SLOT
{
    XA_AGENT_AUTOLOOT = 0,
    XA_AGENT_ORGANIZER,
    XA_AGENT_RESTOCK,
    XA_AGENT_SCAVENGER,
    XA_AGENT_SLOT_COUNT
};

void ScriptAgentList_Set(int slot, const astr_t &name);
astr_t ScriptAgentList_Get(int slot);
void ScriptAgentList_Clear();

// --- PromptTarget handshake (main-thread owned) -----------------------------

void ScriptPromptTarget_Begin();
void ScriptPromptTarget_Deliver(uint32_t serial);
bool ScriptPromptTarget_Consume(uint32_t &outSerial);
void ScriptPromptTarget_Cancel();
bool ScriptPromptTarget_Active();

} // namespace xuoassist
