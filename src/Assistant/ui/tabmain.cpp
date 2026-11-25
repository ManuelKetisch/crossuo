// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

// TabMain: the 23-option checklist bound to g_AssistantConfig.m_Options (the
// same fields the packet handlers consult) and the nested Main/Options pages.
// Toggles apply immediately and persist right away (SaveMainOptions); the
// other page widgets save with the profile on close.

#include "assistantui.h"

#if defined(XUO_ASSISTANT)

#include <external/gfx/imgui/imgui.h>

#include "widgets.h"
#include "mapwindow.h"
#include "../config/assistantconfig.h"
#include "../core/world.h" // g_Player
#include "../core/assist.h"
#include "../core/global.h"
#include "../core/targetassist.h"
#include "Managers/PacketManager.h" // ::g_PacketManager (the light/weather/season apply-cores)
#include "CrossUO.h" // ::g_Game (the client config accessors)

namespace xuoassist {

namespace {

// checklist order (CHECKLISTBOX_IDENTIFICATOR)
struct XA_OPTION_ROW
{
    const char *Label;
    int Index;
};

const XA_OPTION_ROW s_Options[] = {
    { "Open corpses", CLBI_CORPSES_AUTOOPEN },
    { "Stealth steps counter", CLBI_STEALTH_STEP_COUNTER },
    { "Sounds filter", CLBI_SOUND_FILTER },
    { "Always light", CLBI_FILTER_LIGHT },
    { "Weather filter", CLBI_FILTER_WEATHER },
    { "Season filter", CLBI_FILTER_SEASON },
    { "No death screen", CLBI_NO_DEATH_SCREEN },
    { "Open doors", CLBI_OPEN_DOORS },
    { "Tracker", CLBI_TRACKER },
    // "Stay on top" (CLBI_STAY_ON_TOP) hidden: a confirmed no-op (SDL2 has no always-on-top API); the enum bit stays - it is persisted in saved INIs.
    { "Fast turn", CLBI_FAST_ROTATION },
    { "Recurse containers search", CLBI_RECURSE_CONTAINERS_SEARCH },
    { "Autostart", CLBI_AUTOSTART },
    { "Ignore stamina check", CLBI_IGNORE_STAMINA_CHECK },
    // "Minimize to tray" (CLBI_MINIMIZE_TO_TRAY) hidden: the assistant has no
    // tray implementation; the enum bit stays - it is persisted in saved INIs.
    { "Sound echo", CLBI_SOUND_ECHO },
    { "Animation echo", CLBI_ANIMATION_ECHO },
    { "Effect echo", CLBI_EFFECT_ECHO },
    { "Speech filter", CLBI_FILTER_SPEECH },
    { "Text replace", CLBI_TEXT_REPLACE },
    { "Object Inspector", CLBI_OBJECT_INSPECTOR },
    { "No gray screen", CLBI_NO_GRAY_SCREEN },
    { "Save far objects", CLBI_SAVE_FAR_OBJECTS },
};

// "Assistant messages show:" (tabmain.ui cb_AssistMessagesLevel)
const char *const s_MessagesLevels[] = { "Minimum", "Medium", "Show all" };

// Ported from TabMain::OptionChanged: the checklist
// rows that act on the client immediately when toggled.
void option_changed(int index, bool state)
{
    switch (index)
    {
        case CLBI_FILTER_LIGHT:
        {
            if (!EnabledFeatureLightFilter())
            {
                g_AssistantConfig.SetOption(index, false);
                state = false;
            }
            ::g_PacketManager.SetLightLevel((uint8_t)(state ? 0 : g_LightLevel));
            break;
        }
        case CLBI_FILTER_WEATHER:
        {
            if (!EnabledFeatureWeatherFilter())
            {
                g_AssistantConfig.SetOption(index, false);
                state = false;
            }
            ::g_PacketManager.SetWeather(g_Weather.Type, g_Weather.Count, g_Weather.Temperature);
            break;
        }
        case CLBI_FILTER_SEASON:
        {
            if (!EnabledFeatureSeasonFilter())
            {
                g_AssistantConfig.SetOption(index, false);
                state = false;
            }
            // SetSeason derefs g_Player (the death check) - same guard as the
            // NO_GRAY_SCREEN case below
            if (g_Player != nullptr)
            {
                ::g_PacketManager.SetSeason(g_Season.Index, g_Season.Music);
            }
            break;
        }
        case CLBI_TRACKER:
        {
            if (!EnabledFeatureTracker())
            {
                g_AssistantConfig.SetOption(index, false);
                state = false;
            }
            if (!state)
            {
                g_Assistant.Track(false, 0, 0);
            }
            break;
        }
        case CLBI_STAY_ON_TOP:
        {
            // a host-window flag; the
            break;
        }
        case CLBI_FAST_ROTATION:
        {
            if (!EnabledFeatureFastRotation())
            {
                g_AssistantConfig.SetOption(index, false);
                state = false;
            }
            g_Game.ValueInt(VKI_FAST_ROTATION, static_cast<int>(state));
            break;
        }
        case CLBI_IGNORE_STAMINA_CHECK:
        {
            if (!EnabledFeatureIgnoreStaminaCheck())
            {
                g_AssistantConfig.SetOption(index, false);
                state = false;
            }
            g_Game.ValueInt(VKI_IGNORE_STAMINA_CHECK, static_cast<int>(state));
            break;
        }
        case CLBI_NO_GRAY_SCREEN:
        {
            if (!EnabledFeatureNoDeathScreen())
            {
                g_AssistantConfig.SetOption(index, false);
                state = false;
            }
            if (g_Player != nullptr)
            {
                uint16_t graphic = 0;
                if (!state)
                {
                    if (g_Player->IsDead() || (g_MorphGraphic == 0u))
                    {
                        graphic = g_Player->Graphic;
                    }
                    else
                    {
                        graphic = g_MorphGraphic;
                    }
                }
                else
                {
                    if (g_Player->IsDead())
                    {
                        graphic = 0x03DB;
                    }
                    else if (g_MorphGraphic != 0u)
                    {
                        graphic = g_MorphGraphic;
                    }
                    else
                    {
                        graphic = g_Player->Graphic;
                    }
                }
                if (graphic != 0u)
                {
                    g_Game.ValueInt(VKI_SET_PLAYER_GRAPHIC, graphic);
                }
            }
            break;
        }
        default:
            break;
    }
}

// A color row: use-checkbox + hex line edit + hue preview swatch + "..."
// (opens the dye gump; the 0x95 response updates the config).
void font_color_row(const char *label, bool *use, astr_t *color, uint32_t dyeSerial)
{
    // the fixed widget ids (##use/##hex/...) are scoped per row: this is drawn
    // twice ("Font color" / "Characters font color") and they would collide
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    if (ImGui::Checkbox("##use", use))
    {
        AssistantUI_MarkOptionsDirty();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Use this color");
    }
    ImGui::SameLine();
    if (HexEditField("##hex", color, 80.0f))
    {
        AssistantUI_MarkOptionsDirty();
    }
    ImGui::SameLine();
    HueSwatch((uint16_t)CAssistant::TextToGraphic(*color));
    ImGui::SameLine();
    // opens the client's dye gump; the 0x95
    ImGui::BeginDisabled(!g_Connected);
    const bool pick = ImGui::Button("...");
    ImGui::EndDisabled();
    if (pick && g_Connected)
    {
        // the 0x95 DyeData handler's gump open, called directly (the former
        // injected 0x95 carried the same fixed 0x0FAB dye-tub graphic)
        ::g_PacketManager.OpenDyeGump(dyeSerial, 0x0FAB);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Pick the color with the server's dye gump");
    }
    ImGui::PopID();
}

void draw_main_page()
{
    // Assistant messages show:
    int level = g_AssistantConfig.GetMessagesLevel();
    ImGui::SetNextItemWidth(120.0f);
    if (ComboGroup("##messages", &level, s_MessagesLevels, 3))
    {
        g_AssistantConfig.m_MessagesLevel = level;
        AssistantUI_MarkOptionsDirty();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Assistant messages show:");

    ImGui::Separator();

    // Font color / Characters font color rows (the checkbox is the
    font_color_row(
        "Font color:",
        &g_AssistantConfig.m_UseFontColor,
        &g_AssistantConfig.m_FontColor,
        0xFFFFFFFF);
    font_color_row(
        "Characters font color:",
        &g_AssistantConfig.m_UseCharactersFontColor,
        &g_AssistantConfig.m_CharactersFontColor,
        0xFFFFFFFE);

    ImGui::Separator();

    // Auto reconnect after [N] seconds (left X seconds)
    bool reconnect = g_AssistantConfig.AutoReconnect();
    if (ImGui::Checkbox("Auto reconnect after", &reconnect))
    {
        g_AssistantConfig.m_AutoReconnect = reconnect;
        AssistantUI_MarkOptionsDirty();
    }
    ImGui::SameLine();
    int seconds = g_AssistantConfig.AutoReconnectSeconds();
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::InputInt("##reconnect_seconds", &seconds))
    {
        seconds = seconds < 10 ? 10 : (seconds > 9999 ? 9999 : seconds);
        g_AssistantConfig.m_AutoReconnectSeconds = seconds;
        AssistantUI_MarkOptionsDirty();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("seconds");
    if (reconnect && !g_Connected && EnabledFeatureReconnector() &&
        AssistantUI_ReconnectSecondsLeft() > 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(left %d seconds)", AssistantUI_ReconnectSecondsLeft());
    }

    ImGui::Separator();

    const auto currentName = g_AssistantConfig.ProfileName;
    static char profileName[64] = { 0 };
    static astr_t profileStatus;
    static astr_t archiveName;
    static bool clone = false;

    const auto profileChanged = [] {
        AssistantUI_ClearOptionsDirty();
        AssistantUI_RecompileDisplayTitle();
    };
    ImGui::TextUnformatted("Profiles");
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##profile", currentName.c_str()))
    {
        for (const auto &name : g_AssistantConfig.GetProfiles())
        {
            if (ImGui::Selectable(name.c_str(), name == currentName) && name != currentName)
            {
                if (g_AssistantConfig.SwitchProfile(name))
                {
                    profileStatus.clear();
                    profileChanged();
                }
                else
                {
                    profileStatus = "Could not switch profiles.";
                }
                break;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save"))
    {
        if (g_AssistantConfig.SaveProfile())
        {
            AssistantUI_ClearOptionsDirty();
            profileStatus = "Profile saved.";
        }
        else
        {
            profileStatus = "Could not save the profile.";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("New"))
    {
        clone = false;
        profileName[0] = '\0';
        ImGui::OpenPopup("Create profile");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clone"))
    {
        clone = true;
        profileName[0] = '\0';
        ImGui::OpenPopup("Create profile");
    }
    ImGui::SameLine();
    if (ImGui::Button("Archive"))
    {
        archiveName = currentName;
        ImGui::OpenPopup("Archive profile");
    }

    if (ImGui::BeginPopupModal("Create profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(clone ? "Copy the current profile." : "Start with default settings.");
        ImGui::InputText("Name", profileName, sizeof(profileName));
        ImGui::TextUnformatted("Use letters, numbers, spaces, hyphens or underscores.");
        if (ImGui::Button(clone ? "Clone profile" : "Create profile"))
        {
            if (g_AssistantConfig.CreateProfile(profileName, clone))
            {
                profileChanged();
                profileStatus = clone ? "Profile cloned." : "Profile created.";
            }
            else
            {
                profileStatus = "Could not create the profile. Check the name and that it is unused.";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Archive profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("Move %s to Profile Archives?", archiveName.c_str());
        ImGui::TextUnformatted("Characters using it will switch to another profile.");
        if (ImGui::Button("Move to archive"))
        {
            astr_t savePath;
            if (g_AssistantConfig.ProfileName == archiveName &&
                g_AssistantConfig.ArchiveCurrentProfile(savePath))
            {
                profileStatus = "Archived in " + savePath;
                profileChanged();
            }
            else
            {
                profileStatus = "Could not archive the profile.";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!profileStatus.empty())
    {
        ImGui::TextWrapped("%s", profileStatus.c_str());
    }

    ImGui::Separator();

    // Assistant windows opacity: 30-100
    int opacity = g_AssistantConfig.WindowsOpacity();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderInt("Assistant windows opacity:", &opacity, 30, 100, "%d%%"))
    {
        g_AssistantConfig.m_WindowsOpacity = opacity;
        AssistantUI_MarkOptionsDirty();
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        g_AssistantConfig.SaveMainOptions();
    }

    // UI text & control scale: 85-150%
    int uiScale = g_AssistantConfig.UiScale();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderInt("Assistant UI scale:", &uiScale, 85, 150, "%d%%"))
    {
        g_AssistantConfig.m_UiScale = uiScale;
        AssistantUI_MarkOptionsDirty();
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        g_AssistantConfig.SaveMainOptions();
    }

    ImGui::Separator();

    // Open map (map_icon.png) - the map window
    if (IconButton(XA_ICON_MAP, "Open map"))
    {
        MapWindow_Open();
    }
}

void draw_options_page()
{
    // spinbox ranges/defaults
    struct XA_SPIN_ROW
    {
        const char *Label;
        int *Value;
        int Min, Max;
    };
    XA_SPIN_ROW rows[] = {
        { "Use items delay:", &g_AssistantConfig.m_UseItemsDelay, 0, 10000 },
        { "Wait targets delay:", &g_AssistantConfig.m_WaitTargetsDelay, 0, 60000 },
        { "Move items delay:", &g_AssistantConfig.m_MoveItemsDelay, 10, 5000 },
        { "Search objects distance:", &g_AssistantConfig.m_SearchItemsDistance, 0, 4 },
        { "Open corpses distance:", &g_AssistantConfig.m_OpenCorpsesDistance, 0, 4 },
        { "Use objects distance:", &g_AssistantConfig.m_UseItemsDistance, 0, 4 },
        { "keep delay (ms):", &g_AssistantConfig.m_CorpseKeepDelay, 100, 600000 },
    };
    for (int i = 0; i < int(sizeof(rows) / sizeof(rows[0])); i++)
    {
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::InputInt(rows[i].Label, rows[i].Value))
        {
            *rows[i].Value = *rows[i].Value < rows[i].Min
                ? rows[i].Min
                : (*rows[i].Value > rows[i].Max ? rows[i].Max : *rows[i].Value);
            AssistantUI_MarkOptionsDirty();
        }
    }
    if (ImGui::Checkbox("Double click for open doors", &g_AssistantConfig.m_DoubleClickForOpenDoors))
    {
        AssistantUI_MarkOptionsDirty();
    }
}

} // namespace

void TabMain_Draw()
{
    // left: the 23-option checklist (scales with Assistant UI scale so labels don't clip)
    float optionsWidth = ImGui::GetFontSize() * 15.5f;
    if (optionsWidth < 210.0f)
    {
        optionsWidth = 210.0f;
    }
    ImGui::BeginChild("##options_list", ImVec2(optionsWidth, 0.0f), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Options:");
    ImGui::Separator();
    for (int i = 0; i < int(sizeof(s_Options) / sizeof(s_Options[0])); i++)
    {
        const int index = s_Options[i].Index;
        bool state = g_AssistantConfig.GetOption(index);
        if (ImGui::Checkbox(s_Options[i].Label, &state))
        {
            g_AssistantConfig.SetOption(index, state);
            option_changed(index, state); // immediate client effect (parity with the original)
            g_AssistantConfig.SaveMainOptions();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // right: the nested Main/Options tab (tw_Options)
    ImGui::BeginChild("##options_pages", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (ImGui::BeginTabBar("##tw_options", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("Main"))
        {
            draw_main_page();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Options"))
        {
            draw_options_page();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Targeting"))
        {
            TargetAssist_DrawOptions();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

} // namespace xuoassist

#endif // XUO_ASSISTANT
