// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace xuoassist {

// --- Target selector rules (pure logic, no engine dependencies) --------------

constexpr int TargetPresetMaxRange = 30;
constexpr uint8_t TargetAllNotorieties = 0x7F;
constexpr size_t TargetMaxBodies = 32;

enum class TargetFlagFilter : int
{
    Any = 0,
    Only = 1,
    Exclude = 2,
};

enum class TargetFriendFilter : int
{
    Any = 0,
    OnlyFriends = 1,
    ExcludeFriends = 2,
};

enum class TargetSelectorMode : int
{
    Nearest = 0,
    Weakest = 1,
    Farthest = 2,
    Strongest = 3,
    Random = 4,
    Next = 5,
};

struct TargetFilterRule
{
    std::string MobileName;
    std::vector<uint16_t> Bodies;
    int Hue = -1;
    int MinDistance = 0;
    int MaxDistance = 12;
    int Notorieties = 0; // 7-bit mask (Innocent..Invulnerable); 0 = any
    TargetSelectorMode Selector = TargetSelectorMode::Nearest;
    TargetFlagFilter Poisoned = TargetFlagFilter::Any;
    TargetFlagFilter Blessed = TargetFlagFilter::Any;
    TargetFlagFilter Paralyzed = TargetFlagFilter::Any;
    TargetFlagFilter WarMode = TargetFlagFilter::Any;
    TargetFriendFilter Friends = TargetFriendFilter::Any;
    bool RespectIgnoreList = true;
};

struct TargetCandidate
{
    uint32_t Serial = 0;
    std::string Name;
    uint16_t Body = 0;
    uint16_t Hue = 0;
    uint8_t Notoriety = 0; // 1..7 (NT_INNOCENT..NT_INVULNERABLE)
    int Distance = 0;
    uint16_t Hits = 0;
    uint16_t MaxHits = 0;
    bool Dead = false;
    bool Hidden = false;
    bool Ignored = false;
    bool Friend = false;
    bool Poisoned = false;
    bool Blessed = false;
    bool Paralyzed = false;
    bool WarMode = false;
};

struct TargetSelectionContext
{
    bool GlobalProtectFriends = false; // Harmful cursor + Protect friends enabled
    bool GlobalSkipFriends = false;    // Global Skip friends setting
    uint32_t LastCycledSerial = 0;     // For TargetSelectorMode::Next
    uint32_t RandomSeed = 0;           // For TargetSelectorMode::Random
};

inline std::string TargetRules_ToLower(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text)
    {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

inline std::string TargetRules_Trim(const std::string &text)
{
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])))
    {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }
    return text.substr(start, end - start);
}

inline TargetFlagFilter TargetRules_ClampFlag(int value)
{
    if (value == static_cast<int>(TargetFlagFilter::Only))
    {
        return TargetFlagFilter::Only;
    }
    if (value == static_cast<int>(TargetFlagFilter::Exclude))
    {
        return TargetFlagFilter::Exclude;
    }
    return TargetFlagFilter::Any;
}

inline TargetFriendFilter TargetRules_ClampFriendFilter(int value)
{
    if (value == static_cast<int>(TargetFriendFilter::OnlyFriends))
    {
        return TargetFriendFilter::OnlyFriends;
    }
    if (value == static_cast<int>(TargetFriendFilter::ExcludeFriends))
    {
        return TargetFriendFilter::ExcludeFriends;
    }
    return TargetFriendFilter::Any;
}

inline TargetSelectorMode TargetRules_ClampSelector(int value)
{
    if (value >= static_cast<int>(TargetSelectorMode::Nearest) &&
        value <= static_cast<int>(TargetSelectorMode::Next))
    {
        return static_cast<TargetSelectorMode>(value);
    }
    return TargetSelectorMode::Nearest;
}

inline bool TargetRules_ParseUInt32(const std::string &raw, uint32_t &out)
{
    const std::string text = TargetRules_Trim(raw);
    if (text.empty())
    {
        return false;
    }
    int base = 10;
    size_t pos = 0;
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        pos = 2;
    }
    if (pos >= text.size())
    {
        return false;
    }
    uint64_t value = 0;
    for (size_t i = pos; i < text.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        int digit = -1;
        if (ch >= '0' && ch <= '9')
        {
            digit = ch - '0';
        }
        else if (base == 16 && ch >= 'a' && ch <= 'f')
        {
            digit = 10 + (ch - 'a');
        }
        else if (base == 16 && ch >= 'A' && ch <= 'F')
        {
            digit = 10 + (ch - 'A');
        }
        if (digit < 0 || digit >= base)
        {
            return false;
        }
        value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
        if (value > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }
    }
    out = static_cast<uint32_t>(value);
    return true;
}

inline bool TargetRules_ParseBodyList(const std::string &text, std::vector<uint16_t> &out)
{
    std::vector<uint16_t> parsed;
    size_t i = 0;
    while (i < text.size())
    {
        while (i < text.size() && (text[i] == ',' || text[i] == ';' || std::isspace(static_cast<unsigned char>(text[i]))))
        {
            ++i;
        }
        if (i >= text.size())
        {
            break;
        }
        size_t start = i;
        while (i < text.size() && text[i] != ',' && text[i] != ';' && !std::isspace(static_cast<unsigned char>(text[i])))
        {
            ++i;
        }
        const std::string token = text.substr(start, i - start);
        if (token == "-1")
        {
            continue;
        }
        uint32_t value = 0;
        if (!TargetRules_ParseUInt32(token, value) || value > 0xFFFFu)
        {
            return false;
        }
        const uint16_t body = static_cast<uint16_t>(value);
        if (std::find(parsed.begin(), parsed.end(), body) == parsed.end())
        {
            if (parsed.size() >= TargetMaxBodies)
            {
                return false;
            }
            parsed.push_back(body);
        }
    }
    out.swap(parsed);
    return true;
}

inline std::string TargetRules_FormatBodyList(const std::vector<uint16_t> &bodies)
{
    std::string out;
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        if (i != 0)
        {
            out += ", ";
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%04X", bodies[i]);
        out += buf;
    }
    return out;
}

inline void TargetRules_Normalize(TargetFilterRule &rule)
{
    rule.Hue = std::max(-1, std::min(0xFFFF, rule.Hue));
    rule.MaxDistance = std::max(0, std::min(TargetPresetMaxRange, rule.MaxDistance));
    rule.MinDistance = std::max(0, std::min(rule.MaxDistance, rule.MinDistance));
    rule.Notorieties &= TargetAllNotorieties;
    rule.Selector = TargetRules_ClampSelector(static_cast<int>(rule.Selector));
    rule.Poisoned = TargetRules_ClampFlag(static_cast<int>(rule.Poisoned));
    rule.Blessed = TargetRules_ClampFlag(static_cast<int>(rule.Blessed));
    rule.Paralyzed = TargetRules_ClampFlag(static_cast<int>(rule.Paralyzed));
    rule.WarMode = TargetRules_ClampFlag(static_cast<int>(rule.WarMode));
    rule.Friends = TargetRules_ClampFriendFilter(static_cast<int>(rule.Friends));
    if (rule.Bodies.size() > TargetMaxBodies)
    {
        rule.Bodies.resize(TargetMaxBodies);
    }
}

inline bool TargetRules_MatchesFlag(TargetFlagFilter filter, bool actual)
{
    if (filter == TargetFlagFilter::Only)
    {
        return actual;
    }
    if (filter == TargetFlagFilter::Exclude)
    {
        return !actual;
    }
    return true;
}

inline bool TargetRules_MatchesNotoriety(int mask, uint8_t notoriety)
{
    return mask == 0 || (notoriety >= 1 && notoriety <= 7 && (mask & (1 << (notoriety - 1))) != 0);
}

inline bool TargetRules_MatchesCandidate(
    const TargetFilterRule &rule,
    const TargetCandidate &candidate,
    const TargetSelectionContext &ctx)
{
    if (candidate.Serial == 0 || candidate.Dead || candidate.Hidden)
    {
        return false;
    }
    if (candidate.MaxHits != 0 && candidate.Hits == 0)
    {
        return false;
    }
    if (rule.RespectIgnoreList && candidate.Ignored)
    {
        return false;
    }
    // Safety invariant: harmful cursor with ProtectFriends always skips friends.
    if (ctx.GlobalProtectFriends && candidate.Friend)
    {
        return false;
    }
    if (rule.Friends == TargetFriendFilter::OnlyFriends)
    {
        if (!candidate.Friend)
        {
            return false;
        }
    }
    else if (rule.Friends == TargetFriendFilter::ExcludeFriends)
    {
        if (candidate.Friend)
        {
            return false;
        }
    }
    else if (ctx.GlobalSkipFriends && candidate.Friend)
    {
        return false;
    }

    if ((rule.Selector == TargetSelectorMode::Weakest || rule.Selector == TargetSelectorMode::Strongest) &&
        candidate.MaxHits == 0)
    {
        return false;
    }
    if (!rule.Bodies.empty() &&
        std::find(rule.Bodies.begin(), rule.Bodies.end(), candidate.Body) == rule.Bodies.end())
    {
        return false;
    }
    if (rule.Hue >= 0 && candidate.Hue != static_cast<uint16_t>(rule.Hue))
    {
        return false;
    }
    if (!TargetRules_MatchesNotoriety(rule.Notorieties, candidate.Notoriety))
    {
        return false;
    }
    if (candidate.Distance < rule.MinDistance || candidate.Distance > rule.MaxDistance)
    {
        return false;
    }
    if (!TargetRules_MatchesFlag(rule.Poisoned, candidate.Poisoned) ||
        !TargetRules_MatchesFlag(rule.Blessed, candidate.Blessed) ||
        !TargetRules_MatchesFlag(rule.Paralyzed, candidate.Paralyzed) ||
        !TargetRules_MatchesFlag(rule.WarMode, candidate.WarMode))
    {
        return false;
    }
    if (!rule.MobileName.empty())
    {
        const std::string needle = TargetRules_ToLower(rule.MobileName);
        if (TargetRules_ToLower(candidate.Name).find(needle) == std::string::npos)
        {
            return false;
        }
    }
    return true;
}

// Returns the index in `candidates` of the chosen target, or -1 if none matched.
inline int TargetRules_SelectCandidate(
    const TargetFilterRule &rule,
    const std::vector<TargetCandidate> &candidates,
    const TargetSelectionContext &ctx)
{
    std::vector<int> matching;
    matching.reserve(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (TargetRules_MatchesCandidate(rule, candidates[i], ctx))
        {
            matching.push_back(static_cast<int>(i));
        }
    }
    if (matching.empty())
    {
        return -1;
    }

    if (rule.Selector == TargetSelectorMode::Random)
    {
        // Deterministic ordering before indexing so platform/container order doesn't bias.
        std::sort(matching.begin(), matching.end(), [&](int a, int b) {
            return candidates[a].Serial < candidates[b].Serial;
        });
        return matching[ctx.RandomSeed % matching.size()];
    }

    if (rule.Selector == TargetSelectorMode::Next)
    {
        std::sort(matching.begin(), matching.end(), [&](int a, int b) {
            return candidates[a].Serial < candidates[b].Serial;
        });
        for (int idx : matching)
        {
            if (candidates[idx].Serial > ctx.LastCycledSerial)
            {
                return idx;
            }
        }
        return matching.front();
    }

    int best = matching.front();
    for (size_t k = 1; k < matching.size(); ++k)
    {
        const int idx = matching[k];
        const TargetCandidate &cand = candidates[idx];
        const TargetCandidate &cur = candidates[best];
        bool better = false;
        switch (rule.Selector)
        {
            case TargetSelectorMode::Nearest:
                better = cand.Distance < cur.Distance ||
                    (cand.Distance == cur.Distance && cand.Serial < cur.Serial);
                break;
            case TargetSelectorMode::Farthest:
                better = cand.Distance > cur.Distance ||
                    (cand.Distance == cur.Distance && cand.Serial < cur.Serial);
                break;
            case TargetSelectorMode::Weakest:
            {
                const uint64_t lhs = uint64_t(cand.Hits) * cur.MaxHits;
                const uint64_t rhs = uint64_t(cur.Hits) * cand.MaxHits;
                better = lhs < rhs ||
                    (lhs == rhs && (cand.Distance < cur.Distance ||
                        (cand.Distance == cur.Distance && cand.Serial < cur.Serial)));
                break;
            }
            case TargetSelectorMode::Strongest:
            {
                const uint64_t lhs = uint64_t(cand.Hits) * cur.MaxHits;
                const uint64_t rhs = uint64_t(cur.Hits) * cand.MaxHits;
                better = lhs > rhs ||
                    (lhs == rhs && (cand.Distance < cur.Distance ||
                        (cand.Distance == cur.Distance && cand.Serial < cur.Serial)));
                break;
            }
            case TargetSelectorMode::Random:
            case TargetSelectorMode::Next:
                break;
        }
        if (better)
        {
            best = idx;
        }
    }
    return best;
}

// --- Enhanced Friend-Group Import & Export (pure logic) ----------------------

constexpr size_t FriendGroupMaxImportBytes = 256 * 1024;
constexpr size_t FriendGroupMaxNameLength = 63;
constexpr size_t FriendGroupMaxGroups = 32;
constexpr size_t FriendGroupMaxEntriesPerGroup = 512;

struct FriendPlayerRecord
{
    uint32_t Serial = 0;
    std::string Name;
    bool Enabled = true;
};

struct FriendGuildRecord
{
    std::string Name;
    bool Enabled = true;
};

struct FriendGroupRecord
{
    std::string Name;
    bool IncludeParty = false;
    std::vector<FriendPlayerRecord> Players;
    std::vector<FriendGuildRecord> Guilds;
};

struct FriendImportResult
{
    bool Ok = false;
    std::string Error;
    std::vector<FriendGroupRecord> Groups;
    size_t SkippedEntries = 0;
};

inline bool FriendRules_IsPrintableText(const std::string &text, size_t maxLen)
{
    if (text.size() > maxLen)
    {
        return false;
    }
    for (unsigned char c : text)
    {
        if (c < 32 || c == 127)
        {
            return false;
        }
    }
    return true;
}

inline std::string FriendRules_SanitizeName(const std::string &raw)
{
    std::string trimmed = TargetRules_Trim(raw);
    std::string out;
    out.reserve(std::min(trimmed.size(), FriendGroupMaxNameLength));
    for (unsigned char c : trimmed)
    {
        if (c >= 32 && c != 127)
        {
            out.push_back(static_cast<char>(c));
            if (out.size() >= FriendGroupMaxNameLength)
            {
                break;
            }
        }
    }
    return TargetRules_Trim(out);
}

inline std::string FriendRules_FormatSerial(uint32_t serial)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", serial);
    return buf;
}

inline bool FriendRules_ParseBool(const std::string &raw, bool &out)
{
    const std::string lower = TargetRules_ToLower(TargetRules_Trim(raw));
    if (lower == "true" || lower == "1" || lower == "yes" || lower == "on")
    {
        out = true;
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no" || lower == "off")
    {
        out = false;
        return true;
    }
    return false;
}

inline std::string FriendRules_EscapeJson(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size() + 4);
    for (unsigned char c : raw)
    {
        if (c == '\\')
        {
            out += "\\\\";
        }
        else if (c == '"')
        {
            out += "\\\"";
        }
        else if (c >= 32 && c != 127)
        {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

inline std::string FriendRules_ExportJson(const std::vector<FriendGroupRecord> &groups)
{
    std::string out = "{\n  \"format\": \"crossuo-friends\",\n  \"version\": 1,\n  \"groups\": [\n";
    for (size_t g = 0; g < groups.size(); ++g)
    {
        const auto &group = groups[g];
        out += "    {\n";
        out += "      \"Name\": \"" + FriendRules_EscapeJson(group.Name) + "\",\n";
        out += std::string("      \"IncludeParty\": ") + (group.IncludeParty ? "true" : "false") + ",\n";
        out += "      \"Players\": [";
        if (!group.Players.empty())
        {
            out += "\n";
            for (size_t p = 0; p < group.Players.size(); ++p)
            {
                const auto &player = group.Players[p];
                out += "        { \"Name\": \"" + FriendRules_EscapeJson(player.Name) +
                    "\", \"Serial\": " + std::to_string(player.Serial) +
                    ", \"Enabled\": " + (player.Enabled ? "true" : "false") + " }";
                if (p + 1 < group.Players.size())
                {
                    out += ",";
                }
                out += "\n";
            }
            out += "      ";
        }
        out += "],\n";
        out += "      \"Guilds\": [";
        if (!group.Guilds.empty())
        {
            out += "\n";
            for (size_t k = 0; k < group.Guilds.size(); ++k)
            {
                const auto &guild = group.Guilds[k];
                out += "        { \"Name\": \"" + FriendRules_EscapeJson(guild.Name) +
                    "\", \"Enabled\": " + (guild.Enabled ? "true" : "false") + " }";
                if (k + 1 < group.Guilds.size())
                {
                    out += ",";
                }
                out += "\n";
            }
            out += "      ";
        }
        out += "]\n";
        out += "    }";
        if (g + 1 < groups.size())
        {
            out += ",";
        }
        out += "\n";
    }
    out += "  ]\n}\n";
    return out;
}

inline std::string FriendRules_ExportIni(const std::vector<FriendGroupRecord> &groups)
{
    std::string out = "[Meta]\nformat=crossuo-friends\nversion=1\ngroup_count=" +
        std::to_string(groups.size()) + "\n";
    for (size_t g = 0; g < groups.size(); ++g)
    {
        const auto &group = groups[g];
        const std::string gSec = "Group" + std::to_string(g);
        out += "\n[" + gSec + "]\n";
        out += "name=" + FriendRules_SanitizeName(group.Name) + "\n";
        out += std::string("include_party=") + (group.IncludeParty ? "true" : "false") + "\n";
        out += "player_count=" + std::to_string(group.Players.size()) + "\n";
        out += "guild_count=" + std::to_string(group.Guilds.size()) + "\n";
        for (size_t p = 0; p < group.Players.size(); ++p)
        {
            const auto &player = group.Players[p];
            out += "[" + gSec + "Player" + std::to_string(p) + "]\n";
            out += "name=" + FriendRules_SanitizeName(player.Name) + "\n";
            out += "serial=" + FriendRules_FormatSerial(player.Serial) + "\n";
            out += std::string("enabled=") + (player.Enabled ? "true" : "false") + "\n";
        }
        for (size_t k = 0; k < group.Guilds.size(); ++k)
        {
            const auto &guild = group.Guilds[k];
            out += "[" + gSec + "Guild" + std::to_string(k) + "]\n";
            out += "name=" + FriendRules_SanitizeName(guild.Name) + "\n";
            out += std::string("enabled=") + (guild.Enabled ? "true" : "false") + "\n";
        }
    }
    return out;
}

namespace detail {

struct MiniJsonValue
{
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type Kind = Type::Null;
    bool BoolVal = false;
    double NumVal = 0.0;
    std::string StrVal;
    std::vector<MiniJsonValue> ArrVal;
    std::vector<std::pair<std::string, MiniJsonValue>> ObjVal;

    const MiniJsonValue *Find(const char *key) const
    {
        if (Kind != Type::Object)
        {
            return nullptr;
        }
        const std::string lowerKey = TargetRules_ToLower(key);
        for (const auto &kv : ObjVal)
        {
            if (TargetRules_ToLower(kv.first) == lowerKey)
            {
                return &kv.second;
            }
        }
        return nullptr;
    }
};

class MiniJsonParser
{
    const std::string &m_Src;
    size_t m_Pos = 0;
    static constexpr int MaxDepth = 16;

    void SkipWs()
    {
        while (m_Pos < m_Src.size() && std::isspace(static_cast<unsigned char>(m_Src[m_Pos])))
        {
            ++m_Pos;
        }
    }

    bool ParseString(std::string &out)
    {
        if (m_Pos >= m_Src.size() || m_Src[m_Pos] != '"')
        {
            return false;
        }
        ++m_Pos;
        out.clear();
        while (m_Pos < m_Src.size())
        {
            char c = m_Src[m_Pos++];
            if (c == '"')
            {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20)
            {
                return false;
            }
            if (c == '\\')
            {
                if (m_Pos >= m_Src.size())
                {
                    return false;
                }
                char esc = m_Src[m_Pos++];
                switch (esc)
                {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                    {
                        if (m_Pos + 4 > m_Src.size())
                        {
                            return false;
                        }
                        uint32_t cp = 0;
                        for (int k = 0; k < 4; ++k)
                        {
                            char h = m_Src[m_Pos++];
                            int d = -1;
                            if (h >= '0' && h <= '9') d = h - '0';
                            else if (h >= 'a' && h <= 'f') d = 10 + (h - 'a');
                            else if (h >= 'A' && h <= 'F') d = 10 + (h - 'A');
                            if (d < 0)
                            {
                                return false;
                            }
                            cp = (cp << 4) | static_cast<uint32_t>(d);
                        }
                        if (cp >= 32 && cp < 127)
                        {
                            out.push_back(static_cast<char>(cp));
                        }
                        break;
                    }
                    default:
                        return false;
                }
            }
            else
            {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseNumber(double &out)
    {
        const size_t start = m_Pos;
        if (m_Pos < m_Src.size() && m_Src[m_Pos] == '-')
        {
            ++m_Pos;
        }
        if (m_Pos >= m_Src.size() || !std::isdigit(static_cast<unsigned char>(m_Src[m_Pos])))
        {
            return false;
        }
        while (m_Pos < m_Src.size() && std::isdigit(static_cast<unsigned char>(m_Src[m_Pos])))
        {
            ++m_Pos;
        }
        if (m_Pos < m_Src.size() && m_Src[m_Pos] == '.')
        {
            ++m_Pos;
            if (m_Pos >= m_Src.size() || !std::isdigit(static_cast<unsigned char>(m_Src[m_Pos])))
            {
                return false;
            }
            while (m_Pos < m_Src.size() && std::isdigit(static_cast<unsigned char>(m_Src[m_Pos])))
            {
                ++m_Pos;
            }
        }
        if (m_Pos < m_Src.size() && (m_Src[m_Pos] == 'e' || m_Src[m_Pos] == 'E'))
        {
            ++m_Pos;
            if (m_Pos < m_Src.size() && (m_Src[m_Pos] == '+' || m_Src[m_Pos] == '-'))
            {
                ++m_Pos;
            }
            if (m_Pos >= m_Src.size() || !std::isdigit(static_cast<unsigned char>(m_Src[m_Pos])))
            {
                return false;
            }
            while (m_Pos < m_Src.size() && std::isdigit(static_cast<unsigned char>(m_Src[m_Pos])))
            {
                ++m_Pos;
            }
        }
        char *endPtr = nullptr;
        const std::string token = m_Src.substr(start, m_Pos - start);
        out = std::strtod(token.c_str(), &endPtr);
        return endPtr != nullptr && *endPtr == '\0';
    }

    bool ParseValue(MiniJsonValue &out, int depth)
    {
        if (depth > MaxDepth)
        {
            return false;
        }
        SkipWs();
        if (m_Pos >= m_Src.size())
        {
            return false;
        }
        const char c = m_Src[m_Pos];
        if (c == '"')
        {
            out.Kind = MiniJsonValue::Type::String;
            return ParseString(out.StrVal);
        }
        if (c == '{')
        {
            ++m_Pos;
            out.Kind = MiniJsonValue::Type::Object;
            SkipWs();
            if (m_Pos < m_Src.size() && m_Src[m_Pos] == '}')
            {
                ++m_Pos;
                return true;
            }
            while (true)
            {
                SkipWs();
                std::string key;
                if (!ParseString(key))
                {
                    return false;
                }
                SkipWs();
                if (m_Pos >= m_Src.size() || m_Src[m_Pos] != ':')
                {
                    return false;
                }
                ++m_Pos;
                MiniJsonValue val;
                if (!ParseValue(val, depth + 1))
                {
                    return false;
                }
                out.ObjVal.emplace_back(std::move(key), std::move(val));
                SkipWs();
                if (m_Pos >= m_Src.size())
                {
                    return false;
                }
                if (m_Src[m_Pos] == '}')
                {
                    ++m_Pos;
                    return true;
                }
                if (m_Src[m_Pos] != ',')
                {
                    return false;
                }
                ++m_Pos;
            }
        }
        if (c == '[')
        {
            ++m_Pos;
            out.Kind = MiniJsonValue::Type::Array;
            SkipWs();
            if (m_Pos < m_Src.size() && m_Src[m_Pos] == ']')
            {
                ++m_Pos;
                return true;
            }
            while (true)
            {
                MiniJsonValue item;
                if (!ParseValue(item, depth + 1))
                {
                    return false;
                }
                out.ArrVal.push_back(std::move(item));
                SkipWs();
                if (m_Pos >= m_Src.size())
                {
                    return false;
                }
                if (m_Src[m_Pos] == ']')
                {
                    ++m_Pos;
                    return true;
                }
                if (m_Src[m_Pos] != ',')
                {
                    return false;
                }
                ++m_Pos;
            }
        }
        if (m_Src.compare(m_Pos, 4, "true") == 0)
        {
            m_Pos += 4;
            out.Kind = MiniJsonValue::Type::Bool;
            out.BoolVal = true;
            return true;
        }
        if (m_Src.compare(m_Pos, 5, "false") == 0)
        {
            m_Pos += 5;
            out.Kind = MiniJsonValue::Type::Bool;
            out.BoolVal = false;
            return true;
        }
        if (m_Src.compare(m_Pos, 4, "null") == 0)
        {
            m_Pos += 4;
            out.Kind = MiniJsonValue::Type::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
        {
            out.Kind = MiniJsonValue::Type::Number;
            return ParseNumber(out.NumVal);
        }
        return false;
    }

public:
    explicit MiniJsonParser(const std::string &src) : m_Src(src) {}

    bool Parse(MiniJsonValue &out)
    {
        if (!ParseValue(out, 0))
        {
            return false;
        }
        SkipWs();
        return m_Pos == m_Src.size();
    }
};

inline FriendGroupRecord &FindOrAddGroup(std::vector<FriendGroupRecord> &groups, const std::string &rawName)
{
    std::string name = FriendRules_SanitizeName(rawName);
    if (name.empty())
    {
        name = "Imported";
    }
    const std::string key = TargetRules_ToLower(name);
    for (auto &g : groups)
    {
        if (TargetRules_ToLower(g.Name) == key)
        {
            return g;
        }
    }
    groups.push_back({ name, false, {}, {} });
    return groups.back();
}

inline bool AddPlayerUnique(FriendGroupRecord &group, const FriendPlayerRecord &player)
{
    if (player.Serial == 0 || group.Players.size() >= FriendGroupMaxEntriesPerGroup)
    {
        return false;
    }
    for (auto &existing : group.Players)
    {
        if (existing.Serial == player.Serial)
        {
            if (!player.Name.empty())
            {
                existing.Name = player.Name;
            }
            existing.Enabled = player.Enabled;
            return true;
        }
    }
    group.Players.push_back(player);
    return true;
}

inline bool AddGuildUnique(FriendGroupRecord &group, const FriendGuildRecord &guild)
{
    if (guild.Name.empty() || group.Guilds.size() >= FriendGroupMaxEntriesPerGroup)
    {
        return false;
    }
    const std::string key = TargetRules_ToLower(guild.Name);
    for (auto &existing : group.Guilds)
    {
        if (TargetRules_ToLower(existing.Name) == key)
        {
            existing.Enabled = guild.Enabled;
            return true;
        }
    }
    group.Guilds.push_back(guild);
    return true;
}

inline bool ExtractSerialFromJson(const MiniJsonValue &val, uint32_t &out)
{
    if (val.Kind == MiniJsonValue::Type::Number)
    {
        if (val.NumVal >= 1.0 && val.NumVal <= static_cast<double>(std::numeric_limits<uint32_t>::max()))
        {
            const uint32_t s = static_cast<uint32_t>(val.NumVal);
            if (static_cast<double>(s) == val.NumVal)
            {
                out = s;
                return true;
            }
        }
        return false;
    }
    if (val.Kind == MiniJsonValue::Type::String)
    {
        return TargetRules_ParseUInt32(val.StrVal, out) && out != 0;
    }
    return false;
}

inline bool ExtractBoolFromJson(const MiniJsonValue &val, bool &out)
{
    if (val.Kind == MiniJsonValue::Type::Bool)
    {
        out = val.BoolVal;
        return true;
    }
    if (val.Kind == MiniJsonValue::Type::Number)
    {
        out = (val.NumVal != 0.0);
        return true;
    }
    if (val.Kind == MiniJsonValue::Type::String)
    {
        return FriendRules_ParseBool(val.StrVal, out);
    }
    return false;
}

inline bool ParseJsonGroupObject(
    const MiniJsonValue &obj,
    const std::string &fallbackName,
    std::vector<FriendGroupRecord> &groups,
    size_t &skipped)
{
    if (obj.Kind != MiniJsonValue::Type::Object)
    {
        return false;
    }
    std::string groupName = fallbackName;
    const auto *n = obj.Find("Name");
    if (n != nullptr && n->Kind == MiniJsonValue::Type::String)
    {
        groupName = n->StrVal;
    }
    if (groups.size() >= FriendGroupMaxGroups)
    {
        return false;
    }
    auto &group = FindOrAddGroup(groups, groupName);
    const auto *ip = obj.Find("IncludeParty");
    if (ip != nullptr)
    {
        bool inc = false;
        if (ExtractBoolFromJson(*ip, inc))
        {
            group.IncludeParty = inc;
        }
    }
    const auto *players = obj.Find("Players");
    if (players != nullptr && players->Kind == MiniJsonValue::Type::Array)
    {
        for (const auto &row : players->ArrVal)
        {
            if (row.Kind != MiniJsonValue::Type::Object)
            {
                ++skipped;
                continue;
            }
            const auto *serVal = row.Find("Serial");
            uint32_t serial = 0;
            if (serVal == nullptr || !ExtractSerialFromJson(*serVal, serial))
            {
                ++skipped;
                continue;
            }
            std::string name;
            const auto *nameVal = row.Find("Name");
            if (nameVal != nullptr && nameVal->Kind == MiniJsonValue::Type::String)
            {
                name = FriendRules_SanitizeName(nameVal->StrVal);
            }
            if (name.empty())
            {
                name = FriendRules_FormatSerial(serial);
            }
            bool enabled = true;
            const auto *enVal = row.Find("Enabled");
            if (enVal != nullptr)
            {
                ExtractBoolFromJson(*enVal, enabled);
            }
            if (!AddPlayerUnique(group, { serial, name, enabled }))
            {
                ++skipped;
            }
        }
    }
    const auto *guilds = obj.Find("Guilds");
    if (guilds != nullptr && guilds->Kind == MiniJsonValue::Type::Array)
    {
        for (const auto &row : guilds->ArrVal)
        {
            if (row.Kind != MiniJsonValue::Type::Object)
            {
                ++skipped;
                continue;
            }
            const auto *nameVal = row.Find("Name");
            if (nameVal == nullptr || nameVal->Kind != MiniJsonValue::Type::String)
            {
                ++skipped;
                continue;
            }
            std::string gName = FriendRules_SanitizeName(nameVal->StrVal);
            if (gName.empty())
            {
                ++skipped;
                continue;
            }
            bool enabled = true;
            const auto *enVal = row.Find("Enabled");
            if (enVal != nullptr)
            {
                ExtractBoolFromJson(*enVal, enabled);
            }
            if (!AddGuildUnique(group, { gName, enabled }))
            {
                ++skipped;
            }
        }
    }
    return true;
}

inline FriendImportResult ParseFriendJson(const std::string &input, const std::string &defaultGroupName)
{
    FriendImportResult result;
    MiniJsonValue root;
    MiniJsonParser parser(input);
    if (!parser.Parse(root))
    {
        result.Error = "Invalid JSON syntax.";
        return result;
    }

    if (root.Kind == MiniJsonValue::Type::Object)
    {
        const auto *groupsArr = root.Find("groups");
        if (groupsArr != nullptr && groupsArr->Kind == MiniJsonValue::Type::Array)
        {
            for (const auto &g : groupsArr->ArrVal)
            {
                if (!ParseJsonGroupObject(g, defaultGroupName, result.Groups, result.SkippedEntries))
                {
                    ++result.SkippedEntries;
                }
            }
        }
        else if (root.Find("Players") != nullptr || root.Find("Guilds") != nullptr)
        {
            ParseJsonGroupObject(root, defaultGroupName, result.Groups, result.SkippedEntries);
        }
        else
        {
            result.Error = "JSON object does not contain friend groups or players.";
            return result;
        }
    }
    else if (root.Kind == MiniJsonValue::Type::Array)
    {
        // Supports Razor Enhanced DataTable arrays (FRIENDS_PLAYERS, FRIENDS_GUILDS, FRIENDS_LISTS)
        // as well as plain arrays of player objects or group objects.
        for (const auto &row : root.ArrVal)
        {
            if (row.Kind != MiniJsonValue::Type::Object)
            {
                ++result.SkippedEntries;
                continue;
            }
            if (row.Find("Players") != nullptr || row.Find("Guilds") != nullptr)
            {
                if (!ParseJsonGroupObject(row, defaultGroupName, result.Groups, result.SkippedEntries))
                {
                    ++result.SkippedEntries;
                }
                continue;
            }
            // Razor Enhanced FRIENDS_PLAYERS row: { "List": "...", "Name": "...", "Serial": 123, "Enabled": true }
            const auto *serVal = row.Find("Serial");
            if (serVal != nullptr)
            {
                uint32_t serial = 0;
                if (!ExtractSerialFromJson(*serVal, serial))
                {
                    ++result.SkippedEntries;
                    continue;
                }
                std::string listName = defaultGroupName;
                const auto *listVal = row.Find("List");
                if (listVal != nullptr && listVal->Kind == MiniJsonValue::Type::String)
                {
                    listName = listVal->StrVal;
                }
                if (result.Groups.size() >= FriendGroupMaxGroups)
                {
                    ++result.SkippedEntries;
                    continue;
                }
                auto &group = FindOrAddGroup(result.Groups, listName);
                std::string name;
                const auto *nameVal = row.Find("Name");
                if (nameVal != nullptr && nameVal->Kind == MiniJsonValue::Type::String)
                {
                    name = FriendRules_SanitizeName(nameVal->StrVal);
                }
                if (name.empty())
                {
                    name = FriendRules_FormatSerial(serial);
                }
                bool enabled = true;
                const auto *enVal = row.Find("Enabled");
                if (enVal != nullptr)
                {
                    ExtractBoolFromJson(*enVal, enabled);
                }
                if (!AddPlayerUnique(group, { serial, name, enabled }))
                {
                    ++result.SkippedEntries;
                }
                continue;
            }
            // Razor Enhanced FRIENDS_LISTS or FRIENDS_GUILDS row
            const auto *nameVal = row.Find("Name");
            if (nameVal != nullptr && nameVal->Kind == MiniJsonValue::Type::String)
            {
                const auto *listVal = row.Find("List");
                if (listVal != nullptr && listVal->Kind == MiniJsonValue::Type::String)
                {
                    // FRIENDS_GUILDS row
                    if (result.Groups.size() >= FriendGroupMaxGroups)
                    {
                        ++result.SkippedEntries;
                        continue;
                    }
                    auto &group = FindOrAddGroup(result.Groups, listVal->StrVal);
                    bool enabled = true;
                    const auto *enVal = row.Find("Enabled");
                    if (enVal != nullptr)
                    {
                        ExtractBoolFromJson(*enVal, enabled);
                    }
                    const std::string gName = FriendRules_SanitizeName(nameVal->StrVal);
                    if (gName.empty() || !AddGuildUnique(group, { gName, enabled }))
                    {
                        ++result.SkippedEntries;
                    }
                }
                else
                {
                    // FRIENDS_LISTS row
                    if (result.Groups.size() >= FriendGroupMaxGroups)
                    {
                        ++result.SkippedEntries;
                        continue;
                    }
                    auto &group = FindOrAddGroup(result.Groups, nameVal->StrVal);
                    const auto *ip = row.Find("IncludeParty");
                    if (ip != nullptr)
                    {
                        bool inc = false;
                        if (ExtractBoolFromJson(*ip, inc))
                        {
                            group.IncludeParty = inc;
                        }
                    }
                }
                continue;
            }
            ++result.SkippedEntries;
        }
    }
    else
    {
        result.Error = "Expected a JSON object or array.";
        return result;
    }

    if (result.Groups.empty())
    {
        result.Error = "No valid friend groups or players found.";
        return result;
    }
    result.Ok = true;
    return result;
}

inline FriendImportResult ParseFriendIniOrText(const std::string &input, const std::string &defaultGroupName)
{
    FriendImportResult result;
    std::vector<std::string> lines;
    {
        size_t start = 0;
        while (start <= input.size())
        {
            size_t nl = input.find('\n', start);
            std::string line = (nl == std::string::npos) ? input.substr(start) : input.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines.push_back(TargetRules_Trim(line));
            if (nl == std::string::npos)
            {
                break;
            }
            start = nl + 1;
        }
    }

    // Detect INI format if any line starts with '[' and ends with ']'.
    bool isIni = false;
    for (const auto &line : lines)
    {
        if (line.size() >= 3 && line.front() == '[' && line.back() == ']')
        {
            isIni = true;
            break;
        }
    }

    if (isIni)
    {
        struct SectionKV
        {
            std::string Name;
            std::vector<std::pair<std::string, std::string>> Pairs;
            std::string Get(const char *k) const
            {
                const std::string lk = TargetRules_ToLower(k);
                for (const auto &p : Pairs)
                {
                    if (TargetRules_ToLower(p.first) == lk)
                    {
                        return p.second;
                    }
                }
                return {};
            }
        };
        std::vector<SectionKV> sections;
        for (const auto &line : lines)
        {
            if (line.empty() || line[0] == '#' || line[0] == ';')
            {
                continue;
            }
            if (line.front() == '[' && line.back() == ']')
            {
                sections.push_back({ TargetRules_Trim(line.substr(1, line.size() - 2)), {} });
                continue;
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos || sections.empty())
            {
                result.Error = "Malformed INI line.";
                return result;
            }
            sections.back().Pairs.emplace_back(
                TargetRules_Trim(line.substr(0, eq)),
                TargetRules_Trim(line.substr(eq + 1)));
        }

        // Map section prefix "Group<N>" -> group index in result.Groups
        std::vector<std::pair<std::string, size_t>> groupSections;
        for (const auto &sec : sections)
        {
            const std::string lower = TargetRules_ToLower(sec.Name);
            if (lower.rfind("group", 0) == 0 &&
                lower.find("player") == std::string::npos &&
                lower.find("guild") == std::string::npos)
            {
                if (result.Groups.size() >= FriendGroupMaxGroups)
                {
                    ++result.SkippedEntries;
                    continue;
                }
                std::string gName = FriendRules_SanitizeName(sec.Get("name"));
                if (gName.empty())
                {
                    gName = defaultGroupName;
                }
                auto &group = FindOrAddGroup(result.Groups, gName);
                bool inc = false;
                if (FriendRules_ParseBool(sec.Get("include_party"), inc))
                {
                    group.IncludeParty = inc;
                }
                const size_t idx = static_cast<size_t>(&group - result.Groups.data());
                groupSections.emplace_back(lower, idx);
            }
        }

        for (const auto &sec : sections)
        {
            const std::string lower = TargetRules_ToLower(sec.Name);
            const size_t pPos = lower.find("player");
            const size_t gPos = lower.find("guild");
            if (lower.rfind("group", 0) == 0 && pPos != std::string::npos)
            {
                const std::string prefix = lower.substr(0, pPos);
                size_t groupIdx = std::numeric_limits<size_t>::max();
                for (const auto &gs : groupSections)
                {
                    if (gs.first == prefix)
                    {
                        groupIdx = gs.second;
                        break;
                    }
                }
                if (groupIdx >= result.Groups.size())
                {
                    ++result.SkippedEntries;
                    continue;
                }
                uint32_t serial = 0;
                const std::string serialStr = sec.Get("serial").empty() ? sec.Get("id") : sec.Get("serial");
                if (!TargetRules_ParseUInt32(serialStr, serial) || serial == 0)
                {
                    ++result.SkippedEntries;
                    continue;
                }
                std::string name = FriendRules_SanitizeName(sec.Get("name"));
                if (name.empty())
                {
                    name = FriendRules_FormatSerial(serial);
                }
                bool enabled = true;
                FriendRules_ParseBool(sec.Get("enabled"), enabled);
                if (!AddPlayerUnique(result.Groups[groupIdx], { serial, name, enabled }))
                {
                    ++result.SkippedEntries;
                }
            }
            else if (lower.rfind("group", 0) == 0 && gPos != std::string::npos)
            {
                const std::string prefix = lower.substr(0, gPos);
                size_t groupIdx = std::numeric_limits<size_t>::max();
                for (const auto &gs : groupSections)
                {
                    if (gs.first == prefix)
                    {
                        groupIdx = gs.second;
                        break;
                    }
                }
                if (groupIdx >= result.Groups.size())
                {
                    ++result.SkippedEntries;
                    continue;
                }
                const std::string name = FriendRules_SanitizeName(sec.Get("name"));
                bool enabled = true;
                FriendRules_ParseBool(sec.Get("enabled"), enabled);
                if (name.empty() || !AddGuildUnique(result.Groups[groupIdx], { name, enabled }))
                {
                    ++result.SkippedEntries;
                }
            }
            else if (lower.rfind("friend", 0) == 0)
            {
                // Also accept CrossUO's native Friends.ini format ([Friend0], id=..., name=..., enabled=...)
                uint32_t serial = 0;
                const std::string serialStr = sec.Get("id").empty() ? sec.Get("serial") : sec.Get("id");
                if (!TargetRules_ParseUInt32(serialStr, serial) || serial == 0)
                {
                    ++result.SkippedEntries;
                    continue;
                }
                auto &group = FindOrAddGroup(result.Groups, defaultGroupName);
                std::string name = FriendRules_SanitizeName(sec.Get("name"));
                if (name.empty())
                {
                    name = FriendRules_FormatSerial(serial);
                }
                bool enabled = true;
                FriendRules_ParseBool(sec.Get("enabled"), enabled);
                if (!AddPlayerUnique(group, { serial, name, enabled }))
                {
                    ++result.SkippedEntries;
                }
            }
        }
    }
    else
    {
        // Plain text format:
        //   # Group: <name>
        //   0x00001234 Alice
        //   Alice, 0x00001234, true
        std::string currentGroup = defaultGroupName;
        for (const auto &line : lines)
        {
            if (line.empty())
            {
                continue;
            }
            if (line[0] == '#' || line[0] == ';')
            {
                const std::string rest = TargetRules_Trim(line.substr(1));
                const std::string lower = TargetRules_ToLower(rest);
                if (lower.rfind("group:", 0) == 0)
                {
                    const std::string gName = FriendRules_SanitizeName(rest.substr(6));
                    if (!gName.empty())
                    {
                        currentGroup = gName;
                    }
                }
                continue;
            }

            uint32_t serial = 0;
            std::string name;
            bool enabled = true;
            bool parsed = false;

            const size_t comma = line.find(',');
            if (comma != std::string::npos)
            {
                // CSV: Name, Serial [, Enabled]  OR  Serial, Name [, Enabled]
                std::vector<std::string> parts;
                size_t pos = 0;
                while (pos <= line.size())
                {
                    size_t next = line.find(',', pos);
                    parts.push_back(TargetRules_Trim(
                        next == std::string::npos ? line.substr(pos) : line.substr(pos, next - pos)));
                    if (next == std::string::npos)
                    {
                        break;
                    }
                    pos = next + 1;
                }
                if (parts.size() >= 2)
                {
                    if (TargetRules_ParseUInt32(parts[0], serial) && serial != 0)
                    {
                        name = FriendRules_SanitizeName(parts[1]);
                        parsed = true;
                    }
                    else if (TargetRules_ParseUInt32(parts[1], serial) && serial != 0)
                    {
                        name = FriendRules_SanitizeName(parts[0]);
                        parsed = true;
                    }
                    if (parsed && parts.size() >= 3)
                    {
                        FriendRules_ParseBool(parts[2], enabled);
                    }
                }
            }
            else
            {
                // Whitespace separated: 0x00001234 [Name...]
                size_t sp = 0;
                while (sp < line.size() && !std::isspace(static_cast<unsigned char>(line[sp])))
                {
                    ++sp;
                }
                const std::string first = line.substr(0, sp);
                if (TargetRules_ParseUInt32(first, serial) && serial != 0)
                {
                    name = FriendRules_SanitizeName(line.substr(sp));
                    parsed = true;
                }
            }

            if (!parsed)
            {
                ++result.SkippedEntries;
                continue;
            }
            if (name.empty())
            {
                name = FriendRules_FormatSerial(serial);
            }
            if (result.Groups.size() >= FriendGroupMaxGroups)
            {
                ++result.SkippedEntries;
                continue;
            }
            auto &group = FindOrAddGroup(result.Groups, currentGroup);
            if (!AddPlayerUnique(group, { serial, name, enabled }))
            {
                ++result.SkippedEntries;
            }
        }
    }

    if (result.Groups.empty())
    {
        result.Error = "No valid friend entries found.";
        return result;
    }
    result.Ok = true;
    return result;
}

} // namespace detail

inline FriendImportResult FriendRules_ParseImport(
    const std::string &input,
    const std::string &defaultGroupName = "Imported")
{
    FriendImportResult result;
    if (input.size() > FriendGroupMaxImportBytes)
    {
        result.Error = "Input exceeds the 256 KiB limit.";
        return result;
    }
    const std::string trimmed = TargetRules_Trim(input);
    if (trimmed.empty())
    {
        result.Error = "Input is empty.";
        return result;
    }
    if (trimmed.front() == '{')
    {
        return detail::ParseFriendJson(trimmed, defaultGroupName);
    }
    if (trimmed.front() == '[')
    {
        size_t pos = 1;
        while (pos < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[pos])))
        {
            ++pos;
        }
        if (pos < trimmed.size() &&
            (trimmed[pos] == '{' || trimmed[pos] == ']' || trimmed[pos] == '"' || trimmed[pos] == '['))
        {
            return detail::ParseFriendJson(trimmed, defaultGroupName);
        }
    }
    return detail::ParseFriendIniOrText(trimmed, defaultGroupName);
}

} // namespace xuoassist
