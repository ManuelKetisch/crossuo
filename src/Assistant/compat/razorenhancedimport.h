// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "razorimport.h"

namespace xuoassist {

enum class RazorEnhancedTable
{
    General,
    Hotkeys,
    ToolbarItems,
    AutoLoot,
    Scavenger,
    Organizer,
    Restock,
    Dress,
    Buy,
    Sell,
    BandageHeal,
    Targets,
    Friends,
    Scripts,
    Count
};

struct RazorEnhancedProfileInput
{
    RazorEnhancedTable Table;
    std::string Text;
};

const char *RazorEnhancedTableFile(RazorEnhancedTable table);
RazorImportResult ParseRazorEnhancedProfile(const std::vector<RazorEnhancedProfileInput> &inputs);

} // namespace xuoassist
