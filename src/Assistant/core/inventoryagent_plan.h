// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "inventoryagent.h"

#include <algorithm>
#include <bitset>
#include <cctype>
#include <climits>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace xuoassist {

using InventoryAgentPropertyMatches = std::bitset<InventoryAgent_MaxRules>;

constexpr uint16_t InventoryAgent_CorpseGraphic = 0x2006;
constexpr int InventoryAgent_CorpseOpenReach = 2;
constexpr size_t InventoryAgent_MaxCorpseHistory = 4096;
constexpr size_t InventoryAgent_MaxExchangeBytes = 256 * 1024;
constexpr int InventoryAgent_MaxJsonDepth = 16;
constexpr size_t InventoryAgent_MaxExchangeRows = 4096;
constexpr size_t InventoryAgent_MaxJsonNodes = 16384;

struct InventoryAgentNamedProperty
{
    const char *Name;
    int Cliloc;
};

constexpr InventoryAgentNamedProperty InventoryAgent_NamedProperties[] = {
    { "Luck", 1060436 },
    { "Damage increase", 1060401 },
    { "Spell damage increase", 1060483 },
    { "Hit chance increase", 1060415 },
    { "Defense chance increase", 1060408 },
    { "Faster casting", 1060413 },
    { "Faster cast recovery", 1060412 },
    { "Lower mana cost", 1060433 },
    { "Lower reagent cost", 1060434 },
    { "Mana regeneration", 1060440 },
    { "Hit point regeneration", 1060444 },
    { "Stamina regeneration", 1060443 },
    { "Swing speed increase", 1060486 },
    { "Enhance potions", 1060411 },
    { "Reflect physical damage", 1060442 },
    { "Physical resist", 1060448 },
    { "Fire resist", 1060447 },
    { "Cold resist", 1060445 },
    { "Poison resist", 1060449 },
    { "Energy resist", 1060446 },
};

struct InventoryAgentStack
{
    uint32_t Serial;
    uint16_t Graphic;
    uint16_t Hue;
    uint32_t Count;
    const InventoryAgentPropertyMatches *PropertyMatches = nullptr;
};

struct InventoryAgentMove
{
    uint32_t Serial = 0;
    uint16_t Count = 0;
    uint32_t Destination = 0; // Per-rule destination override (0 = inherit profile destination).
};

struct InventoryAgentCorpseCandidate
{
    uint32_t Serial = 0;
    uint16_t Graphic = 0;
    uint32_t Body = 0;
    uint32_t Container = 0xFFFFFFFF;
    int Distance = 0;
    bool IsNpc = false;
    bool HasStaticData = false;
    bool IsContainer = false;
    bool SameMap = true;
    bool Opened = false;
    bool HasOpenGump = false;
    bool Ignored = false;
    bool Dragged = false;
    bool MultiBody = false;
};

struct InventoryAgentAutoLootList
{
    astr_t Name;
    bool IncludeSubcontainers = false;
    bool AutoOpenCorpses = false;
    bool HasIncludeSubcontainers = false;
    bool HasAutoOpenCorpses = false;
    std::vector<InventoryAgentRule> Rules;
};

struct InventoryAgentAutoLootImportResult
{
    bool Success = false;
    astr_t Error;
    size_t SkippedDisabled = 0;
    std::vector<InventoryAgentAutoLootList> Lists;
};

inline uint32_t InventoryAgent_ResolveDestination(
    uint32_t ruleDestination, uint32_t profileDestination, uint32_t backpackSerial)
{
    if (ruleDestination != 0 && ruleDestination != 0xFFFFFFFF)
    {
        return ruleDestination;
    }
    if (profileDestination != 0 && profileDestination != 0xFFFFFFFF)
    {
        return profileDestination;
    }
    return backpackSerial;
}

inline bool InventoryAgent_AutoLootCorpseBody(uint32_t body)
{
    // The corpse amount is the body ID on the wire. Exclude player race
    // bodies, their ghosts, and human staff or alternate character bodies.
    return body != 0 && body <= UINT16_MAX &&
           !(body >= 0x0190 && body <= 0x0193) &&
           !(body >= 0x025D && body <= 0x0260) &&
           !(body >= 0x029A && body <= 0x029B) &&
           !(body >= 0x02B6 && body <= 0x02B7) &&
           !(body >= 0x02E8 && body <= 0x02E9) &&
           body != 0x03DB && body != 0x03DF && body != 0x03E2 && body != 0x04E5;
}

inline bool InventoryAgent_IsLootableCorpse(const InventoryAgentCorpseCandidate &candidate)
{
    // CGameItem::OnGraphicChange leaves static tiledata unset on corpse items,
    // so require container semantics whenever static tiledata is attached.
    return candidate.Serial != 0 && candidate.Serial != 0xFFFFFFFF &&
           !candidate.IsNpc &&
           candidate.Graphic == InventoryAgent_CorpseGraphic &&
           candidate.Container == 0xFFFFFFFF &&
           (!candidate.HasStaticData || candidate.IsContainer) &&
           candidate.SameMap &&
           candidate.Distance >= 0 && candidate.Distance <= InventoryAgent_CorpseOpenReach &&
           !candidate.Ignored && !candidate.Dragged && !candidate.MultiBody &&
           InventoryAgent_AutoLootCorpseBody(candidate.Body);
}

inline bool InventoryAgent_CanAutoOpenCorpse(
    const InventoryAgentCorpseCandidate &candidate, const std::unordered_set<uint32_t> &attempted)
{
    return InventoryAgent_IsLootableCorpse(candidate) &&
           !candidate.Opened && !candidate.HasOpenGump &&
           attempted.count(candidate.Serial) == 0;
}

class InventoryAgentCorpseTracker
{
public:
    bool HasAttempted(uint32_t serial) const
    {
        return m_Attempted.count(serial) != 0;
    }

    bool IsFull() const
    {
        return m_Attempted.size() >= InventoryAgent_MaxCorpseHistory;
    }

    size_t Size() const
    {
        return m_Attempted.size();
    }

    bool RecordAttempted(uint32_t serial)
    {
        if (serial == 0 || serial == 0xFFFFFFFF)
        {
            return false;
        }
        if (m_Attempted.count(serial) != 0)
        {
            return true;
        }
        if (IsFull())
        {
            return false;
        }
        m_Attempted.insert(serial);
        return true;
    }

    void Clear()
    {
        m_Attempted.clear();
    }

    uint32_t SelectNext(const std::vector<InventoryAgentCorpseCandidate> &candidates) const
    {
        uint32_t bestSerial = 0;
        int bestDistance = INT_MAX;
        for (const auto &candidate : candidates)
        {
            if (!InventoryAgent_CanAutoOpenCorpse(candidate, m_Attempted))
            {
                continue;
            }
            if (bestSerial == 0 || candidate.Distance < bestDistance ||
                (candidate.Distance == bestDistance && candidate.Serial < bestSerial))
            {
                bestSerial = candidate.Serial;
                bestDistance = candidate.Distance;
            }
        }
        return bestSerial;
    }

private:
    std::unordered_set<uint32_t> m_Attempted;
};

template<typename Char>
bool InventoryAgent_ParsePropertyInteger(const std::basic_string<Char> &text, int &value)
{
    if (text.empty())
    {
        return false;
    }
    size_t i = 0;
    const bool negative = text.front() == '-';
    if (negative || text.front() == '+')
    {
        ++i;
    }
    if (i == text.size())
    {
        return false;
    }
    uint32_t magnitude = 0;
    const uint32_t limit = static_cast<uint32_t>(INT32_MAX) + (negative ? 1u : 0u);
    for (; i != text.size(); ++i)
    {
        const auto digit = text[i] - '0';
        if (digit < 0 || digit > 9 || magnitude > (limit - static_cast<uint32_t>(digit)) / 10u)
        {
            return false;
        }
        magnitude = magnitude * 10u + static_cast<uint32_t>(digit);
    }
    value = static_cast<int>(negative ? -static_cast<int64_t>(magnitude) : magnitude);
    return true;
}

inline bool InventoryAgent_ValidPropertyRule(const InventoryAgentPropertyRule &property)
{
    return property.Cliloc > 0 && (property.HasMinimum || property.HasMaximum) &&
           (!property.HasMinimum || !property.HasMaximum || property.Minimum <= property.Maximum);
}

inline bool InventoryAgent_MatchesProperties(
    const std::vector<InventoryAgentPropertyRule> &required, const InventoryAgentPropertyRows &rows)
{
    for (const auto &property : required)
    {
        if (!InventoryAgent_ValidPropertyRule(property))
        {
            return false;
        }
        bool found = false;
        for (const auto &row : rows)
        {
            if (row.first != static_cast<uint32_t>(property.Cliloc))
            {
                continue;
            }
            int value = 0;
            if (found || !InventoryAgent_ParsePropertyInteger(row.second, value) ||
                (property.HasMinimum && value < property.Minimum) ||
                (property.HasMaximum && value > property.Maximum))
            {
                return false;
            }
            found = true;
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

inline bool InventoryAgent_MatchesAppearance(const InventoryAgentRule &rule, const InventoryAgentStack &stack)
{
    return rule.Graphic == stack.Graphic && (!rule.MatchHue || rule.Hue == stack.Hue);
}

inline bool InventoryAgent_Matches(
    const InventoryAgentRule &rule, const InventoryAgentStack &stack, size_t ruleIndex = 0)
{
    return InventoryAgent_MatchesAppearance(rule, stack) &&
           (rule.Properties.empty() ||
            (stack.PropertyMatches != nullptr && ruleIndex < stack.PropertyMatches->size() &&
             stack.PropertyMatches->test(ruleIndex)));
}

inline InventoryAgentMove InventoryAgent_Plan(
    InventoryAgentMode mode,
    const std::vector<InventoryAgentRule> &rules,
    const std::vector<InventoryAgentStack> &source,
    const std::vector<InventoryAgentStack> &destination)
{
    for (size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex)
    {
        const auto &rule = rules[ruleIndex];
        const uint32_t desired = mode == InventoryAgentMode::Restock
            ? static_cast<uint32_t>(std::max(0, std::min(rule.Quantity, InventoryAgent_MaxQuantity)))
            : InventoryAgent_MaxQuantity;
        uint32_t needed = desired;

        if (mode == InventoryAgentMode::Restock)
        {
            for (const auto &stack : destination)
            {
                if (InventoryAgent_Matches(rule, stack, ruleIndex))
                {
                    needed -= std::min(needed, stack.Count);
                }
                if (needed == 0)
                {
                    break;
                }
            }
        }

        if (needed > 0)
        {
            for (const auto &stack : source)
            {
                if (stack.Count > 0 && InventoryAgent_Matches(rule, stack, ruleIndex))
                {
                    return { stack.Serial, static_cast<uint16_t>(std::min(needed, stack.Count)), rule.Destination };
                }
            }
        }
    }

    return {};
}

namespace inventory_plan_detail {

inline astr_t TrimAscii(const astr_t &text)
{
    size_t start = 0;
    while (start < text.size() &&
           (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n'))
    {
        ++start;
    }
    size_t end = text.size();
    while (end > start &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n'))
    {
        --end;
    }
    return text.substr(start, end - start);
}

inline astr_t LowerAscii(const astr_t &text)
{
    astr_t out = text;
    for (char &ch : out)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

inline bool SafeName(const astr_t &name)
{
    if (name.empty() || name.size() > 63)
    {
        return false;
    }
    for (unsigned char ch : name)
    {
        if (ch < 32 || ch == 127)
        {
            return false;
        }
    }
    return true;
}

inline astr_t FormatHex16(uint16_t value)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(value));
    return buf;
}

inline astr_t FormatHex32(uint32_t value)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(value));
    return buf;
}

inline astr_t EscapeIni(const astr_t &value)
{
    astr_t out;
    out.reserve(value.size() + 4);
    for (char ch : value)
    {
        if (ch == '\\')
        {
            out += "\\\\";
        }
        else if (ch == '\n')
        {
            out += "\\n";
        }
        else if (ch == '\r')
        {
            out += "\\r";
        }
        else
        {
            out += ch;
        }
    }
    return out;
}

inline astr_t UnescapeIni(const astr_t &value)
{
    astr_t out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '\\' && i + 1 < value.size())
        {
            const char next = value[++i];
            if (next == 'n')
            {
                out += '\n';
            }
            else if (next == 'r')
            {
                out += '\r';
            }
            else
            {
                out += next;
            }
        }
        else
        {
            out += value[i];
        }
    }
    return out;
}

inline astr_t EscapeJson(const astr_t &value)
{
    astr_t out;
    out.reserve(value.size() + 8);
    out += '"';
    for (unsigned char ch : value)
    {
        switch (ch)
        {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 32)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(ch));
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(ch);
                }
                break;
        }
    }
    out += '"';
    return out;
}

inline bool ParseUnsignedText(const astr_t &raw, uint32_t &out)
{
    const auto text = TrimAscii(raw);
    if (text.empty())
    {
        return false;
    }
    uint64_t value = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        for (size_t i = 2; i < text.size(); ++i)
        {
            const char ch = text[i];
            uint32_t digit = 0;
            if (ch >= '0' && ch <= '9')
            {
                digit = static_cast<uint32_t>(ch - '0');
            }
            else if (ch >= 'a' && ch <= 'f')
            {
                digit = static_cast<uint32_t>(10 + (ch - 'a'));
            }
            else if (ch >= 'A' && ch <= 'F')
            {
                digit = static_cast<uint32_t>(10 + (ch - 'A'));
            }
            else
            {
                return false;
            }
            value = (value << 4) | digit;
            if (value > UINT32_MAX)
            {
                return false;
            }
        }
        out = static_cast<uint32_t>(value);
        return true;
    }
    for (char ch : text)
    {
        if (ch < '0' || ch > '9')
        {
            return false;
        }
        value = value * 10 + static_cast<uint64_t>(ch - '0');
        if (value > UINT32_MAX)
        {
            return false;
        }
    }
    out = static_cast<uint32_t>(value);
    return true;
}

inline bool ParseStrictBoolText(const astr_t &raw, bool &out)
{
    const auto lowered = LowerAscii(TrimAscii(raw));
    if (lowered == "true" || lowered == "1")
    {
        out = true;
        return true;
    }
    if (lowered == "false" || lowered == "0")
    {
        out = false;
        return true;
    }
    return false;
}

inline bool FindNamedPropertyCliloc(const astr_t &rawName, int &outCliloc)
{
    const auto normalized = LowerAscii(TrimAscii(rawName));
    if (normalized.empty())
    {
        return false;
    }
    for (const auto &entry : InventoryAgent_NamedProperties)
    {
        if (LowerAscii(entry.Name) == normalized)
        {
            outCliloc = entry.Cliloc;
            return true;
        }
    }
    // Accept common Razor Enhanced shorthand aliases.
    if (normalized == "spell damage" || normalized == "sdi")
    {
        outCliloc = 1060483;
        return true;
    }
    if (normalized == "damage inc" || normalized == "di")
    {
        outCliloc = 1060401;
        return true;
    }
    if (normalized == "hit chance" || normalized == "hci")
    {
        outCliloc = 1060415;
        return true;
    }
    if (normalized == "defense chance" || normalized == "dci")
    {
        outCliloc = 1060408;
        return true;
    }
    if (normalized == "fc")
    {
        outCliloc = 1060413;
        return true;
    }
    if (normalized == "fcr")
    {
        outCliloc = 1060412;
        return true;
    }
    if (normalized == "lmc")
    {
        outCliloc = 1060433;
        return true;
    }
    if (normalized == "lrc")
    {
        outCliloc = 1060434;
        return true;
    }
    if (normalized == "ssi")
    {
        outCliloc = 1060486;
        return true;
    }
    return false;
}

struct JsonValue
{
    enum class Kind
    {
        Null,
        Bool,
        Int,
        Double,
        String,
        Array,
        Object,
    };

    Kind Type = Kind::Null;
    bool BoolValue = false;
    int64_t IntValue = 0;
    double DoubleValue = 0.0;
    astr_t StringValue;
    std::vector<JsonValue> ArrayValue;
    std::map<astr_t, JsonValue> ObjectValue;

    const JsonValue *Field(const char *key) const
    {
        if (Type != Kind::Object)
        {
            return nullptr;
        }
        const auto it = ObjectValue.find(key);
        return it != ObjectValue.end() ? &it->second : nullptr;
    }
};

class JsonParser
{
public:
    explicit JsonParser(const astr_t &input) : m_Input(input) {}

    bool Parse(JsonValue &out, astr_t &error)
    {
        SkipWhitespace();
        if (!ParseValue(out, 0, error))
        {
            return false;
        }
        SkipWhitespace();
        if (m_Pos != m_Input.size())
        {
            error = "Unexpected trailing characters after JSON data.";
            return false;
        }
        return true;
    }

private:
    const astr_t &m_Input;
    size_t m_Pos = 0;
    size_t m_Nodes = 0;

    void SkipWhitespace()
    {
        while (m_Pos < m_Input.size())
        {
            const char ch = m_Input[m_Pos];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            {
                ++m_Pos;
            }
            else
            {
                break;
            }
        }
    }

    bool Consume(char expected)
    {
        if (m_Pos < m_Input.size() && m_Input[m_Pos] == expected)
        {
            ++m_Pos;
            return true;
        }
        return false;
    }

    bool ParseValue(JsonValue &out, int depth, astr_t &error)
    {
        if (depth > InventoryAgent_MaxJsonDepth)
        {
            error = "JSON nesting is too deep.";
            return false;
        }
        if (++m_Nodes > InventoryAgent_MaxJsonNodes)
        {
            error = "JSON document contains too many entries.";
            return false;
        }
        SkipWhitespace();
        if (m_Pos >= m_Input.size())
        {
            error = "Unexpected end of JSON data.";
            return false;
        }
        const char ch = m_Input[m_Pos];
        if (ch == '{')
        {
            return ParseObject(out, depth + 1, error);
        }
        if (ch == '[')
        {
            return ParseArray(out, depth + 1, error);
        }
        if (ch == '"')
        {
            out.Type = JsonValue::Kind::String;
            return ParseString(out.StringValue, error);
        }
        if (ch == 't' && m_Input.compare(m_Pos, 4, "true") == 0)
        {
            m_Pos += 4;
            out.Type = JsonValue::Kind::Bool;
            out.BoolValue = true;
            return true;
        }
        if (ch == 'f' && m_Input.compare(m_Pos, 5, "false") == 0)
        {
            m_Pos += 5;
            out.Type = JsonValue::Kind::Bool;
            out.BoolValue = false;
            return true;
        }
        if (ch == 'n' && m_Input.compare(m_Pos, 4, "null") == 0)
        {
            m_Pos += 4;
            out.Type = JsonValue::Kind::Null;
            return true;
        }
        if (ch == '-' || (ch >= '0' && ch <= '9'))
        {
            return ParseNumber(out, error);
        }
        error = "Malformed JSON value.";
        return false;
    }

    bool ParseHex4(uint32_t &code)
    {
        if (m_Pos + 4 > m_Input.size())
        {
            return false;
        }
        code = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char ch = m_Input[m_Pos++];
            uint32_t digit = 0;
            if (ch >= '0' && ch <= '9')
            {
                digit = static_cast<uint32_t>(ch - '0');
            }
            else if (ch >= 'a' && ch <= 'f')
            {
                digit = static_cast<uint32_t>(10 + (ch - 'a'));
            }
            else if (ch >= 'A' && ch <= 'F')
            {
                digit = static_cast<uint32_t>(10 + (ch - 'A'));
            }
            else
            {
                return false;
            }
            code = (code << 4) | digit;
        }
        return true;
    }

    static void AppendUtf8(astr_t &out, uint32_t cp)
    {
        if (cp <= 0x7F)
        {
            out += static_cast<char>(cp);
        }
        else if (cp <= 0x7FF)
        {
            out += static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp <= 0xFFFF)
        {
            out += static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool ParseString(astr_t &out, astr_t &error)
    {
        if (!Consume('"'))
        {
            error = "Expected opening quote for JSON string.";
            return false;
        }
        out.clear();
        while (m_Pos < m_Input.size())
        {
            const unsigned char ch = static_cast<unsigned char>(m_Input[m_Pos++]);
            if (ch == '"')
            {
                return true;
            }
            if (ch < 0x20)
            {
                error = "Control character in JSON string.";
                return false;
            }
            if (ch != '\\')
            {
                out += static_cast<char>(ch);
                continue;
            }
            if (m_Pos >= m_Input.size())
            {
                error = "Unterminated escape sequence in JSON string.";
                return false;
            }
            const char esc = m_Input[m_Pos++];
            switch (esc)
            {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u':
                {
                    uint32_t code = 0;
                    if (!ParseHex4(code))
                    {
                        error = "Invalid Unicode escape in JSON string.";
                        return false;
                    }
                    if (code >= 0xD800 && code <= 0xDBFF)
                    {
                        if (m_Pos + 2 > m_Input.size() || m_Input[m_Pos] != '\\' || m_Input[m_Pos + 1] != 'u')
                        {
                            error = "Incomplete surrogate pair in JSON string.";
                            return false;
                        }
                        m_Pos += 2;
                        uint32_t low = 0;
                        if (!ParseHex4(low) || low < 0xDC00 || low > 0xDFFF)
                        {
                            error = "Invalid surrogate pair in JSON string.";
                            return false;
                        }
                        code = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
                    }
                    else if (code >= 0xDC00 && code <= 0xDFFF)
                    {
                        error = "Unexpected low surrogate in JSON string.";
                        return false;
                    }
                    AppendUtf8(out, code);
                    break;
                }
                default:
                    error = "Unsupported escape sequence in JSON string.";
                    return false;
            }
        }
        error = "Unterminated JSON string.";
        return false;
    }

    bool ParseNumber(JsonValue &out, astr_t &error)
    {
        const size_t start = m_Pos;
        const bool negative = Consume('-');
        if (m_Pos >= m_Input.size())
        {
            error = "Incomplete JSON number.";
            return false;
        }
        if (m_Input[m_Pos] == '0')
        {
            ++m_Pos;
            if (m_Pos < m_Input.size() && m_Input[m_Pos] >= '0' && m_Input[m_Pos] <= '9')
            {
                error = "Leading zeros are not allowed in JSON numbers.";
                return false;
            }
        }
        else if (m_Input[m_Pos] >= '1' && m_Input[m_Pos] <= '9')
        {
            while (m_Pos < m_Input.size() && m_Input[m_Pos] >= '0' && m_Input[m_Pos] <= '9')
            {
                ++m_Pos;
            }
        }
        else
        {
            error = "Malformed JSON number.";
            return false;
        }

        bool isFloat = false;
        if (m_Pos < m_Input.size() && m_Input[m_Pos] == '.')
        {
            isFloat = true;
            ++m_Pos;
            if (m_Pos >= m_Input.size() || m_Input[m_Pos] < '0' || m_Input[m_Pos] > '9')
            {
                error = "Malformed fractional JSON number.";
                return false;
            }
            while (m_Pos < m_Input.size() && m_Input[m_Pos] >= '0' && m_Input[m_Pos] <= '9')
            {
                ++m_Pos;
            }
        }
        if (m_Pos < m_Input.size() && (m_Input[m_Pos] == 'e' || m_Input[m_Pos] == 'E'))
        {
            isFloat = true;
            ++m_Pos;
            if (m_Pos < m_Input.size() && (m_Input[m_Pos] == '+' || m_Input[m_Pos] == '-'))
            {
                ++m_Pos;
            }
            if (m_Pos >= m_Input.size() || m_Input[m_Pos] < '0' || m_Input[m_Pos] > '9')
            {
                error = "Malformed exponent in JSON number.";
                return false;
            }
            while (m_Pos < m_Input.size() && m_Input[m_Pos] >= '0' && m_Input[m_Pos] <= '9')
            {
                ++m_Pos;
            }
        }

        if (!isFloat)
        {
            uint64_t magnitude = 0;
            const uint64_t limit = static_cast<uint64_t>(INT64_MAX) + (negative ? 1ull : 0ull);
            for (size_t i = start + (negative ? 1u : 0u); i < m_Pos; ++i)
            {
                const uint64_t digit = static_cast<uint64_t>(m_Input[i] - '0');
                if (magnitude > (limit - digit) / 10ull)
                {
                    error = "JSON integer is out of range.";
                    return false;
                }
                magnitude = magnitude * 10ull + digit;
            }
            out.Type = JsonValue::Kind::Int;
            if (negative)
            {
                out.IntValue = magnitude == static_cast<uint64_t>(INT64_MAX) + 1ull
                    ? INT64_MIN
                    : -static_cast<int64_t>(magnitude);
            }
            else
            {
                out.IntValue = static_cast<int64_t>(magnitude);
            }
            return true;
        }

        out.Type = JsonValue::Kind::Double;
        out.DoubleValue = 0.0;
        return true;
    }

    bool ParseArray(JsonValue &out, int depth, astr_t &error)
    {
        if (!Consume('['))
        {
            error = "Expected '[' for JSON array.";
            return false;
        }
        out.Type = JsonValue::Kind::Array;
        out.ArrayValue.clear();
        SkipWhitespace();
        if (Consume(']'))
        {
            return true;
        }
        while (true)
        {
            if (out.ArrayValue.size() >= InventoryAgent_MaxExchangeRows)
            {
                error = "JSON array has too many entries.";
                return false;
            }
            JsonValue elem;
            if (!ParseValue(elem, depth, error))
            {
                return false;
            }
            out.ArrayValue.push_back(std::move(elem));
            SkipWhitespace();
            if (Consume(']'))
            {
                return true;
            }
            if (!Consume(','))
            {
                error = "Expected ',' or ']' in JSON array.";
                return false;
            }
            SkipWhitespace();
            if (m_Pos < m_Input.size() && m_Input[m_Pos] == ']')
            {
                error = "Trailing comma in JSON array.";
                return false;
            }
        }
    }

    bool ParseObject(JsonValue &out, int depth, astr_t &error)
    {
        if (!Consume('{'))
        {
            error = "Expected '{' for JSON object.";
            return false;
        }
        out.Type = JsonValue::Kind::Object;
        out.ObjectValue.clear();
        SkipWhitespace();
        if (Consume('}'))
        {
            return true;
        }
        while (true)
        {
            SkipWhitespace();
            astr_t key;
            if (!ParseString(key, error))
            {
                return false;
            }
            SkipWhitespace();
            if (!Consume(':'))
            {
                error = "Expected ':' after JSON key.";
                return false;
            }
            JsonValue value;
            if (!ParseValue(value, depth, error))
            {
                return false;
            }
            if (!out.ObjectValue.emplace(std::move(key), std::move(value)).second)
            {
                error = "Duplicate key in JSON object.";
                return false;
            }
            SkipWhitespace();
            if (Consume('}'))
            {
                return true;
            }
            if (!Consume(','))
            {
                error = "Expected ',' or '}' in JSON object.";
                return false;
            }
            SkipWhitespace();
            if (m_Pos < m_Input.size() && m_Input[m_Pos] == '}')
            {
                error = "Trailing comma in JSON object.";
                return false;
            }
        }
    }
};

inline bool ParseGraphicJson(const JsonValue &val, uint16_t &out)
{
    if (val.Type == JsonValue::Kind::Int)
    {
        if (val.IntValue < 1 || val.IntValue > UINT16_MAX)
        {
            return false;
        }
        out = static_cast<uint16_t>(val.IntValue);
        return true;
    }
    if (val.Type == JsonValue::Kind::String)
    {
        uint32_t parsed = 0;
        if (!ParseUnsignedText(val.StringValue, parsed) || parsed < 1 || parsed > UINT16_MAX)
        {
            return false;
        }
        out = static_cast<uint16_t>(parsed);
        return true;
    }
    return false;
}

inline bool ParseDestinationJson(const JsonValue &val, bool allowMinusOne, uint32_t &out)
{
    if (val.Type == JsonValue::Kind::Null)
    {
        out = 0;
        return true;
    }
    if (val.Type == JsonValue::Kind::Int)
    {
        if (allowMinusOne && val.IntValue == -1)
        {
            out = 0;
            return true;
        }
        if (val.IntValue < 0 || val.IntValue >= static_cast<int64_t>(0xFFFFFFFFu))
        {
            return false;
        }
        out = static_cast<uint32_t>(val.IntValue);
        return true;
    }
    if (val.Type == JsonValue::Kind::String)
    {
        const auto trimmed = TrimAscii(val.StringValue);
        if (trimmed.empty())
        {
            out = 0;
            return true;
        }
        if (allowMinusOne && trimmed == "-1")
        {
            out = 0;
            return true;
        }
        uint32_t parsed = 0;
        if (!ParseUnsignedText(trimmed, parsed) || parsed == 0xFFFFFFFFu)
        {
            return false;
        }
        out = parsed;
        return true;
    }
    return false;
}

inline bool ParseInt32Json(const JsonValue &val, int &out)
{
    if (val.Type == JsonValue::Kind::Int)
    {
        if (val.IntValue < INT32_MIN || val.IntValue > INT32_MAX)
        {
            return false;
        }
        out = static_cast<int>(val.IntValue);
        return true;
    }
    return false;
}

inline bool ParsePropertyArrayJson(
    const JsonValue &arr, std::vector<InventoryAgentPropertyRule> &out, astr_t &error)
{
    if (arr.Type == JsonValue::Kind::Null)
    {
        return true;
    }
    if (arr.Type != JsonValue::Kind::Array)
    {
        error = "Rule properties must be a list.";
        return false;
    }
    if (arr.ArrayValue.size() > static_cast<size_t>(InventoryAgent_MaxProperties))
    {
        error = "A rule has more than 16 property ranges.";
        return false;
    }
    for (const auto &entry : arr.ArrayValue)
    {
        if (entry.Type != JsonValue::Kind::Object)
        {
            error = "Each property range must be an object.";
            return false;
        }
        InventoryAgentPropertyRule prop;
        const auto *clilocField = entry.Field("cliloc");
        if (clilocField == nullptr)
        {
            clilocField = entry.Field("Cliloc");
        }
        const auto *nameField = entry.Field("name");
        if (nameField == nullptr)
        {
            nameField = entry.Field("Name");
        }
        if (clilocField != nullptr && clilocField->Type != JsonValue::Kind::Null)
        {
            if (!ParseInt32Json(*clilocField, prop.Cliloc) || prop.Cliloc <= 0)
            {
                error = "Invalid property cliloc ID.";
                return false;
            }
        }
        else if (nameField != nullptr && nameField->Type == JsonValue::Kind::String)
        {
            if (!FindNamedPropertyCliloc(nameField->StringValue, prop.Cliloc))
            {
                error = "Unsupported property name: " + nameField->StringValue;
                return false;
            }
        }
        else
        {
            error = "Each property range requires a cliloc ID or supported property name.";
            return false;
        }

        const auto *minField = entry.Field("minimum");
        if (minField == nullptr)
        {
            minField = entry.Field("Minimum");
        }
        if (minField != nullptr && minField->Type != JsonValue::Kind::Null)
        {
            if (!ParseInt32Json(*minField, prop.Minimum))
            {
                error = "Invalid property minimum value.";
                return false;
            }
            prop.HasMinimum = true;
        }

        const auto *maxField = entry.Field("maximum");
        if (maxField == nullptr)
        {
            maxField = entry.Field("Maximum");
        }
        if (maxField != nullptr && maxField->Type != JsonValue::Kind::Null)
        {
            if (!ParseInt32Json(*maxField, prop.Maximum))
            {
                error = "Invalid property maximum value.";
                return false;
            }
            prop.HasMaximum = true;
        }

        if (!InventoryAgent_ValidPropertyRule(prop))
        {
            error = "Property range must have a valid minimum or maximum (minimum <= maximum).";
            return false;
        }
        out.push_back(prop);
    }
    return true;
}

inline bool ParseNativeJsonRule(const JsonValue &obj, InventoryAgentRule &out, astr_t &error)
{
    if (obj.Type != JsonValue::Kind::Object)
    {
        error = "Each rule entry must be an object.";
        return false;
    }
    const auto *graphicField = obj.Field("graphic");
    if (graphicField == nullptr || !ParseGraphicJson(*graphicField, out.Graphic))
    {
        error = "Each rule requires a valid graphic ID (0x0001 to 0xFFFF).";
        return false;
    }

    out.MatchHue = true;
    if (const auto *matchHueField = obj.Field("matchHue"))
    {
        if (matchHueField->Type != JsonValue::Kind::Bool)
        {
            error = "Rule matchHue must be true or false.";
            return false;
        }
        out.MatchHue = matchHueField->BoolValue;
    }

    out.Hue = 0;
    if (const auto *hueField = obj.Field("hue"))
    {
        if (hueField->Type == JsonValue::Kind::Int)
        {
            if (hueField->IntValue < 0 || hueField->IntValue > UINT16_MAX)
            {
                error = "Rule hue is out of range.";
                return false;
            }
            out.Hue = static_cast<uint16_t>(hueField->IntValue);
        }
        else if (hueField->Type == JsonValue::Kind::String)
        {
            uint32_t parsed = 0;
            if (!ParseUnsignedText(hueField->StringValue, parsed) || parsed > UINT16_MAX)
            {
                error = "Rule hue is invalid.";
                return false;
            }
            out.Hue = static_cast<uint16_t>(parsed);
        }
        else
        {
            error = "Rule hue must be a number or hex string.";
            return false;
        }
    }

    out.Quantity = 1;
    out.Destination = 0;
    if (const auto *destField = obj.Field("destination"))
    {
        if (!ParseDestinationJson(*destField, false, out.Destination))
        {
            error = "Rule destination bag serial is invalid.";
            return false;
        }
    }

    if (const auto *propsField = obj.Field("properties"))
    {
        if (!ParsePropertyArrayJson(*propsField, out.Properties, error))
        {
            return false;
        }
    }
    return true;
}

inline bool ParseEnhancedJsonRule(
    const JsonValue &obj,
    const astr_t &defaultListName,
    astr_t &outListName,
    bool &outEnabled,
    InventoryAgentRule &outRule,
    astr_t &error)
{
    if (obj.Type != JsonValue::Kind::Object)
    {
        error = "Each AutoLoot entry must be an object.";
        return false;
    }

    outListName = defaultListName;
    if (const auto *listField = obj.Field("List"))
    {
        if (listField->Type != JsonValue::Kind::String)
        {
            error = "Entry List name must be a string.";
            return false;
        }
        const auto trimmed = TrimAscii(listField->StringValue);
        if (!trimmed.empty())
        {
            if (!SafeName(trimmed))
            {
                error = "Entry List name is invalid or longer than 63 characters.";
                return false;
            }
            outListName = trimmed;
        }
    }

    outEnabled = true;
    if (const auto *selectedField = obj.Field("Selected"))
    {
        if (selectedField->Type != JsonValue::Kind::Bool)
        {
            error = "Entry Selected flag must be true or false.";
            return false;
        }
        outEnabled = selectedField->BoolValue;
    }
    if (!outEnabled)
    {
        return true;
    }

    const auto *graphicField = obj.Field("Graphics");
    if (graphicField == nullptr)
    {
        graphicField = obj.Field("Graphic");
    }
    if (graphicField == nullptr)
    {
        graphicField = obj.Field("ItemID");
    }
    if (graphicField == nullptr)
    {
        graphicField = obj.Field("graphic");
    }
    if (graphicField == nullptr || !ParseGraphicJson(*graphicField, outRule.Graphic))
    {
        error = "Each AutoLoot entry requires a valid Graphics ID (0x0001 to 0xFFFF).";
        return false;
    }

    outRule.MatchHue = false;
    outRule.Hue = 0;
    const auto *colorField = obj.Field("Color");
    if (colorField == nullptr)
    {
        colorField = obj.Field("Hue");
    }
    if (colorField == nullptr)
    {
        colorField = obj.Field("hue");
    }
    if (colorField != nullptr && colorField->Type != JsonValue::Kind::Null)
    {
        if (colorField->Type == JsonValue::Kind::Int)
        {
            if (colorField->IntValue == -1 || colorField->IntValue == UINT16_MAX)
            {
                outRule.MatchHue = false;
                outRule.Hue = 0;
            }
            else if (colorField->IntValue >= 0 && colorField->IntValue < UINT16_MAX)
            {
                outRule.MatchHue = true;
                outRule.Hue = static_cast<uint16_t>(colorField->IntValue);
            }
            else
            {
                error = "Entry Color value is out of range.";
                return false;
            }
        }
        else if (colorField->Type == JsonValue::Kind::String)
        {
            const auto trimmed = TrimAscii(colorField->StringValue);
            if (trimmed == "-1")
            {
                outRule.MatchHue = false;
                outRule.Hue = 0;
            }
            else
            {
                uint32_t parsed = 0;
                if (!ParseUnsignedText(trimmed, parsed) || parsed > UINT16_MAX)
                {
                    error = "Entry Color value is invalid.";
                    return false;
                }
                if (parsed == UINT16_MAX)
                {
                    outRule.MatchHue = false;
                    outRule.Hue = 0;
                }
                else
                {
                    outRule.MatchHue = true;
                    outRule.Hue = static_cast<uint16_t>(parsed);
                }
            }
        }
        else
        {
            error = "Entry Color must be a number or hex string.";
            return false;
        }
    }

    if (const auto *matchHueField = obj.Field("matchHue"))
    {
        if (matchHueField->Type != JsonValue::Kind::Bool)
        {
            error = "Entry matchHue must be true or false.";
            return false;
        }
        outRule.MatchHue = matchHueField->BoolValue;
    }

    outRule.Quantity = 1;
    outRule.Destination = 0;
    const auto *destField = obj.Field("LootBagOverride");
    if (destField == nullptr)
    {
        destField = obj.Field("Destination");
    }
    if (destField == nullptr)
    {
        destField = obj.Field("destination");
    }
    if (destField != nullptr)
    {
        if (!ParseDestinationJson(*destField, true, outRule.Destination))
        {
            error = "Entry LootBagOverride is invalid.";
            return false;
        }
    }

    const auto *propsField = obj.Field("Properties");
    if (propsField == nullptr)
    {
        propsField = obj.Field("properties");
    }
    if (propsField != nullptr)
    {
        if (!ParsePropertyArrayJson(*propsField, outRule.Properties, error))
        {
            return false;
        }
    }
    return true;
}

inline InventoryAgentAutoLootImportResult ParseJsonImport(const astr_t &input)
{
    InventoryAgentAutoLootImportResult result;
    JsonValue root;
    JsonParser parser(input);
    if (!parser.Parse(root, result.Error))
    {
        return result;
    }

    if (root.Type == JsonValue::Kind::Object && root.Field("format") != nullptr)
    {
        const auto *formatField = root.Field("format");
        if (formatField->Type != JsonValue::Kind::String || formatField->StringValue != "crossuo-autoloot")
        {
            result.Error = "Unsupported AutoLoot JSON format.";
            return result;
        }
        const auto *versionField = root.Field("version");
        if (versionField == nullptr || versionField->Type != JsonValue::Kind::Int || versionField->IntValue != 1)
        {
            result.Error = "Unsupported AutoLoot JSON version.";
            return result;
        }
        const auto *nameField = root.Field("name");
        if (nameField == nullptr || nameField->Type != JsonValue::Kind::String)
        {
            result.Error = "AutoLoot JSON requires a list name.";
            return result;
        }
        const auto trimmedName = TrimAscii(nameField->StringValue);
        if (!SafeName(trimmedName))
        {
            result.Error = "AutoLoot list name is invalid or longer than 63 characters.";
            return result;
        }

        InventoryAgentAutoLootList list;
        list.Name = trimmedName;
        if (const auto *subField = root.Field("includeSubcontainers"))
        {
            if (subField->Type != JsonValue::Kind::Bool)
            {
                result.Error = "includeSubcontainers must be true or false.";
                return result;
            }
            list.IncludeSubcontainers = subField->BoolValue;
            list.HasIncludeSubcontainers = true;
        }
        if (const auto *openField = root.Field("autoOpenCorpses"))
        {
            if (openField->Type != JsonValue::Kind::Bool)
            {
                result.Error = "autoOpenCorpses must be true or false.";
                return result;
            }
            list.AutoOpenCorpses = openField->BoolValue;
            list.HasAutoOpenCorpses = true;
        }

        const auto *rulesField = root.Field("rules");
        if (rulesField == nullptr || rulesField->Type != JsonValue::Kind::Array)
        {
            result.Error = "AutoLoot JSON requires a rules array.";
            return result;
        }
        if (rulesField->ArrayValue.size() > static_cast<size_t>(InventoryAgent_MaxRules))
        {
            result.Error = "AutoLoot list exceeds the 128 rule limit.";
            return result;
        }
        for (const auto &entry : rulesField->ArrayValue)
        {
            InventoryAgentRule rule;
            if (!ParseNativeJsonRule(entry, rule, result.Error))
            {
                return result;
            }
            list.Rules.push_back(std::move(rule));
        }
        result.Lists.push_back(std::move(list));
        result.Success = true;
        return result;
    }

    const std::vector<JsonValue> *entries = nullptr;
    astr_t defaultName = "Imported AutoLoot";
    if (root.Type == JsonValue::Kind::Array)
    {
        entries = &root.ArrayValue;
    }
    else if (root.Type == JsonValue::Kind::Object)
    {
        const auto *nameField = root.Field("Name");
        if (nameField == nullptr)
        {
            nameField = root.Field("List");
        }
        if (nameField == nullptr)
        {
            nameField = root.Field("name");
        }
        if (nameField != nullptr)
        {
            if (nameField->Type != JsonValue::Kind::String)
            {
                result.Error = "AutoLoot list name must be a string.";
                return result;
            }
            const auto trimmed = TrimAscii(nameField->StringValue);
            if (!trimmed.empty())
            {
                if (!SafeName(trimmed))
                {
                    result.Error = "AutoLoot list name is invalid or longer than 63 characters.";
                    return result;
                }
                defaultName = trimmed;
            }
        }
        const auto *itemsField = root.Field("Items");
        if (itemsField == nullptr)
        {
            itemsField = root.Field("Rules");
        }
        if (itemsField == nullptr)
        {
            itemsField = root.Field("rules");
        }
        if (itemsField == nullptr || itemsField->Type != JsonValue::Kind::Array)
        {
            result.Error = "Expected an Items or Rules array in JSON AutoLoot data.";
            return result;
        }
        entries = &itemsField->ArrayValue;
    }
    else
    {
        result.Error = "AutoLoot JSON must be an object or an array of entries.";
        return result;
    }

    std::vector<InventoryAgentAutoLootList> grouped;
    for (const auto &entry : *entries)
    {
        astr_t listName;
        bool enabled = true;
        InventoryAgentRule rule;
        if (!ParseEnhancedJsonRule(entry, defaultName, listName, enabled, rule, result.Error))
        {
            return result;
        }
        if (!enabled)
        {
            ++result.SkippedDisabled;
            continue;
        }
        const auto folded = LowerAscii(listName);
        auto it = std::find_if(grouped.begin(), grouped.end(), [&folded](const auto &existing) {
            return LowerAscii(existing.Name) == folded;
        });
        if (it == grouped.end())
        {
            InventoryAgentAutoLootList created;
            created.Name = listName;
            grouped.push_back(std::move(created));
            it = grouped.end() - 1;
        }
        if (it->Rules.size() >= static_cast<size_t>(InventoryAgent_MaxRules))
        {
            result.Error = "AutoLoot list \"" + it->Name + "\" exceeds the 128 rule limit.";
            return result;
        }
        it->Rules.push_back(std::move(rule));
    }

    if (grouped.empty())
    {
        result.Error = "No enabled AutoLoot rules were found.";
        return result;
    }
    result.Lists = std::move(grouped);
    result.Success = true;
    return result;
}

inline InventoryAgentAutoLootImportResult ParseIniImport(const astr_t &input)
{
    InventoryAgentAutoLootImportResult result;
    std::map<astr_t, std::map<astr_t, astr_t>> sections;
    astr_t currentSection;
    bool hasSection = false;

    size_t pos = 0;
    size_t lineNo = 0;
    while (pos <= input.size())
    {
        size_t end = input.find('\n', pos);
        if (end == astr_t::npos)
        {
            end = input.size();
        }
        ++lineNo;
        astr_t line = input.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const auto trimmed = TrimAscii(line);
        if (!trimmed.empty() && trimmed.front() != ';' && trimmed.front() != '#')
        {
            if (trimmed.front() == '[')
            {
                if (trimmed.back() != ']' || trimmed.size() <= 2)
                {
                    result.Error = "Malformed INI section header on line " + std::to_string(lineNo) + ".";
                    return result;
                }
                const auto name = TrimAscii(trimmed.substr(1, trimmed.size() - 2));
                if (name.empty() || name.find('[') != astr_t::npos || name.find(']') != astr_t::npos)
                {
                    result.Error = "Invalid INI section name on line " + std::to_string(lineNo) + ".";
                    return result;
                }
                if (sections.count(name) != 0)
                {
                    result.Error = "Duplicate INI section [" + name + "].";
                    return result;
                }
                currentSection = name;
                sections.emplace(currentSection, std::map<astr_t, astr_t>{});
                hasSection = true;
            }
            else
            {
                if (!hasSection)
                {
                    result.Error = "INI key outside a section on line " + std::to_string(lineNo) + ".";
                    return result;
                }
                const size_t eq = trimmed.find('=');
                if (eq == astr_t::npos || eq == 0)
                {
                    result.Error = "Malformed INI entry on line " + std::to_string(lineNo) + ".";
                    return result;
                }
                const auto key = TrimAscii(trimmed.substr(0, eq));
                const auto val = UnescapeIni(TrimAscii(trimmed.substr(eq + 1)));
                if (key.empty())
                {
                    result.Error = "Empty INI key on line " + std::to_string(lineNo) + ".";
                    return result;
                }
                if (!sections[currentSection].emplace(key, val).second)
                {
                    result.Error = "Duplicate INI key \"" + key + "\" in [" + currentSection + "].";
                    return result;
                }
            }
        }
        if (end == input.size())
        {
            break;
        }
        pos = end + 1;
    }

    const auto headerIt = sections.find("AutoLootList");
    if (headerIt == sections.end())
    {
        result.Error = "Missing [AutoLootList] section.";
        return result;
    }
    const auto &header = headerIt->second;
    const auto findKey = [](const std::map<astr_t, astr_t> &sec, const char *key) -> const astr_t * {
        const auto it = sec.find(key);
        return it != sec.end() ? &it->second : nullptr;
    };

    const auto *format = findKey(header, "format");
    if (format == nullptr || *format != "crossuo-autoloot")
    {
        result.Error = "Unsupported AutoLoot INI format.";
        return result;
    }
    const auto *versionText = findKey(header, "version");
    int version = 0;
    if (versionText == nullptr || !InventoryAgent_ParsePropertyInteger(*versionText, version) || version != 1)
    {
        result.Error = "Unsupported AutoLoot INI version.";
        return result;
    }
    const auto *nameText = findKey(header, "name");
    if (nameText == nullptr || !SafeName(*nameText))
    {
        result.Error = "AutoLoot list name is missing or invalid.";
        return result;
    }
    const auto *rulesCountText = findKey(header, "rules");
    int ruleCount = 0;
    if (rulesCountText == nullptr || !InventoryAgent_ParsePropertyInteger(*rulesCountText, ruleCount) ||
        ruleCount < 0 || ruleCount > InventoryAgent_MaxRules)
    {
        result.Error = "Invalid rule count in [AutoLootList].";
        return result;
    }

    InventoryAgentAutoLootList list;
    list.Name = *nameText;
    if (const auto *subText = findKey(header, "subcontainers"))
    {
        if (!ParseStrictBoolText(*subText, list.IncludeSubcontainers))
        {
            result.Error = "Invalid subcontainers value in [AutoLootList].";
            return result;
        }
        list.HasIncludeSubcontainers = true;
    }
    if (const auto *openText = findKey(header, "autoopencorpses"))
    {
        if (!ParseStrictBoolText(*openText, list.AutoOpenCorpses))
        {
            result.Error = "Invalid autoopencorpses value in [AutoLootList].";
            return result;
        }
        list.HasAutoOpenCorpses = true;
    }

    size_t expectedSections = 1; // AutoLootList
    for (int i = 0; i < ruleCount; ++i)
    {
        const astr_t ruleSecName = "Rule" + std::to_string(i);
        const auto ruleIt = sections.find(ruleSecName);
        if (ruleIt == sections.end())
        {
            result.Error = "Missing [" + ruleSecName + "] section.";
            return result;
        }
        ++expectedSections;
        const auto &ruleSec = ruleIt->second;

        const auto *graphicText = findKey(ruleSec, "graphic");
        uint32_t graphic = 0;
        if (graphicText == nullptr || !ParseUnsignedText(*graphicText, graphic) ||
            graphic == 0 || graphic > UINT16_MAX)
        {
            result.Error = "Invalid graphic in [" + ruleSecName + "].";
            return result;
        }

        bool matchHue = true;
        if (const auto *matchHueText = findKey(ruleSec, "matchhue"))
        {
            if (!ParseStrictBoolText(*matchHueText, matchHue))
            {
                result.Error = "Invalid matchhue in [" + ruleSecName + "].";
                return result;
            }
        }

        uint32_t hue = 0;
        if (const auto *hueText = findKey(ruleSec, "hue"))
        {
            if (!ParseUnsignedText(*hueText, hue) || hue > UINT16_MAX)
            {
                result.Error = "Invalid hue in [" + ruleSecName + "].";
                return result;
            }
        }
        else if (matchHue)
        {
            result.Error = "Missing hue in [" + ruleSecName + "].";
            return result;
        }

        uint32_t destination = 0;
        if (const auto *destText = findKey(ruleSec, "destination"))
        {
            if (!ParseUnsignedText(*destText, destination) || destination == 0xFFFFFFFFu)
            {
                result.Error = "Invalid destination in [" + ruleSecName + "].";
                return result;
            }
        }

        InventoryAgentRule rule;
        rule.Graphic = static_cast<uint16_t>(graphic);
        rule.Hue = static_cast<uint16_t>(hue);
        rule.MatchHue = matchHue;
        rule.Quantity = 1;
        rule.Destination = destination;

        int propCount = 0;
        if (const auto *propCountText = findKey(ruleSec, "properties"))
        {
            if (!InventoryAgent_ParsePropertyInteger(*propCountText, propCount) ||
                propCount < 0 || propCount > InventoryAgent_MaxProperties)
            {
                result.Error = "Invalid properties count in [" + ruleSecName + "].";
                return result;
            }
        }
        for (int k = 0; k < propCount; ++k)
        {
            const astr_t propSecName = ruleSecName + "Property" + std::to_string(k);
            const auto propIt = sections.find(propSecName);
            if (propIt == sections.end())
            {
                result.Error = "Missing [" + propSecName + "] section.";
                return result;
            }
            ++expectedSections;
            const auto &propSec = propIt->second;
            InventoryAgentPropertyRule prop;
            const auto *clilocText = findKey(propSec, "cliloc");
            if (clilocText == nullptr || !InventoryAgent_ParsePropertyInteger(*clilocText, prop.Cliloc))
            {
                result.Error = "Invalid cliloc in [" + propSecName + "].";
                return result;
            }
            if (const auto *minText = findKey(propSec, "minimum"))
            {
                if (!InventoryAgent_ParsePropertyInteger(*minText, prop.Minimum))
                {
                    result.Error = "Invalid minimum in [" + propSecName + "].";
                    return result;
                }
                prop.HasMinimum = true;
            }
            if (const auto *maxText = findKey(propSec, "maximum"))
            {
                if (!InventoryAgent_ParsePropertyInteger(*maxText, prop.Maximum))
                {
                    result.Error = "Invalid maximum in [" + propSecName + "].";
                    return result;
                }
                prop.HasMaximum = true;
            }
            if (!InventoryAgent_ValidPropertyRule(prop))
            {
                result.Error = "Invalid property range in [" + propSecName + "].";
                return result;
            }
            rule.Properties.push_back(prop);
        }

        list.Rules.push_back(std::move(rule));
    }

    if (sections.size() != expectedSections)
    {
        result.Error = "AutoLoot INI contains unexpected or mismatched sections.";
        return result;
    }

    result.Lists.push_back(std::move(list));
    result.Success = true;
    return result;
}

} // namespace inventory_plan_detail

inline bool InventoryAgent_FindPropertyCliloc(const astr_t &name, int &outCliloc)
{
    return inventory_plan_detail::FindNamedPropertyCliloc(name, outCliloc);
}

inline astr_t InventoryAgent_ExportAutoLootIni(const InventoryAgentProfile &profile)
{
    if (profile.Mode != InventoryAgentMode::AutoLoot || !inventory_plan_detail::SafeName(profile.Name))
    {
        return {};
    }
    astr_t out;
    out += "[AutoLootList]\n";
    out += "format=crossuo-autoloot\n";
    out += "version=1\n";
    out += "name=" + inventory_plan_detail::EscapeIni(profile.Name) + "\n";
    out += astr_t("subcontainers=") + (profile.IncludeSubcontainers ? "true\n" : "false\n");
    out += astr_t("autoopencorpses=") + (profile.AutoOpenCorpses ? "true\n" : "false\n");
    out += "rules=" + std::to_string(profile.Rules.size()) + "\n\n";

    for (size_t i = 0; i < profile.Rules.size(); ++i)
    {
        const auto &rule = profile.Rules[i];
        const astr_t sec = "Rule" + std::to_string(i);
        out += "[" + sec + "]\n";
        out += "graphic=" + inventory_plan_detail::FormatHex16(rule.Graphic) + "\n";
        out += "hue=" + inventory_plan_detail::FormatHex16(rule.Hue) + "\n";
        out += astr_t("matchhue=") + (rule.MatchHue ? "true\n" : "false\n");
        if (rule.Destination != 0 && rule.Destination != 0xFFFFFFFFu)
        {
            out += "destination=" + inventory_plan_detail::FormatHex32(rule.Destination) + "\n";
        }
        if (!rule.Properties.empty())
        {
            out += "properties=" + std::to_string(rule.Properties.size()) + "\n";
        }
        out += "\n";

        for (size_t k = 0; k < rule.Properties.size(); ++k)
        {
            const auto &prop = rule.Properties[k];
            out += "[" + sec + "Property" + std::to_string(k) + "]\n";
            out += "cliloc=" + std::to_string(prop.Cliloc) + "\n";
            if (prop.HasMinimum)
            {
                out += "minimum=" + std::to_string(prop.Minimum) + "\n";
            }
            if (prop.HasMaximum)
            {
                out += "maximum=" + std::to_string(prop.Maximum) + "\n";
            }
            out += "\n";
        }
    }
    return out;
}

inline astr_t InventoryAgent_ExportAutoLootJson(const InventoryAgentProfile &profile)
{
    if (profile.Mode != InventoryAgentMode::AutoLoot || !inventory_plan_detail::SafeName(profile.Name))
    {
        return {};
    }
    astr_t out;
    out += "{\n";
    out += "  \"format\": \"crossuo-autoloot\",\n";
    out += "  \"version\": 1,\n";
    out += "  \"name\": " + inventory_plan_detail::EscapeJson(profile.Name) + ",\n";
    out += astr_t("  \"includeSubcontainers\": ") + (profile.IncludeSubcontainers ? "true,\n" : "false,\n");
    out += astr_t("  \"autoOpenCorpses\": ") + (profile.AutoOpenCorpses ? "true,\n" : "false,\n");
    out += "  \"rules\": [";
    if (!profile.Rules.empty())
    {
        out += "\n";
    }
    for (size_t i = 0; i < profile.Rules.size(); ++i)
    {
        const auto &rule = profile.Rules[i];
        out += "    {\n";
        out += "      \"graphic\": " + inventory_plan_detail::EscapeJson(inventory_plan_detail::FormatHex16(rule.Graphic)) + ",\n";
        out += "      \"hue\": " + std::to_string(rule.Hue) + ",\n";
        out += astr_t("      \"matchHue\": ") + (rule.MatchHue ? "true" : "false");
        if (rule.Destination != 0 && rule.Destination != 0xFFFFFFFFu)
        {
            out += ",\n      \"destination\": " +
                inventory_plan_detail::EscapeJson(inventory_plan_detail::FormatHex32(rule.Destination));
        }
        if (!rule.Properties.empty())
        {
            out += ",\n      \"properties\": [\n";
            for (size_t k = 0; k < rule.Properties.size(); ++k)
            {
                const auto &prop = rule.Properties[k];
                out += "        { \"cliloc\": " + std::to_string(prop.Cliloc);
                if (prop.HasMinimum)
                {
                    out += ", \"minimum\": " + std::to_string(prop.Minimum);
                }
                if (prop.HasMaximum)
                {
                    out += ", \"maximum\": " + std::to_string(prop.Maximum);
                }
                out += (k + 1 < rule.Properties.size()) ? " },\n" : " }\n";
            }
            out += "      ]";
        }
        out += "\n    }";
        out += (i + 1 < profile.Rules.size()) ? ",\n" : "\n";
    }
    out += profile.Rules.empty() ? "]\n" : "  ]\n";
    out += "}\n";
    return out;
}

inline InventoryAgentAutoLootImportResult InventoryAgent_ParseAutoLootImport(const astr_t &rawInput)
{
    InventoryAgentAutoLootImportResult result;
    if (rawInput.empty())
    {
        result.Error = "Paste or choose an AutoLoot list first.";
        return result;
    }
    if (rawInput.size() > InventoryAgent_MaxExchangeBytes)
    {
        result.Error = "AutoLoot list exceeds the 256 KiB size limit.";
        return result;
    }
    if (rawInput.find('\0') != astr_t::npos)
    {
        result.Error = "AutoLoot list contains invalid binary characters.";
        return result;
    }

    astr_t input = rawInput;
    if (input.size() >= 3 &&
        static_cast<unsigned char>(input[0]) == 0xEF &&
        static_cast<unsigned char>(input[1]) == 0xBB &&
        static_cast<unsigned char>(input[2]) == 0xBF)
    {
        input.erase(0, 3);
    }

    size_t first = 0;
    while (first < input.size() &&
           (input[first] == ' ' || input[first] == '\t' || input[first] == '\r' || input[first] == '\n'))
    {
        ++first;
    }
    if (first >= input.size())
    {
        result.Error = "AutoLoot list is empty.";
        return result;
    }

    const char lead = input[first];
    if (lead == '{')
    {
        return inventory_plan_detail::ParseJsonImport(input);
    }
    if (lead == '[')
    {
        size_t after = first + 1;
        while (after < input.size() &&
               (input[after] == ' ' || input[after] == '\t' || input[after] == '\r' || input[after] == '\n'))
        {
            ++after;
        }
        if (after < input.size() && (input[after] == '{' || input[after] == ']' || input[after] == '"'))
        {
            return inventory_plan_detail::ParseJsonImport(input);
        }
        return inventory_plan_detail::ParseIniImport(input);
    }
    if (lead == ';' || lead == '#')
    {
        return inventory_plan_detail::ParseIniImport(input);
    }

    result.Error = "Unrecognized AutoLoot list format. Expected INI or JSON.";
    return result;
}

} // namespace xuoassist
