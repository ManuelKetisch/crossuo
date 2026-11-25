// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string>

namespace xuoassist {

struct CHotkeyEntry;

// The IDs are Classic Razor's stable built-in localization IDs, rather than
// indexes into its profile-dependent list of hotkeys.
enum class RazorHotkeyGroup
{
    Targeting,
    Attack,
    BandageHealing,
    UseObject,
    AllNames,
    CastSpell,
    UseSkill,
    InventoryAgent,
    NewTargetSystem,
    ToggleHotkeys,
    ToggleMap,
    StopMacro,
    ClientAction,
};

struct RazorHotkeyDefinition
{
    unsigned Id;
    const char *Name;
    RazorHotkeyGroup Group;
    unsigned Action;
};

const RazorHotkeyDefinition *FindRazorHotkey(const std::string &name);
const RazorHotkeyDefinition *FindRazorHotkey(unsigned id);
std::string RazorHotkeyKeyName(int virtualKey);

struct RazorProfileHotkey
{
    unsigned Id;
    int VirtualKey;
    unsigned Modifiers;
    bool PassToGame;
};

// Builds native bindings for an inactive imported profile and dispatches a
// single built-in action during a running script without synthesizing input.
bool RazorHotkeys_BuildBinding(const RazorProfileHotkey &source, CHotkeyEntry &entry);
bool RazorHotkeys_Execute(unsigned loc);

} // namespace xuoassist
