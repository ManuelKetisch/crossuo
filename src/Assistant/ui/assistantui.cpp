// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

// The assistant shell: window frame, the

#define LOGGER_MODULE Assistant

#include "assistantui.h"

#if defined(XUO_ASSISTANT)

#include <external/gfx/imgui/imgui.h>

#include <common/logging/logging.h>

#include "../../GameWindow.h" // g_GameWindow (the display caption)
#include "../AssistantWindow.h" // AssistantWindow_Show

#include "assistanteventsui.h"
#include "widgets.h"
#include "tabhotkeys.h"
#include "tabmacros.h"
#include "../config/assistantconfig.h"
#include "../core/assist.h"
#include "../core/dressagent.h"
#include "../core/shopagent.h"
#include "../core/global.h"
#include "../../AssetViewer/AssetViewer.h" // AssetViewer_Toggle (the Assets button)
#include "../managers/displaymanager.h"
#include "../macros/macroexecutor.h"
#include "../script/commandqueue.h"
#include "../script/scriptfile.h" // the file-engine sandbox root
#include "../script/scriptgump.h" // the script text window draw
#include "../script/scripthandle.h"
#include "CrossUO.h" // ::g_Game (StartReconnect)
#include "editors.h"
#include "../script/scriptlist.h" // g_LoadedScripts (Autostart entry)
#include "tabscripts.h"
#include "tabtools.h"
#include "tabinventory.h"
#include "../core/damagemeter.h"
#include "../core/bandageheal.h"
#include "../core/potionhands.h"
#include "Gumps/GumpToolbar.h"
#include "tabrazorimport.h"
#include "../core/inventoryagent.h"
#include "../core/itemcounters.h"
#include "../core/targetassist.h"
#include "../core/packettrace.h"
#include "../core/gameplayfilters.h"
#include "mapwindow.h"
#include "inspector.h"
#include "textdialog.h"
#include "../managers/assmap.h"
#include "Managers/CorpseManager.h" // ::g_CorpseManager (corpse auto-open)

#include <revision.h> // PRODUCT_VERSION_STR (generated into the build dir)

namespace xuoassist {

// the script editor window flag (declared in assistantui.h inside this
// namespace - keep the definition in it too, or the compiler warns about a
// missing matching extern declaration)

// --- shell state -------------------------------------------------------------

namespace {

// Comfortable default shell geometry so all 11 main tabs, sub-tabs, and the
// optional journal pane stay readable without crowding.
constexpr int SHELL_DEFAULT_WIDTH = 900;
constexpr int SHELL_DEFAULT_HEIGHT = 640;
constexpr float SHELL_MIN_WIDTH = 760.0f;
constexpr float SHELL_MIN_HEIGHT = 480.0f;

const char *const MAIN_TABS[] = {
    "Main", "Display", "Lists", "Hotkeys", "Macros",
    "Scripts", "Agents", "Skills", "Filters", "Tools", "About",
};
enum MAIN_TAB
{
    MT_MAIN = 0,
    MT_DISPLAY,
    MT_LISTS,
    MT_HOTKEYS,
    MT_MACROS,
    MT_SCRIPTS,
    MT_AGENTS,
    MT_SKILLS,
    MT_FILTERS,
    MT_TOOLS,
    MT_ABOUT
};

const char *const LIST_TABS[] = { "Types", "Objects", "Find", "Ignore", "Friends", "Enemies" };
const char *const FILTER_TABS[] = { "Speech", "Replaces", "Sound", "Gameplay" };
// which module delivers the remaining tabs
const char *const PLACEHOLDER_PHASE[] = {
    "",  // Main
    "",  // Display
    "",  // Lists
    "",  // Hotkeys
    "",  // Macros
    "",  // Scripts
    "",  // Agents (nested: Dress/Party/Shop)
    "",  // Skills
    "",  // Filters
    "",  // Tools
    "",  // About
};

bool s_Initialized = false;
CAssistantEvents *s_PreviousEvents = nullptr; // sink to restore on shutdown
bool s_OptionsDirty = false; // Main/Options pages save on close (original semantics)
uint32_t s_SessionStartTicks = 0;

// auto-reconnect countdown
int s_ReconnectSeconds = 0;
uint32_t s_ReconnectLastTick = 0;

// the display caption: the last astr_t we applied to
astr_t s_LastWindowTitle;
uint32_t s_LastStepTick = 0;

// the Autostart script run (
uint32_t s_AutostartAt = 0;

// one-shot programmatic main tab selection (consumed on next AssistantUI_Draw)
int s_PendingMainTab = -1;
bool s_PendingBandageTab = false;
// keep the shell in its own platform viewport when explicitly popped out
bool s_ForcePopOut = false;

// the display caption: compose the plain textual title and apply it to the
// client window when it changed
void apply_display_title()
{
    g_DisplayManager.Redraw(g_AssistantConfig.DisplayEnabled());

    // apply to the OS window on change (
    const astr_t &title = g_DisplayManager.GetTitleText();
    if (title != s_LastWindowTitle)
    {
        s_LastWindowTitle = title;
        if (g_GameWindow.m_window != nullptr)
        {
            g_GameWindow.SetTitle(title);
        }
    }
}

// the AssistantUI_Tick half of the caption: consume the dirty requests, then
// compose+apply. The {x}/{y}/{z} tags set g_UpdateDisplayOnStep at compile
// time - those refresh at 4 Hz; everything else is change-driven (event or
// edit).
void tick_display_title()
{
    const bool forced = TabDisplay_ConsumeTitleDirty();
    if (!forced && !g_UpdateDisplayOnStep)
    {
        return;
    }
    if (!forced && (g_Ticks - s_LastStepTick) < 250u)
    {
        return;
    }
    s_LastStepTick = g_Ticks;
    apply_display_title();
}

void mark_options_dirty()
{
    s_OptionsDirty = true;
}

} // namespace

namespace
{

// --- placeholder page ---------------------------------------------------------

void draw_placeholder(const char *title, const char *phase)
{
    ImGui::TextDisabled("%s", title);
    ImGui::TextDisabled("Coming in phase %s.", phase);
}

// --- nested tab bars ----------------------------------------------------------

void draw_lists_tabs()
{
    if (ImGui::BeginTabBar("##lists_tabs", ImGuiTabBarFlags_None))
    {
        for (int i = 0; i < int(sizeof(LIST_TABS) / sizeof(LIST_TABS[0])); i++)
        {
            char childId[32];
            snprintf(childId, sizeof(childId), "##lists_sub%d", i);
            if (BeginSubTab(LIST_TABS[i], childId))
            {
                TabLists_Draw(i);
                EndSubTab();
            }
        }
        ImGui::EndTabBar();
    }
}

void draw_filters_tabs()
{
    if (ImGui::BeginTabBar("##filters_tabs", ImGuiTabBarFlags_None))
    {
        for (int i = 0; i < int(sizeof(FILTER_TABS) / sizeof(FILTER_TABS[0])); i++)
        {
            char childId[32];
            snprintf(childId, sizeof(childId), "##filters_sub%d", i);
            if (BeginSubTab(FILTER_TABS[i], childId))
            {
                TabFilters_Draw(i);
                EndSubTab();
            }
        }
        ImGui::EndTabBar();
    }
}

void draw_agents_tabs()
{
    // the nested bar (Dress/Party/Shop), agent pages
    if (ImGui::BeginTabBar("##agents_tabs", ImGuiTabBarFlags_None))
    {
        if (BeginSubTab("Inventory", "##agents_inventory"))
        {
            TabInventory_Draw();
            EndSubTab();
        }
        const ImGuiTabItemFlags bandageFlags =
            s_PendingBandageTab ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        s_PendingBandageTab = false;
        if (ImGui::BeginTabItem("Bandage Heal", nullptr, bandageFlags))
        {
            float height = ImGui::GetContentRegionAvail().y;
            if (height < 1.0f)
            {
                height = 1.0f;
            }
            ImGui::BeginChild("##agents_bandages", ImVec2(0.0f, height), ImGuiChildFlags_None);
            BandageHeal_Draw();
            EndSubTab();
        }
        if (BeginSubTab("Potion Hands", "##agents_potion_hands"))
        {
            PotionHands_Draw();
            EndSubTab();
        }
        if (BeginSubTab("Dress", "##agents_dress"))
        {
            TabDress_Draw();
            EndSubTab();
        }
        if (BeginSubTab("Party", "##agents_party"))
        {
            TabParty_Draw();
            EndSubTab();
        }
        if (BeginSubTab("Shop", "##agents_shop"))
        {
            TabShop_Draw();
            EndSubTab();
        }
        ImGui::EndTabBar();
    }
}

// --- auto reconnect (TabMain::StartReconnect / on_ReconnectTimerTimer) ---------

void tick_reconnect()
{
    const bool feature = EnabledFeatureReconnector();
    if (!g_AssistantConfig.AutoReconnect() || !feature || g_Connected)
    {
        s_ReconnectSeconds = 0;
        return;
    }
    if (s_ReconnectSeconds <= 0)
    {
        s_ReconnectSeconds = g_AssistantConfig.AutoReconnectSeconds();
        s_ReconnectLastTick = g_Ticks;
        return;
    }
    if (g_Ticks - s_ReconnectLastTick >= 1000u)
    {
        s_ReconnectLastTick = g_Ticks;
        s_ReconnectSeconds--;
        if (s_ReconnectSeconds == 0)
        {
            g_Game.StartReconnect();
            s_ReconnectSeconds = g_AssistantConfig.AutoReconnectSeconds();
        }
    }
}

} // namespace

// --- lifecycle ------------------------------------------------------------------

void AssistantUI_Init()
{
    if (s_Initialized)
    {
        return;
    }
    // install the UI events consumer (journal pane feed); handlers unchanged
    s_PreviousEvents = g_AssistantEvents;
    g_AssistantEvents = AssistantUIEvents_Get();
    // pin the file-engine sandbox to the client dir (the config root: the
    // client cwd == binary dir) once, on the main thread
    ScriptFile_SetSandboxRoot(fs_path_str(g_AssistantConfig.GetRootPath()));
    s_Initialized = true;
    if (g_Connected && g_AssistantConfig.Autostart() && s_AutostartAt == 0)
    {
        AssistantUI_QueueAutostart();
    }
    // the profile list for the combo (was AssistForm::Init's scan); the
    // serial-mapped profile is selected + loaded at login (LoadProfileBySerial)
    g_AssistantConfig.ScanProfiles();
    g_AssistantConfig.LoadShopList();
    // compile the display title data from the just-loaded profile (was
    // AssistForm::LoadCurrentProfile -> g_TabDisplay->RecompileData)
    // and paint the initial caption
    AssistantUI_RecompileDisplayTitle();
    // the map settings (loaded before the window opens, so the
    g_MapSettings.Load();
    Info(Assistant, "assistant UI installed");
}

void AssistantUI_RecompileDisplayTitle()
{
    g_DisplayManager.RecompileData(g_AssistantConfig.DisplayTitleData());
    TabDisplay_RequestTitleRedraw();
}

void AssistantUI_OpenDisplayCounters()
{
    if (!AssistantWindow_Show(true))
    {
        return;
    }
    s_PendingMainTab = MT_DISPLAY;
    TabDisplay_SelectCounters();
}

void AssistantUI_RequestClientClose()
{
    // CWindow::Destroy pushes the SDL_EVENT_QUIT that runs the normal shutdown path
    if (g_GameWindow.m_window != nullptr)
    {
        g_GameWindow.Destroy();
    }
}

void AssistantUI_QueueAutostart()
{
    // the checkbox check stays in the EnterWorld path (
    s_AutostartAt = g_Ticks + 1000;
}

void AssistantUI_Tick()
{
    // runs from AssistantWindow_FrameTick (every game frame, visible or not)
    // the deferred Autostart script run
    if (s_AutostartAt != 0u && g_Ticks >= s_AutostartAt)
    {
        s_AutostartAt = 0;
        // runs the loaded script named Autoload.js (the Scripts tab list's
        // autostart entry), else nothing: there is no shared editor buffer
        // anymore and silently running an arbitrary file would surprise
        g_LoadedScripts.DrainPendingRestore();
        const vector<CLoadedScript> &scripts = g_LoadedScripts.Scripts();
        for (const CLoadedScript &script : scripts)
        {
            if (ScriptList_DisplayName(script.Path) == "Autoload.js")
            {
                ScriptManager_PlayFile(script.Path);
                break;
            }
        }
    }
    // the script runtime first: the marshaled commands must run on the main
    // thread before anything else consumes game state this frame (plan:
    // queue -> agents -> draw)
    ScriptQueue_Drain();
    ScriptManager_Get()->Tick();
    tick_reconnect();
    PotionHands_Tick();
    if (!PotionHands_Busy())
    {
        // macro playback (frame-tick driven): a
        if (!InventoryAgent_MoveInFlight())
        {
            g_MacroExecutor.Tick();
        }
        // corpse auto-open: the client manager holds
        if (g_AssistantConfig.CorpsesAutoopen())
        {
            if (::g_CorpseManager.CheckCorpses(
                    g_AssistantConfig.OpenCorpsesDistance(),
                    g_AssistantConfig.UseItemsDelay(),
                    g_LastUseObjectTimer))
            {
                g_LastUseObjectTimer = g_Ticks;
            }
        }
        // the dress sequence state machine
        g_DressAgent.Tick();
        // the shop engine progress countdown
        g_ShopAgent.Tick();
        BandageHeal_Tick();
        InventoryAgent_Tick();
    }
    TabTools_Tick();
    CGumpToolbar::Tick();
    // the display caption painter
    tick_display_title();
}

void AssistantUI_Shutdown()
{
    // disarm a pending Autostart run
    s_AutostartAt = 0;
    s_PendingMainTab = -1;
    s_PendingBandageTab = false;
    s_ForcePopOut = false;
    TabTools_ResetSession();
    TabRazorImport_Reset();
    InventoryAgent_ResetSession();
    ItemCounters_ResetSession();
    DamageMeter_Reset();
    BandageHeal_ResetSession();
    PotionHands_ResetSession();
    CGumpToolbar::ResetSession();
    TargetAssist_ResetSession();
    GameplayFilters_ResetSession();
    PacketTrace_Stop();
    // the queue closes FIRST: its shutdown completes (and wakes) every queued
    // command waiter, so scripts blocked in a pending call fall through
    // immediately instead of outliving the join timeout and being detached
    // into teardown
    ScriptQueue_Shutdown();
    ScriptManager_Get()->StopAll(2000);
    // one more brief join pass: stragglers that only needed their pending
    // call dropped become joinable here
    ScriptManager_Get()->StopAll(250);
    // disarm any from-target request armed before shutdown (CancelPendingFrom
    // Targets): otherwise the stale g_TargetHandler would swallow the first
    // legit 0x6C after a re-init
    CancelPendingFromTargets();
    // the phase 5 windows: persist the map settings + geometry (delta: the
    // original app only wrote Map.ini on the settings form Apply) and clear the
    // inspector/text-dialog state
    MapWindow_Shutdown();
    Inspector_Shutdown();
    TextDialog_Shutdown();
    g_MapSettings.Save();
    // stop the agents
    g_DressAgent.Abort();
    g_ShopAgent.Reset();
    TabShop_CloseEditor();
    if (s_Initialized)
    {
        // flush pending Main/Options page edits (saved on close)
        if (s_OptionsDirty)
        {
            g_AssistantConfig.SaveProfile();
            s_OptionsDirty = false;
        }
        // restore the previous events sink
        g_AssistantEvents = s_PreviousEvents;
        s_PreviousEvents = nullptr;
        s_Initialized = false;
    }
    // NOTE: the UI-owned textures (icon atlas, map radar, About logo) are NOT
    // released here: they must stay registered until the ImGui backend shuts
    // them down (GL release), and the CPU pixels are freed after
    // DestroyContext - both handled by AssistantWindow_Shutdown right after
    // ui_shutdown via AssistantUI_AfterImGuiShutdown.
}

void AssistantUI_ReleaseTexturesImGuiAlive()
{
    // whole-assistant shutdown while the ImGui context survives (the asset
    // viewer keeps drawing): the textures the UI modules own can be released
    // right away; every module lazily rebuilds its texture on next use (the
    // icon atlas, the map radar and the About logo all have init-on-draw
    // paths), and the rebuilt textures are torn down by the next ui_shutdown
    // exactly as before
    Icons_ReleaseNow();
    AssMap_AfterImGuiShutdown();
    TabAbout_AfterImGuiShutdown();
}

void AssistantUI_AfterImGuiShutdown()
{
    // called right after ui_shutdown (the ImGui context is gone): free the
    // CPU-side pixels of the UI-owned textures; their GL names were reclaimed
    // by the backend teardown inside ui_shutdown (RefCount still 1)
    Icons_Shutdown();
    AssMap_AfterImGuiShutdown();
    TabAbout_AfterImGuiShutdown();
}

int AssistantUI_ReconnectSecondsLeft()
{
    return s_ReconnectSeconds;
}

// The script text window (the g_TextDialog port, scriptgump.h): a small
// ImGui window fed by the main-thread scriptgump.cpp state (the ops arrive
// marshaled through SC_TEXT_WINDOW). Position/size come from the engine's
// SetPos/SetSize ops; the window only closes through TextWindow.Close (the
// script owns it).
void ScriptTextWindow_Draw()
{
    if (!ScriptTextWindow_Opened())
    {
        return;
    }
    int x = 40;
    int y = 40;
    int width = 400;
    int height = 300;
    ScriptTextWindow_Rect(&x, &y, &width, &height);
    ImGui::SetNextWindowPos(ImVec2((float)x, (float)y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2((float)width, (float)height), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Script text window", nullptr))
    {
        ImGui::End();
        return;
    }
    const vector<astr_t> &lines = ScriptTextWindow_Lines();
    for (const astr_t &line : lines)
    {
        ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
    }
    // keep the newest line visible (the dialog scrolled to the bottom)
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::End();
}

void CancelPendingFromTargets()
{
    TabHotkeys_CancelPendingFromTargets();
    TabLists_CancelPendingFromTargets();
    TabDress_CancelPendingFromTargets();
    TabParty_CancelPendingFromTargets();
    TabInventory_CancelPendingTarget();
}

void AssistantUI_MarkOptionsDirty()
{
    mark_options_dirty();
}

void AssistantUI_ClearOptionsDirty()
{
    s_OptionsDirty = false;
}

// --- the shell -------------------------------------------------------------------

void AssistantUI_Draw(bool *open, uint32_t sessionStartTicks)
{
    s_SessionStartTicks = sessionStartTicks;

    // closed: submit nothing. Begin() does not skip the window when *open is
    // false, so without this early-out the shell (and everything drawn in this
    // pass) would keep rendering while hidden. The frame pipeline in
    // AssistantWindow_DrawOverlay keeps running so ImGui still processes the
    // close and tears down popped-out platform windows.
    if (open != nullptr && !*open)
    {
        return;
    }

    Icons_Init(); // idempotent; needs the live ImGui context

    ImGui::SetNextWindowSize(
        ImVec2((float)SHELL_DEFAULT_WIDTH, (float)SHELL_DEFAULT_HEIGHT), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(SHELL_MIN_WIDTH, SHELL_MIN_HEIGHT), ImVec2(FLT_MAX, FLT_MAX));
    if (s_ForcePopOut)
    {
        ImGuiWindowClass popoutClass;
        popoutClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&popoutClass);
    }
    if (s_PendingMainTab >= 0)
    {
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
        ImGui::SetNextWindowFocus();
    }

    // assistant windows opacity: clamped to the
    const float opacity = g_AssistantConfig.WindowsOpacity() / 100.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity < 0.3f ? 0.3f : (opacity > 1.0f ? 1.0f : opacity));

    if (!ImGui::Begin("X:UO Assist", open))
    {
        // collapsed: the window X button already flipped *open for us
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    // header row: pop out of / dock back into the game window (ADR D11)
    const ImGuiViewport *mainViewport = ImGui::GetMainViewport();
    const bool poppedOut = ImGui::GetWindowViewport() != mainViewport;
    if (poppedOut)
    {
        if (ImGui::Button("Dock in game window"))
        {
            s_ForcePopOut = false;
            const ImVec2 maxDockSize(
                mainViewport->WorkSize.x > 64.0f ? mainViewport->WorkSize.x - 64.0f : mainViewport->WorkSize.x,
                mainViewport->WorkSize.y > 64.0f ? mainViewport->WorkSize.y - 64.0f : mainViewport->WorkSize.y);
            const ImVec2 currSize = ImGui::GetWindowSize();
            if (currSize.x > maxDockSize.x || currSize.y > maxDockSize.y)
            {
                ImGui::SetWindowSize(ImVec2(
                    currSize.x < maxDockSize.x ? currSize.x : maxDockSize.x,
                    currSize.y < maxDockSize.y ? currSize.y : maxDockSize.y));
            }
            ImGui::SetWindowPos(ImVec2(
                mainViewport->WorkPos.x + 32.0f,
                mainViewport->WorkPos.y + 32.0f));
        }
    }
    else
    {
        // Request a dedicated platform viewport via NoAutoMerge, and only move
        // the window to another monitor or adjacent visible desktop space so it
        // never gets flung off-screen on single-monitor setups.
        if (ImGui::Button("Pop out"))
        {
            s_ForcePopOut = true;
            const ImVec2 winSize = ImGui::GetWindowSize();
            const ImVector<ImGuiPlatformMonitor> &monitors = ImGui::GetPlatformIO().Monitors;
            const ImGuiPlatformMonitor *mainMonitor = nullptr;
            const ImGuiPlatformMonitor *otherMonitor = nullptr;
            for (int m = 0; m < monitors.Size; ++m)
            {
                const ImGuiPlatformMonitor &mon = monitors[m];
                const bool containsMain =
                    mainViewport->Pos.x >= mon.MainPos.x && mainViewport->Pos.y >= mon.MainPos.y &&
                    mainViewport->Pos.x < mon.MainPos.x + mon.MainSize.x &&
                    mainViewport->Pos.y < mon.MainPos.y + mon.MainSize.y;
                if (containsMain)
                {
                    mainMonitor = &mon;
                }
                else if (otherMonitor == nullptr && mon.WorkSize.x >= 200.0f && mon.WorkSize.y >= 200.0f)
                {
                    otherMonitor = &mon;
                }
            }
            if (otherMonitor != nullptr)
            {
                ImGui::SetWindowPos(ImVec2(otherMonitor->WorkPos.x + 48.0f, otherMonitor->WorkPos.y + 48.0f));
            }
            else if (mainMonitor != nullptr)
            {
                const float rightX = mainViewport->Pos.x + mainViewport->Size.x + 24.0f;
                const float leftX = mainViewport->Pos.x - winSize.x - 24.0f;
                if (rightX + winSize.x <= mainMonitor->WorkPos.x + mainMonitor->WorkSize.x)
                {
                    ImGui::SetWindowPos(ImVec2(rightX, mainViewport->Pos.y + 48.0f));
                }
                else if (leftX >= mainMonitor->WorkPos.x)
                {
                    ImGui::SetWindowPos(ImVec2(leftX, mainViewport->Pos.y + 48.0f));
                }
            }
        }
    }

    // the title carries "X:UO Assist v<version> <playerName>"
    ImGui::SameLine();
    const astr_t charName(g_SelectedCharName);
    if (!charName.empty())
    {
        ImGui::TextDisabled("v" PRODUCT_VERSION_STR " %s", charName.c_str());
    }
    else
    {
        ImGui::TextDisabled("v" PRODUCT_VERSION_STR);
    }
    ImGui::SameLine();
    static bool journalOpen = true;
    ImGui::Checkbox("Journal", &journalOpen);
    ImGui::SameLine();
    if (ImGui::Button("Assets"))
    {
        AssetViewer_Toggle();
    }
    ImGui::SameLine();
    const bool autoHealOn = BandageHeal_Running();
    if (autoHealOn)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.50f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.60f, 0.32f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.42f, 0.20f, 1.0f));
    }
    if (ImGui::Button(autoHealOn ? "Auto-Heal: ON" : "Auto-Heal: OFF"))
    {
        if (autoHealOn)
        {
            BandageHeal_Stop();
        }
        else if (!BandageHeal_Start())
        {
            s_PendingMainTab = MT_AGENTS;
            s_PendingBandageTab = true;
        }
    }
    if (autoHealOn)
    {
        ImGui::PopStyleColor(3);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        s_PendingMainTab = MT_AGENTS;
        s_PendingBandageTab = true;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Bandage Auto-Heal: %s\nLeft-click to toggle. Right-click to configure.",
            BandageHeal_Status());
    }
    ImGui::Separator();

    // content: tabs on the left/top, journal pane below, status bar pinned last.
    // The tab bar stays fixed; only each tab's content scrolls (its child).
    const float footerHeight = ImGui::GetFrameHeightWithSpacing();
    float tabsHeight = ImGui::GetContentRegionAvail().y - footerHeight;
    const float maxJournalHeight = tabsHeight * 0.30f;
    const float journalHeight = maxJournalHeight < 140.0f ? maxJournalHeight : 140.0f;
    const bool showJournal = journalOpen && journalHeight >= 72.0f && tabsHeight > journalHeight + footerHeight * 4.0f;
    if (showJournal)
    {
        tabsHeight -= journalHeight;
    }

    const int pendingMainTab = s_PendingMainTab;
    s_PendingMainTab = -1;
    if (ImGui::BeginTabBar("##main_tabs", ImGuiTabBarFlags_None))
    {
        for (int i = 0; i < int(sizeof(MAIN_TABS) / sizeof(MAIN_TABS[0])); i++)
        {
            const ImGuiTabItemFlags tabFlags =
                pendingMainTab == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (!ImGui::BeginTabItem(MAIN_TABS[i], nullptr, tabFlags))
            {
                continue;
            }
            // the child takes the height left over by the tab bar and holds
            // the scroll region for this tab's content
            float contentHeight = tabsHeight - ImGui::GetFrameHeightWithSpacing();
            if (contentHeight < 1.0f)
            {
                contentHeight = 1.0f;
            }
            ImGui::BeginChild(MAIN_TABS[i], ImVec2(0.0f, contentHeight), ImGuiChildFlags_None);
            switch (i)
            {
                case MT_MAIN:
                    TabMain_Draw();
                    break;
                case MT_DISPLAY:
                    TabDisplay_Draw();
                    break;
                case MT_LISTS:
                    draw_lists_tabs();
                    break;
                case MT_HOTKEYS:
                    TabHotkeys_Draw();
                    break;
                case MT_MACROS:
                    TabMacros_Draw();
                    break;
                case MT_SCRIPTS:
                    if (ImGui::BeginTabBar("##script_tools"))
                    {
                        if (ImGui::BeginTabItem("Scripts"))
                        {
                            TabScripts_Draw();
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Razor Import"))
                        {
                            TabRazorImport_Draw();
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                    break;
                case MT_FILTERS:
                    draw_filters_tabs();
                    break;
                case MT_AGENTS:
                    draw_agents_tabs();
                    break;
                case MT_SKILLS:
                    TabSkills_Draw();
                    break;
                case MT_TOOLS:
                    TabTools_Draw();
                    break;
                case MT_ABOUT:
                    TabAbout_Draw();
                    break;
                default:
                    draw_placeholder(MAIN_TABS[i], PLACEHOLDER_PHASE[i]);
                    break;
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (showJournal)
    {
        JournalPane_Draw(journalHeight);
    }

    StatusBar_Draw(s_SessionStartTicks);

    ImGui::End();
    ImGui::PopStyleVar();

    // the pop-out assistant windows (hotkeys form, speech-only control
    // forms): separate ImGui windows gated by this draw call, i.e. by the
    // AssistantWindow visibility like everything else
    TabHotkeys_DrawControlForms();
    // the script editors: one window per open document (the Scripts tab
    // double-click opens them, Save As closes/rebinds them)
    Editors_Draw();
    // the shopping progress popup: agent-owned window drawn next to the other
    // always-on UI so it survives leaving the Shop sub-tab (still only while
    // the assistant window is drawn; the popup itself early-outs when the
    // agent is idle or the "Show shopping progress bar" option is off)
    TabShop_DrawProgress();
    // the script text window (the g_TextDialog port): small ImGui window
    // driven by scriptgump.cpp state. Documented caveat: it lives inside this
    // draw pass, so it hides with the assistant shell (the lines stay; the
    // packet handlers keep running - only the pixels are gated)
    ScriptTextWindow_Draw();
    // the secondary windows: the Map (radar + overlays + context menu), its
    // settings form, the Object Inspector sections and the Text Dialog -
    // each gated by its own open flag, viewport-safe like the shell
    MapWindow_Draw(&g_AssistantMapOpen);
    MapSettings_Draw();
    InspectorWindow_Draw();
    TextDialog_Draw();
}

} // namespace xuoassist

#endif // XUO_ASSISTANT
