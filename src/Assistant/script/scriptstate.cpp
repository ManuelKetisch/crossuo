// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

// See scriptstate.h. The original sources are managers/commandmanager.cpp
// (OnCommandSetGlobal/GetGlobal/ClearGlobals, the Timer family
// over m_Timers/m_Time) and OnCommandBuffExists (over
// g_BuffManager).

#include "scriptstate.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>

namespace xuoassist {

namespace {

std::map<astr_t, astr_t> s_Globals;
std::map<astr_t, uint32_t> s_Timers; // stored stamp: g_Ticks - value
struct ActiveBuff
{
    astr_t Name;
    astr_t Key;
};
std::map<uint16_t, ActiveBuff> s_Buffs;

// The original BuffExists body trimmed + lowered the script argument (the port
// lowers at store time, so only the lookup needs it here).
astr_t buff_key(const astr_t& name)
{
    // leading/trailing space trim
    size_t begin = name.find_first_not_of(" \t\r\n");
    if (begin == astr_t::npos)
    {
        return "";
    }
    size_t end = name.find_last_not_of(" \t\r\n");
    return str_lower(name.substr(begin, end - begin + 1));
}

} // namespace

// --- globals ---------------------------------------------------------------

void ScriptGlobal_Set(const astr_t &name, const astr_t &value)
{
    if (name.empty())
    {
        return;
    }
    s_Globals[name] = value;
}

astr_t ScriptGlobal_Get(const astr_t &name)
{
    std::map<astr_t, astr_t>::const_iterator it = s_Globals.find(name);
    return it != s_Globals.end() ? it->second : astr_t();
}

void ScriptGlobal_Clear()
{
    s_Globals.clear();
}

// --- timers ----------------------------------------------------------------

void ScriptTimer_Set(const astr_t &name, int value)
{
    s_Timers[name] = g_Ticks - (uint32_t)value;
}

int ScriptTimer_Get(const astr_t &name)
{
    std::map<astr_t, uint32_t>::const_iterator it = s_Timers.find(name);
    return it != s_Timers.end() ? (int)(g_Ticks - it->second) : -1;
}

bool ScriptTimer_Exists(const astr_t &name)
{
    return s_Timers.find(name) != s_Timers.end();
}

void ScriptTimer_Remove(const astr_t &name)
{
    s_Timers.erase(name);
}

void ScriptTimer_Clear()
{
    s_Timers.clear();
}

// --- buffs -----------------------------------------------------------------

void ScriptBuffs_Add(uint16_t graphic, uint32_t timer, const astr_t &name)
{
    (void)timer;
    s_Buffs[graphic] = { name, buff_key(name) };
}

void ScriptBuffs_Remove(uint16_t graphic)
{
    s_Buffs.erase(graphic);
}

const astr_t &ScriptBuffs_GetName(uint16_t graphic)
{
    static const astr_t empty;
    const auto entry = s_Buffs.find(graphic);
    return entry != s_Buffs.end() ? entry->second.Name : empty;
}

astr_t ScriptBuffs_List()
{
    astr_t result;
    for (const auto &entry : s_Buffs)
    {
        if (entry.second.Name.empty())
        {
            continue;
        }
        if (!result.empty())
        {
            result += ", ";
        }
        result += entry.second.Name;
    }
    return result;
}

void ScriptBuffs_Clear()
{
    s_Buffs.clear();
}

bool ScriptBuffs_Exists(const astr_t &name)
{
    const astr_t key = buff_key(name);
    if (key.empty())
    {
        return false;
    }
    if (std::any_of(s_Buffs.begin(), s_Buffs.end(), [&](const auto &entry) {
        return entry.second.Key == key;
    }))
    {
        return true;
    }
    // the script may pass the icon id as text ("0x754C")
    char *end = nullptr;
    const auto graphic = strtoul(key.c_str(), &end, 16);
    return *end == '\0' && graphic <= UINT16_MAX && s_Buffs.count(static_cast<uint16_t>(graphic)) != 0;
}

// --- Razor Enhanced countdown timers ---------------------------------------

namespace {

using SteadyClock = std::chrono::steady_clock;

std::map<astr_t, SteadyClock::time_point> s_CountdownTimers;
std::map<astr_t, astr_t> s_SharedValues;
std::set<uint32_t> s_IgnoredSerials;
astr_t s_AgentLists[XA_AGENT_SLOT_COUNT];

bool s_PromptActive = false;
bool s_PromptDelivered = false;
uint32_t s_PromptSerial = 0;

astr_t timer_key(const astr_t &name)
{
    return str_lower(name);
}

} // namespace

void ScriptCountdownTimer_Create(const astr_t &name, int delayMs)
{
    const astr_t key = timer_key(name);
    if (key.empty())
    {
        return;
    }
    const int clamped = delayMs > 0 ? delayMs : 0;
    s_CountdownTimers[key] = SteadyClock::now() + std::chrono::milliseconds(clamped);
}

bool ScriptCountdownTimer_Check(const astr_t &name)
{
    return ScriptCountdownTimer_Remaining(name) > 0;
}

int ScriptCountdownTimer_Remaining(const astr_t &name)
{
    const astr_t key = timer_key(name);
    auto it = s_CountdownTimers.find(key);
    if (it == s_CountdownTimers.end())
    {
        return 0;
    }
    const auto now = SteadyClock::now();
    if (it->second <= now)
    {
        return 0;
    }
    const auto diff =
        std::chrono::duration_cast<std::chrono::milliseconds>(it->second - now).count();
    return diff > 0 ? static_cast<int>(diff) : 0;
}

bool ScriptCountdownTimer_Remove(const astr_t &name)
{
    const astr_t key = timer_key(name);
    return s_CountdownTimers.erase(key) > 0;
}

void ScriptCountdownTimer_Clear()
{
    s_CountdownTimers.clear();
}

// --- Razor Enhanced shared values ------------------------------------------

void ScriptSharedValue_Set(const astr_t &name, const astr_t &jsonValue)
{
    if (name.empty())
    {
        return;
    }
    s_SharedValues[name] = jsonValue;
}

bool ScriptSharedValue_Get(const astr_t &name, astr_t &outJsonValue)
{
    auto it = s_SharedValues.find(name);
    if (it == s_SharedValues.end())
    {
        outJsonValue.clear();
        return false;
    }
    outJsonValue = it->second;
    return true;
}

bool ScriptSharedValue_Exists(const astr_t &name)
{
    return s_SharedValues.find(name) != s_SharedValues.end();
}

bool ScriptSharedValue_Remove(const astr_t &name)
{
    return s_SharedValues.erase(name) > 0;
}

vector<astr_t> ScriptSharedValue_Keys()
{
    vector<astr_t> keys;
    keys.reserve(s_SharedValues.size());
    for (const auto &entry : s_SharedValues)
    {
        keys.push_back(entry.first);
    }
    return keys;
}

void ScriptSharedValue_Clear()
{
    s_SharedValues.clear();
}

// --- Razor Enhanced ignore set ---------------------------------------------

void ScriptIgnore_Add(uint32_t serial)
{
    if (serial != 0)
    {
        s_IgnoredSerials.insert(serial);
    }
}

void ScriptIgnore_Remove(uint32_t serial)
{
    s_IgnoredSerials.erase(serial);
}

bool ScriptIgnore_Contains(uint32_t serial)
{
    return serial != 0 && s_IgnoredSerials.count(serial) != 0;
}

void ScriptIgnore_Clear()
{
    s_IgnoredSerials.clear();
}

// --- Razor Enhanced active agent preset memory -----------------------------

void ScriptAgentList_Set(int slot, const astr_t &name)
{
    if (slot >= 0 && slot < XA_AGENT_SLOT_COUNT)
    {
        s_AgentLists[slot] = name;
    }
}

astr_t ScriptAgentList_Get(int slot)
{
    if (slot >= 0 && slot < XA_AGENT_SLOT_COUNT)
    {
        return s_AgentLists[slot];
    }
    return astr_t();
}

void ScriptAgentList_Clear()
{
    for (int i = 0; i < XA_AGENT_SLOT_COUNT; i++)
    {
        s_AgentLists[i].clear();
    }
}

// --- PromptTarget handshake ------------------------------------------------

void ScriptPromptTarget_Begin()
{
    s_PromptActive = true;
    s_PromptDelivered = false;
    s_PromptSerial = 0;
}

void ScriptPromptTarget_Deliver(uint32_t serial)
{
    s_PromptSerial = serial;
    s_PromptDelivered = true;
    s_PromptActive = false;
}

bool ScriptPromptTarget_Consume(uint32_t &outSerial)
{
    if (!s_PromptDelivered)
    {
        return false;
    }
    outSerial = s_PromptSerial;
    s_PromptDelivered = false;
    s_PromptActive = false;
    return true;
}

void ScriptPromptTarget_Cancel()
{
    s_PromptActive = false;
    s_PromptDelivered = false;
    s_PromptSerial = 0;
}

bool ScriptPromptTarget_Active()
{
    return s_PromptActive;
}

} // namespace xuoassist
