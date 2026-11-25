// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

#define LOGGER_MODULE Assistant

#include "AssistantWindow.h"
#include "AssistantWindow_rules.h"

#if defined(XUO_ASSISTANT)

#include <string.h>

#include <common/fs.h>
#include <common/logging/logging.h>
#include <external/gfx/gfx.h>
#include <external/gfx/imgui/imgui.h>
#include <external/gfx/imgui/imgui_internal.h> // RegisterUserTexture
#include <external/gfx/ui.h>
#include <xuocore/enumlist.h>
#include <xuocore/uodata.h> // ExeFilePath
#include <vector>
#include "core/textcommandmanager.h" // g_TextCommandManager (Client.* table init)
#include "ui/assistantui.h" // AssistantUI_Draw/Init/Tick/Shutdown (xuoassist)
#include "ui/widgets.h"     // Icons_Init/Icons_Shutdown (icon atlas lifecycle)
#include "../AssetViewer/AssetViewer.h" // AssetViewer_Draw (the viewer shares this frame)
#include "core/global.h"    // g_Connected (xuoassist)
#include "core/itemcounters.h" // the gameplay HUD
#include "config/assistantconfig.h" // g_AssistantConfig (UI scale)
#include "core/hotkeys.h"   // the hotkey engine
#include "macros/macroexecutor.h" // g_MacroExecutor.Reset (disconnect reset)
#include "script/scripthandle.h" // ScriptManager (stop self-test)
#include "core/world.h"          // g_World/g_Player (stop self-test gate)
#include "Globals.h"        // g_GameState, g_Ticks
#include "../GameWindow.h" // g_GameWindow
#include "../Managers/MouseManager.h"
#include "../Config.h"      // g_Config.AssistantEnabled (the Assistant= crossuo.cfg key)

// X11 (pulled in by SDL_syswm.h on Linux) defines `Status` as a macro, which
// collides with ImGui's ImTextureData::Status member below
#undef Status

// The assistant wraps the game window and the game GL context: it never creates
// a window or a GL context of its own. Popped-out platform windows are created,
// rendered and destroyed by the ImGui SDL2 backend inside ui_draw.
static win_context s_assistantWin;
static ui_context s_ctx;
static bool s_initialized = false;
static bool s_visible = false;
static uint32_t s_sessionStartTicks = 0;
static bool s_textInputActive = false;
// io.IniFilename keeps the pointer handed over in win_context.inifile, so the
// buffer must outlive the ImGui context: it lives for the process lifetime.
static char s_iniFile[512] = {};

// the assistant is a gameplay tool: it exists exclusively while the client is
// at the in-game stage (ADR D10)
static bool at_game_stage()
{
    return g_GameState == GS_GAME || g_GameState == GS_GAME_BLOCKED;
}

// the Assistant= crossuo.cfg key ("no") forbids launching the assistant at
// all: every lazy-init path (overlay draw, the ToggleAssistant macro) checks
// here first and never brings the ImGui host up
static bool assistant_enabled()
{
    return g_Config.AssistantEnabled;
}

static xuoassist::AssistantOverlayInputState current_overlay_input_state()
{
    const ImGuiIO &io = ImGui::GetIO();
    xuoassist::AssistantOverlayInputState state;
    state.ShellVisible = s_visible;
    state.HudVisible = xuoassist::ItemCounters_IsHUDVisible();
    state.AssetViewerOpen = AssetViewer_IsWindowOpen();
    state.WantCaptureMouse = io.WantCaptureMouse;
    state.WantCaptureKeyboard = io.WantCaptureKeyboard;
    state.WantTextInput = io.WantTextInput;
    return state;
}

static bool event_belongs_to_game(const SDL_Event &ev)
{
    // mirrors WispWindow.cpp OnWindowProc: per-event window ids decide who owns
    // the event; anything not addressed to the game window belongs to an
    // assistant pop-out platform window.
    const uint32_t gameId = s_assistantWin.window != nullptr
                                ? SDL_GetWindowID(s_assistantWin.window)
                                : 0;
    if (gameId == 0)
        return true;
    switch (ev.type)
    {
        // SDL3: window events are their own event types, all with windowID
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_MOVED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            return ev.window.windowID == gameId;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return ev.key.windowID == gameId;
        case SDL_EVENT_TEXT_EDITING:
        case SDL_EVENT_TEXT_INPUT:
            return ev.text.windowID == gameId;
        case SDL_EVENT_MOUSE_MOTION:
            return ev.motion.windowID == gameId;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return ev.button.windowID == gameId;
        case SDL_EVENT_MOUSE_WHEEL:
            return ev.wheel.windowID == gameId;
        default:
            return true; // window-less events (SDL_EVENT_QUIT, ...) go to the game
    }
}

// Sole owner of the SDL text-input state while the assistant is initialized,
// called once per frame from AssistantWindow_DrawOverlay right after ui_newframe
// (the assistant ImGui backend runs first; its SDL2 backend only touches
// SDL_Start/StopTextInput on Android, so this has the final say everywhere).
// Precedence: assistant text field > game console > stop.
// - io.WantTextInput (an assistant InputText is active): keep text input
//   started. The game console receives nothing meanwhile: the event pump in
//   Main.cpp hands SDL_EVENT_TEXT_INPUT to ImGui (AssistantWindow_ProcessEvent
//   consumes it) whenever the assistant wants text, so nothing is typed twice.
// - game stage (GS_GAME/GS_GAME_BLOCKED): the game console (CGameScreen::
//   OnTextInput -> g_EntryPointer->Insert) and text-entry gumps (including
//   CGumpTextEntryDialog on the blocked screen, via CGumpManager::OnTextInput)
//   are fed EXCLUSIVELY from SDL_EVENT_TEXT_INPUT events (WispWindow OnWindowProc),
//   so keep text input started even when the assistant does not want text.
//   The client never toggles the state itself: SDL leaves it enabled from
//   video init, and stopping it here whenever an assistant field lost focus is
//   exactly what muted the game console (no SDL_EVENT_TEXT_INPUT -> cannot talk).
// - otherwise: stopped.
static void sync_text_input(bool assistantWantsText, bool gameStage)
{
    const bool wantText = assistantWantsText || gameStage;
    if (wantText == s_textInputActive)
        return;
    s_textInputActive = wantText;
    // SDL3: text input is per-window; the game window owns the console and
    // the assistant fields draw into its overlay
    SDL_Window *window = g_GameWindow.m_window;
    if (window == nullptr)
    {
        return;
    }
    if (wantText)
    {
        SDL_StartTextInput(window);
    }
    else
    {
        SDL_StopTextInput(window);
    }
}

static bool init()
{
    if (s_initialized)
        return true;

    // the game owns the GL context on the main thread: initialization may run
    // from AssistantWindow_DrawOverlay (in-game render pass) or from the
    // ToggleAssistant game macro (AssistantWindow_Toggle); both run on the
    // main thread with the game GL context current at the in-game stage
    SDL_Window *window =
        g_GameWindow.m_window != nullptr ? g_GameWindow.m_window : nullptr;
    SDL_GLContext context = SDL_GL_GetCurrentContext();
#if defined(XUO_EMSC)
    // SDL does not track the WebGL context: the renderer binds it directly on
    // the canvas (not through SDL_GL_MakeCurrent), so SDL_GL_GetCurrentContext
    // returns null even though the GL is current and functional. The init call
    // sites only run from the in-game render pass with the context current,
    // so skip the SDL context gate entirely (DrawOverlay would otherwise
    // re-attempt init every frame and spam this error)
    (void)context;
#else
    if (window == nullptr || context == nullptr)
    {
        Error(Assistant, "could not initialize: no game window/context");
        return false;
    }
#endif

    memset(&s_assistantWin, 0, sizeof(s_assistantWin));
    s_assistantWin.title = "X:UO Assist";
    s_assistantWin.window = window;
    s_assistantWin.context = context;
    SDL_GetWindowSize(window, &s_assistantWin.width, &s_assistantWin.height);
    // fs_path_ascii hands out a pointer into a temporary fs_path: hold the
    // string in an astr_t that outlives the copy into s_iniFile below
    const astr_t ini = fs_path_str(ExeFilePath("xuoassist.ini"));
    const size_t iniLength = ini.length();
    const size_t iniMax = sizeof(s_iniFile) - 1;
    const size_t iniSize = iniLength < iniMax ? iniLength : iniMax;
    memcpy(s_iniFile, ini.c_str(), iniSize);
    s_iniFile[iniSize] = '\0';
    s_assistantWin.inifile = s_iniFile;

    // docking + multi-viewport (pop-out, ADR D11): ui_draw saves/restores the
    // game context around the platform windows it renders (external/gfx/ui.cpp)
    s_ctx = ui_init(s_assistantWin, /*enable_viewports=*/true);
    if (ImGui::GetCurrentContext() == nullptr)
    {
        // ui_init created only ImGui/backend resources: ui_shutdown destroys
        // them; the SDL window and GL context belong to the game and stay
        ui_shutdown(s_ctx);
        s_ctx = ui_context();
        memset(&s_assistantWin, 0, sizeof(s_assistantWin));
        // DrawOverlay retries init every frame while uninitialized: report
        // the failure once, not per frame
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            Error(Assistant, "initialization failed: no ImGui context");
        }
        return false;
    }
    // the launcher color scheme is the final style: applied after ui_init (its
    // StyleColorsDark and the viewport WindowBg tweak run first) so the
    // assistant renders with the same theme as the launcher
    ui_style_launcher();
    // the ImGui SDL2 backend toggles the OS cursor from NewFrame via
    // ImGui_ImplSDL2_UpdateMouseCursor unless ImGuiConfigFlags_NoMouseCursorChange
    // is set; the game draws its own UO software cursor, so the backend must
    // never touch the process cursor - AssistantWindow_DrawOverlay enforces the
    // cursor policy instead
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    s_initialized = true;
    s_sessionStartTicks = g_Ticks;
    xuoassist::AssistantUI_Init(); // journal pane events consumer
    // the script runtime's text-command table (Client.* calls): build it now
    // and say so - a script command must never answer "Unknown command"
    // because nothing had populated the table yet
    xuoassist::g_TextCommandManager.EnsureCommands();
    Info(Assistant, "text commands registered: %d",
        (int)xuoassist::CTextCommandManager::CommandCount());
    // icon atlas lifecycle is bound to the ImGui context: registered after it
    // exists, released after it is destroyed (see AssistantWindow_Shutdown)
    xuoassist::Icons_Init();
    Info(Assistant, "initialized (ini: %s)", s_iniFile);
    return true;
}

bool AssistantWindow_Show(bool show)
{
    if (!assistant_enabled())
    {
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            Error(Assistant, "assistant disabled");
        }
        return false;
    }
    if (!at_game_stage())
        return false;
    if (!s_initialized && !init())
    {
        // log once: the macro falls through to the game instead, and retrying
        // every toggle attempt must not spam the log
        static bool warned = false;
        if (!warned)
        {
            Error(Assistant, "toggle failed: assistant not initialized");
            warned = true;
        }
        return false;
    }
    s_visible = show;
    if (!s_visible)
    {
        // disarm a pending capture field: with the shell hidden nothing would
        // commit it, and the engine would keep eating keys into the buffer
        xuoassist::g_Hotkeys.CancelCapture();
        // hand the cursor back to the client policy when no other ImGui surface
        // (such as the HUD or Asset Viewer) remains active; the backend no
        // longer re-hides it for us since init() set NoMouseCursorChange
        if (!xuoassist::ItemCounters_IsHUDVisible() && !AssetViewer_IsWindowOpen())
        {
            SDL_HideCursor();
        }
    }
    return true;
}

bool AssistantWindow_Toggle()
{
    return AssistantWindow_Show(!AssistantWindow_IsVisible());
}

void AssistantWindow_FrameTick()
{
    // per-frame assistant logic that must run while visible or not
    if (s_initialized)
        xuoassist::AssistantUI_Tick(); // auto-reconnect countdown (was TabMain::m_ReconnectTimer)

#if defined(XUO_ASSISTANT)
    // Forced-stop self-test (XUO_SCRIPT_TEST=1, needs a world: run with
    // XUO_AUTO_LOGIN): starts an API-hammering loop and a pure-JS loop, stops
    // both after 2 s and logs whether each one left the running set. The tight
    // loops are the shapes the QuickJS interrupt handler alone cannot stop.
    if (s_initialized && getenv("XUO_SCRIPT_TEST") != nullptr && g_World != nullptr)
    {
        static int s_Phase = 0;
        static uint32_t s_Next = 0;
        if (s_Phase == 0)
        {
            s_Phase = 1;
            Info(Assistant, "selftest: starting two tight loops");
            // the forced-stop shape: a tight loop that never returns to JS
            // bytecode long enough for the interrupt handler (it hammers the
            // queued API instead), plus a plain JS loop as the second victim
            xuoassist::ScriptManager_Get()->RunFile("selftest.js",
                "while (true) { Client.Wait('1s'); }");
            xuoassist::ScriptManager_Get()->RunFile("selftest2.js", "while (true) {}");
        }
        else if (s_Phase == 1 && g_Ticks >= s_Next)
        {
            s_Next = g_Ticks + 2000;
            s_Phase = 2;
        }
        else if (s_Phase == 2 && g_Ticks >= s_Next)
        {
            Info(Assistant,
                 "selftest: stopping (api loop running=%d, js loop running=%d)",
                 xuoassist::ScriptManager_RunningFile("selftest.js") ? 1 : 0,
                 xuoassist::ScriptManager_RunningFile("selftest2.js") ? 1 : 0);
            xuoassist::ScriptManager_TerminateFile("selftest.js");
            xuoassist::ScriptManager_TerminateFile("selftest2.js");
            s_Phase = 3;
            s_Next = g_Ticks + 2000;
        }
        else if (s_Phase == 3 && g_Ticks >= s_Next)
        {
            Info(Assistant,
                 "selftest: after stop (api loop running=%d, js loop running=%d)",
                 xuoassist::ScriptManager_RunningFile("selftest.js") ? 1 : 0,
                 xuoassist::ScriptManager_RunningFile("selftest2.js") ? 1 : 0);
            s_Phase = 4;
        }
    }
#endif

    // disconnect stage-gate: when the client leaves the in-game stage the assistant
    // drops its connected flag and resets its own state; running here instead
    // of per packet means a stage change is noticed within one frame
    if (xuoassist::g_Connected && !at_game_stage())
    {
        xuoassist::g_MacroExecutor.Reset();
        xuoassist::g_Connected = false;
    }

    // in-game only (ADR D10): logout, disconnect or back-to-main-menu shuts the
    // assistant down, no matter whether its windows are docked or popped out;
    // the client quit path is covered by the shutdown hook in Run()
    if (s_initialized && !at_game_stage())
        AssistantWindow_Shutdown();
}

bool AssistantWindow_ProcessEvent(const SDL_Event &ev)
{
    // Note: the window toggle is no longer a hardcoded key here - it is the
    // ToggleAssistant game macro (Options -> Macros), run by the macro
    // executor through AssistantWindow_Toggle.
    if (!s_initialized)
        return false; // pop-out windows exist only while initialized

    // ImGui's SDL2 backend must see every event, including the ones belonging
    // to its own platform windows
    ui_process_event(s_ctx, &ev);

    if (!event_belongs_to_game(ev))
        return true; // assistant pop-out platform window

    // Shared-window motion still positions the game cursor. A release belongs
    // to the game if it received that button's press before entering the overlay.
    if (ev.type == SDL_EVENT_MOUSE_MOTION ||
        (ev.type == SDL_EVENT_MOUSE_BUTTON_UP && g_MouseManager.IsButtonPressed(ev.button.button)))
    {
        return false;
    }

    const xuoassist::AssistantOverlayInputState overlayState = current_overlay_input_state();

    // The capture field of the hotkeys form ("listening"): it must see the
    // next raw key/wheel/mouse event even when the game would. Gated on
    // !WantTextInput so a busy text field always wins.
    if (xuoassist::Hotkeys_IsCapturing() && !overlayState.WantTextInput &&
        xuoassist::Hotkeys_ProcessCapture(ev))
    {
        return true;
    }

    switch (ev.type)
    {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            if (xuoassist::AssistantOverlay_ShouldCaptureMouse(overlayState))
                return true;
            return xuoassist::Hotkeys_ProcessGameEvent(ev);
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_TEXT_EDITING:
            // Active ImGui text fields always win; otherwise only windows with
            // keyboard controls (shell / Asset Viewer) capture keyboard events.
            if (xuoassist::AssistantOverlay_ShouldCaptureKeyboard(overlayState))
                return true;
            return xuoassist::Hotkeys_ProcessGameEvent(ev);
        default:
            return false;
    }
}

void AssistantWindow_DrawOverlay()
{
    if (!at_game_stage())
        return; // never draws outside the game stage (FrameTick owns shutdown)
    if (!assistant_enabled())
        return;
    // Shut down with the asset viewer's window still open: the viewer owns a
    // top-level window in the (still existing) ImGui context, so its frames
    // keep flowing - only the viewer is drawn, the shell stays closed.
    const bool shellInit = s_initialized;
    if (!s_initialized && !init())
        return;
    if (!shellInit && !AssetViewer_IsWindowOpen())
        return;

    // the game render pass owns the context: ImGui renders the main viewport
    // into it, and ui_draw saves/restores the game context around the popped
    // out platform windows
    // NOTE: when the game window is minimized CGameScreen::Render is not called
    // at all, so this pass (and any popped-out window) freezes until restore;
    // acceptable for now
    // The pipeline keeps running while hidden: the shell X (and the OS close
    // button of a popped-out window) only REPORTS the click through p_open -
    // AssistantUI_Draw stops submitting the window when the flag is false, but
    // the frames must keep flowing so ImGui processes the close and destroys
    // the popped-out platform windows. Bailing out here left them frozen on
    // screen: the assistant could not be closed via its X button.
    ui_newframe(s_ctx);
    ImGui::GetStyle().FontScaleMain =
        xuoassist::AssistantOverlay_ClampUiScale(xuoassist::g_AssistantConfig.UiScale()) / 100.0f;
    if (shellInit)
    {
        xuoassist::ItemCounters_DrawHUD();
        xuoassist::AssistantUI_Draw(&s_visible, s_sessionStartTicks);
    }
    // the asset viewer submits its window into this same ImGui frame, after
    // the assistant shell (its own top-level window; a no-op while hidden)
    AssetViewer_Draw();
    // Cursor policy, taken once every ImGui surface of this frame is submitted.
    // The client hides the OS cursor and draws its own UO software cursor
    // inside the game window, so over the game world that cursor is the
    // pointer. Over an ImGui surface it is the ImGui widgets the user points
    // at, and those are composited above the game-drawn cursor - so there the
    // system cursor takes over: shown while the mouse is over an assistant or
    // asset viewer window (docked in the game viewport or popped out), hidden
    // again as soon as it leaves them, and hidden when no such window is left
    // at all. Deciding here rather than before the submissions is what makes
    // the X button of the docked shell put the cursor back in one frame: the
    // button flips s_visible while the shell is drawn, and WantCaptureMouse
    // answers for the windows of the frame being built, not the previous one.
    // init() set ImGuiConfigFlags_NoMouseCursorChange so the SDL2 backend
    // never fights this; while the pipeline is not running the assistant
    // leaves the cursor alone entirely: the client owns it.
    if (xuoassist::AssistantOverlay_ShouldShowSystemCursor(current_overlay_input_state()))
    {
        SDL_ShowCursor();
    }
    else
    {
        SDL_HideCursor();
    }
    ui_draw(s_ctx);

    // belt and braces: ui_draw restores the game context, nothing should have
    // been able to change it since
    if (SDL_GL_GetCurrentWindow() != s_assistantWin.window)
        SDL_GL_MakeCurrent(s_assistantWin.window, s_assistantWin.context);

    sync_text_input(ImGui::GetIO().WantTextInput, at_game_stage());
}

void AssistantWindow_Shutdown()
{
    if (!s_initialized)
        return;

    // leaving the game stage: restore the state the client shipped with (text
    // input enabled from SDL video init) instead of stopping it - the main-menu
    // stage still types through SDL_EVENT_TEXT_INPUT (login/account fields and, on
    // embedded-launcher builds, the launcher's ImGui text fields)
    sync_text_input(true, false);
    // flush pending edits + uninstall the UI events consumer while the ImGui
    // context (and the registered icon texture) still exist. The atlas stays
    // registered with RefCount == 1 so that ui_shutdown below is the one to
    // release its GL texture (ImGui_ImplOpenGL3_Shutdown -> DestroyDeviceObjects
    // destroys every registered texture whose RefCount is 1,
    // imgui_impl_opengl3.cpp:1023-1031); freeing it earlier would leave a
    // dangling ImTextureData in PlatformIO.Textures (UAF) and leak the GL name
    xuoassist::AssistantUI_Shutdown();
    // the asset viewer window outlives this shutdown when it is open
    AssetViewer_OnAssistantShutdown();
    // The ImGui context is about to go away - unless the asset viewer kept its
    // own window open past this shutdown: then the context and the viewer's
    // textures live on, and only the shell's textures are released (the
    // backend teardown below would never walk them).
    const bool keepContext = AssetViewer_IsWindowOpen();
    if (keepContext)
    {
        xuoassist::AssistantUI_ReleaseTexturesImGuiAlive();
    }
    else
    {
        // destroys the ImGui context and every platform window it created;
        // must not SDL_Quit: the client keeps running
        ui_shutdown(s_ctx);
        // only now, with the context gone and the GL names released, may the
        // CPU-side pixels be freed (icon atlas + map radar + About logo;
        // idempotent, deletes the ImTextureData objects)
        xuoassist::AssistantUI_AfterImGuiShutdown();
    }
    s_initialized = false;
    s_visible = false;
    s_ctx = ui_context();
    memset(&s_assistantWin, 0, sizeof(s_assistantWin));
    // the game draws its own software cursor and the ImGui SDL2 backend toggles
    // the process cursor on mouse focus changes: make sure it stays hidden
    SDL_HideCursor();
    Info(Assistant, "shutdown");
}

bool AssistantWindow_IsVisible()
{
    return s_initialized && s_visible;
}

#endif // XUO_ASSISTANT
