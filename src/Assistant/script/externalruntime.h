// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

#pragma once

// Out-of-process external script runner for Python (.py) and C# (.cs) scripts.
//
// Each external script runs in its own child process group communicating with
// a worker-thread CScriptEngine over stdin/stdout single-line UTF-8 JSON-RPC.
// No shell (`sh -c`) and no network sockets are used.

#include "../core/global.h"

#include <atomic>
#include <mutex>

namespace xuoassist {

class CScriptEngine;

enum class ExternalScriptKind
{
    None = 0,
    Python,
    CSharp
};

// Cross-thread process-group control shared between CScriptHandle::Terminate()
// and ExternalScript_Run() on the worker thread.
struct ExternalScriptControl
{
    std::atomic<int> TerminateRequested{ 0 };
    std::atomic<int> ChildPgid{ 0 };
    std::mutex SignalMutex;
};

// Case-insensitive extension detection (.py -> Python, .cs -> CSharp).
ExternalScriptKind ExternalScript_DetectKind(const astr_t &path);
bool ExternalScript_IsExternal(const astr_t &path);

// Human-readable script language label for UI tooltips/lists.
const char *ExternalScript_LanguageLabel(const astr_t &path);

// Signals the live child process group (SIGTERM to -pgid) if active and marks
// termination requested. Safe to call from any thread at any lifecycle point.
void ExternalScript_Terminate(ExternalScriptControl *control);

// Resolves runtime/dotnet/cache paths used for child environment variables.
astr_t ExternalScript_ResolveRuntimeDir();
astr_t ExternalScript_ResolveDotnetBin();
astr_t ExternalScript_ResolveCacheDir();

// Dispatches one single-line JSON-RPC request against a configured
// CScriptEngine (strict allowlisted namespaces, no eval, pause/terminate
// aware). Exposed for direct harness verification as well as pipe I/O.
bool ExternalScript_DispatchRpcLine(
    CScriptEngine &engine,
    const astr_t &requestLine,
    astr_t &responseLine,
    bool &outInterrupted);

// Launches and drives an external .py or .cs script against `engine` until
// exit or termination. Returns true on clean exit (or user-requested stop)
// and false on runtime/launch/protocol error.
bool ExternalScript_Run(
    CScriptEngine &engine,
    const astr_t &scriptPath,
    const vector<astr_t> &args,
    ExternalScriptControl *control);

} // namespace xuoassist
