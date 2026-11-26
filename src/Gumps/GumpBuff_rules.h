// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace xuobuff {

constexpr uint32_t kBuffIndefiniteTimer = 0xFFFFFFFFu;
constexpr uint32_t kBuffWarningSeconds = 60u;
constexpr uint32_t kBuffCriticalSeconds = 15u;
constexpr int kBuffMinSlotWidth = 44;
constexpr int kBuffIconPadX = 8;
constexpr int kBuffIconFramePad = 4;
constexpr int kBuffProgressBarHeight = 3;
constexpr int kBuffTimerPillHeight = 14;
constexpr int kBuffSlotSpacingX = 4;
constexpr int kBuffSlotSpacingY = 4;

enum class BuffUrgency : uint8_t
{
    Indefinite = 0,
    Normal = 1,
    Warning = 2,
    Critical = 3
};

struct BuffSlotSize
{
    int Width = kBuffMinSlotWidth;
    int Height = 28 + kBuffIconFramePad + 1 + kBuffProgressBarHeight + 1 + kBuffTimerPillHeight;
};

inline BuffSlotSize ComputeSlotSize(int iconWidth, int iconHeight)
{
    const int safeW = iconWidth > 0 ? iconWidth : 28;
    const int safeH = iconHeight > 0 ? iconHeight : 28;
    BuffSlotSize size;
    size.Width = std::max(safeW + kBuffIconPadX, kBuffMinSlotWidth);
    size.Height = safeH + kBuffIconFramePad + 1 + kBuffProgressBarHeight + 1 + kBuffTimerPillHeight;
    return size;
}

inline bool HasTimer(uint32_t expiryTicks)
{
    return expiryTicks != kBuffIndefiniteTimer;
}

inline uint32_t ComputeRemainingMs(uint32_t expiryTicks, uint32_t nowTicks)
{
    if (!HasTimer(expiryTicks))
    {
        return 0u;
    }
    const int32_t delta = static_cast<int32_t>(expiryTicks - nowTicks);
    return delta > 0 ? static_cast<uint32_t>(delta) : 0u;
}

inline uint32_t RemainingSecondsCeil(uint32_t remainingMs)
{
    return remainingMs == 0u ? 0u : (remainingMs + 999u) / 1000u;
}

inline BuffUrgency ClassifyUrgency(uint32_t remainingSeconds, bool hasTimer)
{
    if (!hasTimer)
    {
        return BuffUrgency::Indefinite;
    }
    if (remainingSeconds <= kBuffCriticalSeconds)
    {
        return BuffUrgency::Critical;
    }
    if (remainingSeconds <= kBuffWarningSeconds)
    {
        return BuffUrgency::Warning;
    }
    return BuffUrgency::Normal;
}

inline float ComputeProgressFraction(uint32_t remainingMs, uint32_t totalDurationMs, bool hasTimer)
{
    if (!hasTimer)
    {
        return 1.0f;
    }
    if (totalDurationMs == 0u)
    {
        return remainingMs > 0u ? 1.0f : 0.0f;
    }
    if (remainingMs >= totalDurationMs)
    {
        return 1.0f;
    }
    return static_cast<float>(remainingMs) / static_cast<float>(totalDurationMs);
}

inline std::string FormatBarTimer(uint32_t remainingSeconds, bool hasTimer)
{
    if (!hasTimer)
    {
        return "ON";
    }
    char buf[32]{};
    if (remainingSeconds >= 3600u)
    {
        const unsigned hours = remainingSeconds / 3600u;
        const unsigned minutes = (remainingSeconds % 3600u) / 60u;
        const unsigned seconds = remainingSeconds % 60u;
        std::snprintf(buf, sizeof(buf), "%u:%02u:%02u", hours, minutes, seconds);
        return buf;
    }
    if (remainingSeconds >= 60u)
    {
        const unsigned minutes = remainingSeconds / 60u;
        const unsigned seconds = remainingSeconds % 60u;
        std::snprintf(buf, sizeof(buf), "%u:%02u", minutes, seconds);
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%us", static_cast<unsigned>(remainingSeconds));
    return buf;
}

inline std::wstring FormatTooltipDuration(uint32_t remainingSeconds, bool hasTimer)
{
    if (!hasTimer)
    {
        return L"Duration: Active until canceled";
    }
    char buf[64]{};
    if (remainingSeconds >= 3600u)
    {
        const unsigned hours = remainingSeconds / 3600u;
        const unsigned minutes = (remainingSeconds % 3600u) / 60u;
        const unsigned seconds = remainingSeconds % 60u;
        std::snprintf(buf, sizeof(buf), "Time remaining: %uh %02um %02us", hours, minutes, seconds);
    }
    else if (remainingSeconds >= 60u)
    {
        const unsigned minutes = remainingSeconds / 60u;
        const unsigned seconds = remainingSeconds % 60u;
        std::snprintf(buf, sizeof(buf), "Time remaining: %um %02us", minutes, seconds);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "Time remaining: %us", static_cast<unsigned>(remainingSeconds));
    }

    std::wstring out;
    out.reserve(std::char_traits<char>::length(buf));
    for (const char *p = buf; *p != '\0'; ++p)
    {
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    }
    return out;
}

inline std::wstring StripOuterLeftTags(const std::wstring &text)
{
    static const std::wstring kOpen = L"<left>";
    static const std::wstring kClose = L"</left>";
    size_t start = 0;
    size_t end = text.size();

    if (text.size() >= kOpen.size() && text.compare(0, kOpen.size(), kOpen) == 0)
    {
        start = kOpen.size();
    }
    if (end >= start + kClose.size() && text.compare(end - kClose.size(), kClose.size(), kClose) == 0)
    {
        end -= kClose.size();
    }
    while (start < end && (text[start] == L'\n' || text[start] == L'\r'))
    {
        ++start;
    }
    while (end > start && (text[end - 1] == L'\n' || text[end - 1] == L'\r'))
    {
        --end;
    }
    return text.substr(start, end - start);
}

inline const wchar_t *UrgencyTooltipColorHex(BuffUrgency urgency)
{
    switch (urgency)
    {
        case BuffUrgency::Critical:
            return L"#FF6B6B";
        case BuffUrgency::Warning:
            return L"#FFD166";
        case BuffUrgency::Normal:
            return L"#8CE99A";
        case BuffUrgency::Indefinite:
        default:
            return L"#A5D8FF";
    }
}

inline std::wstring BuildTooltipText(const std::wstring &rawText, uint32_t remainingSeconds, bool hasTimer)
{
    std::wstring body = StripOuterLeftTags(rawText);
    if (body.empty())
    {
        body = L"Active buff";
    }

    const BuffUrgency urgency = ClassifyUrgency(remainingSeconds, hasTimer);
    const std::wstring durationLine = FormatTooltipDuration(remainingSeconds, hasTimer);

    std::wstring result = L"<left>";
    result += body;
    result += L"\n<basefont color=";
    result += UrgencyTooltipColorHex(urgency);
    result += L">";
    result += durationLine;
    result += L"</basefont></left>";
    return result;
}

} // namespace xuobuff
