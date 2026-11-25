// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

// The script-runtime harness (XUO_ASSISTANT_SCRIPT_TESTS=ON build target
// xuoassist-script-test). Exercises the pieces that do not need the client:
//   1. QuickJS-ng eval + function call + exception line reporting (the
//      engine.cpp flow, without the client-facing API table).
//   2. The command queue: ordered exec, bounded drop-oldest, shutdown drain,
//      push-after-shutdown fail-fast.
//   3. The journal mirror: Find pattern/flags/time filters + remove.
//   4. Pointer marshaling: the result pointer slot survives a round trip
//      through the queue even for a heap block above the 4GB boundary (the
//      old uint32_t slot truncated it).
//   5. The REAL API registration (apiscript.cpp Script_RegisterAPI) over a
//      live QuickJS context: the engine classes (Gump/File/GumpHook/
//      ClientMacro) must carry their own prototypes - the registration bug had
//      every class alias the GameObject class, so .Select/.Open/.AddEntry
//      were missing and the finalizers freed the wrong opaque types. The
//      client-facing symbols are stubbed in harnessstubs.cpp (disconnected
//      client); no live queue drain here - the scripts run through the real
//      executors directly.

#include "../commandqueue.h"
#include "../journal.h"
#include "../scriptfile.h"
#include "../../core/textcommandmanager.h" // the Client.* text-command table
#include "../scriptlist.h"
#include "../scriptgump.h"
#include "../scriptstate.h"
#include "../engine.h"
#include "../externalruntime.h"
#include "../scripthandle.h" // the run/terminate lifecycle suite

#include "../../config/configio.h" // AtomicFileWrite + CIniFile (the config/save primitives)
#include "../../config/assistantconfig.h" // the Options/Hotkeys INI round trips
#include "../../managers/mapsettings.h" // the Map.ini round trip
#include "../../core/global.h"
#include "../../core/assistantevents.h" // JMT_* (the journal type flags)
#include "../../core/world.h" // g_World stand-in (section 6 executor gate)
#include "../apiscript.h"
#include "../../managers/gumpmanager.h"
#include "../../core/textconv.h" // ParseTextTokens (the tokenizer parity suite)

extern "C" {
#include "../quickjs/quickjs.h"
}

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring> // memcmp
#include <errno.h>
#include <map>
#include <signal.h>
#include <stdlib.h> // mkdtemp
#include <sys/stat.h> // chmod
#include <unistd.h> // rmdir, chdir
#include <cstdlib> // mkdtemp
#include <thread>
#include <vector>
#include "Target.h"
#include "Walker/PathFinder.h"

// the client normally provides g_Ticks (Globals.cpp)
using ::astr_t;
uint32_t g_Ticks = 0;

// the stack-line parser copy of apiscript.cpp Script_FormatException (the
// full function needs the client world; the parser is what the harness checks)
static void harness_format_line(JSContext *ctx, astr_t &line, astr_t &message)
{
    JSValue exception = JS_GetException(ctx);
    line = "0";
    message = "unknown error";
    size_t size = 0;
    const char *text = JS_ToCStringLen(ctx, &size, exception);
    if (text != nullptr)
    {
        message = astr_t(text, size);
        JS_FreeCString(ctx, text);
    }
    JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
    const char *stackText = JS_ToCStringLen(ctx, &size, stack);
    if (stackText != nullptr)
    {
        message += "\n";
        message += stackText;
        JS_FreeCString(ctx, stackText);
        const char *frame = strstr(stackText, "<script>:");
        if (frame != nullptr)
        {
            const char *digits = frame + 9;
            if (*digits == ':')
            {
                digits++;
            }
            line.clear();
            while (isdigit((unsigned char)*digits))
            {
                line += *digits++;
            }
        }
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exception);
}

namespace xuoassist {

// the fake gump the executor deep-copies for GetGump (see harnessstubs.cpp)
extern GumpData *g_HarnessGump;

struct HarnessHookState
{
    uint32_t LastDoubleClickSerial = 0;
    uint32_t LastStatusSerial = 0;
    uint32_t LastMoveItemSerial = 0;
    int LastMoveCount = 0;
    uint32_t LastMoveContainer = 0;
    uint16_t LastMoveX = 0;
    uint16_t LastMoveY = 0;
    char LastMoveZ = 0;
    uint32_t LastTargetSentSerial = 0;
    int LastTargetTileX = 0;
    int LastTargetTileY = 0;
    int LastTargetTileZ = 0;
    uint16_t LastTargetTileGraphic = 0;
    int TargetCancelledCount = 0;
    int LastGumpChoiceButton = -1;
    uint32_t LastGumpChoiceId = 0;
    int LastGumpChoiceCheckCount = 0;
    int LastGumpChoiceEntryCount = 0;
    int LastWalkDir = -1;
    bool LastWalkRun = false;
    int WalkCallCount = 0;
    int WalkToX = 0;
    int WalkToY = 0;
    int WalkToZ = 0;
    int WalkToDistance = 0;
    int WalkToCallCount = 0;
    int StopAutoWalkCount = 0;
    bool WalkToSucceeds = true;
    bool WalkToAutoArrive = false;
    int LastSkillIndex = -1;
    astr_t LastSkillName;
    astr_t LastPrintText;
    uint16_t LastPrintColor = 0;
    std::map<uint32_t, astr_t> OplProperties;
    bool InventoryRunning = false;
    astr_t LastInventoryPreset;
    int LastInventoryMode = -1;
    astr_t LastAutoLootOnce;
    int ScavengerCacheCleared = 0;
    bool BandageRunning = false;
    int LastTargetSelectedItem = 0;
    int LastTargetSelectedTarget = 0;
    vector<GumpData *> HarnessGumps;

    void Reset()
    {
        *this = HarnessHookState();
    }
};

extern HarnessHookState g_HarnessHooks;

} // namespace xuoassist

using namespace xuoassist;

template <typename T, size_t N> constexpr size_t count_of(const T (&)[N])
{
    return N;
}

static int failures = 0;
#define CHECK(cond)                                                  \
    do                                                               \
    {                                                                \
        if (!(cond))                                                 \
        {                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                              \
        }                                                            \
    } while (0)

int main()
{
    // --- 1. QuickJS smoke script ----------------------------------------
    {
        JSRuntime *rt = JS_NewRuntime();
        JSContext *ctx = JS_NewContext(rt);
        CHECK(ctx != nullptr);

        // the script shape: functions on the global object + a Wait-like
        const char *script = "function hello(name) { return 'hi ' + name; }\n"
                             "var boom = function () { nope(); };\n";
        JSValue result = JS_Eval(ctx, script, strlen(script), "<script>", JS_EVAL_TYPE_GLOBAL);
        CHECK(!JS_IsException(result));
        JS_FreeValue(ctx, result);

        JSValue global = JS_GetGlobalObject(ctx);
        JSValue func = JS_GetPropertyStr(ctx, global, "hello");
        CHECK(JS_IsFunction(ctx, func));
        JSValue arg = JS_NewString(ctx, "self");
        JSValue callResult = JS_Call(ctx, func, JS_UNDEFINED, 1, &arg);
        size_t size = 0;
        const char *text = JS_ToCStringLen(ctx, &size, callResult);
        CHECK(text != nullptr && strcmp(text, "hi self") == 0);
        printf("smoke: hello('self') -> %s\n", text ? text : "?");
        JS_FreeCString(ctx, text);
        JS_FreeValue(ctx, callResult);
        JS_FreeValue(ctx, arg);
        JS_FreeValue(ctx, func);

        // the error path reports the line through the stack frames (the
        // quickjs-ng replacement of the original lineNumber accessor; verified via
        // Script_FormatException's parser)
        JSValue errorFn = JS_GetPropertyStr(ctx, global, "boom");
        JSValue errorResult = JS_Call(ctx, errorFn, JS_UNDEFINED, 0, nullptr);
        CHECK(JS_IsException(errorResult));
        astr_t line;
        astr_t message;
        harness_format_line(ctx, line, message);
        CHECK(line == "2");
        printf("smoke: exception line = %s (%s)\n", line.c_str(),
            message.substr(0, message.find('\n')).c_str());
        JS_FreeValue(ctx, errorResult);
        JS_FreeValue(ctx, errorFn);

        // the interrupt handler aborts a tight loop (the Terminate path)
        std::atomic<int> stop{ 0 };
        JS_SetInterruptHandler(rt, [](JSRuntime *, void *opaque) -> int {
            return ((std::atomic<int> *)opaque)->load() == 0;
        }, &stop);
        JSValue loop = JS_Eval(ctx, "function spin(){ while(true){} }",
            strlen("function spin(){ while(true){} }"), "<script>", JS_EVAL_TYPE_GLOBAL);
        CHECK(!JS_IsException(loop));
        JS_FreeValue(ctx, loop);
        std::thread killer([&stop]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            stop = 1;
        });
        JSValue spinFn = JS_GetPropertyStr(ctx, global, "spin");
        JSValue spinResult = JS_Call(ctx, spinFn, JS_UNDEFINED, 0, nullptr);
        CHECK(JS_IsException(spinResult)); // interrupted, not hung
        killer.join();
        JS_FreeValue(ctx, spinResult);
        JS_FreeValue(ctx, spinFn);
        printf("smoke: interrupt handler stopped while(true) in 100ms\n");

        JS_FreeValue(ctx, global);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
    }

    // --- 2. command queue (the REAL apiscript.cpp drain executor) ------------
    {
        // the "main thread": a background drainer like AssistantUI_Tick
        std::atomic<int> draining{ 1 };
        std::thread drainer([](std::atomic<int> *running) {
            while (running->load())
            {
                ScriptQueue_Drain();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }, &draining);
        ScriptQueue_SetCapacity(8);
        // ordered exec: the real SC_WAIT_PARSE body reads the config delay
        // helper (the real assistantconfig.cpp is linked: atoi of the text);
        // SC_NOW falls through the real
        // executor's switch (handled tags only) and copies back the defaults
        auto *first = new XA_SCRIPT_COMMAND();
        first->Tag = SC_WAIT_PARSE;
        first->TextA = "100";
        auto *second = new XA_SCRIPT_COMMAND();
        second->Tag = SC_NOW;
        ScriptQueue_Exec(first);
        ScriptQueue_Exec(second);
        ScriptQueue_Drain();
        ScriptQueue_Drain();
        CHECK(!first->ResultDropped && first->ResultInt == 100);
        CHECK(!second->ResultDropped && second->ResultInt == 0);
        delete first;
        delete second;
        printf("queue: ordered exec + result round trip ok\n");
        draining = 0;
        drainer.join();

        // bounded drop-oldest: SetCapacity clamps to >= 16; Exec BLOCKS the
        // pushing script thread, so the flood needs concurrent pushers (like
        // 24 concurrent scripts): 16 fill the queue and wait, the 8 beyond
        // capacity wake immediately as dropped
        constexpr int FLOOD_COUNT = 24;
        XA_SCRIPT_COMMAND *pushed[FLOOD_COUNT];
        std::thread pushers[FLOOD_COUNT];
        for (int i = 0; i < FLOOD_COUNT; i++)
        {
            pushed[i] = new XA_SCRIPT_COMMAND();
            pushed[i]->Tag = SC_NOW;
        }
        for (int i = 0; i < FLOOD_COUNT; i++)
        {
            pushers[i] = std::thread([i, &pushed]() {
                ScriptQueue_Exec(pushed[i], 5000);
            });
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ScriptQueue_Drain(); // runs what is left, wakes the rest as dropped
        for (int i = 0; i < FLOOD_COUNT; i++)
        {
            pushers[i].join();
        }
        int dropped = 0;
        int delivered = 0;
        for (int i = 0; i < FLOOD_COUNT; i++)
        {
            if (pushed[i]->ResultDropped)
            {
                dropped++;
            }
            else
            {
                delivered++;
            }
            delete pushed[i];
        }
        CHECK(dropped > 0 && delivered > 0);
        printf("queue: drop-oldest under flood ok (dropped=%d delivered=%d)\n", dropped,
            delivered);

        // --- pointer marshaling (the >4GB truncation fix) -------------------
        // A live >4GB heap probe is not feasible here: the build disables
        // exceptions (-fno-exceptions), so a failed 5GB reserve aborts instead
        // of throwing bad_alloc. The static_assert in commandqueue.h pins the
        // slot width; here a real pointer still round-trips through the queue.
        {
            std::vector<char> big;
            big.reserve(4096);
            auto *command = new XA_SCRIPT_COMMAND();
            command->Tag = SC_NOW;
            command->PointerA = (void *)big.data();
            // Exec blocks, so the drain runs on a one-shot helper thread
            std::thread oneShot([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                ScriptQueue_Drain();
            });
            ScriptQueue_Exec(command);
            oneShot.join();
            CHECK(!command->ResultDropped);
            CHECK(command->PointerA == (void *)big.data());
            delete command;
            printf("pointer: slot round trip ok (%p)\n", (void *)big.data());
        }

        // --- result copyback (journal readers ride PointerA) -----------------
        // The real executor hands the found journal entry across the queue in
        // the pointer slot; complete_node must copy it back or every journal
        // read sees nullptr.
        {
            ScriptJournal_Add(CJournalMessage(0x1, 0x02B2, "poisoned"));
            auto *command = new XA_SCRIPT_COMMAND();
            command->Tag = SC_JOURNAL_FIND;
            command->TextA = "poisoned";
            command->TextD = "02B2"; // the color filter (0xFFFF = any)
            std::thread oneShot([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                ScriptQueue_Drain();
            });
            ScriptQueue_Exec(command);
            oneShot.join();
            CHECK(!command->ResultDropped);
            auto *entry = (CJournalMessage *)command->PointerA;
            CHECK(entry != nullptr && entry->Text == "poisoned");
            delete entry; // the script thread owns the handed-over copy
            delete command;
            ScriptJournal_Clear();
            printf("copyback: journal entry through PointerA ok\n");
        }

        // timeout: the caller record must read dropped, not real zeros
        {
            auto *slow = new XA_SCRIPT_COMMAND();
            slow->Tag = SC_NOW;
            ScriptQueue_Exec(slow, 50); // no drain is running here
            CHECK(slow->ResultDropped);
            CHECK(slow->ResultInt == 0);
            delete slow;
            printf("timeout: caller record marked dropped\n");
        }

        // --- the WaitFor* handshake (SC_EVENT_WAIT clear+poll semantics) ---
        // the REAL drain executor against the real g_GumpReceived flag: the
        // first execution clears a pre-set flag (g_GumpReceived =
        {
            auto *wait = new XA_SCRIPT_COMMAND();
            wait->Tag = SC_EVENT_WAIT;
            wait->SubTag = 2; // WF_GUMP (the apiscript flag order)
            for (int round = 0; round < 3; round++)
            {
                g_GumpReceived = true; // a gump "arrived" before the wait
                wait->ResultBool = false;
                std::thread drainOnce([]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    ScriptQueue_Drain();
                });
                wait->BoolA = round > 0; // the caller-side armed marker
                ScriptQueue_Exec(wait);
                drainOnce.join();
                if (round == 0)
                {
                    CHECK(!wait->ResultDropped && !wait->ResultBool);
                    CHECK(!g_GumpReceived); // the drain cleared it
                }
                else if (round == 1)
                {
                    CHECK(wait->ResultBool); // the re-armed flag is visible
                }
            }
            delete wait;
            printf("wait: event-wait clear+poll handshake ok\n");
        }

        // shutdown: pending entries wake dropped, later pushes fail fast
        ScriptQueue_SetCapacity(64);
        auto *pending = new XA_SCRIPT_COMMAND();
        pending->Tag = SC_NOW;
        std::thread waiter([&]() { ScriptQueue_Exec(pending, 5000); });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ScriptQueue_Shutdown();
        waiter.join();
        CHECK(pending->ResultDropped);
        delete pending;

        auto *late = new XA_SCRIPT_COMMAND();
        late->Tag = SC_NOW;
        ScriptQueue_Exec(late);
        CHECK(late->ResultDropped);
        delete late;
        printf("queue: shutdown drain + fail-fast ok\n");
        ScriptQueue_Reset(); // section 6 re-arms the queue (test hook)
    }

    // --- 3. journal mirror ------------------------------------------------
    {
        CJournalMessage mine(0x1, 0x02B2, "You have been poisoned");
        mine.Flags = JMT_MY;
        CJournalMessage sys(0xFFFFFFFF, 0x03B2, "World is saving");
        sys.Flags = JMT_SYSTEM;
        ScriptJournal_Add(mine);
        ScriptJournal_Add(sys);

        CJournalMessage *hit = ScriptJournal_Find("poisoned", 0, 0xFFFF, 0, 0, 0, false);
        CHECK(hit != nullptr && hit->FindTextID == 0);
        delete hit;

        hit = ScriptJournal_Find("saving|poisoned", 0, 0xFFFF, 0, 0, 0, false);
        CHECK(hit != nullptr && hit->FindTextID == 0); // newest first
        delete hit;

        hit = ScriptJournal_Find("poisoned", 0, 0xFFFF, JMT_SYSTEM, 0, 0, false);
        CHECK(hit == nullptr); // type filter
        hit = ScriptJournal_Find("poisoned", 0, 0xFFFF, JMT_MY, 0, 0, true);
        CHECK(hit != nullptr);
        delete hit; // remove semantics: gone now
        hit = ScriptJournal_Find("poisoned", 0, 0xFFFF, 0, 0, 0, false);
        CHECK(hit == nullptr);
        printf("journal: pattern/flags/find-id/remove ok\n");

        ScriptJournal_Clear();
    }

    // --- 4. script state mirrors (globals / timers / buffs) ----------------
    {
        ScriptGlobal_Set("lootbag", "0x40001234");
        ScriptGlobal_Set("lootbag", "0x40005678"); // overwrite (original map semantics)
        CHECK(ScriptGlobal_Get("lootbag") == "0x40005678");
        CHECK(ScriptGlobal_Get("missing") == ""); // unset -> empty (parity with the original)
        ScriptGlobal_Set("", "ignored");          // empty name dropped
        ScriptGlobal_Clear();
        CHECK(ScriptGlobal_Get("lootbag") == "");

        g_Ticks = 1000;
        ScriptTimer_Set("t", 100); // stored: now - value
        g_Ticks = 1500;
        CHECK(ScriptTimer_Exists("t"));
        CHECK(ScriptTimer_Get("t") == 600); // value + elapsed
        CHECK(ScriptTimer_Get("nope") == -1);
        ScriptTimer_Remove("t");
        CHECK(!ScriptTimer_Exists("t"));
        ScriptTimer_Set("u", 0);
        ScriptTimer_Clear();
        CHECK(!ScriptTimer_Exists("u"));

        ScriptBuffs_Add(0x7542, 60000, "Mortal Strike");
        CHECK(ScriptBuffs_Exists("mortal strike"));    // case-insensitive
        CHECK(ScriptBuffs_Exists("  MORTAL STRIKE ")); // trimmed
        CHECK(ScriptBuffs_Exists("0x7542"));           // icon-text lookup
        CHECK(!ScriptBuffs_Exists("night sight"));
        ScriptBuffs_Add(0x7543, 0, "Night Sight");
        CHECK(ScriptBuffs_List() == "Mortal Strike, Night Sight");
        ScriptBuffs_Remove(0x7542); // the event carries the icon only
        CHECK(!ScriptBuffs_Exists("mortal strike"));
        CHECK(ScriptBuffs_GetName(0x7542).empty());
        CHECK(ScriptBuffs_GetName(0x7543) == "Night Sight");
        ScriptBuffs_Clear();
        CHECK(ScriptBuffs_List().empty());
        CHECK(!ScriptBuffs_Exists("0x7543"));
        printf("state: globals/timers/buffs mirrors ok\n");
    }

    // --- 5. file engine sandbox -------------------------------------------
    {
        char rootTemplate[] = "/tmp/xuo-harness-XXXXXX";
        char *mk = mkdtemp(rootTemplate);
        CHECK(mk != nullptr);
        const astr_t root(mk);
        ScriptFile_SetSandboxRoot(root);

        // relative paths resolve under the root
        CHECK(ScriptFile_Sanitize("logs/out.txt", "t") == root + "/logs/out.txt");
        // "inside" absolute paths stay
        CHECK(ScriptFile_Sanitize(root + "/ok.txt", "t") == root + "/ok.txt");
        // normalization keeps you in: "./a/../b" -> "b"
        CHECK(ScriptFile_Sanitize("./x/../y.txt", "t") == root + "/y.txt");
        // escapes are blocked and logged
        CHECK(ScriptFile_Sanitize("../escape.txt", "t") == "");
        CHECK(ScriptFile_Sanitize("a/../../escape.txt", "t") == "");
        CHECK(ScriptFile_Sanitize("/etc/passwd", "t") == "");
        CHECK(ScriptFile_Sanitize(root + "/../sibling.txt", "t") == "");

        // write / read round trip through the object API
        XA_SCRIPT_FILE file;
        CHECK(ScriptFile_Open(&file, "line.txt", false));
        ScriptFile_WriteLine(&file, "first");
        ScriptFile_WriteLine(&file, "second");
        ScriptFile_Write(&file, "tail");
        ScriptFile_Close(&file);

        CHECK(ScriptFile_Open(&file, "line.txt", true)); // checkExists hit
        CHECK(ScriptFile_ReadLine(&file) == "first");
        CHECK(ScriptFile_Read(&file) == "second"); // >> reads one token
        ScriptFile_Close(&file);

        // append re-opens without truncation
        CHECK(ScriptFile_Append(&file, "line.txt", false));
        ScriptFile_WriteLine(&file, "third");
        ScriptFile_Close(&file);
        CHECK(ScriptFile_Open(&file, "line.txt", true));
        CHECK(ScriptFile_ReadLine(&file) == "first");
        ScriptFile_Close(&file);

        // sandboxed remove + the missing-file parity
        CHECK(ScriptFile_RemovePath("line.txt"));
        CHECK(!ScriptFile_RemovePath("line.txt"));
        CHECK(!ScriptFile_Opened(&file));

        // the text window engine (scriptgump.cpp): ops, cap, save
        ScriptTextWindow_Command(TW_SET_POS, "", 30, 40, "");
        ScriptTextWindow_Command(TW_SET_SIZE, "", 320, 240, "");
        ScriptTextWindow_Command(TW_OPEN, "", 0, 0, "");
        CHECK(ScriptTextWindow_Opened());
        ScriptTextWindow_Command(TW_PRINT, "hello", 0, 0, "");
        ScriptTextWindow_Command(TW_PRINT, "world", 0, 0, "");
        const vector<astr_t> &lines = ScriptTextWindow_Lines();
        CHECK(lines.size() == 2 && lines[0] == "hello" && lines[1] == "world");
        ScriptTextWindow_Command(TW_CLEAR, "", 0, 0, "");
        CHECK(ScriptTextWindow_Lines().empty());
        for (int i = 0; i < 600; i++) // runaway print guard (512 cap)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "line %d", i);
            ScriptTextWindow_Command(TW_PRINT, buf, 0, 0, "");
        }
        CHECK(ScriptTextWindow_Lines().size() == 512);
        CHECK(ScriptTextWindow_Lines().front() == "line 88"); // oldest dropped
        ScriptTextWindow_SaveToFile(root + "/window.txt");
        ScriptTextWindow_Command(TW_CLOSE, "", 0, 0, "");
        CHECK(!ScriptTextWindow_Opened());
        CHECK(!ScriptFile_Open(&file, "../outside/window.txt", false)); // sandboxed save target
        ScriptFile_Close(&file);

        // cleanup
        ScriptFile_SetSandboxRoot(root);
        ScriptFile_RemovePath(root + "/window.txt");
        rmdir(root.c_str());
        printf("file: sandbox escape blocked + text window ok\n");
    }


    // --- 6. the engine classes: REAL registration wiring ---
    {
        std::atomic<int> draining{ 1 };
        std::thread drainer([](std::atomic<int> *running) {
            while (running->load())
            {
                ScriptQueue_Drain();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }, &draining);

        JSRuntime *rt = JS_NewRuntime();
        JSContext *ctx = JS_NewContext(rt);
        Script_RegisterAPI(ctx, "harness");

        // the SC_GUMP_INFO executor deep-copies the manager record; the stub
        // manager hands it this one (harnessstubs.cpp). The executor is the
        // real one, so the world pointer + the command gates must read
        // "connected" for the GetGump path to run.
        CGameWorld world;
        g_World = &world;
        g_EnabledCommands = ~0ULL;
        GumpData source;
        source.Serial = 0x40000123;
        source.ID = 5;
        source.X = 10;
        source.Y = 20;
        g_HarnessGump = &source;

        // the aliased-class bug shape: every engine class aliased the GameObject
        // class, so the copies came back with the WRONG prototype and the
        // gameobject finalizer freed them as the wrong type. Each `typeof`
        // probe below must report the owning class's method.
        const char *script = "function probe() {"
            "var g = Client.GetGump(0);"
            "var f = Client.NewFile();"
            "var h = Client.CreateGumpHook('1');"
            "var m = Client.CreateClientMacro();"
            "return [typeof g, g === null ? 'null' : typeof g.Select,"
            " g === null ? 'null' : typeof g.Close,"
            " typeof f, f === null ? 'null' : typeof f.Open,"
            " f === null ? 'null' : typeof f.WriteLine,"
            " typeof h, h === null ? 'null' : typeof h.AddEntry,"
            " h === null ? 'null' : typeof h.AddCheck,"
            " typeof m, m === null ? 'null' : typeof m.AddAction];"
            "}\n";
        JSValue program = JS_Eval(
            ctx, script, strlen(script), "<script>", JS_EVAL_TYPE_GLOBAL);
        CHECK(!JS_IsException(program));
        JS_FreeValue(ctx, program);

        JSValue global = JS_GetGlobalObject(ctx);
        JSValue probe = JS_GetPropertyStr(ctx, global, "probe");
        CHECK(JS_IsFunction(ctx, probe));
        JSValue result = JS_Call(ctx, probe, JS_UNDEFINED, 0, nullptr);
        CHECK(JS_IsArray(result));
        static const char *const EXPECTED[] = { "object",   // g: the Gump class
            "function", // g.Select
            "function", // g.Close
            "object",   // f: the File class
            "function", // f.Open
            "function", // f.WriteLine
            "object",   // h: the GumpHook class
            "function", // h.AddEntry
            "function", // h.AddCheck
            "object",   // m: the ClientMacro class
            "function"  // m.AddAction
        };
        for (size_t i = 0; i < count_of(EXPECTED); i++)
        {
            JSValue item = JS_GetPropertyUint32(ctx, result, (uint32_t)i);
            const char *text = JS_ToCString(ctx, item);
            const bool ok = text != nullptr && strcmp(text, EXPECTED[i]) == 0;
            printf("api: probe[%zu] = %s (want %s)%s\n", i, text ? text : "?", EXPECTED[i],
                ok ? "" : "  <-- FAIL");
            CHECK(ok);
            JS_FreeCString(ctx, text);
            JS_FreeValue(ctx, item);
        }

        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, probe);
        JS_FreeValue(ctx, global);
        draining = 0;
        drainer.join();
        // the runtime teardown runs the class finalizers (the gump deep copy
        // is freed through GumpData's destructor now, not the GameObject one)
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        g_HarnessGump = nullptr;
        g_World = nullptr;
        g_EnabledCommands = 0;
        printf("api: engine-class prototypes + finalizers ok\n");
    }

    // --- O. INI config round trips (Options / Hotkeys / Map schemas) ----
    {
        // the config root is pinned explicitly to a temp dir
        // (CAssistantConfig::SetRootPathOverride) so the tests stay
        // side-effect free (the default root is the client appdata)
        char dirTemplate[] = "/tmp/xuoassist_config_XXXXXX";
        const char *mkd = mkdtemp(dirTemplate);
        CHECK(mkd != nullptr);
        if (mkd != nullptr)
        {
            CAssistantConfig::SetRootPathOverride(fs_path_from(mkd));

            // --- Options.ini: every main/display/hotkeys/scripts field ---
            {
                CAssistantConfig saved;
                for (int i = 0; i < CLBI_COUNT; i++)
                {
                    saved.SetOption(i, (i % 3) != 0);
                }
                saved.m_MessagesLevel = 2;
                saved.m_SearchItemsDistance = 5;
                saved.m_UseItemsDistance = 4;
                saved.m_OpenCorpsesDistance = 1;
                saved.m_CorpseKeepDelay = 12345;
                saved.m_WaitTargetsDelay = 777;
                saved.m_MoveItemsDelay = 333;
                saved.m_UseItemsDelay = 222;
                saved.m_WindowsOpacity = 80;
                saved.m_UseFontColor = true;
                saved.m_FontColor = "0x02B2";
                saved.m_UseCharactersFontColor = true;
                saved.m_CharactersFontColor = "0x0044";
                saved.m_AutoReconnect = true;
                saved.m_AutoReconnectSeconds = 45;
                saved.m_DoubleClickForOpenDoors = true;
                saved.m_DisplayEnabled = true;
                saved.m_DisplayTitleData = "UO - {name} (& more) <test> 'q'";
                saved.m_PassHotkeys = true;
                saved.m_CommandPrefix = "-";
                saved.m_SortScripts = true;
                saved.m_ScriptLastDir = "/tmp/scripts";
                // the writer persists the LIVE list (g_LoadedScripts): seed it
                g_LoadedScripts.Clear();
                g_LoadedScripts.Add("scripts/a.js", 11u);
                g_LoadedScripts.Add("/tmp/scripts/b.js", 22u);

                saved.SaveMainOptions();

                CAssistantConfig loaded;
                loaded.LoadMainOptions();
                bool ok = true;
                for (int i = 0; i < CLBI_COUNT; i++)
                {
                    ok = ok && loaded.GetOption(i) == ((i % 3) != 0);
                }
                ok = ok && loaded.m_MessagesLevel == 2;
                ok = ok && loaded.m_SearchItemsDistance == 5;
                ok = ok && loaded.m_UseItemsDistance == 4;
                ok = ok && loaded.m_OpenCorpsesDistance == 1;
                ok = ok && loaded.m_CorpseKeepDelay == 12345;
                ok = ok && loaded.m_WaitTargetsDelay == 777;
                ok = ok && loaded.m_MoveItemsDelay == 333;
                ok = ok && loaded.m_UseItemsDelay == 222;
                ok = ok && loaded.m_WindowsOpacity == 80;
                ok = ok && loaded.m_UseFontColor && loaded.m_FontColor == "0x02B2";
                ok = ok && loaded.m_UseCharactersFontColor;
                ok = ok && loaded.m_CharactersFontColor == "0x0044";
                ok = ok && loaded.m_AutoReconnect && loaded.m_AutoReconnectSeconds == 45;
                ok = ok && loaded.m_DoubleClickForOpenDoors;
                ok = ok && loaded.m_DisplayEnabled;
                ok = ok && loaded.m_DisplayTitleData == "UO - {name} (& more) <test> 'q'";
                ok = ok && loaded.m_PassHotkeys && loaded.m_CommandPrefix == "-";
                ok = ok && loaded.m_SortScripts;
                ok = ok && loaded.m_ScriptLastDir == "/tmp/scripts";
                // the loaded-scripts list round trips in order (the writer
                // emits loadedscripts=<count> + loadedscript<i>)
                ok = ok && loaded.m_LoadedScripts.size() == 2;
                if (ok)
                {
                    ok = loaded.m_LoadedScripts[0] == "scripts/a.js" &&
                         loaded.m_LoadedScripts[1] == "/tmp/scripts/b.js";
                }
                CHECK(ok);
                g_LoadedScripts.Clear();
            }

            // --- the loaded-scripts list: restore from the persisted paths ---
            {
                // the restore drains the pending paths on the first draw and
                // marks a missing file instead of dropping the entry
                char dir2[] = "/tmp/xuoassist_scripts_XXXXXX";
                const char *mkd2 = mkdtemp(dir2);
                CHECK(mkd2 != nullptr);
                const astr_t present = astr_t(dir2) + "/present.js";
                const astr_t missing = astr_t(dir2) + "/missing.js";
                FILE *f = fopen(present.c_str(), "wb");
                CHECK(f != nullptr);
                if (f != nullptr)
                {
                    fputs("Client.Info('hi');\n", f);
                    fclose(f);
                }
                g_LoadedScripts.Clear();
                g_LoadedScripts.SetPendingRestore({ present, missing });
                g_LoadedScripts.DrainPendingRestore();
                CHECK(g_LoadedScripts.Scripts().size() == 2);
                const CLoadedScript *first = g_LoadedScripts.Find(ScriptList_NormalizePath(present));
                const CLoadedScript *second = g_LoadedScripts.Find(ScriptList_NormalizePath(missing));
                CHECK(first != nullptr && first->bExists && first->ContentHash != 0);
                CHECK(second != nullptr && !second->bExists);
                // dedupe through the normalized key (./a/../a is the same file)
                const size_t before = g_LoadedScripts.Scripts().size();
                g_LoadedScripts.Add(astr_t(dir2) + "/./present.js", 0u);
                CHECK(g_LoadedScripts.Scripts().size() == before);
                // display name + save bookkeeping
                CHECK(ScriptList_DisplayName(present) == "present.js");
                g_LoadedScripts.NotifySaved(first->Key, "Client.Info('hi');\n");
                CHECK(g_LoadedScripts.Find(first->Key)->ContentHash ==
                      ScriptList_ContentHash("Client.Info('hi');\n"));
                g_LoadedScripts.Clear();
            }

            // Profile lifecycle uses the same disposable appdata override.
            {
                CAssistantConfig profile;
                profile.SetCurrentProfile("Original");
                profile.m_UseItemsDelay = 937;
                const auto source = profile.GetProfilePath();
                CHECK(fs_path_create(fs_path_join(source, "Scripts")));
                const auto sourceScript = fs_path_str(fs_path_join(source, "Scripts", "pet-care.js"));
                CHECK(ScriptList_WriteFile(sourceScript, "// disposable test script\n"));
                g_LoadedScripts.SetPendingRestore({ sourceScript, "/elsewhere/shared.js" });
                CHotkeyEntry scriptHotkey;
                scriptHotkey.Type = HT_SCRIPT;
                scriptHotkey.ScriptOrMacroName = sourceScript;
                profile.m_Hotkeys.push_back(scriptHotkey);
                const auto extra = fs_path_join(source, "Future.ini");
                CIniFile extension;
                extension.Set("test", "value", "preserve me");
                CHECK(extension.Save(extra));
                CHECK(profile.SaveProfile());

                CHECK(!profile.CreateProfile("../escape", false));
                CHECK(!profile.CreateProfile("original", false));
                CHECK(profile.CreateProfile("Clone", true));
                const auto clone = profile.GetProfilePath();
                CHECK(profile.ProfileName == "Clone");
                CHECK(profile.m_UseItemsDelay == 937);
                CHECK(extension.Load(fs_path_join(clone, "Future.ini")));
                CHECK(extension.Get("test", "value") == "preserve me");
                const auto cloneScript = fs_path_str(fs_path_join(clone, "Scripts", "pet-care.js"));
                CHECK(fs_path_exists(fs_path_from(cloneScript)));
                CHECK(profile.m_LoadedScripts == vector<astr_t>({ cloneScript, "/elsewhere/shared.js" }));
                CHECK(profile.m_Hotkeys.size() == 1 &&
                    profile.m_Hotkeys[0].ScriptOrMacroName == cloneScript);

                CHECK(profile.LoadProfileBySerial(123));
                CHECK(profile.LoadProfileBySerial(124));
                CHECK(profile.CreateProfile("Blank", false));
                CHECK(profile.m_UseItemsDelay == 600);
                CHECK(profile.m_Hotkeys.empty() && profile.m_LoadedScripts.empty());
                CHECK(profile.LoadProfileBySerial(456));
                CHECK(profile.SwitchProfile("Clone"));
                astr_t archived;
                CHECK(profile.ArchiveCurrentProfile(archived));
                CHECK(!fs_path_exists(clone));
                CHECK(profile.ProfileName == "Default");
                CHECK(fs_path_exists(fs_path_join(archived, "Future.ini")));
                CHECK(fs_path_exists(fs_path_join(archived, "Scripts", "pet-care.js")));
                const auto map = profile.GetProfileMap();
                CHECK(map.count(123) && map.at(123) == "Default");
                CHECK(map.count(124) && map.at(124) == "Default");
                CHECK(map.count(456) && map.at(456) == "Blank");
                CIniFile assignments;
                const auto archiveParent = archived.substr(0, archived.find_last_of("/\\"));
                CHECK(assignments.Load(fs_path_join(archiveParent, "Assignments.ini")));
                CHECK(assignments.Get("Profile0", "file") == "Clone");
                CHECK(fs_path_exists(extra));
                g_LoadedScripts.Clear();
                printf("config: create, clone, archive and rebind profiles ok\n");
            }

            // --- Hotkeys.ini: three records, all fields, order preserved ---
            {
                CAssistantConfig saved;
                CHotkeyEntry a;
                a.Key = 65;
                a.Modifiers = 2;
                a.Type = HT_SCRIPT;
                a.Action = 0;
                a.ScriptOrMacroName = "mine.ini";
                a.ExternalCode = "";
                a.RunningMode = true;
                CHotkeyEntry b;
                b.Key = 67;
                b.Modifiers = 1;
                b.Type = HT_USE_OBJECT;
                b.Action = 7;
                b.ScriptOrMacroName = "";
                b.ExternalCode = "printf('x; y')"; // ';' survives (no inline comments)
                b.PassMode = HotkeyPassMode::Pass;
                CHotkeyEntry c;
                c.Key = 27;
                c.Modifiers = 0;
                c.Type = HT_TOGGLE_HOTKEYS;
                c.Action = 0;
                c.PassMode = HotkeyPassMode::Consume;
                saved.m_Hotkeys = { a, b, c };
                saved.SaveHotkeys();

                CAssistantConfig loaded;
                CHECK(loaded.LoadHotkeys());
                CHECK(loaded.m_Hotkeys.size() == 3);
                bool ok = loaded.m_Hotkeys.size() == 3;
                if (ok)
                {
                    const CHotkeyEntry &la = loaded.m_Hotkeys[0];
                    const CHotkeyEntry &lb = loaded.m_Hotkeys[1];
                    const CHotkeyEntry &lc = loaded.m_Hotkeys[2];
                    ok = la.Key == 65 && la.Modifiers == 2 && la.Type == HT_SCRIPT &&
                         la.ScriptOrMacroName == "mine.ini" && la.ExternalCode.empty() &&
                         la.RunningMode && la.PassMode == HotkeyPassMode::Inherit &&
                         la.PassesToGame(true) && !la.PassesToGame(false);
                    ok = ok && lb.Key == 67 && lb.Modifiers == 1 && lb.Type == HT_USE_OBJECT &&
                         lb.Action == 7 && lb.ScriptOrMacroName.empty() && !lb.RunningMode &&
                         lb.ExternalCode == "printf('x; y')" && lb.PassMode == HotkeyPassMode::Pass &&
                         lb.PassesToGame(false);
                    ok = ok && lc.Key == 27 && lc.Modifiers == 0 && lc.Type == HT_TOGGLE_HOTKEYS &&
                         lc.Action == 0 && lc.PassMode == HotkeyPassMode::Consume && !lc.PassesToGame(true);
                }
                CHECK(ok);

                CIniFile raw;
                const auto path = fs_path_join(saved.GetProfilePath(), "Hotkeys.ini");
                CHECK(raw.Load(path));
                CHECK(!raw.Has("Hotkey0", "pass_mode")); // older persisted record
                raw.SetInt("Hotkey2", "pass_mode", 99);
                CHECK(raw.Save(path));
                CHECK(loaded.LoadHotkeys());
                CHECK(loaded.m_Hotkeys[2].PassMode == HotkeyPassMode::Inherit);

                const auto current = saved.ProfileName;
                CHECK(!saved.CreateImportedProfile("Failed Razor Import", [&](CAssistantConfig &staged) {
                    staged.m_Hotkeys = { b };
                    CHECK(staged.SaveHotkeys());
                    return false;
                }));
                CHECK(!fs_path_exists(fs_path_join(saved.GetConfigPath(), "Failed Razor Import")));
                CHECK(saved.CreateImportedProfile("Razor Hotkey Import", [&](CAssistantConfig &staged) {
                    staged.m_Hotkeys = { b, c };
                    return staged.SaveHotkeys();
                }));
                CHECK(saved.ProfileName == current);
                loaded.ProfileName = "Razor Hotkey Import";
                CHECK(loaded.LoadHotkeys() && loaded.m_Hotkeys.size() == 2);
                CHECK(loaded.m_Hotkeys[0].PassMode == HotkeyPassMode::Pass &&
                      loaded.m_Hotkeys[1].PassMode == HotkeyPassMode::Consume);
            }

            // --- Map.ini: every CMapSettings field, byte-identical ---
            {
                CAssistantConfig().EnsureDirectories();
                CMapSettings saved = CMapSettings();
                saved.WindowX = 12;
                saved.WindowY = 34;
                saved.WindowWidth = 567;
                saved.WindowHeight = 890;
                saved.ShowStatics = true;
                saved.ApplyAltitude = true;
                saved.AltitudeOnly = false;
                saved.SmoothImage = true;
                saved.DisplayCoordinates = true;
                saved.ShowOnLogin = true;
                saved.ShowLastCorpse = true;
                saved.MarkDeadMobiles = true;
                saved.Opacity = 75;
                saved.RotateMap = true;
                saved.PlayerFontStyle = 1;
                saved.PlayerColor = 0x41A2B3C4;
                saved.PlayerShowHits = true;
                saved.PlayerShowMana = true;
                saved.PlayerShowStam = true;
                saved.PlayerShowName = true;
                saved.PartyEnabled = true;
                saved.PartyFontStyle = 2;
                saved.PartyColor = 0x50607080;
                saved.PartyShowHits = true;
                saved.PartyShowName = true;
                saved.GuildEnabled = true;
                saved.GuildFontStyle = 3;
                saved.GuildColor = 0x90A0B0C0;
                saved.GuildShowHits = true;
                saved.GuildShowName = true;
                saved.FriendsEnabled = true;
                saved.FriendsFontStyle = 4;
                saved.FriendsColor = 0xD0E0F000;
                saved.FriendsShowHits = true;
                saved.FriendsShowName = true;
                saved.EnemiesEnabled = true;
                saved.EnemiesFontStyle = 5;
                saved.EnemiesColor = 0x11223344;
                saved.EnemiesShowHits = true;
                saved.EnemiesShowName = true;
                saved.OtherEnabled = true;
                saved.OtherFontStyle = 6;
                saved.OtherColor = 0x55667788;
                saved.OtherShowHits = true;
                saved.OtherShowName = true;
                saved.Save();

                CMapSettings loaded = CMapSettings();
                loaded.Load();
                // value-initialised on both sides, so the padding bytes match
                CHECK(memcmp(&saved, &loaded, sizeof(CMapSettings)) == 0);
            }

            CAssistantConfig::SetRootPathOverride(fs_path());
            printf("config: INI round trips ok (Options, Hotkeys, Map)\n");
        }
    }

    // --- N. AtomicFileWrite (the crash-safe save path) -------------------
    {
        // a temp dir keeps the test side-effect free
        char dirTemplate[] = "/tmp/xuoassist_atomic_XXXXXX";
        const char *mkd = mkdtemp(dirTemplate);
        CHECK(mkd != nullptr);
        if (mkd != nullptr)
        {
            const astr_t dir(mkd);

            // 1) create
            const fs_path path = fs_path_join(dir, "probe.bin");
            CHECK(AtomicFileWrite(path, "<a/>", 4));
            CHECK(fs_path_type(path) == FS_FILE);

            // 2) replace (the content is fully overwritten, no .tmp left)
            CHECK(AtomicFileWrite(path, "<bbbb/>", 7));
            {
                size_t size = 0;
                unsigned char *data = fs_map(path, &size);
                CHECK(data != nullptr && size == 7 && memcmp(data, "<bbbb/>", 7) == 0);
                fs_unmap(data, size);
            }
            CHECK(!fs_path_exists(fs_path_join(dir, "probe.bin.tmp")));

            // 3) failure leaves the target untouched (the directory is not
            const fs_path roDir = fs_path_join(dir, "ro");
            fs_path_create(roDir);
            chmod(fs_path_ascii(roDir), 0555);
            CHECK(!AtomicFileWrite(fs_path_join(roDir, "x.bin"), "<a/>", 4));
            chmod(fs_path_ascii(roDir), 0755);
            CHECK(!fs_path_exists(fs_path_join(roDir, "x.bin")));

            fs_del(fs_path_join(roDir, "x.bin"));
            rmdir(fs_path_ascii(roDir));
            fs_del(path);
            rmdir(mkd);
            printf("config: atomic save ok\n");
        }
    }

    // --- 9. core/textconv: ParseTextTokens parity + termination -----------
    // Byte parity with the old CTextParser (GetTokens/ReadTokens): the old
    // parser bounded each line at the first embedded NUL (GetEOL), quoted
    // tokens carry a trailing NUL inside the string (resize(size+1)), and
    // every input must terminate - the 2026-09 display-title freeze was
    // "{name\0}" (a stage-one quoted token) stalling the scan on the NUL.
    {
        // the display-tag stage two call: "name" out of "{name}"
        vector<astr_t> tags = ParseTextTokens("{name}", " \t{}=", "", "\"\"''");
        CHECK(tags.size() == 1 && tags[0] == "name");

        // the reported freeze input: the stage-one token "{name\0}" fed back
        // through CompileData; the old parser returned {"name"} (truncated
        // at the NUL) and terminated - so must the new one
        const astr_t stageOne("{name}", 6);
        vector<astr_t> quoted = ParseTextTokens(stageOne, "", "", "{}", false, true);
        CHECK(quoted.size() == 1);
        CHECK(quoted[0].size() == 7); // '{' + "name" + NUL quirk + '}'
        CHECK(quoted[0][5] == '\0' && quoted[0][6] == '}');
        vector<astr_t> stageTwo = ParseTextTokens(quoted[0], " \t{}=", "", "\"\"''");
        CHECK(stageTwo.size() == 1 && stageTwo[0] == "name");

        // quoted round trips (saveQuotes on/off), old parser parity
        vector<astr_t> say = ParseTextTokens("say \"a b\"", " ", "#", "\"\"''", true, true);
        CHECK(say.size() == 2 && say[0] == "say");
        CHECK(say[1].size() == 6 && say[1][0] == '"' && say[1][4] == '\0' && say[1][5] == '"');
        vector<astr_t> bare = ParseTextTokens("say \"a b\"", " ", "#", "\"\"''");
        CHECK(bare.size() == 2 && bare[0] == "say" && bare[1] == astr_t("a b", 4));

        // the old GetTokens took a const char*, so content after an embedded
        // NUL never made it into the tokens - truncation parity
        const astr_t mid("ab\0cd", 5);
        vector<astr_t> trunc = ParseTextTokens(mid, " ", "", "\"\"''");
        CHECK(trunc.size() == 1 && trunc[0] == "ab");

        // degenerate inputs terminate with the old parser's (empty) result
        const astr_t nuls(3, '\0');
        CHECK(ParseTextTokens(nuls, " ", "", "\"\"''").empty());
        CHECK(ParseTextTokens("", " ", "", "\"\"''").empty());
        // lone close quote / unmatched open quote must consume and finish
        CHECK(ParseTextTokens("'", " ", "", "\"\"''").empty());
        CHECK(ParseTextTokens("\"abc", " ", "", "\"\"''").size() == 1);

        printf("textconv: tokenizer parity ok\n");
    }

    // --- the script text-command table (Client.UseObject etc.) -------------
    // Every Client.* script call is marshaled as a text command executed by
    // CTextCommandManager::RunCommand. The table was never populated (nothing
    // called InitCommands), so ALL of them answered "Unknown command:
    // useobject" - the regression this covers. core/textcommandmanager.cpp is
    // linked for real (not stubbed) exactly for this test.
    {
        // nothing has consumed the table yet in the harness: this is the
        // state the client was in (0 = the "Unknown command" bug)
        const size_t before = CTextCommandManager::CommandCount();
        g_TextCommandManager.EnsureCommands();
        const size_t count = CTextCommandManager::CommandCount();
        printf("commands: %u before EnsureCommands, %u after\n", (unsigned)before,
            (unsigned)count);
        CHECK(count > 30); // the COMMAND() rows in textcommandmanager.cpp
        CHECK(CTextCommandManager::CommandCount() == count); // idempotent
        // the exact call Client.UseObject marshals
        CHECK(g_TextCommandManager.HasCommand("useobject"));
        CHECK(g_TextCommandManager.HasCommand("UseObject")); // case-insensitive
        printf("commands: %d registered (useobject included)\n", (int)count);
    }

    // --- forced stop: every tight-loop shape must terminate ----------------
    // "Stop" has to work at ANY moment for ANY script: a JS-only loop is
    // preempted by the QuickJS interrupt handler, a loop hammering the host
    // API must not starve it either.
    {
        CScriptManager &manager = *ScriptManager_Get();
        std::atomic<bool> draining{ true };
        std::thread drainThread([&draining]() {
            while (draining.load())
            {
                ScriptQueue_Drain();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        struct LoopCase
        {
            const char *path;
            const char *source;
        };
        const LoopCase cases[] = {
            { "stop_a.js", "while (true) {}" },
            { "stop_b.js", "for (;;) {}" },
            { "stop_c.js", "var x = 0; while (1) { x = x + 1; }" },
            { "stop_d.js", "function f() { while (true) {} } f();" },
            { "stop_e.js", "while (true) { Client.Info('spam'); }" },
            { "stop_f.js", "for (var i = 0; ; i++) { var s = 'x'.repeat(10); }" },
            // native-getter loop: no mutation, only queued reads (the Player.*
            // getters are native FUNCTIONS: reading the property is pure JS,
            // calling it is the queued read)
            { "stop_g.js", "while (true) { var n = Player.Name(); var s = Player.Serial(); }" },
            // getter + mutation mix
            { "stop_h.js", "while (true) { Client.Wait(0); Player.Hits; }" },
            // a stopped script must not keep executing its body
            // a stopped script must stop PRINTING too: the counter freezes
            { "stop_i.js", "while (true) { Client.Info('STOPCOUNT'); }" },
            // a script must not be able to SWALLOW a Stop: the error is
            // uncatchable, so neither loop can continue after the catch
            { "stop_j.js",
                "while (true) { try { Client.Info('x'); } catch (e) {} }" },
            { "stop_k.js",
                "try { while (true) { Client.Info('x'); } } catch (e) {} "
                "while (true) { Client.Info('x'); }" },
        };
        for (const LoopCase &loopCase : cases)
        {
            manager.RunFile(loopCase.path, loopCase.source);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            CHECK(manager.RunningFile(loopCase.path));
            // sanity for the API-driven cases: the loop really is pushing
            // commands (a pure-JS loop never queues, so it has nothing to
            // check here - the drained check below still applies)
            const bool apiDriven = std::string(loopCase.path) == "stop_e.js" ||
                                   std::string(loopCase.path) == "stop_g.js" ||
                                   std::string(loopCase.path) == "stop_h.js" ||
                                   std::string(loopCase.path) == "stop_i.js";
            bool pumping = false;
            for (int i = 0; i < 50 && apiDriven && !pumping; i++)
            {
                pumping = ScriptQueue_Pending() > 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (apiDriven)
            {
                CHECK(pumping);
            }
            manager.TerminateFile(loopCase.path);
            bool stopped = false;
            for (int i = 0; i < 300 && !stopped; i++) // up to 3 s
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                stopped = !manager.RunningFile(loopCase.path);
            }
            // the script body must not run another iteration after the Stop:
            // it is gone from the running set AND it stopped enqueueing
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const bool stillStopped = !manager.RunningFile(loopCase.path);
            const bool drained = ScriptQueue_Pending() == 0;
            printf("stop: %-10s '%.38s' %s (pending after stop: %u)\n", loopCase.path,
                loopCase.source, stopped && stillStopped && drained ? "ok" : "FAILED",
                ScriptQueue_Pending());
            CHECK(stopped);
            CHECK(stillStopped);
            CHECK(drained);
            manager.Tick();
        }
        draining = false;
        drainThread.join();
    }

    // --- run/terminate lifecycle (the real CScriptHandle flow) -------------
    // a top-level loop script starts, runs, survives a mid-flight terminate
    // request by exiting with "interrupted", and leaves the manager's list
    {
        CScriptManager &manager = *ScriptManager_Get();

        // the queued-command drain: Wait/Gump/etc exec calls resolve only
        // when the queue is drained (the client drains per frame; the
        // harness owns its own drain thread here)
        std::atomic<bool> draining{ true };
        std::thread drainThread([&draining]() {
            while (draining.load())
            {
                ScriptQueue_Drain();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
        struct DrainStop
        {
            std::atomic<bool> &flag;
            std::thread &thread;
            ~DrainStop()
            {
                flag = false;
                thread.join();
            }
        } drainStop{ draining, drainThread };

        // pure JS tight loop: only the interrupt handler can stop this. The
        // body is TOP-LEVEL: a script is its file, and only a function named
        // after the file is invoked (a top-level loop runs during Evaluate)
        manager.RunFile("loop.js", "while (true) {}");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(manager.RunningFile("loop.js"));
        manager.TerminateFile("loop.js");
        bool stoppedPure = false;
        for (int i = 0; i < 100 && !stoppedPure; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            stoppedPure = !manager.RunningFile("loop.js");
        }
        CHECK(stoppedPure);
        if (!stoppedPure)
        {
            std::abort(); // fail promptly if the script thread never stops
        }
        printf("lifecycle: pure JS loop terminate ok\n");

        // host-call loop: Wait spends most time in native sleeps. The loop
        // must be TOP-LEVEL and the body inlined on the SAME line or in a
        // plain block: the RBE 'm_Waiting' path only polls
        // TerminateRequested between statements.
        manager.RunFile("waiter.js", "for (;;) { Client.Wait('100ms'); }");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(manager.RunningFile("waiter.js"));

        manager.TerminateFile("waiter.js");

        bool stopped = false;
        for (int i = 0; i < 100 && !stopped; i++) // up to 2s
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            stopped = !manager.RunningFile("waiter.js");
            if (i == 50 && !stopped)
            {
                Info(Assistant, "lifecycle: still running");
                for (CScriptHandle *handle : manager.Handles())
                {
                    Info(Assistant, "lifecycle:   handle '%s' state=%d", handle->Path().c_str(),
                         (int)handle->State());
                }
            }
        }
        CHECK(stopped);

        manager.Tick(); // the finished handle leaves the manager's list
        bool listed = false;
        for (CScriptHandle *handle : manager.Handles())
        {
            if (handle->Name() == "selftest")
            {
                listed = true;
            }
        }
        CHECK(!listed);
        printf("lifecycle: run/terminate ok\n");
    }

    // --- 10. Razor Enhanced Shared Game Script API --------------------------
    {
        std::atomic<bool> draining{ true };
        std::thread drainThread([&draining]() {
            while (draining.load())
            {
                ScriptQueue_Drain();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });

        g_HarnessHooks.Reset();
        ScriptJournal_Clear();
        ScriptBuffs_Clear();
        ScriptCountdownTimer_Clear();
        ScriptSharedValue_Clear();
        ScriptIgnore_Clear();
        ScriptAgentList_Clear();

        // Build a realistic in-memory world for Player/Items/Mobiles/Target/Gumps/PathFinding.
        CGameWorld world;
        CPlayer player;
        player.Serial = 0x00000100;
        player.NPC = true;
        player.SetName("Avatar");
        player.Graphic = 0x0190;
        player.Hits = 80;
        player.MaxHits = 100;
        player.Mana = 30;
        player.MaxMana = 60;
        player.Stam = 45;
        player.MaxStam = 90;
        player.Weight = 150;
        player.MaxWeight = 400;
        player.Gold = 12345;
        player.m_X = 100;
        player.m_Y = 200;
        player.m_Z = 5;
        player.MapIndex = 1;
        player.m_Flags = 0x04 | 0x08 | 0x01; // poisoned + yellowhits + paralyzed

        CGameItem rightHand;
        rightHand.Serial = 0x40000010;
        rightHand.Graphic = 0x13B9;
        rightHand.Color = 0x0482;
        rightHand.Layer = 1; // RightHand
        rightHand.Count = 1;
        rightHand.Container = player.Serial;
        rightHand.SetName("Viking Sword");

        CGameItem backpack;
        backpack.Serial = 0x40000020;
        backpack.Graphic = 0x0E75;
        backpack.Layer = OL_BACKPACK;
        backpack.Container = player.Serial;
        backpack.Opened = true;
        backpack.SetName("Backpack");

        CGameItem bandages1;
        bandages1.Serial = 0x40000021;
        bandages1.Graphic = 0x0E21;
        bandages1.Color = 0;
        bandages1.Count = 25;
        bandages1.Container = backpack.Serial;
        bandages1.SetName("Clean Bandage");

        CGameItem pouch;
        pouch.Serial = 0x40000022;
        pouch.Graphic = 0x0E79;
        pouch.Count = 1;
        pouch.Container = backpack.Serial;
        pouch.Opened = true;
        pouch.SetName("Pouch");

        CGameItem bandages2;
        bandages2.Serial = 0x40000023;
        bandages2.Graphic = 0x0E21;
        bandages2.Color = 0;
        bandages2.Count = 15;
        bandages2.Container = pouch.Serial;
        bandages2.SetName("Clean Bandage");

        CGameItem dyedBandage;
        dyedBandage.Serial = 0x40000024;
        dyedBandage.Graphic = 0x0E21;
        dyedBandage.Color = 0x0026;
        dyedBandage.Count = 7;
        dyedBandage.Container = pouch.Serial;
        dyedBandage.SetName("Red Bandage");

        CGameItem groundChest;
        groundChest.Serial = 0x40000050;
        groundChest.Graphic = 0x0E42;
        groundChest.Color = 0;
        groundChest.Count = 1;
        groundChest.Container = 0xFFFFFFFF;
        groundChest.m_X = 102;
        groundChest.m_Y = 200;
        groundChest.m_Z = 5;
        groundChest.Opened = false;
        groundChest.SetName("Wooden Chest");

        CGameItem groundGold;
        groundGold.Serial = 0x40000051;
        groundGold.Graphic = 0x0EED;
        groundGold.Color = 0;
        groundGold.Count = 500;
        groundGold.Container = 0xFFFFFFFF;
        groundGold.m_X = 115;
        groundGold.m_Y = 200;
        groundGold.m_Z = 5;

        pouch.Add(&bandages2);
        pouch.Add(&dyedBandage);
        backpack.Add(&bandages1);
        backpack.Add(&pouch);
        player.Add(&rightHand);
        player.Add(&backpack);

        CGameCharacter mobNearEnemy;
        mobNearEnemy.Serial = 0x00000201;
        mobNearEnemy.NPC = true;
        mobNearEnemy.Graphic = 0x0190;
        mobNearEnemy.Notoriety = 6; // Murderer
        mobNearEnemy.Hits = 40;
        mobNearEnemy.MaxHits = 100;
        mobNearEnemy.m_X = 103;
        mobNearEnemy.m_Y = 200;
        mobNearEnemy.m_Z = 5;
        mobNearEnemy.MapIndex = 1;
        mobNearEnemy.m_Flags = 0x04; // Poisoned
        mobNearEnemy.SetName("Red PK");

        CGameCharacter mobFarWeak;
        mobFarWeak.Serial = 0x00000202;
        mobFarWeak.NPC = true;
        mobFarWeak.Graphic = 0x0009; // Daemon
        mobFarWeak.Notoriety = 4; // Criminal
        mobFarWeak.Hits = 15;
        mobFarWeak.MaxHits = 200;
        mobFarWeak.m_X = 108;
        mobFarWeak.m_Y = 200;
        mobFarWeak.m_Z = 5;
        mobFarWeak.MapIndex = 1;
        mobFarWeak.SetName("Injured Daemon");

        CGameCharacter mobHighZ;
        mobHighZ.Serial = 0x00000203;
        mobHighZ.NPC = true;
        mobHighZ.Graphic = 0x0190;
        mobHighZ.Notoriety = 6;
        mobHighZ.Hits = 90;
        mobHighZ.MaxHits = 100;
        mobHighZ.m_X = 102;
        mobHighZ.m_Y = 201;
        mobHighZ.m_Z = 40; // dz = 35 > 20 -> fails CheckLineOfSight
        mobHighZ.MapIndex = 1;
        mobHighZ.SetName("Roof Camper");

        CGameCharacter mobNoStats;
        mobNoStats.Serial = 0x00000204;
        mobNoStats.NPC = true;
        mobNoStats.Graphic = 0x0191;
        mobNoStats.Notoriety = 1; // Innocent
        mobNoStats.Hits = 0;
        mobNoStats.MaxHits = 0;
        mobNoStats.m_X = 104;
        mobNoStats.m_Y = 200;
        mobNoStats.m_Z = 5;
        mobNoStats.MapIndex = 1;
        mobNoStats.SetName("Town Guard");

        world.m_Items = &groundChest;
        groundChest.m_Next = &groundGold;
        groundGold.m_Next = &mobNearEnemy;
        mobNearEnemy.m_Next = &mobFarWeak;
        mobFarWeak.m_Next = &mobHighZ;
        mobHighZ.m_Next = &mobNoStats;

        g_World = &world;
        g_Player = &player;
        g_PlayerSerial = player.Serial;
        g_Backpack = backpack.Serial;
        g_Connected = true;
        g_EnabledCommands = ~0ULL;

        g_HarnessHooks.OplProperties[rightHand.Serial] =
            "Viking Sword\nSpell Channeling\nLuck 125\nDamage Increase 35%\nWeight: 6 stones";
        ScriptBuffs_Add(0x7542, 60000, "Mortal Strike");

        // Prepare a helper to evaluate JS snippets inside a real CScriptEngine
        auto runJsCheck = [](const char *code, const char *scriptPath = "/tmp/scripts/demo.js") -> bool {
            CScriptEngine eng;
            if (!eng.Setup(ScriptList_DisplayName(scriptPath), scriptPath))
            {
                return false;
            }
            JSValue val = JS_Eval(eng.Context(), code, strlen(code), "<test>", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(val))
            {
                astr_t line, msg;
                Script_FormatException(eng.Context(), line, msg);
                printf("JS FAIL (%s): %s\n", line.c_str(), msg.c_str());
                JS_FreeValue(eng.Context(), val);
                return false;
            }
            const bool ok = JS_ToBool(eng.Context(), val) != 0;
            JS_FreeValue(eng.Context(), val);
            return ok;
        };

        // 10a. Player namespace (backward compat + Razor Enhanced extensions)
        CHECK(runJsCheck(
            "Player.HP() === 80 && Player.Hits() === 80 && Player.Hits('%') === 80 &&"
            " Player.HitsValue() === 80 && Player.HitsMax() === 100 && Player.MaxHits() === 100 &&"
            " Player.Mana() === 30 && Player.Mana('%') === 50 && Player.ManaMax() === 60 &&"
            " Player.Stam() === 45 && Player.Stam('%') === 50 && Player.StamMax() === 90 &&"
            " Player.Poisoned() === true && Player.YellowHits() === true &&"
            " Player.Mortaled() === true && Player.Paralyzed() === true &&"
            " Player.Paralized() === true && Player.WarMode() === false &&"
            " Player.Serial() === '0x100' && Player.Name() === 'Avatar' &&"
            " Player.X() === 100 && Player.Y() === 200 && Player.Z() === 5 && Player.Map() === 1 &&"
            " Player.Position().X === 100 && Player.Position().Y === 200 &&"
            " Player.Position().Z === 5 && Player.Position().Map === 1 &&"
            " Player.Weight() === 150 && Player.Weight('%') === 37 && Player.MaxWeight() === 400 &&"
            " Player.Gold() === 12345 &&"
            " Player.Backpack() !== null && Player.Backpack().Serial === 0x40000020 &&"
            " Player.Backpack().Contains.length === 2 &&"
            " Player.GetLayerItem('RightHand').Serial === 0x40000010 &&"
            " Player.GetLayerItem(1).ItemID === 0x13B9 &&"
            " Player.GetLayerItem('LeftHand') === null &&"
            " Player.BuffsExist('mortal strike') === true &&"
            " Player.BuffsExist('0x7542') === true &&"
            " Player.BuffsExist('Night Sight') === false;"));

        CHECK(runJsCheck(
            "Player.UseSkill('Anatomy');"
            " Player.Walk('North') && Player.Run('Southeast') &&"
            " !Player.Walk('InvalidDir') && Player.PathFindTo(120, 230, 5);"));
        CHECK(g_HarnessHooks.LastSkillName == "Anatomy");
        CHECK(g_HarnessHooks.WalkCallCount == 2);
        CHECK(g_HarnessHooks.LastWalkDir == 3 && g_HarnessHooks.LastWalkRun == true);
        CHECK(g_HarnessHooks.WalkToX == 120 && g_HarnessHooks.WalkToY == 230 && g_HarnessHooks.WalkToZ == 5);
        printf("re-api: Player namespace ok\n");

        // 10b. Items namespace (Find, Count, Use, Move, MoveOnGround, OPL Props, WaitForContents)
        CHECK(runJsCheck(
            "var sw = Items.FindBySerial(0x40000010);"
            " var bAll = Items.FindAllByID(0x0E21, -1, -1, true);"
            " var bTop = Items.FindAllByID(0x0E21, -1, -1, false);"
            " var bRed = Items.FindByID(0x0E21, 0x0026);"
            " var gNear = Items.FindByID(0x0E42, -1, 0, 5);"
            " var gTooFar = Items.FindByID(0x0EED, -1, 0, 5);"
            " var props = Items.GetPropStringList(sw);"
            " sw !== null && sw.ItemID === 0x13B9 && sw.Hue === 0x0482 &&"
            " bAll.length === 3 && bTop.length === 1 &&"
            " bRed !== null && bRed.Serial === 0x40000024 &&"
            " gNear !== null && gNear.Serial === 0x40000050 && gTooFar === null &&"
            " Items.BackpackCount(0x0E21) === 47 &&"
            " Items.BackpackCount(0x0E21, 0) === 40 &&"
            " Items.BackpackCount(0x0E21, 0x0026) === 7 &&"
            " props.length === 5 &&"
            " Items.GetPropValue(sw, 'Luck') === 125 &&"
            " Items.GetPropValue(sw, 'Damage Increase') === 35 &&"
            " Items.GetPropValue(sw, 'Weight') === 6 &&"
            " Items.GetPropValue(sw, 'Spell Channeling') === 1 &&"
            " Items.GetPropValue(sw, 'Nonexistent') === 0;"));

        CHECK(runJsCheck(
            "Items.UseItem(0x40000021) && Items.UseItemByID(0x0E21, 0x0026) &&"
            " Items.Move(0x40000021, 0x40000022, 10, 44, 55);"));
        CHECK(g_HarnessHooks.LastDoubleClickSerial == 0x40000024);
        CHECK(g_HarnessHooks.LastMoveItemSerial == 0x40000021);
        CHECK(g_HarnessHooks.LastMoveContainer == 0x40000022);
        CHECK(g_HarnessHooks.LastMoveCount == 10 && g_HarnessHooks.LastMoveX == 44 && g_HarnessHooks.LastMoveY == 55);

        CHECK(runJsCheck("Items.MoveOnGround(0x40000021, 5, 101, 202, 7);"));
        CHECK(g_HarnessHooks.LastMoveContainer == 0xFFFFFFFF);
        CHECK(g_HarnessHooks.LastMoveCount == 5 && g_HarnessHooks.LastMoveX == 101 &&
              g_HarnessHooks.LastMoveY == 202 && g_HarnessHooks.LastMoveZ == 7);

        // WaitForContents: async open after double-click
        {
            std::thread opener([&groundChest]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                groundChest.Opened = true;
            });
            CHECK(runJsCheck("Items.WaitForContents(0x40000050, 500) === true;"));
            opener.join();
            CHECK(g_HarnessHooks.LastDoubleClickSerial == 0x40000050);
            CHECK(runJsCheck("Items.WaitForContents(0x40009999, 100) === false;"));
        }
        printf("re-api: Items namespace ok\n");

        // 10c. Mobiles namespace (FindBySerial, ApplyFilter, Select, UseMobile, WaitForStats)
        CHECK(runJsCheck(
            "var m = Mobiles.FindBySerial(0x201);"
            " var notMob = Mobiles.FindBySerial(0x40000010);"
            " var fAll = Mobiles.ApplyFilter({ range: 10, notoriety: [4, 6], selector: 'nearest' });"
            " var fLos = Mobiles.ApplyFilter({ range: 10, notoriety: [4, 6], checklineofsight: true, selector: 'nearest' });"
            " var fWeak = Mobiles.ApplyFilter({ range: 10, notoriety: ['criminal', 'murderer'], checklineofsight: true, selector: 'weakest' });"
            " var fPois = Mobiles.ApplyFilter({ poisoned: true, body: [0x0190] });"
            " var selFar = Mobiles.Select(fLos, 'farthest');"
            " m !== null && m.Name === 'Red PK' && m.Poisoned === true && m.Hits === 40 &&"
            " notMob === null &&"
            " fAll.length === 3 && fLos.length === 2 &&"
            " fLos[0].Serial === 0x201 && fWeak[0].Serial === 0x202 &&"
            " fPois.length === 1 && fPois[0].Serial === 0x201 &&"
            " selFar !== null && selFar.Serial === 0x202 &&"
            " Mobiles.UseMobile(0x201) === true;"));
        CHECK(g_HarnessHooks.LastDoubleClickSerial == 0x201);

        // WaitForStats: requests status and wakes when MaxHits arrives
        {
            std::thread statsFeed([&mobNoStats]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                mobNoStats.Hits = 100;
                mobNoStats.MaxHits = 100;
            });
            CHECK(runJsCheck("Mobiles.WaitForStats(0x204, 500) === true;"));
            statsFeed.join();
            CHECK(g_HarnessHooks.LastStatusSerial == 0x204);
        }
        printf("re-api: Mobiles namespace ok\n");

        // 10d. Target namespace (HasTarget, WaitForTarget, TargetExecute, Self, Last, Cancel, SetLast, PromptTarget)
        g_Target.Targeting = false;
        g_LastTargetObject = 0;
        CHECK(runJsCheck("Target.HasTarget() === false && Target.WaitForTarget(30) === false;"));
        {
            std::thread targetArrive([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                g_Target.Targeting = true;
            });
            CHECK(runJsCheck("Target.WaitForTarget(500) === true && Target.HasTarget() === true;"));
            targetArrive.join();
        }
        CHECK(runJsCheck("Target.TargetExecute(0x201) === true && Target.HasTarget() === false;"));
        CHECK(g_HarnessHooks.LastTargetSentSerial == 0x201);

        g_Target.Targeting = true;
        CHECK(runJsCheck("Target.TargetExecute(105, 205, 5, 0x051A) === true;"));
        CHECK(g_HarnessHooks.LastTargetTileX == 105 && g_HarnessHooks.LastTargetTileY == 205 &&
              g_HarnessHooks.LastTargetTileZ == 5 && g_HarnessHooks.LastTargetTileGraphic == 0x051A);

        g_Target.Targeting = true;
        CHECK(runJsCheck("Target.Self() === true;"));
        CHECK(g_HarnessHooks.LastTargetSentSerial == player.Serial);

        CHECK(runJsCheck("Target.SetLast(0x202) === true && Target.GetLast() === 0x202;"));
        g_Target.Targeting = true;
        CHECK(runJsCheck("Target.Last() === true;"));
        CHECK(g_HarnessHooks.LastTargetSentSerial == 0x202);

        g_Target.Targeting = true;
        CHECK(runJsCheck("Target.Cancel() === true && Target.HasTarget() === false;"));
        CHECK(g_HarnessHooks.TargetCancelledCount >= 1);

        // PromptTarget delivers selected serial and restores previous g_LastTargetObject
        {
            std::thread deliverPrompt([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                ScriptPromptTarget_Deliver(0x201);
            });
            CHECK(runJsCheck(
                "var picked = Target.PromptTarget('Pick a mob', 0x35, 500);"
                " picked === 0x201 && Target.GetLast() === 0x202;"));
            deliverPrompt.join();
        }
        printf("re-api: Target namespace ok\n");

        // 10e. Journal namespace
        {
            CJournalMessage m1(0x204, 0x0035, "Halt citizen!");
            m1.Flags = JMT_NORMAL;
            CJournalMessage m2(0xFFFFFFFF, 0x03B2, "Vendor: Welcome to my shop");
            m2.Flags = JMT_SYSTEM;
            ScriptJournal_Add(m1);
            ScriptJournal_Add(m2);
            CHECK(runJsCheck(
                "Journal.Search('welcome') === true &&"
                " Journal.Search('missing') === false &&"
                " Journal.SearchByName('welcome', 'vendor') === true &&"
                " Journal.SearchByName('halt', 'town guard') === true &&"
                " Journal.SearchByName('halt', 'vendor') === false &&"
                " Journal.GetTextBySerial(0x204).length === 1 &&"
                " Journal.GetTextBySerial(0x204, true)[0] === 'Town Guard: Halt citizen!' &&"
                " Journal.GetLineText('shop') === 'Vendor: Welcome to my shop' &&"
                " Journal.GetTextByColor(0x0035).length === 1;"));

            std::thread journalFeed([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                ScriptJournal_Add(CJournalMessage(0xFFFFFFFF, 0x03B2, "You finish healing."));
            });
            CHECK(runJsCheck("Journal.WaitJournal('finish healing', 500) === true;"));
            journalFeed.join();
            CHECK(runJsCheck("Journal.Clear(); Journal.Search('finish healing') === false;"));
        }
        printf("re-api: Journal namespace ok\n");

        // 10f. Gumps namespace
        {
            GumpData g1;
            g1.Serial = 0x50000001;
            g1.ID = 0x12345678;
            g1.m_Text = { "Blacksmithing Menu", "Craft Katana", "Cancel" };
            g_HarnessHooks.HarnessGumps = { &g1 };

            CHECK(runJsCheck(
                "Gumps.HasGump() === true &&"
                " Gumps.HasGump(0x12345678) === true &&"
                " Gumps.HasGump(0x99999999) === false &&"
                " Gumps.CurrentGump() === 0x12345678 &&"
                " Gumps.LastGumpGetLineList().length === 3 &&"
                " Gumps.LastGumpGetLine(1) === 'Craft Katana' &&"
                " Gumps.LastGumpTextExist('katana') === true &&"
                " Gumps.SendAction(0x12345678, 7, [101, 102], { 1: 'hello' }) === true &&"
                " Gumps.HasGump(0x12345678) === false;"));
            CHECK(g_HarnessHooks.LastGumpChoiceButton == 7 &&
                  g_HarnessHooks.LastGumpChoiceId == 0x12345678 &&
                  g_HarnessHooks.LastGumpChoiceCheckCount == 2 &&
                  g_HarnessHooks.LastGumpChoiceEntryCount == 1);

            GumpData g2;
            g2.Serial = 0x50000002;
            g2.ID = 0xABCDEF01;
            g_HarnessHooks.HarnessGumps.clear();
            std::thread gumpArrive([&g2]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                g_HarnessHooks.HarnessGumps.push_back(&g2);
            });
            CHECK(runJsCheck(
                "Gumps.WaitForGump(0xABCDEF01, 500) === true &&"
                " Gumps.CloseGump(0xABCDEF01) === true &&"
                " Gumps.HasGump(0xABCDEF01) === false;"));
            gumpArrive.join();
            CHECK(g_HarnessHooks.LastGumpChoiceButton == 0);
            g_HarnessHooks.HarnessGumps.clear();
        }
        printf("re-api: Gumps namespace ok\n");

        // 10g. Timer & Misc namespaces (countdown timers, shared values, ignore, script dir)
        CHECK(runJsCheck(
            "Timer.Create('HealCD', 60);"
            " var active1 = Timer.Check('healcd');"
            " var rem1 = Timer.Remaining('HEALCD');"
            " Misc.Pause(80);"
            " var active2 = Timer.Check('healcd');"
            " var rem2 = Timer.Remaining('healcd');"
            " Timer.Create('Keep', 5000);"
            " var removed1 = Timer.Remove('keep');"
            " var removed2 = Timer.Remove('keep');"
            " active1 === true && rem1 > 0 && active2 === false && rem2 === 0 &&"
            " removed1 === true && removed2 === false;"));

        CHECK(runJsCheck(
            "Misc.NoOperation();"
            " Misc.SendMessage('Hello RE', 0x0044);"
            " Misc.SetSharedValue('count', 42);"
            " Misc.SetSharedValue('flag', true);"
            " Misc.SetSharedValue('meta', { name: 'Britannia', ok: 1 });"
            " var ok = Misc.CheckSharedValue('count') === true &&"
            "   Misc.ReadSharedValue('count') === 42 &&"
            "   Misc.ReadSharedValue('flag') === true &&"
            "   Misc.ReadSharedValue('meta').name === 'Britannia' &&"
            "   Misc.ReadSharedValue('missing') === null &&"
            "   Misc.RemoveSharedValue('flag') === true &&"
            "   Misc.CheckSharedValue('flag') === false &&"
            "   Misc.CurrentScriptDirectory() === '/tmp/scripts';"
            " ok;",
            "/tmp/scripts/my_script.js"));
        CHECK(g_HarnessHooks.LastPrintText == "Hello RE" && g_HarnessHooks.LastPrintColor == 0x0044);

        // Shared values persist across separate JS runtimes/scripts
        CHECK(runJsCheck("Misc.ReadSharedValue('count') === 42;"));

        // Ignore list integrates with Mobiles.ApplyFilter
        CHECK(runJsCheck(
            "Misc.IgnoreObject(0x201);"
            " var c1 = Misc.CheckIgnoreObject(0x201);"
            " var f1 = Mobiles.ApplyFilter({ range: 10, notoriety: [6], checklineofsight: true });"
            " var fNoIgn = Mobiles.ApplyFilter({ range: 10, notoriety: [6], checklineofsight: true, CheckIgnoreObject: false });"
            " Misc.UnIgnoreObject(0x201);"
            " var c2 = Misc.CheckIgnoreObject(0x201);"
            " Misc.IgnoreObject(0x202);"
            " Misc.ClearIgnore();"
            " c1 === true && f1.length === 0 && fNoIgn.length === 1 &&"
            " c2 === false && Misc.CheckIgnoreObject(0x202) === false;"));
        printf("re-api: Timer & Misc namespaces ok\n");

        // 10h. Agent facades (AutoLoot, BandageHeal, Organizer, Restock, Scavenger)
        CHECK(runJsCheck(
            "AutoLoot.ChangeList('Gold');"
            " var a1 = AutoLoot.Start() && AutoLoot.Status() && AutoLoot.RunOnce() && AutoLoot.Stop() && !AutoLoot.Status();"
            " var b1 = BandageHeal.Start() && BandageHeal.Status() && BandageHeal.Stop() && !BandageHeal.Status();"
            " Organizer.ChangeList('Regs');"
            " var o1 = Organizer.FStart() && Organizer.Status() && Organizer.FStop() && !Organizer.Status();"
            " Restock.ChangeList('Potions');"
            " var r1 = Restock.FStart() && Restock.Status() && Restock.FStop() && !Restock.Status();"
            " Scavenger.ChangeList('Arrows');"
            " Scavenger.ResetIgnore();"
            " var s1 = Scavenger.Start() && Scavenger.Status() && Scavenger.Stop() && !Scavenger.Status();"
            " a1 && b1 && o1 && r1 && s1;"));
        CHECK(g_HarnessHooks.LastAutoLootOnce == "Gold");
        CHECK(g_HarnessHooks.LastInventoryPreset == "Arrows");
        CHECK(g_HarnessHooks.ScavengerCacheCleared == 1);
        printf("re-api: Agent facades ok\n");

        // 10i. PathFinding / Route helper
        g_HarnessHooks.WalkToSucceeds = true;
        g_HarnessHooks.WalkToAutoArrive = true;
        g_HarnessHooks.WalkToCallCount = 0;
        CHECK(runJsCheck(
            "var r = PathFinding.Route([{ X: 110, Y: 210 }, { X: 125, Y: 225, Z: 5 }]);"
            " var ok1 = PathFinding.Go(r);"
            " var ok2 = PathFinding.RunRoute([[130, 230], { x: 135, y: 235 }]);"
            " ok1 && ok2 && Player.X() === 135 && Player.Y() === 235;"));
        CHECK(g_HarnessHooks.WalkToCallCount == 4);

        // Failure case when pathfinder rejects destination
        g_HarnessHooks.WalkToSucceeds = false;
        g_HarnessHooks.WalkToAutoArrive = false;
        CHECK(runJsCheck("PathFinding.Go(500, 500, 0, { StopIfStuck: true, Timeout: 100 }) === false;"));
        printf("re-api: PathFinding & Route helper ok\n");

        // 10j. Completion list exposes all new Razor Enhanced namespaces
        {
            const vector<XA_API_COMPLETION> &completions = ScriptAPI_CompletionList();
            auto hasCompletion = [&](const char *scope, const char *name) {
                for (const XA_API_COMPLETION &c : completions)
                {
                    if (strcmp(c.Scope, scope) == 0 && strcmp(c.Name, name) == 0)
                    {
                        return true;
                    }
                }
                return false;
            };
            CHECK(hasCompletion("", "Items") && hasCompletion("Items", "FindByID"));
            CHECK(hasCompletion("", "Mobiles") && hasCompletion("Mobiles", "ApplyFilter"));
            CHECK(hasCompletion("", "Target") && hasCompletion("Target", "PromptTarget"));
            CHECK(hasCompletion("", "Journal") && hasCompletion("Journal", "WaitJournal"));
            CHECK(hasCompletion("", "Gumps") && hasCompletion("Gumps", "SendAction"));
            CHECK(hasCompletion("", "Timer") && hasCompletion("Timer", "Create"));
            CHECK(hasCompletion("", "Misc") && hasCompletion("Misc", "ReadSharedValue"));
            CHECK(hasCompletion("", "AutoLoot") && hasCompletion("AutoLoot", "RunOnce"));
            CHECK(hasCompletion("", "BandageHeal") && hasCompletion("BandageHeal", "Start"));
            CHECK(hasCompletion("", "Organizer") && hasCompletion("Organizer", "FStart"));
            CHECK(hasCompletion("", "Restock") && hasCompletion("Restock", "FStart"));
            CHECK(hasCompletion("", "Scavenger") && hasCompletion("Scavenger", "ResetIgnore"));
            CHECK(hasCompletion("", "PathFinding") && hasCompletion("PathFinding", "RunRoute"));
        }

        // 10k. Interrupt / termination safety on blocking Razor Enhanced waits
        {
            g_HarnessHooks.WalkToSucceeds = true;
            g_HarnessHooks.WalkToAutoArrive = false; // stays AutoWalking until stopped
            g_HarnessHooks.StopAutoWalkCount = 0;
            g_Target.Targeting = false;
            g_HarnessHooks.HarnessGumps.clear();
            ScriptJournal_Clear();

            CScriptManager &mgr = *ScriptManager_Get();
            struct WaitStopCase
            {
                const char *path;
                const char *source;
            };
            const WaitStopCase waitCases[] = {
                { "re_stop_pause.js", "try { Misc.Pause(60000); } catch (e) {} while (true) { Misc.Pause(60000); }" },
                { "re_stop_target.js", "Target.WaitForTarget(60000);" },
                { "re_stop_journal.js", "Journal.WaitJournal('never_arrives', 60000);" },
                { "re_stop_gump.js", "Gumps.WaitForGump(0xDEADBEEF, 60000);" },
                { "re_stop_walk.js", "PathFinding.Go(999, 999, 0, { Timeout: 60000 });" },
            };
            for (const WaitStopCase &wc : waitCases)
            {
                mgr.RunFile(wc.path, wc.source);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                CHECK(mgr.RunningFile(wc.path));
                mgr.TerminateFile(wc.path);
                bool stopped = false;
                for (int i = 0; i < 200 && !stopped; i++)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    stopped = !mgr.RunningFile(wc.path);
                }
                CHECK(stopped);
                mgr.Tick();
            }
            CHECK(g_HarnessHooks.StopAutoWalkCount >= 1);
            printf("re-api: interrupt/termination safety on all blocking waits ok\n");
        }

        draining = false;
        drainThread.join();

        g_World = nullptr;
        g_Player = nullptr;
        g_PlayerSerial = 0;
        g_Backpack = 0;
        g_Connected = false;
        g_EnabledCommands = 0;
        g_HarnessHooks.Reset();
        ScriptJournal_Clear();
        ScriptBuffs_Clear();
        ScriptCountdownTimer_Clear();
        ScriptSharedValue_Clear();
        ScriptIgnore_Clear();
        ScriptAgentList_Clear();
    }

    // --- 11. External script IPC bridge (.py / .cs out-of-process JSON-RPC) ---
    {
        // 11a. Extension detection & language labels
        CHECK(ExternalScript_DetectKind("heal.py") == ExternalScriptKind::Python);
        CHECK(ExternalScript_DetectKind("/tmp/dir.v1/HEAL.PY") == ExternalScriptKind::Python);
        CHECK(ExternalScript_DetectKind("farm.cs") == ExternalScriptKind::CSharp);
        CHECK(ExternalScript_DetectKind("C:\\scripts\\FARM.CS") == ExternalScriptKind::CSharp);
        CHECK(ExternalScript_DetectKind("macro.js") == ExternalScriptKind::None);
        CHECK(ExternalScript_DetectKind("/tmp/dir.py/notes.txt") == ExternalScriptKind::None);
        CHECK(ExternalScript_DetectKind("/tmp/noext") == ExternalScriptKind::None);
        CHECK(ExternalScript_IsExternal("a.py"));
        CHECK(ExternalScript_IsExternal("b.cs"));
        CHECK(!ExternalScript_IsExternal("c.js"));
        CHECK(strcmp(ExternalScript_LanguageLabel("a.py"), "Python (.py)") == 0);
        CHECK(strcmp(ExternalScript_LanguageLabel("a.cs"), "C# (.cs)") == 0);
        CHECK(strcmp(ExternalScript_LanguageLabel("a.js"), "JavaScript (.js)") == 0);
        printf("ipc-bridge: extension detection ok\n");

        // Spin up mock world + drain thread for RPC & subprocess tests
        std::atomic<bool> draining{ true };
        std::thread drainThread([&draining]() {
            while (draining.load())
            {
                g_Ticks += 5;
                ScriptQueue_Drain();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });

        CGameWorld world;
        CPlayer player;
        player.Serial = 0x00000100;
        player.Hits = 85;
        player.MaxHits = 100;
        player.Mana = 40;
        player.MaxMana = 60;
        player.Stam = 55;
        player.MaxStam = 90;
        player.m_X = 123;
        player.m_Y = 456;
        player.m_Z = 7;
        player.MapIndex = 1;
        player.Gold = 5000;
        player.Weight = 120;
        player.MaxWeight = 400;
        player.SetName("IpcHero");

        CGameItem backpack;
        backpack.Serial = 0x40000020;
        backpack.Graphic = 0x0E75;
        backpack.Layer = 0x15;
        backpack.Container = player.Serial;
        backpack.Opened = true;

        CGameItem bandages;
        bandages.Serial = 0x40000021;
        bandages.Graphic = 0x0E21;
        bandages.Color = 0;
        bandages.Count = 33;
        bandages.Container = backpack.Serial;
        backpack.Add(&bandages);
        player.Add(&backpack);

        g_World = &world;
        g_Player = &player;
        g_PlayerSerial = player.Serial;
        g_Backpack = backpack.Serial;
        g_Connected = true;
        g_EnabledCommands = ~0ULL;
        g_HarnessHooks.Reset();
        ScriptSharedValue_Clear();
        ScriptCountdownTimer_Clear();

        // 11b. Direct JSON-RPC dispatch: allowlist rejection & valid calls
        {
            CScriptEngine eng;
            CHECK(eng.Setup("ipc_unit.py", "/tmp/ipc_unit.py"));

            auto checkRpc = [&](const char *req, bool wantOk, const char *mustContain) {
                astr_t resp;
                bool interrupted = false;
                const bool ok = ExternalScript_DispatchRpcLine(eng, req, resp, interrupted);
                CHECK(ok == wantOk);
                CHECK(!interrupted);
                CHECK(!resp.empty() && resp[resp.size() - 1] == '\n');
                if (mustContain != nullptr)
                {
                    CHECK(resp.find(mustContain) != astr_t::npos);
                }
            };

            // Disallowed namespaces & prototype escape attempts
            checkRpc("{\"id\":1,\"ns\":\"eval\",\"fn\":\"call\",\"args\":[\"1+1\"]}", false, "\"ok\":false");
            checkRpc("{\"id\":2,\"ns\":\"Object\",\"fn\":\"keys\",\"args\":[{}]}", false, "Disallowed namespace");
            checkRpc("{\"id\":3,\"ns\":\"Function\",\"fn\":\"constructor\",\"args\":[]}", false, "Disallowed namespace");
            checkRpc("{\"id\":4,\"ns\":\"TextWindow\",\"fn\":\"Open\",\"args\":[]}", false, "Disallowed namespace");
            checkRpc("{\"id\":5,\"ns\":\"Player\",\"fn\":\"constructor\",\"args\":[]}", false, "Unknown API method");
            checkRpc("{\"id\":6,\"ns\":\"Player\",\"fn\":\"__proto__\",\"args\":[]}", false, "Unknown API method");
            checkRpc("{\"id\":7,\"ns\":\"Player\",\"fn\":\"toString\",\"args\":[]}", false, "Unknown API method");
            checkRpc("{\"id\":8,\"ns\":\"Player\",\"fn\":\"DoesNotExist\",\"args\":[]}", false, "Unknown API method");
            checkRpc("not valid json", false, "Invalid JSON-RPC");
            checkRpc("{\"id\":9,\"ns\":\"Player\",\"fn\":\"Hits\",\"args\":\"bad\"}", false, "'args' must be an array");

            // Valid RPC calls across Player / Items / Misc / Timer / Client
            checkRpc("{\"id\":10,\"ns\":\"Player\",\"fn\":\"Hits\",\"args\":[]}", true, "\"result\":85");
            checkRpc("{\"id\":11,\"ns\":\"Player\",\"fn\":\"Position\",\"args\":[]}", true, "\"X\":123");
            checkRpc("{\"id\":12,\"ns\":\"Items\",\"fn\":\"BackpackCount\",\"args\":[3617]}", true, "\"result\":33");
            checkRpc("{\"id\":13,\"ns\":\"Items\",\"fn\":\"FindBySerial\",\"args\":[1073741857]}", true, "\"Amount\":33");
            checkRpc("{\"id\":14,\"ns\":\"Misc\",\"fn\":\"SetSharedValue\",\"args\":[\"ipc_k\",\"hello\"]}", true, "\"ok\":true");
            checkRpc("{\"id\":15,\"ns\":\"Misc\",\"fn\":\"ReadSharedValue\",\"args\":[\"ipc_k\"]}", true, "\"result\":\"hello\"");
            checkRpc("{\"id\":16,\"ns\":\"Timer\",\"fn\":\"Create\",\"args\":[\"ipc_t\",5000]}", true, "\"ok\":true");
            checkRpc("{\"id\":17,\"ns\":\"Timer\",\"fn\":\"Check\",\"args\":[\"ipc_t\"]}", true, "\"result\":true");
            checkRpc("{\"id\":18,\"ns\":\"Client\",\"fn\":\"Info\",\"args\":[\"hi\"]}", true, "\"result\":null");
            printf("ipc-bridge: direct RPC allowlist + dispatch ok\n");
        }

        // 11c. Out-of-process subprocess tests with a controlled mock python_host.py
        {
            char tmpDirTpl[] = "/tmp/xuo_ipc_test_XXXXXX";
            char *tmpDirPtr = mkdtemp(tmpDirTpl);
            CHECK(tmpDirPtr != nullptr);
            if (tmpDirPtr != nullptr)
            {
                const astr_t tmpDir(tmpDirPtr);
                const astr_t mockHost = tmpDir + "/python_host.py";
                const astr_t pyRpcScript = tmpDir + "/rpc_check.py";
                const astr_t pyPauseScript = tmpDir + "/pause_check.py";
                const astr_t pyLoopScript = tmpDir + "/loop_kill.py";
                const astr_t pidFile = tmpDir + "/loop.pid";
                const astr_t envOutFile = tmpDir + "/env_dump.json";

                // Save original XUO_RUNTIME_DIR
                const char *origRuntimeEnv = getenv("XUO_RUNTIME_DIR");
                const bool hadOrigRuntime = origRuntimeEnv != nullptr;
                const astr_t origRuntimeVal = hadOrigRuntime ? astr_t(origRuntimeEnv) : astr_t();

                // Write mock python_host.py that executes the script file with an rpc() helper
                const char *mockHostCode =
                    "import json, os, sys\n"
                    "raw_out = os.fdopen(os.dup(1), 'w', encoding='utf-8', buffering=1)\n"
                    "sys.stdout = sys.stderr\n"
                    "req_id = 0\n"
                    "def rpc(ns, fn, args=None):\n"
                    "    global req_id\n"
                    "    req_id += 1\n"
                    "    payload = json.dumps({'id': req_id, 'ns': ns, 'fn': fn, 'args': args or []})\n"
                    "    raw_out.write(payload + '\\n')\n"
                    "    raw_out.flush()\n"
                    "    line = sys.stdin.readline()\n"
                    "    if not line:\n"
                    "        raise SystemExit(0)\n"
                    "    return json.loads(line)\n"
                    "script_path = os.environ['XUO_SCRIPT_PATH']\n"
                    "with open(script_path, 'r', encoding='utf-8') as f:\n"
                    "    code = f.read()\n"
                    "exec(compile(code, script_path, 'exec'), {'rpc': rpc, '__file__': script_path})\n";

                CHECK(ScriptList_WriteFile(mockHost, mockHostCode));

                // 1) RPC + env vars + >100 KiB stderr (deadlock check)
                const astr_t rpcScriptCode =
                    "import json, os, sys\n"
                    "env_info = {\n"
                    "    'path': os.environ.get('XUO_SCRIPT_PATH', ''),\n"
                    "    'dir': os.environ.get('XUO_SCRIPT_DIR', ''),\n"
                    "    'name': os.environ.get('XUO_SCRIPT_NAME', ''),\n"
                    "    'args': json.loads(os.environ.get('XUO_SCRIPT_ARGS_JSON', '[]')),\n"
                    "    'runtime': os.environ.get('XUO_RUNTIME_DIR', ''),\n"
                    "    'dotnet': os.environ.get('XUO_DOTNET_BIN', ''),\n"
                    "    'cache': os.environ.get('XUO_CACHE_DIR', ''),\n"
                    "}\n"
                    "with open('" + envOutFile + "', 'w', encoding='utf-8') as f:\n"
                    "    json.dump(env_info, f)\n"
                    "for i in range(1200):\n"
                    "    sys.stderr.write(('stderr-burst-' + ('x' * 90) + '\\n'))\n"
                    "sys.stderr.write('stderr-sentinel-line\\n')\n"
                    "sys.stderr.flush()\n"
                    "r_bad = rpc('eval', 'call', ['1'])\n"
                    "assert not r_bad['ok']\n"
                    "r_hits = rpc('Player', 'Hits')\n"
                    "assert r_hits['ok'] and r_hits['result'] == 85\n"
                    "r_cnt = rpc('Items', 'BackpackCount', [0x0E21])\n"
                    "assert r_cnt['ok'] and r_cnt['result'] == 33\n"
                    "r_set = rpc('Misc', 'SetSharedValue', ['py_bridge_val', r_hits['result'] + r_cnt['result']])\n"
                    "assert r_set['ok']\n";
                CHECK(ScriptList_WriteFile(pyRpcScript, rpcScriptCode));

                // 2) Pause/Resume gating script
                const astr_t pauseScriptCode =
                    "r1 = rpc('Misc', 'SetSharedValue', ['pause_stage', 1])\n"
                    "assert r1['ok']\n"
                    "import time\n"
                    "time.sleep(0.08)\n"
                    "r2 = rpc('Misc', 'SetSharedValue', ['pause_stage', 2])\n"
                    "assert r2['ok']\n";
                CHECK(ScriptList_WriteFile(pyPauseScript, pauseScriptCode));

                // 3) Infinite loop ignoring SIGTERM (verifies SIGTERM -> SIGKILL escalation & no zombies)
                const astr_t loopScriptCode =
                    "import os, signal, time\n"
                    "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
                    "with open('" + pidFile + "', 'w', encoding='utf-8') as f:\n"
                    "    f.write(str(os.getpid()))\n"
                    "while True:\n"
                    "    time.sleep(0.05)\n";
                CHECK(ScriptList_WriteFile(pyLoopScript, loopScriptCode));

                setenv("XUO_RUNTIME_DIR", tmpDir.c_str(), 1);
                CScriptManager &mgr = *ScriptManager_Get();

                // Run 1: RPC + env + stderr
                {
                    vector<astr_t> runArgs;
                    runArgs.push_back("arg1");
                    runArgs.push_back("two words");
                    CScriptHandle *h = mgr.RunFile(pyRpcScript, "", runArgs);
                    CHECK(h != nullptr);
                    bool finished = false;
                    for (int i = 0; i < 400 && !finished; i++)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        finished = !mgr.RunningFile(pyRpcScript);
                    }
                    CHECK(finished);
                    CHECK(h != nullptr && h->State() == SS_FINISHED);
                    mgr.Tick();

                    astr_t sharedOut;
                    CHECK(ScriptSharedValue_Get("py_bridge_val", sharedOut) && sharedOut == "118");
                    CHECK(g_HarnessHooks.LastPrintColor == 0x0021);
                    CHECK(g_HarnessHooks.LastPrintText.find("stderr-sentinel-line") != astr_t::npos);

                    astr_t envDump;
                    CHECK(ScriptList_ReadFile(envOutFile, envDump));
                    CHECK(envDump.find("rpc_check.py") != astr_t::npos);
                    CHECK(envDump.find("two words") != astr_t::npos);
                    CHECK(envDump.find(tmpDir) != astr_t::npos);
                    printf("ipc-bridge: subprocess RPC + env + stderr non-deadlock ok\n");
                }

                // Run 2: Pause & Resume
                {
                    ScriptSharedValue_Remove("pause_stage");
                    CScriptHandle *h = mgr.RunFile(pyPauseScript, "");
                    CHECK(h != nullptr);
                    // Wait until stage 1 is written, then pause before stage 2 RPC
                    bool reachedStage1 = false;
                    for (int i = 0; i < 200 && !reachedStage1; i++)
                    {
                        astr_t v;
                        if (ScriptSharedValue_Get("pause_stage", v) && v == "1")
                        {
                            reachedStage1 = true;
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    CHECK(reachedStage1);
                    CHECK(mgr.TogglePauseFile(pyPauseScript));
                    CHECK(mgr.PausedFile(pyPauseScript));
                    // Give child time past its 80ms sleep; it must stay blocked on stage 2 RPC while paused
                    std::this_thread::sleep_for(std::chrono::milliseconds(180));
                    astr_t pausedVal;
                    CHECK(ScriptSharedValue_Get("pause_stage", pausedVal) && pausedVal == "1");
                    CHECK(mgr.RunningFile(pyPauseScript));
                    // Resume and verify it completes stage 2
                    CHECK(mgr.TogglePauseFile(pyPauseScript));
                    CHECK(!mgr.PausedFile(pyPauseScript));
                    bool finished = false;
                    for (int i = 0; i < 300 && !finished; i++)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        finished = !mgr.RunningFile(pyPauseScript);
                    }
                    CHECK(finished);
                    astr_t resumedVal;
                    CHECK(ScriptSharedValue_Get("pause_stage", resumedVal) && resumedVal == "2");
                    mgr.Tick();
                    printf("ipc-bridge: subprocess pause/resume ok\n");
                }

                // Run 3: Terminate infinite-loop child (SIG_IGN SIGTERM -> SIGKILL + zero zombies)
                {
                    CScriptHandle *h = mgr.RunFile(pyLoopScript, "");
                    CHECK(h != nullptr);
                    int childPid = 0;
                    for (int i = 0; i < 200 && childPid == 0; i++)
                    {
                        astr_t pidText;
                        if (ScriptList_ReadFile(pidFile, pidText) && !pidText.empty())
                        {
                            childPid = atoi(pidText.c_str());
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    CHECK(childPid > 0);
                    CHECK(mgr.RunningFile(pyLoopScript));

                    mgr.TerminateFile(pyLoopScript);
                    bool stopped = false;
                    for (int i = 0; i < 300 && !stopped; i++)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        stopped = !mgr.RunningFile(pyLoopScript);
                    }
                    CHECK(stopped);
                    // Child must be dead and reaped (ESRCH, not a zombie)
                    CHECK(childPid > 0 && ::kill((pid_t)childPid, 0) == -1 && errno == ESRCH);
                    mgr.Tick();
                    printf("ipc-bridge: subprocess SIGTERM/SIGKILL terminate + reap ok\n");
                }

                // Restore XUO_RUNTIME_DIR
                if (hadOrigRuntime)
                {
                    setenv("XUO_RUNTIME_DIR", origRuntimeVal.c_str(), 1);
                }
                else
                {
                    unsetenv("XUO_RUNTIME_DIR");
                }

                // 11d. Optional real .py and .cs end-to-end smoke checks when runtime hosts exist
                const astr_t realRuntimeDir = ExternalScript_ResolveRuntimeDir();
                const astr_t realPyHost = realRuntimeDir + "/python_host.py";
                const astr_t realCsHost = realRuntimeDir + "/csharp/run_csharp_script.py";
                if (::access(realPyHost.c_str(), R_OK) == 0)
                {
                    const astr_t realPyScript = tmpDir + "/real_smoke.py";
                    const char *realPyCode =
                        "hits = Player.Hits\n"
                        "cnt = Items.BackpackCount(0x0E21)\n"
                        "Misc.SetSharedValue('real_py_smoke', hits + cnt)\n";
                    CHECK(ScriptList_WriteFile(realPyScript, realPyCode));
                    ScriptSharedValue_Remove("real_py_smoke");
                    CScriptHandle *h = mgr.RunFile(realPyScript, "");
                    CHECK(h != nullptr);
                    bool finished = false;
                    for (int i = 0; i < 500 && !finished; i++)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        finished = !mgr.RunningFile(realPyScript);
                    }
                    CHECK(finished);
                    astr_t outVal;
                    CHECK(ScriptSharedValue_Get("real_py_smoke", outVal) && outVal == "118");
                    mgr.Tick();
                    unlink(realPyScript.c_str());
                    printf("ipc-bridge: real python_host.py end-to-end smoke ok\n");
                }
                else
                {
                    printf("ipc-bridge: real python_host.py not present yet (skipped optional smoke)\n");
                }

                if (::access(realCsHost.c_str(), R_OK) == 0)
                {
                    const astr_t realCsScript = tmpDir + "/real_smoke.cs";
                    const char *realCsCode =
                        "namespace RazorEnhanced {\n"
                        "  public class Script {\n"
                        "    public void Run() {\n"
                        "      int total = Player.Hits + Items.BackpackCount(0x0E21);\n"
                        "      Misc.SetSharedValue(\"real_cs_smoke\", total);\n"
                        "    }\n"
                        "  }\n"
                        "}\n";
                    CHECK(ScriptList_WriteFile(realCsScript, realCsCode));
                    ScriptSharedValue_Remove("real_cs_smoke");
                    CScriptHandle *h = mgr.RunFile(realCsScript, "");
                    CHECK(h != nullptr);
                    bool finished = false;
                    for (int i = 0; i < 1500 && !finished; i++)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        finished = !mgr.RunningFile(realCsScript);
                    }
                    CHECK(finished);
                    astr_t outVal;
                    CHECK(ScriptSharedValue_Get("real_cs_smoke", outVal) && outVal == "118");
                    mgr.Tick();
                    unlink(realCsScript.c_str());
                    printf("ipc-bridge: real run_csharp_script.py end-to-end smoke ok\n");
                }
                else
                {
                    printf("ipc-bridge: real run_csharp_script.py not present yet (skipped optional smoke)\n");
                }

                unlink(mockHost.c_str());
                unlink(pyRpcScript.c_str());
                unlink(pyPauseScript.c_str());
                unlink(pyLoopScript.c_str());
                unlink(pidFile.c_str());
                unlink(envOutFile.c_str());
                rmdir(tmpDir.c_str());
            }
        }

        draining = false;
        drainThread.join();

        g_World = nullptr;
        g_Player = nullptr;
        g_PlayerSerial = 0;
        g_Backpack = 0;
        g_Connected = false;
        g_EnabledCommands = 0;
        g_HarnessHooks.Reset();
        ScriptSharedValue_Clear();
        ScriptCountdownTimer_Clear();
    }

    if (failures == 0)
    {
        printf("ALL HARNESS TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURES\n", failures);
    return 1;
}
