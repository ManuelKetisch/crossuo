// SPDX-License-Identifier: AGPL-3.0-or-later

#include "tabinventory.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include <external/gfx/imgui/imgui.h>

#include "../config/assistantconfig.h"
#include "../config/configio.h"
#include "../core/assist.h"
#include "../core/inventoryagent.h"
#include "../core/inventoryagent_plan.h"
#include "../core/world.h"
#include "Globals.h"
#include "Target.h"

#if !defined(XUO_EMSC)
#include <external/tinyfiledialogs.h>
#include <memory>
#endif

namespace xuoassist {
namespace {

enum class TargetPurpose
{
    None,
    Source,
    Destination,
    Rule,
    RuleDestination,
    UseItem,
    UseContainer,
};

int s_Selected = 0;
int s_NewMode = 0;
char s_NewName[64]{};
char s_Alias[64]{};
astr_t s_AliasProfile;
astr_t s_Notice;
TargetPurpose s_Target = TargetPurpose::None;
astr_t s_TargetPreset;
astr_t s_TargetAssistantProfile;
uint32_t s_TargetCharacter = 0;
int s_TargetRuleIndex = -1;
uint16_t s_NewLootGraphic = 0;
uint16_t s_NewLootHue = 0;
bool s_NewLootMatchHue = true;
InventoryAgentPropertyRule s_PropertyDraft;
int s_EditingProperty = -1;

int s_ExportFormat = 0; // 0 = INI, 1 = JSON
std::array<char, InventoryAgent_MaxExchangeBytes + 1> s_ExchangeBuffer{};
InventoryAgentAutoLootImportResult s_ExchangePreview;
bool s_HasExchangePreview = false;
int s_ExchangeListIndex = 0;
char s_ImportPresetName[64]{};

void ResetExchangePreview()
{
    s_HasExchangePreview = false;
    s_ExchangePreview = {};
    s_ExchangeListIndex = 0;
}

void SyncImportPresetName()
{
    if (s_HasExchangePreview && s_ExchangePreview.Success && !s_ExchangePreview.Lists.empty())
    {
        s_ExchangeListIndex = std::max(
            0, std::min(s_ExchangeListIndex, static_cast<int>(s_ExchangePreview.Lists.size()) - 1));
        std::snprintf(
            s_ImportPresetName, sizeof(s_ImportPresetName), "%s",
            s_ExchangePreview.Lists[s_ExchangeListIndex].Name.c_str());
    }
    else
    {
        s_ImportPresetName[0] = '\0';
    }
}

void SetExchangeText(const astr_t &text)
{
    const size_t count = std::min(text.size(), s_ExchangeBuffer.size() - 1);
    std::copy_n(text.data(), count, s_ExchangeBuffer.data());
    s_ExchangeBuffer[count] = '\0';
}

void PreviewExchangeBuffer()
{
    ResetExchangePreview();
    s_ExchangePreview = InventoryAgent_ParseAutoLootImport(s_ExchangeBuffer.data());
    s_HasExchangePreview = true;
    SyncImportPresetName();
}

#if !defined(XUO_EMSC)
bool ReadExchangeFile(const fs_path &path, astr_t &outText)
{
    if (!fs_path_is_file(path))
    {
        return false;
    }
    const std::unique_ptr<FILE, decltype(&fs_close)> file(fs_open(path, FS_READ), fs_close);
    if (!file || std::fseek(file.get(), 0, SEEK_END) != 0)
    {
        return false;
    }
    const auto bytes = std::ftell(file.get());
    if (bytes < 0 || bytes > static_cast<long>(InventoryAgent_MaxExchangeBytes) ||
        std::fseek(file.get(), 0, SEEK_SET) != 0)
    {
        return false;
    }
    outText.resize(static_cast<size_t>(bytes));
    return outText.empty() || std::fread(&outText[0], 1, outText.size(), file.get()) == outText.size();
}
#endif

void Save()
{
    if (!InventoryAgent_Save())
    {
        s_Notice = "The inventory profile could not be saved.";
    }
}

bool InBackpack(uint32_t serial)
{
    const auto *pack = g_Player != nullptr ? g_Player->FindLayer(OL_BACKPACK) : nullptr;
    if (pack == nullptr)
    {
        return false;
    }

    for (int depth = 0; depth < 16 && serial != 0 && serial != 0xFFFFFFFF; ++depth)
    {
        if (serial == pack->Serial)
        {
            return true;
        }
        const auto *object = g_World->FindWorldObject(serial);
        serial = object != nullptr ? object->Container : 0;
    }
    return false;
}

void AddUseItem(InventoryAgentProfile &profile, const CGameItem &item)
{
    const auto *pack = g_Player->FindLayer(OL_BACKPACK);
    auto &items = profile.UseItems;
    if (pack != nullptr && item.Serial != pack->Serial && !item.IsCorpse() &&
        InBackpack(item.Serial) && items.size() < InventoryAgent_MaxUseItems &&
        std::find(items.begin(), items.end(), item.Serial) == items.end())
    {
        items.push_back(item.Serial);
    }
}

bool StartTarget(InventoryAgentProfile &profile, TargetPurpose purpose, const char *prompt)
{
    if (g_TargetHandler != nullptr || ::g_Target.IsTargeting())
    {
        s_Notice = "Finish the current target first.";
        return false;
    }

    s_Notice.clear();
    s_Target = purpose;
    s_TargetCharacter = g_PlayerSerial;
    s_TargetPreset = profile.Name;
    s_TargetAssistantProfile = g_AssistantConfig.ProfileName;
    g_TargetHandler = &CAssistant::HandleTargetInventory;
    g_Assistant.RequestTarget(prompt);
    return true;
}

void ContainerPicker(
    InventoryAgentProfile &profile, const char *label, uint32_t &value, TargetPurpose purpose)
{
    ImGui::PushID(label);
    if (value == 0)
    {
        ImGui::Text("%s   Backpack", label);
    }
    else
    {
        auto *item = g_World->FindWorldItem(value);
        const auto name = item != nullptr ? item->GetName() : astr_t{};
        if (name.empty())
        {
            ImGui::Text("%s   0x%08X", label, value);
        }
        else
        {
            ImGui::Text("%s   %s", label, name.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Target"))
    {
        StartTarget(profile, purpose, profile.Mode == InventoryAgentMode::AutoLoot ?
            "Choose your backpack or a bag inside it." : "Choose a container.");
    }
    if (value != 0)
    {
        ImGui::SameLine();
        if (ImGui::Button("Use backpack"))
        {
            value = 0;
            Save();
        }
    }
    ImGui::PopID();
}

void DrawUseItems(InventoryAgentProfile &profile)
{
    if (ImGui::Button("Add item"))
    {
        StartTarget(profile, TargetPurpose::UseItem, "Choose an item in your backpack.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Add bag contents"))
    {
        StartTarget(profile, TargetPurpose::UseContainer, "Choose an open bag in your backpack.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear list"))
    {
        profile.UseItems.clear();
        Save();
    }
    if (profile.UseItems.empty())
    {
        ImGui::TextDisabled("This list is empty.");
    }
    for (int i = 0; i < static_cast<int>(profile.UseItems.size()); ++i)
    {
        const auto serial = profile.UseItems[i];
        ImGui::PushID(i);
        if (ImGui::Button("Remove"))
        {
            profile.UseItems.erase(profile.UseItems.begin() + i);
            Save();
            ImGui::PopID();
            break;
        }
        ImGui::SameLine();
        const auto *item = g_World->FindWorldItem(serial);
        if (item != nullptr)
        {
            ImGui::Text("0x%08X   graphic 0x%04X", serial, item->Graphic);
        }
        else
        {
            ImGui::Text("0x%08X   unavailable", serial);
        }
        ImGui::PopID();
    }
}

bool AddRule(InventoryAgentProfile &profile, const InventoryAgentRule &rule)
{
    if (rule.Graphic == 0 || profile.Rules.size() >= InventoryAgent_MaxRules ||
        std::any_of(profile.Rules.begin(), profile.Rules.end(), [&rule](const auto &other) {
            return other.Graphic == rule.Graphic &&
                (!other.MatchHue || (rule.MatchHue && other.Hue == rule.Hue));
        }))
    {
        return false;
    }
    profile.Rules.push_back(rule);
    return true;
}

astr_t PropertyLabel(int cliloc)
{
    for (const auto &property : InventoryAgent_NamedProperties)
    {
        if (property.Cliloc == cliloc)
        {
            return property.Name;
        }
    }
    return "Cliloc " + str_from(cliloc);
}

void DrawPropertyRules(InventoryAgentRule &rule)
{
    if (!ImGui::TreeNode("Property ranges"))
    {
        return;
    }

    ImGui::TextWrapped("An item must satisfy every range. Missing properties leave it on the corpse.");
    for (int i = 0; i < static_cast<int>(rule.Properties.size()); ++i)
    {
        const auto &property = rule.Properties[i];
        auto label = PropertyLabel(property.Cliloc);
        if (property.HasMinimum)
        {
            label += "  Min " + str_from(property.Minimum);
        }
        if (property.HasMaximum)
        {
            label += "  Max " + str_from(property.Maximum);
        }
        ImGui::PushID(i);
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        const bool edit = ImGui::SmallButton("Edit");
        ImGui::SameLine();
        const bool remove = ImGui::SmallButton("Remove");
        ImGui::PopID();
        if (remove)
        {
            rule.Properties.erase(rule.Properties.begin() + i);
            Save();
            break;
        }
        if (edit)
        {
            s_PropertyDraft = property;
            s_EditingProperty = i;
            ImGui::OpenPopup("Property range");
        }
    }

    ImGui::BeginDisabled(rule.Properties.size() >= InventoryAgent_MaxProperties);
    if (ImGui::Button("Add property range"))
    {
        s_PropertyDraft = { 1060436, 1, 0, true, false };
        s_EditingProperty = -1;
        ImGui::OpenPopup("Property range");
    }
    ImGui::EndDisabled();

    if (ImGui::BeginPopup("Property range"))
    {
        if (ImGui::BeginCombo("Common properties", PropertyLabel(s_PropertyDraft.Cliloc).c_str()))
        {
            for (const auto &property : InventoryAgent_NamedProperties)
            {
                if (ImGui::Selectable(property.Name, property.Cliloc == s_PropertyDraft.Cliloc))
                {
                    s_PropertyDraft.Cliloc = property.Cliloc;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputInt("Cliloc ID", &s_PropertyDraft.Cliloc, 0);
        ImGui::Checkbox("Use minimum", &s_PropertyDraft.HasMinimum);
        ImGui::BeginDisabled(!s_PropertyDraft.HasMinimum);
        ImGui::InputInt("Minimum", &s_PropertyDraft.Minimum);
        ImGui::EndDisabled();
        ImGui::Checkbox("Use maximum", &s_PropertyDraft.HasMaximum);
        ImGui::BeginDisabled(!s_PropertyDraft.HasMaximum);
        ImGui::InputInt("Maximum", &s_PropertyDraft.Maximum);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Use a property with one numeric value.");
        const bool valid = InventoryAgent_ValidPropertyRule(s_PropertyDraft);
        if (!valid)
        {
            ImGui::TextWrapped("Enter a positive cliloc ID and a valid minimum or maximum.");
        }
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button("Save range"))
        {
            if (s_EditingProperty < 0 && rule.Properties.size() < InventoryAgent_MaxProperties)
            {
                rule.Properties.push_back(s_PropertyDraft);
            }
            else if (s_EditingProperty >= 0 && s_EditingProperty < static_cast<int>(rule.Properties.size()))
            {
                rule.Properties[s_EditingProperty] = s_PropertyDraft;
            }
            Save();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::TreePop();
}

void DrawRules(InventoryAgentProfile &profile)
{
    if (ImGui::Button("Add item rule"))
    {
        StartTarget(profile, TargetPurpose::Rule, "Choose an item to match by type and hue.");
    }
    ImGui::SameLine();
    if (profile.Mode == InventoryAgentMode::AutoLoot && ImGui::Button("Add by graphic ID"))
    {
        ImGui::OpenPopup("Loot rule");
    }
    if (ImGui::BeginPopup("Loot rule"))
    {
        constexpr auto flags = ImGuiInputTextFlags_CharsHexadecimal;
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputScalar("Graphic ID", ImGuiDataType_U16, &s_NewLootGraphic, nullptr, nullptr, "%04X", flags);
        ImGui::Checkbox("Match hue", &s_NewLootMatchHue);
        ImGui::BeginDisabled(!s_NewLootMatchHue);
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputScalar("Hue", ImGuiDataType_U16, &s_NewLootHue, nullptr, nullptr, "%04X", flags);
        ImGui::EndDisabled();
        if (ImGui::Button("Add rule"))
        {
            if (AddRule(profile, { s_NewLootGraphic, s_NewLootHue, s_NewLootMatchHue, 1 }))
            {
                s_Notice.clear();
                Save();
            }
            else
            {
                s_Notice = "Enter a unique graphic ID and hue, or remove an unused rule.";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (profile.Mode == InventoryAgentMode::AutoLoot)
    {
        ImGui::SameLine();
    }
    if (ImGui::Button("Clear rules"))
    {
        TabInventory_CancelPendingTarget();
        profile.Rules.clear();
        Save();
    }
    if (profile.Rules.empty())
    {
        ImGui::TextDisabled("No item rules yet.");
    }

    for (int i = 0; i < static_cast<int>(profile.Rules.size()); ++i)
    {
        ImGui::PushID(i);
        if (ImGui::Button("Remove"))
        {
            TabInventory_CancelPendingTarget();
            profile.Rules.erase(profile.Rules.begin() + i);
            Save();
            ImGui::PopID();
            break;
        }

        auto &rule = profile.Rules[i];
        ImGui::SameLine();
        ImGui::Text("0x%04X   hue 0x%04X", rule.Graphic, rule.Hue);
        ImGui::SameLine();
        if (ImGui::Checkbox("Match hue", &rule.MatchHue))
        {
            Save();
        }
        if (profile.Mode == InventoryAgentMode::AutoLoot)
        {
            ImGui::Indent();
            if (rule.Destination == 0)
            {
                ImGui::TextDisabled("Loot bag: Default");
            }
            else
            {
                auto *bag = g_World->FindWorldItem(rule.Destination);
                const auto bagName = bag != nullptr ? bag->GetName() : astr_t{};
                if (bagName.empty())
                {
                    ImGui::Text("Loot bag: 0x%08X", rule.Destination);
                }
                else
                {
                    ImGui::Text("Loot bag: %s", bagName.c_str());
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Set bag"))
            {
                s_TargetRuleIndex = i;
                StartTarget(profile, TargetPurpose::RuleDestination, "Choose your backpack or a bag inside it.");
            }
            if (rule.Destination != 0)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Use default"))
                {
                    rule.Destination = 0;
                    Save();
                }
            }
            ImGui::PushID(profile.Name.c_str());
            DrawPropertyRules(rule);
            ImGui::PopID();
            ImGui::Unindent();
        }
        if (profile.Mode == InventoryAgentMode::Restock)
        {
            ImGui::SetNextItemWidth(84.0f);
            int quantity = rule.Quantity;
            if (ImGui::InputInt("Target count", &quantity, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue))
            {
                rule.Quantity = std::max(1, std::min(quantity, InventoryAgent_MaxQuantity));
                Save();
            }
        }
        ImGui::PopID();
    }
}

} // namespace

void TabInventory_Draw()
{
    auto &profiles = InventoryAgent_Profiles();
    if (g_World == nullptr || g_Player == nullptr || profiles.empty())
    {
        ImGui::TextDisabled("Enter the world to configure inventory agents.");
        return;
    }

    s_Selected = std::max(0, std::min(s_Selected, static_cast<int>(profiles.size()) - 1));
    auto &profile = profiles[s_Selected];
    if (ImGui::BeginCombo("Inventory agent", profile.Name.c_str()))
    {
        for (int i = 0; i < static_cast<int>(profiles.size()); ++i)
        {
            if (ImGui::Selectable(profiles[i].Name.c_str(), s_Selected == i))
            {
                TabInventory_CancelPendingTarget();
                s_Selected = i;
            }
        }
        ImGui::EndCombo();
    }

    // Selection can change within BeginCombo.
    auto &selected = profiles[s_Selected];
    const bool busy = InventoryAgent_Busy();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button(selected.Mode == InventoryAgentMode::UseOnce ? "Use next item" : "Run"))
    {
        s_Notice.clear();
        TabInventory_CancelPendingTarget();
        InventoryAgent_Run(selected.Name);
    }
    if (selected.Mode == InventoryAgentMode::AutoLoot)
    {
        ImGui::SameLine();
        if (ImGui::Button("Loot open corpses once"))
        {
            s_Notice.clear();
            TabInventory_CancelPendingTarget();
            InventoryAgent_AutoLootOnce(selected.Name);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if (ImGui::Button("Stop"))
    {
        InventoryAgent_Stop();
    }
    ImGui::EndDisabled();
    if (selected.Mode == InventoryAgentMode::Scavenger)
    {
        ImGui::SameLine();
        if (ImGui::Button("Clear attempted items"))
        {
            InventoryAgent_ClearScavengerCache();
        }
    }
    else if (selected.Mode == InventoryAgentMode::AutoLoot)
    {
        ImGui::SameLine();
        if (ImGui::Button("Clear opened corpses"))
        {
            s_Notice.clear();
            InventoryAgent_ClearAutoLootCorpseCache();
        }
    }

    const auto &status = s_Notice.empty() ? InventoryAgent_Status() : s_Notice;
    if (!status.empty())
    {
        ImGui::TextWrapped("%s", status.c_str());
    }
    ImGui::TextDisabled("%s", InventoryAgent_ModeName(selected.Mode));
    if (selected.Mode == InventoryAgentMode::AutoLoot)
    {
        ImGui::TextWrapped("Moves matching items from open creature corpses into your loot bag.");
    }

    ImGui::Separator();
    ImGui::BeginDisabled(InventoryAgent_Busy());
    if (s_AliasProfile != selected.Name)
    {
        std::snprintf(s_Alias, sizeof(s_Alias), "%s", selected.Name.c_str());
        s_AliasProfile = selected.Name;
    }
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Alias", s_Alias, sizeof(s_Alias), ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const astr_t name = str_trim(s_Alias);
        const auto folded = str_lower(name);
        if (!name.empty() && std::none_of(profiles.begin(), profiles.end(),
                                         [&selected, &folded](const auto &entry) {
                                             return &entry != &selected && str_lower(entry.Name) == folded;
                                         }))
        {
            TabInventory_CancelPendingTarget();
            selected.Name = name;
            s_AliasProfile = name;
            s_Notice.clear();
            Save();
        }
        else
        {
            s_Notice = "Choose a unique agent name.";
        }
    }

    if (selected.Mode == InventoryAgentMode::UseOnce)
    {
        DrawUseItems(selected);
    }
    else
    {
        if (selected.Mode != InventoryAgentMode::Scavenger && selected.Mode != InventoryAgentMode::AutoLoot)
        {
            ContainerPicker(selected, "Source", selected.Source, TargetPurpose::Source);
        }
        ContainerPicker(selected, "Destination", selected.Destination, TargetPurpose::Destination);
        if (selected.Mode != InventoryAgentMode::Scavenger &&
            ImGui::Checkbox("Include open bags inside containers", &selected.IncludeSubcontainers))
        {
            Save();
        }
        if (selected.Mode == InventoryAgentMode::AutoLoot)
        {
            if (ImGui::Checkbox("Auto-open nearby creature corpses", &selected.AutoOpenCorpses))
            {
                Save();
            }
            if (selected.AutoOpenCorpses)
            {
                ImGui::TextDisabled(
                    "Opens unopened creature corpses within 2 tiles, skipping player and staff bodies.");
            }
        }
        DrawRules(selected);
        if (selected.Mode == InventoryAgentMode::AutoLoot && ImGui::CollapsingHeader("Import and export rules"))
        {
            const char *formats[] = { "INI", "JSON" };
            ImGui::SetNextItemWidth(100.0f);
            ImGui::Combo("Export format", &s_ExportFormat, formats, IM_ARRAYSIZE(formats));
            ImGui::SameLine();
            if (ImGui::Button("Generate export"))
            {
                astr_t exported, error;
                if (InventoryAgent_ExportAutoLootList(selected.Name, s_ExportFormat == 1, exported, error))
                {
                    SetExchangeText(exported);
                    PreviewExchangeBuffer();
                    s_Notice = "Generated AutoLoot list export.";
                }
                else
                {
                    s_Notice = error;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy export"))
            {
                astr_t exported, error;
                if (InventoryAgent_ExportAutoLootList(selected.Name, s_ExportFormat == 1, exported, error))
                {
                    SetExchangeText(exported);
                    ImGui::SetClipboardText(exported.c_str());
                    s_Notice = "Copied AutoLoot list to clipboard.";
                }
                else
                {
                    s_Notice = error;
                }
            }
#if !defined(XUO_EMSC)
            ImGui::SameLine();
            if (ImGui::Button("Export to file"))
            {
                astr_t exported, error;
                if (InventoryAgent_ExportAutoLootList(selected.Name, s_ExportFormat == 1, exported, error))
                {
                    const char *iniPatterns[] = { "*.ini" };
                    const char *jsonPatterns[] = { "*.json" };
                    const astr_t defaultFile = selected.Name + (s_ExportFormat == 1 ? ".json" : ".ini");
                    const char *savePath = tinyfd_saveFileDialog(
                        "Export AutoLoot list",
                        defaultFile.c_str(),
                        1,
                        s_ExportFormat == 1 ? jsonPatterns : iniPatterns,
                        s_ExportFormat == 1 ? "AutoLoot JSON list" : "AutoLoot INI list");
                    if (savePath != nullptr)
                    {
                        if (AtomicFileWrite(fs_path_from(savePath), exported.data(), exported.size()))
                        {
                            s_Notice = "Exported AutoLoot list to file.";
                        }
                        else
                        {
                            s_Notice = "Could not write the export file.";
                        }
                    }
                }
                else
                {
                    s_Notice = error;
                }
            }
            if (ImGui::Button("Load file"))
            {
                static const char *Patterns[] = { "*.ini", "*.json", "*.AUTOLOOT_ITEMS" };
                const char *openPath = tinyfd_openFileDialog(
                    "Import AutoLoot list", "", IM_ARRAYSIZE(Patterns), Patterns, "AutoLoot list file", 0);
                if (openPath != nullptr)
                {
                    astr_t content;
                    if (!ReadExchangeFile(fs_path_from(openPath), content))
                    {
                        ResetExchangePreview();
                        s_Notice = "Choose a readable AutoLoot file of up to 256 KiB.";
                    }
                    else
                    {
                        SetExchangeText(content);
                        PreviewExchangeBuffer();
                        s_Notice.clear();
                    }
                }
            }
            ImGui::SameLine();
#endif
            if (ImGui::Button("Paste clipboard"))
            {
                const char *clip = ImGui::GetClipboardText();
                if (clip != nullptr && clip[0] != '\0')
                {
                    SetExchangeText(clip);
                    PreviewExchangeBuffer();
                    s_Notice.clear();
                }
                else
                {
                    s_Notice = "The clipboard does not contain text.";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Preview text"))
            {
                PreviewExchangeBuffer();
                s_Notice.clear();
            }

            if (ImGui::InputTextMultiline(
                    "##autoloot_exchange", s_ExchangeBuffer.data(), s_ExchangeBuffer.size(), ImVec2(-1.0f, 100.0f)))
            {
                ResetExchangePreview();
            }

            if (s_HasExchangePreview)
            {
                if (!s_ExchangePreview.Success)
                {
                    ImGui::TextWrapped("%s", s_ExchangePreview.Error.c_str());
                }
                else if (!s_ExchangePreview.Lists.empty())
                {
                    s_ExchangeListIndex = std::max(
                        0, std::min(s_ExchangeListIndex, static_cast<int>(s_ExchangePreview.Lists.size()) - 1));
                    if (s_ExchangePreview.Lists.size() > 1)
                    {
                        const auto &chosen = s_ExchangePreview.Lists[s_ExchangeListIndex];
                        if (ImGui::BeginCombo("List to import", chosen.Name.c_str()))
                        {
                            for (int idx = 0; idx < static_cast<int>(s_ExchangePreview.Lists.size()); ++idx)
                            {
                                if (ImGui::Selectable(s_ExchangePreview.Lists[idx].Name.c_str(), s_ExchangeListIndex == idx))
                                {
                                    s_ExchangeListIndex = idx;
                                    SyncImportPresetName();
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }

                    const auto &chosen = s_ExchangePreview.Lists[s_ExchangeListIndex];
                    if (s_ExchangePreview.SkippedDisabled > 0)
                    {
                        ImGui::Text(
                            "Ready: %s (%zu rules, %zu disabled skipped)",
                            chosen.Name.c_str(), chosen.Rules.size(), s_ExchangePreview.SkippedDisabled);
                    }
                    else
                    {
                        ImGui::Text("Ready: %s (%zu rules)", chosen.Name.c_str(), chosen.Rules.size());
                    }

                    if (ImGui::Button("Replace current rules"))
                    {
                        TabInventory_CancelPendingTarget();
                        astr_t message;
                        if (InventoryAgent_ImportAutoLootList(
                                s_ExchangeBuffer.data(), selected.Name, false, "",
                                static_cast<size_t>(s_ExchangeListIndex), message))
                        {
                            ResetExchangePreview();
                        }
                        s_Notice = message;
                    }

                    ImGui::SetNextItemWidth(160.0f);
                    ImGui::InputText("New preset name", s_ImportPresetName, sizeof(s_ImportPresetName));
                    ImGui::SameLine();
                    if (ImGui::Button("Import as new preset"))
                    {
                        TabInventory_CancelPendingTarget();
                        astr_t message;
                        astr_t createdName;
                        if (InventoryAgent_ImportAutoLootList(
                                s_ExchangeBuffer.data(), selected.Name, true, s_ImportPresetName,
                                static_cast<size_t>(s_ExchangeListIndex), message, &createdName))
                        {
                            ResetExchangePreview();
                            for (int idx = 0; idx < static_cast<int>(profiles.size()); ++idx)
                            {
                                if (profiles[idx].Name == createdName)
                                {
                                    s_Selected = idx;
                                    break;
                                }
                            }
                        }
                        s_Notice = message;
                    }
                }
            }
        }
    }
    if (ImGui::CollapsingHeader("Manage agents"))
    {
        const char *modes[] = { "Organizer", "Restock", "Scavenger", "Use Once", "AutoLoot" };
        ImGui::SetNextItemWidth(140.0f);
        ImGui::Combo("Type", &s_NewMode, modes, static_cast<int>(sizeof(modes) / sizeof(modes[0])));
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("Name", s_NewName, sizeof(s_NewName));
        ImGui::SameLine();
        if (ImGui::Button("Add agent"))
        {
            if (InventoryAgent_AddProfile(static_cast<InventoryAgentMode>(s_NewMode), s_NewName))
            {
                TabInventory_CancelPendingTarget();
                s_NewName[0] = '\0';
                s_Selected = static_cast<int>(profiles.size()) - 1;
                s_Notice.clear();
                Save();
            }
            else
            {
                s_Notice = "Choose a unique name or remove an unused agent first.";
            }
        }
        ImGui::BeginDisabled(profiles.size() == 1);
        if (ImGui::Button("Remove selected agent"))
        {
            TabInventory_CancelPendingTarget();
            profiles.erase(profiles.begin() + s_Selected);
            s_Selected = std::max(s_Selected - 1, 0);
            Save();
        }
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();
}

void TabInventory_CancelPendingTarget()
{
    s_Target = TargetPurpose::None;
    s_TargetRuleIndex = -1;
    if (g_TargetHandler == &CAssistant::HandleTargetInventory)
    {
        g_TargetHandler = nullptr;
        if (::g_Target.FromAssist)
        {
            ::g_Target.CancelCursor();
            ::g_Target.FromAssist = false;
        }
    }
}

void TabInventory_TargetReturned(uint32_t serial)
{
    const auto purpose = s_Target;
    const int targetRuleIndex = s_TargetRuleIndex;
    s_Target = TargetPurpose::None;
    s_TargetRuleIndex = -1;
    if (purpose == TargetPurpose::None || serial == 0 || serial == 0xFFFFFFFF ||
        s_TargetCharacter != g_PlayerSerial || s_TargetAssistantProfile != g_AssistantConfig.ProfileName ||
        g_World == nullptr || g_Player == nullptr ||
        InventoryAgent_Busy())
    {
        return;
    }

    auto &profiles = InventoryAgent_Profiles();
    const auto profile = std::find_if(profiles.begin(), profiles.end(), [](const auto &entry) {
        return entry.Name == s_TargetPreset;
    });
    auto *item = g_World->FindWorldItem(serial);
    if (profile == profiles.end() || item == nullptr)
    {
        s_Notice = "Select a visible item.";
        return;
    }

    switch (purpose)
    {
        case TargetPurpose::Source:
        case TargetPurpose::Destination:
            if (item->GetStaticData() == nullptr || !item->IsContainer() || item->IsCorpse())
            {
                s_Notice = "Select a bag or other container.";
                return;
            }
            if (purpose == TargetPurpose::Destination && profile->Mode == InventoryAgentMode::AutoLoot &&
                !InBackpack(serial))
            {
                s_Notice = "Choose your backpack or a bag inside it.";
                return;
            }
            (purpose == TargetPurpose::Source ? profile->Source : profile->Destination) = serial;
            break;
        case TargetPurpose::Rule:
        {
            if (!AddRule(*profile, { item->Graphic, item->Color, true, 1 }))
            {
                s_Notice = "That item is already covered, or the rule list is full.";
                return;
            }
            break;
        }
        case TargetPurpose::RuleDestination:
            if (profile->Mode != InventoryAgentMode::AutoLoot ||
                targetRuleIndex < 0 || targetRuleIndex >= static_cast<int>(profile->Rules.size()))
            {
                s_Notice = "Select a valid AutoLoot rule first.";
                return;
            }
            if (item->GetStaticData() == nullptr || !item->IsContainer() || item->IsCorpse() || !InBackpack(serial))
            {
                s_Notice = "Choose your backpack or a bag inside it.";
                return;
            }
            profile->Rules[targetRuleIndex].Destination = serial;
            break;
        case TargetPurpose::UseItem:
            if (!InBackpack(serial))
            {
                s_Notice = "Select an item in your backpack.";
                return;
            }
            AddUseItem(*profile, *item);
            break;
        case TargetPurpose::UseContainer:
            if (!InBackpack(serial) || item->GetStaticData() == nullptr ||
                !item->IsContainer() || !item->Opened)
            {
                s_Notice = "Select an open bag in your backpack.";
                return;
            }
            for (auto *child = item->m_Items; child != nullptr; child = child->m_Next)
            {
                const auto *object = static_cast<const CGameObject *>(child);
                if (!object->NPC)
                {
                    AddUseItem(*profile, *static_cast<const CGameItem *>(object));
                }
            }
            break;
        case TargetPurpose::None:
            return;
    }

    s_Notice.clear();
    Save();
}

} // namespace xuoassist
