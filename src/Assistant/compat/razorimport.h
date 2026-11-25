// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "razorhotkeys.h"

namespace xuoassist {

enum class RazorImportKind
{
    Script,
    Macro,
    Profile,
    UOSteam,
};

struct RazorImportIssue
{
    std::size_t Line = 0;
    std::string Message;
};

struct RazorProfileOption
{
    std::string Key;
    std::string Value;
    std::string File = "Options.ini";
    std::string Section = "main";
};

struct ClassicCounterSelection
{
    std::string Name;
    bool Icon;
};

struct RazorImportResult
{
    std::string JavaScript;
    std::vector<RazorProfileOption> ProfileOptions;
    std::vector<RazorProfileHotkey> ProfileHotkeys;
    std::vector<RazorImportIssue> Notices;
    std::vector<RazorImportIssue> Issues;
    std::vector<ClassicCounterSelection> ClassicCounters;
    bool HasClassicCounterSelections = false;
    int ClassicCounterWarning = 0;
    std::size_t Imported = 0;
    std::size_t Ignored = 0;

    bool CanImport() const { return Imported != 0 && Issues.empty(); }
};

// Parses data only. The returned script must still be reviewed and run separately.
RazorImportResult ParseRazorImport(RazorImportKind kind, const std::string &input);
RazorImportResult AddClassicCounterDefinitions(const RazorImportResult &profile, const std::string &input);

} // namespace xuoassist
