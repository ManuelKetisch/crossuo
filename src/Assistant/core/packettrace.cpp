// SPDX-License-Identifier: AGPL-3.0-or-later

#include "packettrace.h"

#include <cstdio>
#include <ctime>

#include "Globals.h"
#include "../config/assistantconfig.h"

namespace xuoassist {
namespace {

FILE *s_trace = nullptr;

bool omitPayload(uint8_t id)
{
    switch (id)
    {
        case 0x03: // speech may include server account commands
        case 0x1C: // incoming speech
        case 0x80: // account login
        case 0x83: // legacy deletion password
        case 0x84: // legacy account password change
        case 0x91: // game server login
        case 0x9A: // ASCII prompt responses
        case 0xAD: // outgoing Unicode speech
        case 0xAE: // incoming Unicode speech
        case 0xB2: // chat
        case 0xB3:
        case 0xC1: // localized speech and arguments
        case 0xC2: // Unicode prompt responses
        case 0xCC: // localized speech with affixes
            return true;
        default:
            return false;
    }
}

} // namespace

fs_path PacketTrace_Directory()
{
    return fs_path_join(CAssistantConfig::GetRootPath(), "PacketCaptures");
}

bool PacketTrace_Start()
{
    if (s_trace)
    {
        return true;
    }

    if (g_GameState != GS_GAME && g_GameState != GS_GAME_BLOCKED)
    {
        return false;
    }

    const auto directory = PacketTrace_Directory();
    if (!fs_path_is_dir(directory) && !fs_path_create(directory))
    {
        return false;
    }

    const auto stem = std::to_string(std::time(nullptr));
    auto path = fs_path_join(directory, stem + ".log");
    for (unsigned suffix = 1; fs_path_exists(path); ++suffix)
    {
        path = fs_path_join(directory, stem + "-" + std::to_string(suffix) + ".log");
    }

    s_trace = fs_open(path, FS_WRITE);
    return s_trace != nullptr;
}

void PacketTrace_Stop()
{
    if (s_trace)
    {
        std::fclose(s_trace);
        s_trace = nullptr;
    }
}

bool PacketTrace_Enabled()
{
    return s_trace != nullptr;
}

void PacketTrace_Record(bool outgoing, const uint8_t *bytes, size_t size)
{
    if (!s_trace || size == 0 || !bytes ||
        (g_GameState != GS_GAME && g_GameState != GS_GAME_BLOCKED))
    {
        return;
    }

    std::fprintf(s_trace, "%u %s %02X %zu", g_Ticks, outgoing ? "SEND" : "RECV", bytes[0], size);
    if (!omitPayload(bytes[0]))
    {
        for (size_t i = 1; i < size; ++i)
        {
            std::fprintf(s_trace, " %02X", bytes[i]);
        }
    }
    std::fputc('\n', s_trace);
    if (std::ferror(s_trace))
    {
        PacketTrace_Stop();
    }
}

} // namespace xuoassist
