// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>
#include <common/str.h>

namespace xuoassist {

void DamageMeter_OnDamage(uint32_t serial, const astr_t &name, int amount);
void DamageMeter_Draw();
void DamageMeter_Reset();
bool DamageMeter_Start();
void DamageMeter_Pause();
void DamageMeter_Resume();
void DamageMeter_Stop();
bool DamageMeter_Running();
uint64_t DamageMeter_GetDamage(uint32_t serial);

} // namespace xuoassist
