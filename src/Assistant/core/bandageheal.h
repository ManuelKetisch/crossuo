// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace xuoassist {

// Game-thread entry points. The agent remains off until started each session.
bool BandageHeal_Start();
void BandageHeal_Stop();
bool BandageHeal_Running();
const char *BandageHeal_Status();
void BandageHeal_Tick();
void BandageHeal_Draw();
void BandageHeal_ResetSession();
void BandageHeal_OnGameSend(const uint8_t *data, std::size_t size);
void BandageHeal_OnCliloc(uint32_t message);
void BandageHeal_OnBuff(uint16_t icon, bool added, uint16_t seconds);

} // namespace xuoassist
