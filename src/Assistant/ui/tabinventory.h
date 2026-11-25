// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstdint>

namespace xuoassist {

void TabInventory_Draw();
void TabInventory_CancelPendingTarget();
void TabInventory_TargetReturned(uint32_t serial);

} // namespace xuoassist
