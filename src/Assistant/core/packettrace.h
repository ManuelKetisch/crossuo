// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <common/fs.h>

namespace xuoassist {

fs_path PacketTrace_Directory();
bool PacketTrace_Start();
void PacketTrace_Stop();
bool PacketTrace_Enabled();
void PacketTrace_Record(bool outgoing, const uint8_t *bytes, size_t size);

} // namespace xuoassist
