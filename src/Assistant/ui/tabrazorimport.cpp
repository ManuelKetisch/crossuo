// SPDX-License-Identifier: AGPL-3.0-or-later

#include "tabrazorimport.h"

#include <external/gfx/imgui/imgui.h>

#include "../compat/razorimport.h"
#include "../compat/razorenhancedimport.h"
#include "../config/assistantconfig.h"
#include "../config/configio.h"
#include "editors.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>

#if !defined(XUO_EMSC)
#include <external/tinyfiledialogs.h>
#include <cstdio>
#include <memory>
#endif

namespace xuoassist {
namespace {

constexpr size_t FILE_LIMIT = 256 * 1024;
std::array<char, FILE_LIMIT + 1> s_Input = {};
std::array<char, 65> s_Name = { 'R', 'a', 'z', 'o', 'r', ' ', 'I', 'm', 'p', 'o', 'r', 't' };
RazorImportResult s_Preview;
RazorImportResult s_ClassicBase;
constexpr int EnhancedProfileFormat = 4;
int s_Format = 0;
int s_EnhancedTable = 0;
bool s_HasPreview = false;
std::string s_Status;

bool profile_format()
{
    return s_Format == static_cast<int>(RazorImportKind::Profile) || s_Format == EnhancedProfileFormat;
}

RazorImportKind selected_kind()
{
    return static_cast<RazorImportKind>(s_Format);
}

void reset_preview()
{
    s_HasPreview = false;
    s_Preview = {};
    s_ClassicBase = {};
    s_Status.clear();
}

void preview(const std::string &text)
{
    reset_preview();
    s_Preview = s_Format == EnhancedProfileFormat ?
        ParseRazorEnhancedProfile({ { static_cast<RazorEnhancedTable>(s_EnhancedTable), text } }) :
        ParseRazorImport(selected_kind(), text);
    if (s_Format == static_cast<int>(RazorImportKind::Profile))
    {
        s_ClassicBase = s_Preview;
    }
    s_HasPreview = true;
}

#if !defined(XUO_EMSC)
bool read_file(const fs_path &path, std::string &input)
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
    if (bytes < 0 || bytes > static_cast<long>(FILE_LIMIT) || std::fseek(file.get(), 0, SEEK_SET) != 0)
    {
        return false;
    }
    input.resize(static_cast<std::size_t>(bytes));
    return input.empty() || std::fread(&input[0], 1, input.size(), file.get()) == input.size();
}

void choose_file()
{
    static const char *PATTERNS[] = {
        "*.txt", "*.macro", "*.xml", "*.uos",
        "*.GENERAL", "*.HOTKEYS", "*.TOOLBAR_ITEMS", "*.AUTOLOOT", "*.SCAVENGER",
        "*.ORGANIZER", "*.RESTOCK", "*.DRESS", "*.BUY", "*.SELL",
        "*.BANDAGE_HEAL", "*.TARGETS", "*.FRIENDS", "*.SCRIPTS", "*.json"
    };
    const char *path = tinyfd_openFileDialog("Open Razor or UOSteam file", "", IM_ARRAYSIZE(PATTERNS), PATTERNS, "Import file", 0);
    if (path == nullptr)
    {
        return;
    }
    std::string input;
    if (!read_file(fs_path_from(path), input))
    {
        reset_preview();
        s_Status = "Choose a readable file of up to 256 KiB.";
        return;
    }
    const std::string filename(path);
    const auto extension = filename.find_last_of('.');
    if (extension != std::string::npos)
    {
        const auto kind = str_lower(filename.substr(extension));
        if (kind == ".macro") s_Format = static_cast<int>(RazorImportKind::Macro);
        if (kind == ".xml") s_Format = static_cast<int>(RazorImportKind::Profile);
        if (kind == ".txt") s_Format = static_cast<int>(RazorImportKind::Script);
        if (kind == ".uos") s_Format = static_cast<int>(RazorImportKind::UOSteam);
        constexpr const char *suffixes[] = {
            ".general", ".hotkeys", ".toolbar_items", ".autoloot", ".scavenger",
            ".organizer", ".restock", ".dress", ".buy", ".sell",
            ".bandage_heal", ".targets", ".friends", ".scripts"
        };
        for (int i = 0; i < IM_ARRAYSIZE(suffixes); ++i)
        {
            if (kind == suffixes[i])
            {
                s_Format = EnhancedProfileFormat;
                s_EnhancedTable = i;
                break;
            }
        }
    }
    s_Input.fill(0);
    preview(input);
}

void choose_classic_counters()
{
    static const char *Patterns[] = { "*.xml" };
    const char *path = tinyfd_openFileDialog("Open Classic counters.xml", "", 1, Patterns, "Classic counters XML", 0);
    if (path == nullptr)
    {
        return;
    }
    std::string input;
    if (!read_file(fs_path_from(path), input))
    {
        s_Status = "Choose a readable counters file of up to 256 KiB.";
        return;
    }
    s_Status.clear();
    s_Input.fill(0);
    s_Preview = AddClassicCounterDefinitions(s_ClassicBase, input);
    s_HasPreview = true;
}

void choose_enhanced_folder()
{
    const char *path = tinyfd_selectFolderDialog("Choose an Enhanced profile folder", "");
    if (path == nullptr)
    {
        return;
    }
    reset_preview();
    s_Input.fill(0);
    std::vector<RazorEnhancedProfileInput> inputs;
    const auto folder = fs_path_from(path);
    for (int i = 0; i < static_cast<int>(RazorEnhancedTable::Count); ++i)
    {
        const auto table = static_cast<RazorEnhancedTable>(i);
        const auto filepath = fs_path_join(folder, RazorEnhancedTableFile(table));
        if (!fs_path_exists(filepath))
        {
            continue;
        }
        std::string content;
        if (!read_file(filepath, content))
        {
            s_Status = "A supported table could not be read or exceeded 256 KiB.";
            return;
        }
        inputs.push_back({ table, std::move(content) });
    }
    if (inputs.empty())
    {
        s_Status = "Choose a named Enhanced profile folder with supported settings files.";
        return;
    }
    s_Preview = ParseRazorEnhancedProfile(inputs);
    s_Preview.Notices.push_back({ 0, "Folder import reads supported Enhanced settings, hotkeys, toolbar, agents, targets, friends, and script entries." });
    s_HasPreview = true;
}
#endif

} // namespace

bool PublishRazorProfile(const std::string &name, const RazorImportResult &preview)
{
    if (!preview.CanImport() || (preview.ProfileHotkeys.empty() && preview.ProfileOptions.empty()))
    {
        return false;
    }
    return g_AssistantConfig.CreateImportedProfile(name, [&preview](CAssistantConfig &profile) {
        std::map<std::string, CIniFile> files;
        const auto directory = profile.GetProfilePath();
        constexpr std::array<const char *, 13> AllowedFiles = {{
            "Options.ini", "Targeting.ini", "GameplayFilters.ini", "ItemCounters.ini",
            "BandageHeal.ini", "PotionHands.ini", "Toolbar.ini",
            "InventoryAgents.ini", "DressSets.ini", "ShopList.ini", "SellHotBag.ini",
            "Friends.ini", "Scripts.ini",
        }};
        for (const auto &option : preview.ProfileOptions)
        {
            if (std::none_of(AllowedFiles.begin(), AllowedFiles.end(), [&](const char *allowed) {
                    return option.File == allowed;
                }))
            {
                return false;
            }
            auto inserted = files.emplace(option.File, CIniFile());
            const auto path = fs_path_join(directory, option.File);
            if (inserted.second && fs_path_exists(path) && !inserted.first->second.Load(path))
            {
                return false;
            }
            inserted.first->second.Set(option.Section, option.Key, option.Value);
        }
        for (const auto &file : files)
        {
            if (!file.second.Save(fs_path_join(directory, file.first)))
            {
                return false;
            }
        }
        for (const auto &binding : preview.ProfileHotkeys)
        {
            CHotkeyEntry entry;
            if (!RazorHotkeys_BuildBinding(binding, entry))
            {
                return false;
            }
            profile.m_Hotkeys.push_back(std::move(entry));
        }
        return profile.SaveHotkeys();
    });
}

void TabRazorImport_Draw()
{
    ImGui::TextWrapped("Preview Classic Razor scripts, recorded macros, UOSteam scripts, and Classic or Enhanced profiles.");
    constexpr const char *FORMATS[] = {
        "Razor script", "Recorded .macro", "Classic profile XML", "UOSteam .uos", "Enhanced profile"
    };
    if (ImGui::Combo("Format", &s_Format, FORMATS, IM_ARRAYSIZE(FORMATS)))
    {
        reset_preview();
    }
    if (s_Format == EnhancedProfileFormat)
    {
        constexpr const char *TABLES[] = {
            "General settings", "Hotkeys", "Toolbar items",
            "AutoLoot", "Scavenger", "Organizer", "Restock",
            "Dress", "Buy", "Sell", "Bandage Heal",
            "Targets", "Friends", "Scripts"
        };
        if (ImGui::Combo("Table for pasted text", &s_EnhancedTable, TABLES, IM_ARRAYSIZE(TABLES)))
        {
            reset_preview();
        }
#if !defined(XUO_EMSC)
        if (ImGui::Button("Open profile folder"))
        {
            choose_enhanced_folder();
        }
        ImGui::SameLine();
#endif
    }
#if !defined(XUO_EMSC)
    if (ImGui::Button("Open file"))
    {
        choose_file();
    }
    ImGui::SameLine();
    if (s_Format == static_cast<int>(RazorImportKind::Profile))
    {
        if (ImGui::Button("Attach counters.xml"))
        {
            choose_classic_counters();
        }
        ImGui::SameLine();
    }
#endif
    if (ImGui::Button("Preview pasted text"))
    {
        preview(s_Input.data());
        if (profile_format())
        {
            // The file may contain legacy credentials. Display only mapped options.
            s_Input.fill(0);
        }
    }
    const bool previewSource = ImGui::InputTextMultiline(
        "##razor_source", s_Input.data(), s_Input.size(), ImVec2(-1.0f, 93.0f));
    if (previewSource)
    {
        reset_preview();
    }
    if (!s_Status.empty())
    {
        ImGui::TextWrapped("%s", s_Status.c_str());
    }
    if (!s_HasPreview)
    {
        return;
    }

    if (!s_Preview.Issues.empty())
    {
        ImGui::TextUnformatted("Review the unsupported lines before importing.");
        for (const auto &issue : s_Preview.Issues)
        {
            if (issue.Line != 0)
            {
                ImGui::TextWrapped("Line %zu  %s", issue.Line, issue.Message.c_str());
            }
            else
            {
                ImGui::TextWrapped("%s", issue.Message.c_str());
            }
        }
    }
    if (profile_format())
    {
        if (!s_ClassicBase.ClassicCounters.empty() && s_Preview.ProfileOptions.size() == s_ClassicBase.ProfileOptions.size())
        {
            ImGui::TextWrapped("Attach Classic counters.xml to include selected counters.");
        }
        ImGui::Text("%zu settings, %zu hotkeys, %zu skipped entries", s_Preview.ProfileOptions.size(),
            s_Preview.ProfileHotkeys.size(), s_Preview.Ignored);
        ImGui::BeginChild("##razor_profile_preview", ImVec2(0, 100), ImGuiChildFlags_Borders);
        for (const auto &option : s_Preview.ProfileOptions)
        {
            ImGui::TextWrapped("%s / %s  %s", option.Section.c_str(), option.Key.c_str(), option.Value.c_str());
        }
        for (const auto &binding : s_Preview.ProfileHotkeys)
        {
            const auto *action = FindRazorHotkey(binding.Id);
            const auto key = RazorHotkeyKeyName(binding.VirtualKey);
            ImGui::Text("%s%s%s%s   %s", binding.Modifiers & 1 ? "Alt+" : "",
                binding.Modifiers & 2 ? "Ctrl+" : "", binding.Modifiers & 4 ? "Shift+" : "",
                key.c_str(), action->Name);
        }
        for (const auto &notice : s_Preview.Notices)
        {
            if (notice.Line != 0)
            {
                ImGui::TextWrapped("Line %zu  %s", notice.Line, notice.Message.c_str());
            }
            else
            {
                ImGui::TextWrapped("%s", notice.Message.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::InputText("New profile", s_Name.data(), s_Name.size());
        ImGui::BeginDisabled(!s_Preview.CanImport());
        if (ImGui::Button("Create profile"))
        {
            const bool created = PublishRazorProfile(s_Name.data(), s_Preview);
            s_Status = created ? "Profile saved. Select it under Main when you want to use it." :
                "Could not create the profile. Choose an unused profile name and try again.";
            if (created)
            {
                s_HasPreview = false;
            }
        }
        ImGui::EndDisabled();
        return;
    }

    ImGui::Text("%zu compatible actions", s_Preview.Imported);
    if (!s_Preview.JavaScript.empty())
    {
        ImGui::BeginChild("##razor_preview", ImVec2(0, 94), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(s_Preview.JavaScript.c_str());
        ImGui::EndChild();
    }
    ImGui::BeginDisabled(!s_Preview.CanImport());
    if (ImGui::Button("Open in script editor"))
    {
        const char *title = s_Format == static_cast<int>(RazorImportKind::UOSteam) ? "UOSteam import" : "Razor import";
        Editors_OpenUntitled(title, s_Preview.JavaScript);
    }
    ImGui::EndDisabled();
}

void TabRazorImport_Reset()
{
    s_Input.fill(0);
    reset_preview();
}

} // namespace xuoassist
