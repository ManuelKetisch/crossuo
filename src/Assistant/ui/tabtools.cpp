// SPDX-License-Identifier: AGPL-3.0-or-later

#include "tabtools.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include <SDL3/SDL.h>
#include <external/gfx/imgui/imgui.h>
#include <xuocore/enumlist.h>
#include <xuocore/uodata.h>

#include "Globals.h"
#include "Managers/ConfigManager.h"
#include "Network/Packets.h"
#include "ScreenshotBuilder.h"
#include "../config/assistantconfig.h"
#include "../config/configio.h"
#include "../core/world.h"
#include "../core/packettrace.h"
#include "../script/scriptlist.h"

namespace xuoassist {
namespace {

char s_directory[1024] = {};
int s_format = SF_PNG;
int s_delaySeconds = 0;
bool s_gameViewOnly = false;
bool s_onDeath = false;
bool s_wasDead = false;
bool s_pending = false;
uint32_t s_started = 0;
fs_path s_profile;
astr_t s_status;
astr_t s_backupStatus;
astr_t s_diagnosticsStatus;
char s_itemType[96] = "WoodenChair";
char s_doorType[96] = "LightWoodDoor";

fs_path captureDirectory()
{
    if (s_directory[0] == '\0')
    {
        return ExeFilePath("screenshots");
    }

    astr_t path = s_directory;
    if (path[0] == '/' || (path.size() > 2 && path[1] == ':'))
    {
        return fs_path_from(path);
    }

    return ExeFilePath(path.c_str());
}

bool openDirectory(const fs_path &path)
{
    if (fs_path_empty(path) || !fs_path_is_dir(path))
    {
        return false;
    }

    astr_t nativePath = fs_path_str(path);
    std::replace(nativePath.begin(), nativePath.end(), '\\', '/');
    astr_t url = nativePath[0] == '/' ? "file://" : "file:///";
    const char *hex = "0123456789ABCDEF";
    for (const unsigned char ch : nativePath)
    {
        if (std::isalnum(ch) || ch == '/' || ch == ':' || ch == '.' || ch == '-' || ch == '_')
        {
            url += static_cast<char>(ch);
        }
        else
        {
            url += '%';
            url += hex[ch >> 4];
            url += hex[ch & 15];
        }
    }

    return SDL_OpenURL(url.c_str());
}

void loadSettings()
{
    if (g_AssistantConfig.ProfileName.empty())
    {
        return;
    }

    const auto profile = g_AssistantConfig.GetProfilePath();
    if (!fs_path_empty(s_profile) && fs_path_equal(profile, s_profile))
    {
        return;
    }

    s_profile = profile;
    s_pending = false;
    s_wasDead = g_Player != nullptr && g_Player->IsDead();
    s_status.clear();
    s_backupStatus.clear();
    s_diagnosticsStatus.clear();
    CIniFile settings;
    settings.Load(fs_path_join(s_profile, "Capture.ini"));
    const auto directory = settings.Get("capture", "directory");
    snprintf(s_directory, sizeof(s_directory), "%s", directory.c_str());
    s_format = std::min<int>(SF_JPG,
        std::max<int>(SF_BMP, settings.GetInt("capture", "format", g_ConfigManager.ScreenshotFormat)));
    s_delaySeconds = std::min(30, std::max(0, settings.GetInt("capture", "delay", 0)));
    s_gameViewOnly = settings.GetBool("capture", "game_view_only", false);
    s_onDeath = settings.GetBool("capture", "on_death", false);
    snprintf(s_itemType, sizeof(s_itemType), "%s", settings.Get("staff", "item", "WoodenChair").c_str());
    snprintf(s_doorType, sizeof(s_doorType), "%s", settings.Get("staff", "door", "LightWoodDoor").c_str());
}

void saveCaptureSettings()
{
    if (fs_path_empty(s_profile))
    {
        return;
    }

    g_AssistantConfig.EnsureDirectories();
    CIniFile settings;
    settings.Set("capture", "directory", s_directory);
    settings.SetInt("capture", "format", s_format);
    settings.SetInt("capture", "delay", s_delaySeconds);
    settings.SetBool("capture", "game_view_only", s_gameViewOnly);
    settings.SetBool("capture", "on_death", s_onDeath);
    settings.Set("staff", "item", s_itemType);
    settings.Set("staff", "door", s_doorType);
    if (!settings.Save(fs_path_join(s_profile, "Capture.ini")))
    {
        s_status = "Could not save the capture settings.";
    }
}

void queueCapture()
{
    s_started = g_Ticks;
    s_pending = true;
    s_status.clear();
}

bool backupFiles(const fs_path &source, const fs_path &target, const char *const *names, size_t count)
{
    if (!fs_path_is_dir(target) && !fs_path_create(target))
    {
        return false;
    }

    bool complete = true;
    for (size_t i = 0; i < count; i++)
    {
        const auto file = fs_path_join(source, names[i]);
        if (fs_path_is_file(file))
        {
            complete = fs_copy(file, fs_path_join(target, names[i])) && complete;
        }
    }

    return complete;
}

bool safeBasename(const astr_t &name)
{
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\") == astr_t::npos;
}

bool backupRegisteredScripts(const fs_path &source, const fs_path &target)
{
    const auto optionsPath = fs_path_join(source, "Options.ini");
    if (!fs_path_is_file(optionsPath))
    {
        return true;
    }
    CIniFile options;
    if (!options.Load(optionsPath))
    {
        return false;
    }

    const int registered = options.GetInt("scripts", "loadedscripts", 0);
    const int count = std::min(2048, registered);
    if (count <= 0)
    {
        return true;
    }

    const auto scripts = fs_path_join(target, "Scripts");
    if (!fs_path_create(scripts))
    {
        return false;
    }

    CIniFile manifest;
    int copied = 0;
    bool complete = count == registered;
    for (int i = 0; i < count; i++)
    {
        const auto path = options.Get("scripts", astr_t("loadedscript") + str_from(i));
        if (path.empty())
        {
            complete = false;
            continue;
        }

        const auto name = ScriptList_DisplayName(path);
        if (safeBasename(name) && fs_path_is_file(fs_path_from(path)))
        {
            const auto filename = std::to_string(i + 1) + "-" + name;
            if (fs_copy(fs_path_from(path), fs_path_join(scripts, filename)))
            {
                const auto section = IniSectionName("Script", copied++);
                manifest.Set(section, "original", path);
                manifest.Set(section, "copy", "Scripts/" + filename);
            }
            else
            {
                complete = false;
            }
        }
        else
        {
            complete = false;
        }
    }

    manifest.SetInt("Meta", "count", copied);
    return manifest.Save(fs_path_join(target, "ScriptSources.ini")) && complete;
}

void sendAddCommand(const char *type)
{
    const astr_t value = type;
    if (!std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
            return std::isalnum(ch) || ch == '_' || ch == '.';
        }))
    {
        s_diagnosticsStatus = "Enter a server item type.";
        return;
    }

    const auto command = wstr_from(value.empty() ? "[Add" : "[Add " + value);
    CPacketUnicodeSpeechRequest(command, ST_NORMAL, 3, g_ConfigManager.SpeechColor, g_ServerLanguageReport).Send();
    s_diagnosticsStatus.clear();
}

void createBackup()
{
    g_LoadedScripts.DrainPendingRestore();
    if (!g_AssistantConfig.SaveProfile())
    {
        s_backupStatus = "Could not save the active profile.";
        return;
    }

    const auto base = fs_path_join(CAssistantConfig::GetRootPath(), "Backups");
    if (!fs_path_is_dir(base) && !fs_path_create(base))
    {
        s_backupStatus = "Could not create the backup folder.";
        return;
    }

    const auto stamp = std::time(nullptr);
    auto target = fs_path_join(base, std::to_string(stamp));
    for (unsigned suffix = 1; fs_path_exists(target); suffix++)
    {
        target = fs_path_join(base, std::to_string(stamp) + "-" + std::to_string(suffix));
    }
    const auto profilesDir = fs_path_join(target, "Config");
    if (!fs_path_create(target) || !fs_path_create(profilesDir))
    {
        s_backupStatus = "Could not create a new backup.";
        return;
    }

    static const char *const profileFiles[] = {
        "Options.ini", "Objects.ini", "Macros.ini", "Hotkeys.ini", "Party.ini",
        "Friends.ini", "FriendGroups.ini", "Enemies.ini", "Dress.coa", "Capture.ini", "ItemCounters.ini", "Targeting.ini", "GameplayFilters.ini",
        "BandageHeal.ini", "SellHotBag.ini", "Toolbar.ini", "PotionHands.ini"
    };
    static const char *const globalFiles[] = {
        "Profiles.ini", "Map.ini", "Types.ini", "FindList.ini", "IgnoreList.ini",
        "SpeechFilter.ini", "TextReplaces.ini", "SoundFilter.ini", "ShopList.ini",
        "DisplayItems.ini", "DisplayGroups.ini"
    };
    auto profiles = g_AssistantConfig.GetProfiles();
    if (std::find(profiles.begin(), profiles.end(), g_AssistantConfig.ProfileName) == profiles.end())
    {
        profiles.push_back(g_AssistantConfig.ProfileName);
    }

    const auto config = CAssistantConfig::GetConfigPath();
    bool complete = true;
    for (const auto &name : profiles)
    {
        if (!safeBasename(name))
        {
            continue;
        }

        const auto profile = fs_path_join(config, name);
        if (!fs_path_is_dir(profile))
        {
            continue;
        }

        const auto output = fs_path_join(profilesDir, name);
        const bool files = backupFiles(profile, output, profileFiles, sizeof(profileFiles) / sizeof(profileFiles[0]));
        complete = files && backupRegisteredScripts(profile, output) && complete;
        if (files)
        {
            std::vector<fs_path> entries;
            fs_path_list_shallow(profile, entries);
            for (const auto &file : entries)
            {
                const auto basename = ScriptList_DisplayName(fs_path_str(file));
                if (safeBasename(basename) && basename.compare(0, 16, "InventoryAgents-") == 0 &&
                    basename.size() > 20 && basename.compare(basename.size() - 4, 4, ".ini") == 0)
                {
                    complete = fs_copy(file, fs_path_join(output, basename)) && complete;
                }
            }
        }
    }

    complete = backupFiles(CAssistantConfig::GetGlobalConfigPath(),
        fs_path_join(target, "GlobalConfig"), globalFiles, sizeof(globalFiles) / sizeof(globalFiles[0])) && complete;
    s_backupStatus = complete ? "Backup saved." : "Backup saved with missing files.";
}

} // namespace

void TabTools_Tick()
{
    loadSettings();
    if (g_Player == nullptr)
    {
        return;
    }

    const bool dead = g_Player->IsDead();
    if (s_onDeath && dead && !s_wasDead && !s_pending)
    {
        queueCapture();
    }
    s_wasDead = dead;

    if (s_pending && g_Ticks - s_started >= static_cast<uint32_t>(s_delaySeconds * 1000))
    {
        s_pending = false;
        if (!g_ScreenshotBuilder.Capture(captureDirectory(), static_cast<uint8_t>(s_format), s_gameViewOnly))
        {
            s_status = "Could not start the screenshot.";
        }
    }
}

void TabTools_ResetSession()
{
    s_pending = false;
    s_profile = {};
}

void TabTools_Draw()
{
    loadSettings();
    ImGui::SeparatorText("Screenshots");
    ImGui::InputText("Folder", s_directory, sizeof(s_directory));
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        saveCaptureSettings();
    }

    if (ImGui::Combo("Format", &s_format, "BMP\0PNG\0TGA\0JPEG\0"))
    {
        saveCaptureSettings();
    }

    bool changed = ImGui::SliderInt("Delay in seconds", &s_delaySeconds, 0, 30);
    changed |= ImGui::Checkbox("Capture game view only", &s_gameViewOnly);
    if (changed)
    {
        saveCaptureSettings();
    }

    if (ImGui::Checkbox("Capture on death", &s_onDeath))
    {
        saveCaptureSettings();
    }

    if (ImGui::Button(s_pending ? "Cancel screenshot" : "Take screenshot"))
    {
        if (s_pending)
        {
            s_pending = false;
        }
        else
        {
            queueCapture();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open screenshot folder") && !openDirectory(captureDirectory()))
    {
        s_status = "Could not open the screenshot folder.";
    }
    if (s_pending)
    {
        const uint32_t elapsed = (g_Ticks - s_started) / 1000;
        ImGui::Text("Capturing in %u seconds", static_cast<uint32_t>(s_delaySeconds) > elapsed
            ? static_cast<uint32_t>(s_delaySeconds) - elapsed : 0u);
    }
    if (!s_status.empty())
    {
        ImGui::TextWrapped("%s", s_status.c_str());
    }

    ImGui::SeparatorText("Assistant backup");
    ImGui::TextDisabled("ScriptSources.ini lists each backed-up script and its original path.");
    if (ImGui::Button("Back up profiles, macros and scripts"))
    {
        createBackup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open backup folder") &&
        !openDirectory(fs_path_join(CAssistantConfig::GetRootPath(), "Backups")))
    {
        s_backupStatus = "Could not open the backup folder.";
    }
    if (!s_backupStatus.empty())
    {
        ImGui::TextWrapped("%s", s_backupStatus.c_str());
    }

    if (ImGui::CollapsingHeader("Packet capture"))
    {
        if (ImGui::Button(PacketTrace_Enabled() ? "Stop recording" : "Record game packets"))
        {
            if (PacketTrace_Enabled())
            {
                PacketTrace_Stop();
            }
            else if (!PacketTrace_Start())
            {
                s_diagnosticsStatus = "Could not start packet capture.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open capture folder") && !openDirectory(PacketTrace_Directory()))
        {
            s_diagnosticsStatus = "Could not open the capture folder.";
        }
    }

    if (ImGui::CollapsingHeader("ModernUO staff tools"))
    {
        if (ImGui::Button("Open decoration browser"))
        {
            sendAddCommand("");
        }
        ImGui::InputText("Decoration type", s_itemType, sizeof(s_itemType));
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            saveCaptureSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Place decoration") && s_itemType[0])
        {
            sendAddCommand(s_itemType);
        }
        ImGui::InputText("Door type", s_doorType, sizeof(s_doorType));
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            saveCaptureSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Place door") && s_doorType[0])
        {
            sendAddCommand(s_doorType);
        }
    }

    if (!s_diagnosticsStatus.empty())
    {
        ImGui::TextWrapped("%s", s_diagnosticsStatus.c_str());
    }
}

} // namespace xuoassist
