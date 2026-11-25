// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

#include "externalruntime.h"
#include "apiscript.h"
#include "commandqueue.h"
#include "engine.h"
#include "scriptlist.h"

#define LOGGER_MODULE Assistant

#include <common/logging/logging.h>

extern "C" {
#include "quickjs/quickjs.h"
}

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#if (defined(__unix__) || defined(__APPLE__)) && !defined(XUO_EMSC)
#define XUO_EXTERNAL_RUNTIME_POSIX 1
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#else
extern char **environ;
#endif
#else
#define XUO_EXTERNAL_RUNTIME_POSIX 0
#endif

namespace xuoassist {

namespace {

constexpr size_t MAX_RPC_LINE_BYTES = 1024 * 1024; // 1 MiB hard cap per JSON-RPC line
constexpr size_t MAX_STDERR_LINE_BYTES = 64 * 1024;
constexpr uint32_t MAX_RPC_ARGS = 64;

// Strict allowlist of global namespaces reachable over external JSON-RPC.
static const char *const ALLOWED_NAMESPACES[] = {
    "Player",
    "Items",
    "Mobiles",
    "Target",
    "Journal",
    "Gumps",
    "Timer",
    "Misc",
    "AutoLoot",
    "BandageHeal",
    "Organizer",
    "Restock",
    "Scavenger",
    "PathFinding",
    "Client"
};

bool is_allowed_namespace(const astr_t &ns)
{
    for (size_t i = 0; i < sizeof(ALLOWED_NAMESPACES) / sizeof(ALLOWED_NAMESPACES[0]); i++)
    {
        if (ns == ALLOWED_NAMESPACES[i])
        {
            return true;
        }
    }
    return false;
}

// Marshals error text onto the main thread through SC_SAY (SAY_ERROR), matching
// engine.cpp so worker threads never touch client UI state directly.
void external_script_error(const astr_t &title, const astr_t &text)
{
    Warning(Assistant, "external script error: %s %s\n", title.c_str(), text.c_str());
    auto *command = new XA_SCRIPT_COMMAND();
    command->Tag = SC_SAY;
    command->SubTag = SAY_ERROR;
    command->TextB = title;
    command->TextC = text;
    ScriptQueue_ExecCleanup(command, 250);
    delete command;
}

astr_t get_env_str(const char *name)
{
    const char *val = getenv(name);
    return val != nullptr ? astr_t(val) : astr_t();
}

astr_t join_path(const astr_t &base, const astr_t &rel)
{
    if (base.empty())
    {
        return rel;
    }
    if (rel.empty())
    {
        return base;
    }
    const char last = base[base.size() - 1];
    if (last == '/' || last == '\\')
    {
        return base + rel;
    }
    return base + "/" + rel;
}

astr_t serialize_json_response(JSContext *ctx, JSValueConst resp)
{
    JSValue jsonVal = JS_JSONStringify(ctx, resp, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(jsonVal))
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return "{\"id\":null,\"ok\":false,\"error\":\"Failed to serialize JSON response\",\"interrupted\":false}\n";
    }
    size_t len = 0;
    const char *raw = JS_ToCStringLen(ctx, &len, jsonVal);
    astr_t out;
    if (raw != nullptr)
    {
        out.assign(raw, len);
        JS_FreeCString(ctx, raw);
    }
    else
    {
        out = "{\"id\":null,\"ok\":false,\"error\":\"Failed to encode JSON response\",\"interrupted\":false}";
    }
    JS_FreeValue(ctx, jsonVal);
    out.push_back('\n');
    return out;
}

astr_t make_error_response(JSContext *ctx, JSValueConst idVal, const astr_t &errorMsg, bool interrupted)
{
    JSValue resp = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, resp, "id", JS_DupValue(ctx, idVal));
    JS_SetPropertyStr(ctx, resp, "ok", JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, resp, "error", JS_NewStringLen(ctx, errorMsg.c_str(), errorMsg.size()));
    JS_SetPropertyStr(ctx, resp, "interrupted", JS_NewBool(ctx, interrupted ? 1 : 0));
    const astr_t out = serialize_json_response(ctx, resp);
    JS_FreeValue(ctx, resp);
    return out;
}

astr_t serialize_args_json(JSContext *ctx, const vector<astr_t> &args)
{
    if (ctx == nullptr)
    {
        return "[]";
    }
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < (uint32_t)args.size(); i++)
    {
        JS_SetPropertyUint32(ctx, arr, i, JS_NewStringLen(ctx, args[i].c_str(), args[i].size()));
    }
    JSValue jsonVal = JS_JSONStringify(ctx, arr, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(ctx, arr);
    if (JS_IsException(jsonVal))
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return "[]";
    }
    size_t len = 0;
    const char *raw = JS_ToCStringLen(ctx, &len, jsonVal);
    astr_t out = raw != nullptr ? astr_t(raw, len) : astr_t("[]");
    if (raw != nullptr)
    {
        JS_FreeCString(ctx, raw);
    }
    JS_FreeValue(ctx, jsonVal);
    return out;
}

#if XUO_EXTERNAL_RUNTIME_POSIX

bool path_is_dir(const astr_t &path)
{
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool path_is_readable_file(const astr_t &path)
{
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
        ::access(path.c_str(), R_OK) == 0;
}

bool path_is_executable_file(const astr_t &path)
{
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
        ::access(path.c_str(), X_OK) == 0;
}

bool runtime_dir_has_hosts(const astr_t &dir)
{
    return path_is_readable_file(join_path(dir, "python_host.py")) ||
        path_is_readable_file(join_path(dir, "csharp/run_csharp_script.py"));
}

astr_t get_executable_dir()
{
    char buf[4096];
    buf[0] = '\0';
#if defined(__APPLE__)
    uint32_t size = (uint32_t)sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
    {
        char resolved[4096];
        if (realpath(buf, resolved) != nullptr)
        {
            return ScriptList_DirName(resolved);
        }
        return ScriptList_DirName(buf);
    }
#else
    const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0)
    {
        buf[len] = '\0';
        return ScriptList_DirName(buf);
    }
#endif
    return astr_t();
}

astr_t get_current_working_dir()
{
    char buf[4096];
    if (::getcwd(buf, sizeof(buf)) != nullptr)
    {
        return astr_t(buf);
    }
    return astr_t();
}

astr_t resolve_script_full_path(const astr_t &scriptPath)
{
    if (scriptPath.empty())
    {
        return scriptPath;
    }
    if (scriptPath[0] == '/')
    {
        return ScriptList_NormalizePath(scriptPath);
    }
    const astr_t cwd = get_current_working_dir();
    if (!cwd.empty())
    {
        return ScriptList_NormalizePath(join_path(cwd, scriptPath));
    }
    return ScriptList_NormalizePath(scriptPath);
}

void set_cloexec(int fd)
{
    if (fd >= 0)
    {
        const int flags = ::fcntl(fd, F_GETFD);
        if (flags >= 0)
        {
            ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
    }
}

void set_nonblock(int fd)
{
    if (fd >= 0)
    {
        const int flags = ::fcntl(fd, F_GETFL);
        if (flags >= 0)
        {
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }
}

void ensure_sigpipe_ignored()
{
    static std::once_flag s_SigpipeOnce;
    std::call_once(s_SigpipeOnce, []() {
        ::signal(SIGPIPE, SIG_IGN);
    });
}

void send_group_signal(pid_t pgid, int sig)
{
    if (pgid <= 0)
    {
        return;
    }
    if (::kill(-pgid, sig) != 0 && errno == ESRCH)
    {
        // Fallback if the child has not completed setsid() yet.
        ::kill(pgid, sig);
    }
}

bool write_all_fd(int fd, const char *data, size_t len)
{
    size_t offset = 0;
    while (offset < len)
    {
        const ssize_t n = ::write(fd, data + offset, len - offset);
        if (n > 0)
        {
            offset += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
        {
            continue;
        }
        return false;
    }
    return true;
}

void flush_stderr_lines(
    const astr_t &scriptName,
    astr_t &stderrBuf,
    bool flushPartial,
    bool &sawStderr)
{
    constexpr size_t MAX_STDERR_BATCH_BYTES = 4096;
    astr_t batch;

    auto emitBatch = [&]() {
        if (!batch.empty())
        {
            sawStderr = true;
            external_script_error("Script error (" + scriptName + ")", batch);
            batch.clear();
        }
    };

    auto pushLine = [&](astr_t &line) {
        if (!line.empty() && line[line.size() - 1] == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            return;
        }
        if (!batch.empty() && batch.size() + 1 + line.size() > MAX_STDERR_BATCH_BYTES)
        {
            emitBatch();
        }
        if (!batch.empty())
        {
            batch.push_back('\n');
        }
        batch.append(line);
    };

    size_t nl = astr_t::npos;
    while ((nl = stderrBuf.find('\n')) != astr_t::npos)
    {
        astr_t line = stderrBuf.substr(0, nl);
        stderrBuf.erase(0, nl + 1);
        pushLine(line);
    }
    if (flushPartial && !stderrBuf.empty())
    {
        astr_t line = stderrBuf;
        stderrBuf.clear();
        pushLine(line);
    }
    emitBatch();
}

bool try_reap_child(pid_t pid, int &outStatus, ExternalScriptControl *control)
{
    if (control != nullptr)
    {
        std::lock_guard<std::mutex> lock(control->SignalMutex);
        const pid_t r = ::waitpid(pid, &outStatus, WNOHANG);
        if (r == pid || (r < 0 && errno == ECHILD))
        {
            control->ChildPgid.store(0);
            return true;
        }
        return false;
    }
    const pid_t r = ::waitpid(pid, &outStatus, WNOHANG);
    return r == pid || (r < 0 && errno == ECHILD);
}

bool env_key_is_overridden(const char *entry)
{
    static const char *const OVERRIDES[] = {
        "XUO_SCRIPT_PATH=",
        "XUO_SCRIPT_DIR=",
        "XUO_SCRIPT_NAME=",
        "XUO_SCRIPT_ARGS_JSON=",
        "XUO_RUNTIME_DIR=",
        "XUO_DOTNET_BIN=",
        "XUO_CACHE_DIR=",
        "PYTHONUNBUFFERED="
    };
    for (size_t i = 0; i < sizeof(OVERRIDES) / sizeof(OVERRIDES[0]); i++)
    {
        const size_t prefixLen = strlen(OVERRIDES[i]);
        if (strncmp(entry, OVERRIDES[i], prefixLen) == 0)
        {
            return true;
        }
    }
    return false;
}

#endif // XUO_EXTERNAL_RUNTIME_POSIX

} // namespace

ExternalScriptKind ExternalScript_DetectKind(const astr_t &path)
{
    const astr_t name = ScriptList_DisplayName(path);
    const size_t dot = name.find_last_of('.');
    if (dot == astr_t::npos || dot + 1 >= name.size())
    {
        return ExternalScriptKind::None;
    }
    const astr_t ext = str_lower(name.substr(dot));
    if (ext == ".py")
    {
        return ExternalScriptKind::Python;
    }
    if (ext == ".cs")
    {
        return ExternalScriptKind::CSharp;
    }
    return ExternalScriptKind::None;
}

bool ExternalScript_IsExternal(const astr_t &path)
{
    return ExternalScript_DetectKind(path) != ExternalScriptKind::None;
}

const char *ExternalScript_LanguageLabel(const astr_t &path)
{
    switch (ExternalScript_DetectKind(path))
    {
        case ExternalScriptKind::Python:
            return "Python (.py)";
        case ExternalScriptKind::CSharp:
            return "C# (.cs)";
        default:
            return "JavaScript (.js)";
    }
}

void ExternalScript_Terminate(ExternalScriptControl *control)
{
    if (control == nullptr)
    {
        return;
    }
    control->TerminateRequested.store(1);
#if XUO_EXTERNAL_RUNTIME_POSIX
    std::lock_guard<std::mutex> lock(control->SignalMutex);
    const int pgid = control->ChildPgid.load();
    if (pgid > 0)
    {
        send_group_signal((pid_t)pgid, SIGTERM);
    }
#endif
}

astr_t ExternalScript_ResolveRuntimeDir()
{
    const astr_t envDir = get_env_str("XUO_RUNTIME_DIR");
    if (!envDir.empty())
    {
        return ScriptList_NormalizePath(envDir);
    }

#if XUO_EXTERNAL_RUNTIME_POSIX
    vector<astr_t> candidates;
    const astr_t exeDir = get_executable_dir();
    if (!exeDir.empty())
    {
        candidates.push_back(ScriptList_NormalizePath(join_path(exeDir, "script/runtime")));
        candidates.push_back(ScriptList_NormalizePath(join_path(exeDir, "runtime")));
        candidates.push_back(
            ScriptList_NormalizePath(join_path(exeDir, "../Resources/script/runtime")));
        candidates.push_back(
            ScriptList_NormalizePath(join_path(exeDir, "../share/crossuo/script/runtime")));
        candidates.push_back(ScriptList_NormalizePath(
            join_path(exeDir, "../../../crossuo/src/Assistant/script/runtime")));
    }

    const astr_t cwd = get_current_working_dir();
    if (!cwd.empty())
    {
        candidates.push_back(
            ScriptList_NormalizePath(join_path(cwd, "crossuo/src/Assistant/script/runtime")));
        candidates.push_back(
            ScriptList_NormalizePath(join_path(cwd, "src/Assistant/script/runtime")));
        candidates.push_back(ScriptList_NormalizePath(join_path(cwd, "script/runtime")));
    }

    // Source-tree fallback derived from this file's compile path so standalone
    // harnesses built in a temp directory still locate script/runtime cleanly.
    const astr_t sourceDir = ScriptList_DirName(__FILE__);
    if (!sourceDir.empty() && sourceDir != ".")
    {
        candidates.push_back(ScriptList_NormalizePath(join_path(sourceDir, "runtime")));
    }

    for (const astr_t &dir : candidates)
    {
        if (runtime_dir_has_hosts(dir))
        {
            return dir;
        }
    }
    for (const astr_t &dir : candidates)
    {
        if (path_is_dir(dir))
        {
            return dir;
        }
    }
    if (!candidates.empty())
    {
        return candidates.back();
    }
#endif

    return "script/runtime";
}

astr_t ExternalScript_ResolveDotnetBin()
{
    const astr_t envBin = get_env_str("XUO_DOTNET_BIN");
    if (!envBin.empty())
    {
        return envBin;
    }

#if XUO_EXTERNAL_RUNTIME_POSIX
    vector<astr_t> candidates;
    const astr_t exeDir = get_executable_dir();
    if (!exeDir.empty())
    {
        candidates.push_back(ScriptList_NormalizePath(join_path(exeDir, "../../dotnet/dotnet")));
        candidates.push_back(ScriptList_NormalizePath(join_path(exeDir, "../dotnet/dotnet")));
        candidates.push_back(ScriptList_NormalizePath(join_path(exeDir, "dotnet/dotnet")));
    }

    const astr_t cwd = get_current_working_dir();
    if (!cwd.empty())
    {
        candidates.push_back(ScriptList_NormalizePath(join_path(cwd, "runtime/dotnet/dotnet")));
        candidates.push_back(ScriptList_NormalizePath(join_path(cwd, "../runtime/dotnet/dotnet")));
    }

    const astr_t sourceDir = ScriptList_DirName(__FILE__);
    if (!sourceDir.empty() && sourceDir != ".")
    {
        candidates.push_back(
            ScriptList_NormalizePath(join_path(sourceDir, "../../../../../runtime/dotnet/dotnet")));
    }

    for (const astr_t &bin : candidates)
    {
        if (path_is_executable_file(bin))
        {
            return bin;
        }
    }
#endif

    return "dotnet";
}

astr_t ExternalScript_ResolveCacheDir()
{
    const astr_t envDir = get_env_str("XUO_CACHE_DIR");
    if (!envDir.empty())
    {
        return ScriptList_NormalizePath(envDir);
    }

    astr_t tmpDir = get_env_str("TMPDIR");
    while (tmpDir.size() > 1 && (tmpDir[tmpDir.size() - 1] == '/' || tmpDir[tmpDir.size() - 1] == '\\'))
    {
        tmpDir.pop_back();
    }
    if (!tmpDir.empty())
    {
        return join_path(tmpDir, "crossuo-cs-cache");
    }
    return "/tmp/crossuo-cs-cache";
}

bool ExternalScript_DispatchRpcLine(
    CScriptEngine &engine,
    const astr_t &requestLine,
    astr_t &responseLine,
    bool &outInterrupted)
{
    outInterrupted = false;
    JSContext *ctx = engine.Context();
    if (ctx == nullptr)
    {
        responseLine =
            "{\"id\":null,\"ok\":false,\"error\":\"Script engine is not initialized\",\"interrupted\":false}\n";
        return false;
    }

    if (requestLine.size() > MAX_RPC_LINE_BYTES)
    {
        responseLine = make_error_response(
            ctx, JS_NULL, "RPC request exceeds 1 MiB limit", false);
        return false;
    }

    JSValue req = JS_ParseJSON(ctx, requestLine.c_str(), requestLine.size(), "<ipc>");
    if (JS_IsException(req))
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        responseLine = make_error_response(ctx, JS_NULL, "Invalid JSON-RPC request", false);
        return false;
    }
    if (!JS_IsObject(req) || JS_IsNull(req) || JS_IsArray(req))
    {
        JS_FreeValue(ctx, req);
        responseLine = make_error_response(ctx, JS_NULL, "JSON-RPC request must be an object", false);
        return false;
    }

    JSValue idVal = JS_GetPropertyStr(ctx, req, "id");
    if (JS_IsException(idVal) || JS_IsUndefined(idVal))
    {
        if (JS_IsException(idVal))
        {
            JS_FreeValue(ctx, JS_GetException(ctx));
        }
        idVal = JS_NULL;
    }

    // Honor Pause before invoking any host API method, while remaining
    // immediately preemptible by Stop/Terminate.
    while (engine.PauseFlag() && !engine.TerminateRequested())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (engine.TerminateRequested())
    {
        outInterrupted = true;
        responseLine = make_error_response(ctx, idVal, "interrupted", true);
        JS_FreeValue(ctx, idVal);
        JS_FreeValue(ctx, req);
        return false;
    }

    // Validate namespace string against strict allowlist (never eval).
    JSValue nsVal = JS_GetPropertyStr(ctx, req, "ns");
    if (!JS_IsString(nsVal))
    {
        JS_FreeValue(ctx, nsVal);
        responseLine = make_error_response(ctx, idVal, "Missing or invalid 'ns' field", false);
        JS_FreeValue(ctx, idVal);
        JS_FreeValue(ctx, req);
        return false;
    }
    size_t nsLen = 0;
    const char *nsRaw = JS_ToCStringLen(ctx, &nsLen, nsVal);
    const astr_t ns = nsRaw != nullptr ? astr_t(nsRaw, nsLen) : astr_t();
    if (nsRaw != nullptr)
    {
        JS_FreeCString(ctx, nsRaw);
    }
    JS_FreeValue(ctx, nsVal);

    if (!is_allowed_namespace(ns))
    {
        responseLine = make_error_response(ctx, idVal, "Disallowed namespace: " + ns, false);
        JS_FreeValue(ctx, idVal);
        JS_FreeValue(ctx, req);
        return false;
    }

    // Validate method name string.
    JSValue fnNameVal = JS_GetPropertyStr(ctx, req, "fn");
    if (!JS_IsString(fnNameVal))
    {
        JS_FreeValue(ctx, fnNameVal);
        responseLine = make_error_response(ctx, idVal, "Missing or invalid 'fn' field", false);
        JS_FreeValue(ctx, idVal);
        JS_FreeValue(ctx, req);
        return false;
    }
    size_t fnLen = 0;
    const char *fnRaw = JS_ToCStringLen(ctx, &fnLen, fnNameVal);
    const astr_t fnName = fnRaw != nullptr ? astr_t(fnRaw, fnLen) : astr_t();
    if (fnRaw != nullptr)
    {
        JS_FreeCString(ctx, fnRaw);
    }
    JS_FreeValue(ctx, fnNameVal);

    if (fnName.empty())
    {
        responseLine = make_error_response(ctx, idVal, "Empty 'fn' field", false);
        JS_FreeValue(ctx, idVal);
        JS_FreeValue(ctx, req);
        return false;
    }

    // Look up globalThis[ns][fn] strictly as an OWN function property so
    // inherited Object.prototype properties (constructor, __proto__, toString)
    // can never be invoked.
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue nsObj = JS_GetPropertyStr(ctx, global, ns.c_str());
    JS_FreeValue(ctx, global);

    if (!JS_IsObject(nsObj) || JS_IsNull(nsObj))
    {
        JS_FreeValue(ctx, nsObj);
        responseLine = make_error_response(ctx, idVal, "Namespace unavailable: " + ns, false);
        JS_FreeValue(ctx, idVal);
        JS_FreeValue(ctx, req);
        return false;
    }

    JSAtom fnAtom = JS_NewAtomLen(ctx, fnName.c_str(), fnName.size());
    const int ownResult = JS_GetOwnProperty(ctx, nullptr, nsObj, fnAtom);
    JSValue fnVal = ownResult == 1 ? JS_GetProperty(ctx, nsObj, fnAtom) : JS_UNDEFINED;
    JS_FreeAtom(ctx, fnAtom);

    if (ownResult < 0)
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
    }
    if (ownResult != 1 || !JS_IsFunction(ctx, fnVal))
    {
        JS_FreeValue(ctx, fnVal);
        JS_FreeValue(ctx, nsObj);
        responseLine = make_error_response(
            ctx, idVal, "Unknown API method: " + ns + "." + fnName, false);
        JS_FreeValue(ctx, idVal);
        JS_FreeValue(ctx, req);
        return false;
    }

    // Unpack optional args array.
    JSValue argsVal = JS_GetPropertyStr(ctx, req, "args");
    vector<JSValue> argv;
    if (!JS_IsUndefined(argsVal) && !JS_IsNull(argsVal))
    {
        if (!JS_IsArray(argsVal))
        {
            JS_FreeValue(ctx, argsVal);
            JS_FreeValue(ctx, fnVal);
            JS_FreeValue(ctx, nsObj);
            responseLine = make_error_response(ctx, idVal, "'args' must be an array", false);
            JS_FreeValue(ctx, idVal);
            JS_FreeValue(ctx, req);
            return false;
        }
        JSValue lenVal = JS_GetPropertyStr(ctx, argsVal, "length");
        uint32_t argc = 0;
        if (JS_ToUint32(ctx, &argc, lenVal) != 0)
        {
            JS_FreeValue(ctx, JS_GetException(ctx));
            argc = 0;
        }
        JS_FreeValue(ctx, lenVal);

        if (argc > MAX_RPC_ARGS)
        {
            JS_FreeValue(ctx, argsVal);
            JS_FreeValue(ctx, fnVal);
            JS_FreeValue(ctx, nsObj);
            responseLine = make_error_response(ctx, idVal, "Too many arguments", false);
            JS_FreeValue(ctx, idVal);
            JS_FreeValue(ctx, req);
            return false;
        }

        argv.reserve(argc);
        for (uint32_t i = 0; i < argc; i++)
        {
            argv.push_back(JS_GetPropertyUint32(ctx, argsVal, i));
        }
    }
    JS_FreeValue(ctx, argsVal);

    // Invoke registered native JS method directly (never eval).
    JSValue callRes = JS_Call(
        ctx,
        fnVal,
        nsObj,
        (int)argv.size(),
        argv.empty() ? nullptr : argv.data());

    for (size_t i = 0; i < argv.size(); i++)
    {
        JS_FreeValue(ctx, argv[i]);
    }
    JS_FreeValue(ctx, fnVal);
    JS_FreeValue(ctx, nsObj);
    JS_FreeValue(ctx, req);

    if (JS_IsException(callRes))
    {
        JSValue exc = JS_GetException(ctx);
        const bool interrupted = engine.TerminateRequested() || JS_IsUncatchableError(exc);
        astr_t errMsg = interrupted ? "interrupted" : "Script API call failed";
        if (!interrupted)
        {
            const char *excText = JS_ToCString(ctx, exc);
            if (excText != nullptr)
            {
                errMsg = excText;
                JS_FreeCString(ctx, excText);
            }
        }
        JS_FreeValue(ctx, exc);
        outInterrupted = interrupted;
        responseLine = make_error_response(ctx, idVal, errMsg, interrupted);
        JS_FreeValue(ctx, idVal);
        return false;
    }

    if (engine.TerminateRequested())
    {
        JS_FreeValue(ctx, callRes);
        outInterrupted = true;
        responseLine = make_error_response(ctx, idVal, "interrupted", true);
        JS_FreeValue(ctx, idVal);
        return false;
    }

    // Convert JS undefined / non-JSON values -> JSON null so the response always
    // carries a valid JSON `result` field.
    JSValue resultField =
        (JS_IsUndefined(callRes) || JS_IsFunction(ctx, callRes) || JS_IsSymbol(callRes))
            ? JS_NULL
            : callRes;
    if (JS_IsNull(resultField) && !JS_IsNull(callRes))
    {
        JS_FreeValue(ctx, callRes);
    }
    JSValue resp = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, resp, "id", JS_DupValue(ctx, idVal));
    JS_SetPropertyStr(ctx, resp, "ok", JS_NewBool(ctx, 1));
    JS_SetPropertyStr(ctx, resp, "result", resultField); // ownership transferred to resp

    JSValue jsonVal = JS_JSONStringify(ctx, resp, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(ctx, resp);
    if (JS_IsException(jsonVal))
    {
        JS_FreeValue(ctx, JS_GetException(ctx));
        responseLine = make_error_response(
            ctx, idVal, "API result is not JSON-serializable", false);
        JS_FreeValue(ctx, idVal);
        return false;
    }

    size_t outLen = 0;
    const char *outRaw = JS_ToCStringLen(ctx, &outLen, jsonVal);
    if (outRaw != nullptr)
    {
        responseLine.assign(outRaw, outLen);
        JS_FreeCString(ctx, outRaw);
        responseLine.push_back('\n');
    }
    else
    {
        responseLine = make_error_response(ctx, idVal, "Failed to encode JSON result", false);
    }
    JS_FreeValue(ctx, jsonVal);
    JS_FreeValue(ctx, idVal);
    return true;
}

bool ExternalScript_Run(
    CScriptEngine &engine,
    const astr_t &scriptPath,
    const vector<astr_t> &args,
    ExternalScriptControl *control)
{
    const ExternalScriptKind kind = ExternalScript_DetectKind(scriptPath);
    if (kind == ExternalScriptKind::None)
    {
        external_script_error(
            "External script error",
            "Unsupported external script extension: " + ScriptList_DisplayName(scriptPath));
        return false;
    }

#if !XUO_EXTERNAL_RUNTIME_POSIX
    (void)engine;
    (void)args;
    (void)control;
    external_script_error(
        "External script error",
        "Python (.py) and C# (.cs) scripts are only supported on macOS and Linux.");
    return false;
#else
    if (engine.TerminateRequested() ||
        (control != nullptr && control->TerminateRequested.load() != 0))
    {
        return true;
    }

    ensure_sigpipe_ignored();

    const astr_t fullScriptPath = resolve_script_full_path(scriptPath);
    const astr_t scriptDir = ScriptList_DirName(fullScriptPath);
    const astr_t scriptName = ScriptList_DisplayName(fullScriptPath);
    const astr_t runtimeDir = ExternalScript_ResolveRuntimeDir();
    const astr_t dotnetBin = ExternalScript_ResolveDotnetBin();
    const astr_t cacheDir = ExternalScript_ResolveCacheDir();
    const astr_t argsJson = serialize_args_json(engine.Context(), args);

    astr_t pythonBin = get_env_str("XUO_PYTHON_BIN");
    if (pythonBin.empty())
    {
        pythonBin = "python3";
    }

    const astr_t hostScript = kind == ExternalScriptKind::Python
        ? join_path(runtimeDir, "python_host.py")
        : join_path(runtimeDir, "csharp/run_csharp_script.py");

    if (!path_is_readable_file(hostScript))
    {
        external_script_error(
            "External script runtime missing",
            "Could not find script runtime host: " + hostScript);
        return false;
    }

    int toChild[2] = { -1, -1 };
    int fromChildOut[2] = { -1, -1 };
    int fromChildErr[2] = { -1, -1 };
    if (::pipe(toChild) != 0)
    {
        external_script_error("External script error", "Could not create stdin pipe.");
        return false;
    }
    if (::pipe(fromChildOut) != 0)
    {
        ::close(toChild[0]);
        ::close(toChild[1]);
        external_script_error("External script error", "Could not create stdout pipe.");
        return false;
    }
    if (::pipe(fromChildErr) != 0)
    {
        ::close(toChild[0]);
        ::close(toChild[1]);
        ::close(fromChildOut[0]);
        ::close(fromChildOut[1]);
        external_script_error("External script error", "Could not create stderr pipe.");
        return false;
    }

    set_cloexec(toChild[0]);
    set_cloexec(toChild[1]);
    set_cloexec(fromChildOut[0]);
    set_cloexec(fromChildOut[1]);
    set_cloexec(fromChildErr[0]);
    set_cloexec(fromChildErr[1]);

    set_nonblock(fromChildOut[0]);
    set_nonblock(fromChildErr[0]);

#if defined(F_SETNOSIGPIPE)
    ::fcntl(toChild[1], F_SETNOSIGPIPE, 1);
#endif

    // Build argv and envp in the parent BEFORE fork() so the child performs
    // zero heap allocations and zero setenv() calls between fork and execvp.
    vector<astr_t> argvStorage;
    argvStorage.push_back(pythonBin);
    argvStorage.push_back(hostScript);

    vector<char *> argvPtrs;
    argvPtrs.reserve(argvStorage.size() + 1);
    for (size_t i = 0; i < argvStorage.size(); i++)
    {
        argvPtrs.push_back(const_cast<char *>(argvStorage[i].c_str()));
    }
    argvPtrs.push_back(nullptr);

    vector<astr_t> envStorage;
#if defined(__APPLE__)
    char **parentEnv = *_NSGetEnviron();
#else
    char **parentEnv = environ;
#endif
    if (parentEnv != nullptr)
    {
        for (char **entry = parentEnv; *entry != nullptr; ++entry)
        {
            if (!env_key_is_overridden(*entry))
            {
                envStorage.push_back(*entry);
            }
        }
    }
    envStorage.push_back("XUO_SCRIPT_PATH=" + fullScriptPath);
    envStorage.push_back("XUO_SCRIPT_DIR=" + scriptDir);
    envStorage.push_back("XUO_SCRIPT_NAME=" + scriptName);
    envStorage.push_back("XUO_SCRIPT_ARGS_JSON=" + argsJson);
    envStorage.push_back("XUO_RUNTIME_DIR=" + runtimeDir);
    envStorage.push_back("XUO_DOTNET_BIN=" + dotnetBin);
    envStorage.push_back("XUO_CACHE_DIR=" + cacheDir);
    envStorage.push_back("PYTHONUNBUFFERED=1");

    vector<char *> envPtrs;
    envPtrs.reserve(envStorage.size() + 1);
    for (size_t i = 0; i < envStorage.size(); i++)
    {
        envPtrs.push_back(const_cast<char *>(envStorage[i].c_str()));
    }
    envPtrs.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0)
    {
        ::close(toChild[0]);
        ::close(toChild[1]);
        ::close(fromChildOut[0]);
        ::close(fromChildOut[1]);
        ::close(fromChildErr[0]);
        ::close(fromChildErr[1]);
        external_script_error("External script error", "Could not start script process.");
        return false;
    }

    if (pid == 0)
    {
        // Child process: isolate in its own process group/session so Terminate
        // signals the entire subtree cleanly, wire stdin/stdout/stderr pipes,
        // and execvp without any shell.
        ::setsid();
        ::signal(SIGPIPE, SIG_DFL);
        ::signal(SIGTERM, SIG_DFL);
        ::signal(SIGINT, SIG_DFL);

        ::dup2(toChild[0], STDIN_FILENO);
        ::dup2(fromChildOut[1], STDOUT_FILENO);
        ::dup2(fromChildErr[1], STDERR_FILENO);

        const int fdsToClose[6] = {
            toChild[0], toChild[1],
            fromChildOut[0], fromChildOut[1],
            fromChildErr[0], fromChildErr[1]
        };
        for (int i = 0; i < 6; i++)
        {
            if (fdsToClose[i] > STDERR_FILENO)
            {
                ::close(fdsToClose[i]);
            }
        }

#if defined(__APPLE__)
        *(_NSGetEnviron()) = envPtrs.data();
#else
        environ = envPtrs.data();
#endif
        ::execvp(argvPtrs[0], argvPtrs.data());

        const char msg[] = "Failed to launch external script interpreter.\n";
        const ssize_t ignored = ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        (void)ignored;
        ::_exit(127);
    }

    // Parent process.
    ::close(toChild[0]);
    ::close(fromChildOut[1]);
    ::close(fromChildErr[1]);

    int childInFd = toChild[1];
    int childOutFd = fromChildOut[0];
    int childErrFd = fromChildErr[0];

    if (control != nullptr)
    {
        std::lock_guard<std::mutex> lock(control->SignalMutex);
        control->ChildPgid.store((int)pid);
        if (control->TerminateRequested.load() != 0 || engine.TerminateRequested())
        {
            send_group_signal(pid, SIGTERM);
        }
    }

    bool outOpen = true;
    bool errOpen = true;
    bool sawStderr = false;
    bool protocolFailed = false;
    astr_t stdoutBuf;
    astr_t stderrBuf;
    char readBuf[4096];

    auto isTerminateRequested = [&]() -> bool {
        return engine.TerminateRequested() ||
            (control != nullptr && control->TerminateRequested.load() != 0);
    };

    auto drainStderrNonBlocking = [&]() {
        while (errOpen)
        {
            const ssize_t n = ::read(childErrFd, readBuf, sizeof(readBuf));
            if (n > 0)
            {
                stderrBuf.append(readBuf, (size_t)n);
                flush_stderr_lines(scriptName, stderrBuf, false, sawStderr);
                if (stderrBuf.size() > MAX_STDERR_LINE_BYTES)
                {
                    flush_stderr_lines(scriptName, stderrBuf, true, sawStderr);
                }
                continue;
            }
            if (n == 0)
            {
                errOpen = false;
                flush_stderr_lines(scriptName, stderrBuf, true, sawStderr);
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            errOpen = false;
            flush_stderr_lines(scriptName, stderrBuf, true, sawStderr);
            break;
        }
    };

    while ((outOpen || errOpen) && !protocolFailed)
    {
        if (isTerminateRequested())
        {
            break;
        }

        struct pollfd pfds[2];
        nfds_t nfds = 0;
        int outIdx = -1;
        int errIdx = -1;
        if (outOpen)
        {
            outIdx = (int)nfds;
            pfds[nfds].fd = childOutFd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
        if (errOpen)
        {
            errIdx = (int)nfds;
            pfds[nfds].fd = childErrFd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        const int pollRc = ::poll(pfds, nfds, 20);
        if (pollRc < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        if (pollRc == 0)
        {
            continue;
        }

        // Drain stderr first so verbose compiler/traceback output can never
        // fill the stderr pipe buffer and deadlock the child.
        if (errIdx >= 0 && (pfds[errIdx].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0)
        {
            drainStderrNonBlocking();
        }

        if (outIdx >= 0 && (pfds[outIdx].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0)
        {
            while (outOpen)
            {
                const ssize_t n = ::read(childOutFd, readBuf, sizeof(readBuf));
                if (n > 0)
                {
                    if (stdoutBuf.size() + (size_t)n > MAX_RPC_LINE_BYTES + 1)
                    {
                        const size_t firstNl = stdoutBuf.find('\n');
                        if (firstNl == astr_t::npos)
                        {
                            external_script_error(
                                "External script error (" + scriptName + ")",
                                "RPC message exceeded maximum size (1 MiB).");
                            protocolFailed = true;
                            break;
                        }
                    }
                    stdoutBuf.append(readBuf, (size_t)n);
                    if (stdoutBuf.find('\n') != astr_t::npos ||
                        stdoutBuf.size() >= MAX_RPC_LINE_BYTES)
                    {
                        break;
                    }
                    continue;
                }
                if (n == 0)
                {
                    outOpen = false;
                    break;
                }
                if (errno == EINTR)
                {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                outOpen = false;
                break;
            }

            if (protocolFailed)
            {
                break;
            }

            size_t nl = astr_t::npos;
            while ((nl = stdoutBuf.find('\n')) != astr_t::npos)
            {
                if (nl > MAX_RPC_LINE_BYTES)
                {
                    external_script_error(
                        "External script error (" + scriptName + ")",
                        "RPC message exceeded maximum size (1 MiB).");
                    protocolFailed = true;
                    break;
                }

                astr_t line = stdoutBuf.substr(0, nl);
                stdoutBuf.erase(0, nl + 1);
                if (!line.empty() && line[line.size() - 1] == '\r')
                {
                    line.pop_back();
                }
                if (line.empty())
                {
                    continue;
                }

                astr_t response;
                bool interrupted = false;
                ExternalScript_DispatchRpcLine(engine, line, response, interrupted);

                if (childInFd >= 0 && !response.empty())
                {
                    if (!write_all_fd(childInFd, response.data(), response.size()))
                    {
                        ::close(childInFd);
                        childInFd = -1;
                    }
                }

                // Keep stderr drained even during back-to-back RPC bursts.
                drainStderrNonBlocking();

                if (interrupted || isTerminateRequested())
                {
                    break;
                }
            }
        }
    }

    // Close parent pipe ends (closing stdin sends EOF to any blocked child read).
    if (childInFd >= 0)
    {
        ::close(childInFd);
        childInFd = -1;
    }
    if (childOutFd >= 0)
    {
        ::close(childOutFd);
        childOutFd = -1;
    }
    if (childErrFd >= 0)
    {
        ::close(childErrFd);
        childErrFd = -1;
    }
    flush_stderr_lines(scriptName, stderrBuf, true, sawStderr);

    // Reap child cleanly (zero zombies). If termination was requested or the
    // protocol failed, send SIGTERM to -pgid, wait briefly, then SIGKILL.
    int status = 0;
    bool reaped = try_reap_child(pid, status, control);
    bool signaledTerm = false;

    auto signalGroupUnderLock = [&](int sig, bool clearPgid) {
        if (control != nullptr)
        {
            std::lock_guard<std::mutex> lock(control->SignalMutex);
            if (control->ChildPgid.load() > 0)
            {
                send_group_signal(pid, sig);
                if (clearPgid)
                {
                    control->ChildPgid.store(0);
                }
            }
        }
        else
        {
            send_group_signal(pid, sig);
        }
    };

    const auto killDeadline = [&]() {
        return std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    };
    std::chrono::steady_clock::time_point termDeadline;

    while (!reaped)
    {
        const bool mustStop = protocolFailed || isTerminateRequested();
        if (mustStop && !signaledTerm)
        {
            signalGroupUnderLock(SIGTERM, false);
            signaledTerm = true;
            termDeadline = killDeadline();
        }

        if (signaledTerm && std::chrono::steady_clock::now() >= termDeadline)
        {
            signalGroupUnderLock(SIGKILL, true);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR)
            {
            }
            reaped = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        reaped = try_reap_child(pid, status, control);
    }

    if (isTerminateRequested())
    {
        return true;
    }
    if (protocolFailed)
    {
        return false;
    }
    if (WIFEXITED(status))
    {
        const int exitCode = WEXITSTATUS(status);
        if (exitCode == 0)
        {
            return true;
        }
        if (!sawStderr)
        {
            external_script_error(
                "External script error (" + scriptName + ")",
                "Script exited with status " + str_from(exitCode) + ".");
        }
        return false;
    }
    if (WIFSIGNALED(status))
    {
        const int sigNum = WTERMSIG(status);
        if (!sawStderr)
        {
            external_script_error(
                "External script error (" + scriptName + ")",
                "Script terminated by signal " + str_from(sigNum) + ".");
        }
        return false;
    }
    return false;
#endif
}

} // namespace xuoassist
