#!/usr/bin/env python3
"""Compile, cache, and exec Razor Enhanced C# (.cs) scripts for CrossUO.

Uses the locally installed .NET SDK's Roslyn compiler (`Roslyn/bincore/csc.dll`)
and local `Microsoft.NETCore.App.Ref` reference assemblies with ZERO network
access and ZERO NuGet downloads. Replaces itself via `os.execv` so stdin/stdout
JSON-RPC pipes and process-group signals go straight to the compiled .NET host.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import List, Optional, Tuple


CACHE_SCHEMA_VERSION = b"xuo-cs-cache-v1\0"


def _parse_version(text: str) -> Tuple[Tuple[int, ...], int, str]:
    """Parse a .NET semver folder name into a sortable key (stable > prerelease)."""
    main_part, _, pre_part = text.partition("-")
    nums: List[int] = []
    for part in main_part.split("."):
        m = re.match(r"^(\d+)", part)
        nums.append(int(m.group(1)) if m else 0)
    while len(nums) < 3:
        nums.append(0)
    is_stable = 1 if not pre_part else 0
    return (tuple(nums), is_stable, pre_part)


def _is_executable_file(path: Path) -> bool:
    return path.is_file() and os.access(str(path), os.X_OK)


def _has_sdk_layout(dotnet_bin: Path) -> bool:
    try:
        root = dotnet_bin.resolve().parent
        if (root / "sdk").is_dir():
            return True
        env_root = os.environ.get("DOTNET_ROOT", "").strip()
        if env_root and (Path(env_root) / "sdk").is_dir():
            return True
    except OSError:
        return False
    return False


def resolve_dotnet_bin(script_dir: Path) -> Path:
    """Resolve dotnet binary: XUO_DOTNET_BIN -> sibling runtime/dotnet/dotnet -> PATH."""
    candidates: List[Path] = []
    env_bin = os.environ.get("XUO_DOTNET_BIN", "").strip()
    if env_bin:
        candidate = Path(env_bin)
        if _is_executable_file(candidate):
            candidates.append(candidate)
        else:
            found = shutil.which(env_bin)
            if found:
                candidates.append(Path(found))

    # Walk upwards from runtime dir and cwd looking for <workspace>/runtime/dotnet/dotnet
    search_roots = [script_dir.resolve(), Path.cwd().resolve()]
    for start in search_roots:
        for parent in [start] + list(start.parents):
            candidate = parent / "runtime" / "dotnet" / "dotnet"
            if _is_executable_file(candidate) and candidate not in candidates:
                candidates.append(candidate)

    which_dotnet = shutil.which("dotnet")
    if which_dotnet:
        p = Path(which_dotnet)
        if p not in candidates:
            candidates.append(p)

    # Prefer the first candidate (respecting XUO_DOTNET_BIN priority) that has a full SDK layout.
    for cand in candidates:
        if _has_sdk_layout(cand):
            return cand
    if candidates:
        return candidates[0]

    raise RuntimeError(
        "Unable to locate a .NET SDK (`dotnet`). Set XUO_DOTNET_BIN or install "
        "a .NET SDK under runtime/dotnet/dotnet."
    )


def discover_dotnet_layout(dotnet_bin: Path) -> Tuple[Path, str, Path, str, Path, str]:
    """Discover (dotnet_root, sdk_version, csc_dll, runtime_version, ref_dir, tfm)."""
    resolved_bin = dotnet_bin.resolve()
    dotnet_root = resolved_bin.parent

    # Allow DOTNET_ROOT override if binary is a thin shim outside the SDK tree
    if not (dotnet_root / "sdk").is_dir():
        env_root = os.environ.get("DOTNET_ROOT", "").strip()
        if env_root and (Path(env_root) / "sdk").is_dir():
            dotnet_root = Path(env_root).resolve()

    sdk_root = dotnet_root / "sdk"
    if not sdk_root.is_dir():
        raise RuntimeError(f"No .NET SDK directory found under {dotnet_root}")

    sdk_candidates: List[Tuple[Tuple[Tuple[int, ...], int, str], str, Path]] = []
    for entry in sdk_root.iterdir():
        if not entry.is_dir():
            continue
        csc_dll = entry / "Roslyn" / "bincore" / "csc.dll"
        if csc_dll.is_file():
            sdk_candidates.append((_parse_version(entry.name), entry.name, csc_dll))
    if not sdk_candidates:
        raise RuntimeError(f"No Roslyn compiler (Roslyn/bincore/csc.dll) found under {sdk_root}")
    sdk_candidates.sort(key=lambda item: item[0], reverse=True)

    shared_root = dotnet_root / "shared" / "Microsoft.NETCore.App"
    if not shared_root.is_dir():
        raise RuntimeError(f"No Microsoft.NETCore.App runtime found under {shared_root}")
    runtime_versions = [
        (_parse_version(entry.name), entry.name)
        for entry in shared_root.iterdir()
        if entry.is_dir()
    ]
    if not runtime_versions:
        raise RuntimeError(f"No runtime versions found under {shared_root}")
    runtime_versions.sort(key=lambda item: item[0], reverse=True)

    packs_root = dotnet_root / "packs" / "Microsoft.NETCore.App.Ref"
    ref_candidates: List[Tuple[Tuple[Tuple[int, ...], int, str], str, Path, str]] = []
    if packs_root.is_dir():
        for ver_dir in packs_root.iterdir():
            if not ver_dir.is_dir():
                continue
            ref_parent = ver_dir / "ref"
            if not ref_parent.is_dir():
                continue
            for tfm_dir in sorted(ref_parent.iterdir()):
                if tfm_dir.is_dir() and any(tfm_dir.glob("*.dll")):
                    ref_candidates.append(
                        (_parse_version(ver_dir.name), ver_dir.name, tfm_dir, tfm_dir.name)
                    )
    if not ref_candidates:
        raise RuntimeError(
            f"No Microsoft.NETCore.App.Ref reference assemblies found under {packs_root}"
        )
    ref_candidates.sort(key=lambda item: item[0], reverse=True)

    # Pick highest runtime version with matching major ref pack (preferring exact version)
    chosen_runtime: Optional[str] = None
    chosen_ref_dir: Optional[Path] = None
    chosen_tfm: Optional[str] = None
    for rt_key, rt_ver in runtime_versions:
        rt_major = rt_key[0][0]
        exact = [c for c in ref_candidates if c[1] == rt_ver]
        if exact:
            chosen_runtime = rt_ver
            chosen_ref_dir = exact[0][2]
            chosen_tfm = exact[0][3]
            break
        same_major = [c for c in ref_candidates if c[0][0][0] == rt_major]
        if same_major:
            chosen_runtime = rt_ver
            chosen_ref_dir = same_major[0][2]
            chosen_tfm = same_major[0][3]
            break

    if chosen_runtime is None or chosen_ref_dir is None or chosen_tfm is None:
        chosen_runtime = runtime_versions[0][1]
        chosen_ref_dir = ref_candidates[0][2]
        chosen_tfm = ref_candidates[0][3]

    # Prefer SDK matching chosen runtime major version
    chosen_major = _parse_version(chosen_runtime)[0][0]
    matching_sdks = [s for s in sdk_candidates if s[0][0][0] == chosen_major]
    _, sdk_version, csc_dll = matching_sdks[0] if matching_sdks else sdk_candidates[0]

    return dotnet_root, sdk_version, csc_dll, chosen_runtime, chosen_ref_dir, chosen_tfm


def compute_cache_key(
    source_files: List[Path],
    tfm: str,
    runtime_version: str,
    sdk_version: str,
) -> str:
    """Compute deterministic SHA-256 over runtime sources, user script, and TFM."""
    h = hashlib.sha256()
    h.update(CACHE_SCHEMA_VERSION)
    meta = f"{tfm}\0{runtime_version}\0{sdk_version}\0".encode("utf-8")
    h.update(meta)
    for src in source_files:
        data = src.read_bytes()
        h.update(src.name.encode("utf-8"))
        h.update(b"\0")
        h.update(str(len(data)).encode("ascii"))
        h.update(b"\0")
        h.update(data)
        h.update(b"\0")
    return h.hexdigest()


def _quote_rsp_arg(arg: str) -> str:
    if any(ch in arg for ch in (' ', '\t', '"')):
        return '"' + arg.replace('"', '\\"') + '"'
    return arg


def compile_to_cache(
    dotnet_bin: Path,
    dotnet_root: Path,
    csc_dll: Path,
    ref_dir: Path,
    tfm: str,
    runtime_version: str,
    source_files: List[Path],
    cache_dir: Path,
    cache_key: str,
) -> Path:
    """Compile into cache_dir/<cache_key>/ScriptAssembly.dll atomically on cache miss."""
    entry_dir = cache_dir / cache_key
    dll_path = entry_dir / "ScriptAssembly.dll"
    rc_path = entry_dir / "ScriptAssembly.runtimeconfig.json"

    if dll_path.is_file() and rc_path.is_file():
        return dll_path

    cache_dir.mkdir(parents=True, exist_ok=True)
    tmp_dir = Path(tempfile.mkdtemp(prefix=f".tmp-{cache_key[:12]}-", dir=str(cache_dir)))
    try:
        tmp_dll = tmp_dir / "ScriptAssembly.dll"
        tmp_rc = tmp_dir / "ScriptAssembly.runtimeconfig.json"
        tmp_rsp = tmp_dir / "compile.rsp"

        ref_dlls = sorted(ref_dir.glob("*.dll"), key=lambda p: p.name)
        if not ref_dlls:
            raise RuntimeError(f"Reference assembly directory contains no DLLs: {ref_dir}")

        rsp_lines = [
            "-nologo",
            "-target:exe",
            "-optimize+",
            "-langversion:latest",
            "-nullable:disable",
            "-nowarn:CS0168,CS0219,CS0414,CS1701,CS1702,CS8019",
            f"-out:{_quote_rsp_arg(str(tmp_dll))}",
        ]
        for ref_dll in ref_dlls:
            rsp_lines.append(f"-r:{_quote_rsp_arg(str(ref_dll))}")
        for src in source_files:
            rsp_lines.append(_quote_rsp_arg(str(src)))

        tmp_rsp.write_text("\n".join(rsp_lines) + "\n", encoding="utf-8")

        env = os.environ.copy()
        env["DOTNET_ROOT"] = str(dotnet_root)
        env["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
        env["DOTNET_NOLOGO"] = "1"
        env["DOTNET_SKIP_FIRST_TIME_EXPERIENCE"] = "1"

        proc = subprocess.run(
            [str(dotnet_bin), "exec", str(csc_dll), f"@{tmp_rsp}"],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
            check=False,
        )

        if proc.returncode != 0:
            diagnostics = []
            if proc.stdout and proc.stdout.strip():
                diagnostics.append(proc.stdout.strip())
            if proc.stderr and proc.stderr.strip():
                diagnostics.append(proc.stderr.strip())
            msg = "\n".join(diagnostics) if diagnostics else f"C# compiler exited with code {proc.returncode}"
            sys.stderr.write(msg + "\n")
            sys.stderr.flush()
            shutil.rmtree(tmp_dir, ignore_errors=True)
            sys.exit(proc.returncode if proc.returncode != 0 else 1)

        if not tmp_dll.is_file():
            sys.stderr.write("C# compiler succeeded but ScriptAssembly.dll was not produced.\n")
            sys.stderr.flush()
            shutil.rmtree(tmp_dir, ignore_errors=True)
            sys.exit(1)

        runtime_config = {
            "runtimeOptions": {
                "tfm": tfm,
                "framework": {
                    "name": "Microsoft.NETCore.App",
                    "version": runtime_version,
                },
            }
        }
        tmp_rc.write_text(json.dumps(runtime_config, indent=2) + "\n", encoding="utf-8")
        try:
            tmp_rsp.unlink()
        except OSError:
            pass

        try:
            os.rename(str(tmp_dir), str(entry_dir))
        except OSError:
            # Another concurrent process may have published the same content-addressed entry first.
            shutil.rmtree(tmp_dir, ignore_errors=True)
            if not (dll_path.is_file() and rc_path.is_file()):
                raise

        return dll_path
    except BaseException:
        shutil.rmtree(tmp_dir, ignore_errors=True)
        raise


def main() -> None:
    runtime_csharp_dir = Path(__file__).resolve().parent

    script_path_str = os.environ.get("XUO_SCRIPT_PATH", "").strip()
    if not script_path_str and len(sys.argv) >= 2:
        script_path_str = sys.argv[1]
    if not script_path_str:
        sys.stderr.write("XUO_SCRIPT_PATH is not set.\n")
        sys.stderr.flush()
        sys.exit(2)

    script_path = Path(script_path_str).resolve()
    if not script_path.is_file():
        sys.stderr.write(f"C# script file not found: {script_path}\n")
        sys.stderr.flush()
        sys.exit(2)

    # Ensure child env vars have sensible defaults if launched standalone
    os.environ.setdefault("XUO_SCRIPT_PATH", str(script_path))
    os.environ.setdefault("XUO_SCRIPT_DIR", str(script_path.parent))
    os.environ.setdefault("XUO_SCRIPT_NAME", script_path.name)
    os.environ.setdefault("XUO_SCRIPT_ARGS_JSON", "[]")

    cache_dir_str = os.environ.get("XUO_CACHE_DIR", "").strip()
    if cache_dir_str:
        cache_dir = Path(cache_dir_str)
    else:
        cache_dir = Path(tempfile.gettempdir()) / "crossuo-cs-cache"

    global_usings = runtime_csharp_dir / "GlobalUsings.cs"
    api_cs = runtime_csharp_dir / "RazorEnhancedApi.cs"
    host_cs = runtime_csharp_dir / "HostProgram.cs"
    for required in (global_usings, api_cs, host_cs):
        if not required.is_file():
            sys.stderr.write(f"Missing CrossUO C# runtime source file: {required}\n")
            sys.stderr.flush()
            sys.exit(2)

    try:
        dotnet_bin = resolve_dotnet_bin(runtime_csharp_dir)
        dotnet_root, sdk_version, csc_dll, runtime_version, ref_dir, tfm = discover_dotnet_layout(dotnet_bin)
        source_files = [global_usings, api_cs, host_cs, script_path]
        cache_key = compute_cache_key(source_files, tfm, runtime_version, sdk_version)
        dll_path = compile_to_cache(
            dotnet_bin=dotnet_bin,
            dotnet_root=dotnet_root,
            csc_dll=csc_dll,
            ref_dir=ref_dir,
            tfm=tfm,
            runtime_version=runtime_version,
            source_files=source_files,
            cache_dir=cache_dir,
            cache_key=cache_key,
        )
    except SystemExit:
        raise
    except Exception as exc:
        sys.stderr.write(f"CrossUO C# runtime error: {exc}\n")
        sys.stderr.flush()
        sys.exit(1)

    os.environ["DOTNET_ROOT"] = str(dotnet_root)
    os.environ["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1"
    os.environ["DOTNET_NOLOGO"] = "1"
    os.environ["DOTNET_SKIP_FIRST_TIME_EXPERIENCE"] = "1"

    os.execv(str(dotnet_bin), [str(dotnet_bin), "exec", str(dll_path)])


if __name__ == "__main__":
    main()
