#!/usr/bin/env python3
"""Automated tests for the CrossUO Razor Enhanced C# (.cs) script runtime.

Uses the workspace's local .NET SDK (`runtime/dotnet/dotnet`) with zero network
access and a real mock JSON-RPC pipe server implementing the exact `ipc-bridge`
wire contract.
"""

import json
import os
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple


REPO_ROOT = Path(__file__).resolve().parents[2]
CROSSUO_ROOT = Path(__file__).resolve().parents[1]
CSHARP_RUNTIME_DIR = CROSSUO_ROOT / "src" / "Assistant" / "script" / "runtime" / "csharp"
RUNNER_SCRIPT = CSHARP_RUNTIME_DIR / "run_csharp_script.py"
WORKSPACE_DOTNET = REPO_ROOT / "runtime" / "dotnet" / "dotnet"


def run_cs_with_mock_rpc(
    script_source: str,
    rpc_handler: Callable[[str, str, List[Any], int], Dict[str, Any]],
    cache_dir: Path,
    script_name: str = "test_script.cs",
    script_args: Optional[List[str]] = None,
    timeout: float = 30.0,
) -> Tuple[int, List[Dict[str, Any]], List[str], str, Path]:
    """Run a .cs script via run_csharp_script.py connected to a mock JSON-RPC pipe server.

    Returns (returncode, recorded_requests, raw_stdout_lines, stderr_text, script_path).
    """
    with tempfile.TemporaryDirectory(prefix="xuo-cs-script-") as script_tmp:
        script_path = Path(script_tmp) / script_name
        script_path.write_text(script_source, encoding="utf-8")
        return run_cs_file_with_mock_rpc(
            script_path=script_path,
            rpc_handler=rpc_handler,
            cache_dir=cache_dir,
            script_args=script_args,
            timeout=timeout,
        )


def run_cs_file_with_mock_rpc(
    script_path: Path,
    rpc_handler: Callable[[str, str, List[Any], int], Dict[str, Any]],
    cache_dir: Path,
    script_args: Optional[List[str]] = None,
    timeout: float = 30.0,
) -> Tuple[int, List[Dict[str, Any]], List[str], str, Path]:
    """Run an existing .cs file via run_csharp_script.py connected to a mock JSON-RPC server."""
    env = os.environ.copy()
    env["XUO_SCRIPT_PATH"] = str(script_path)
    env["XUO_SCRIPT_DIR"] = str(script_path.parent)
    env["XUO_SCRIPT_NAME"] = script_path.name
    env["XUO_SCRIPT_ARGS_JSON"] = json.dumps(script_args or [])
    env["XUO_RUNTIME_DIR"] = str(CSHARP_RUNTIME_DIR.parent)
    env["XUO_CACHE_DIR"] = str(cache_dir)
    if WORKSPACE_DOTNET.is_file():
        env["XUO_DOTNET_BIN"] = str(WORKSPACE_DOTNET)

    proc = subprocess.Popen(
        [sys.executable, str(RUNNER_SCRIPT)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=env,
        bufsize=1,
    )

    stderr_chunks: List[str] = []

    def _drain_stderr() -> None:
        assert proc.stderr is not None
        for line in proc.stderr:
            stderr_chunks.append(line)

    stderr_thread = threading.Thread(target=_drain_stderr, daemon=True)
    stderr_thread.start()

    requests: List[Dict[str, Any]] = []
    raw_stdout_lines: List[str] = []

    assert proc.stdout is not None
    assert proc.stdin is not None
    try:
        for raw_line in proc.stdout:
            raw_stdout_lines.append(raw_line)
            req = json.loads(raw_line)
            requests.append(req)
            req_id = req["id"]
            ns = req["ns"]
            fn = req["fn"]
            args = req.get("args", [])
            resp = rpc_handler(ns, fn, args, req_id)
            resp.setdefault("id", req_id)
            try:
                proc.stdin.write(json.dumps(resp) + "\n")
                proc.stdin.flush()
            except BrokenPipeError:
                break
    finally:
        try:
            proc.stdin.close()
        except OSError:
            pass

    rc = proc.wait(timeout=timeout)
    stderr_thread.join(timeout=5.0)
    try:
        proc.stdout.close()
    except OSError:
        pass
    try:
        assert proc.stderr is not None
        proc.stderr.close()
    except OSError:
        pass
    return rc, requests, raw_stdout_lines, "".join(stderr_chunks), script_path


class MockGameState:
    """In-memory mock matching CrossUO QuickJS Razor Enhanced RPC shapes."""

    def __init__(self) -> None:
        self.shared: Dict[str, Any] = {}
        self.ignored: set = set()
        self.timers: Dict[str, int] = {}
        self.last_target: int = 0x00002222
        self.war_mode: bool = False
        self.journal: List[str] = [
            "You see: an orc",
            "System: Welcome to Britannia",
        ]
        self.gump_lines: List[str] = ["Crafting Menu", "Katana", "Make Last"]
        self.autoloot_running: bool = False
        self.bandage_running: bool = False

    def handle(self, ns: str, fn: str, args: List[Any], req_id: int) -> Dict[str, Any]:
        key = (ns, fn)
        backpack_item = {
            "Serial": 0x40000100,
            "SerialHex": "0x40000100",
            "ItemID": 0x0E75,
            "Hue": 0,
            "Amount": 1,
            "Container": 0x00001234,
            "Layer": 21,
            "X": 0,
            "Y": 0,
            "Z": 0,
            "Map": 0,
            "Position": {"X": 0, "Y": 0, "Z": 0, "Map": 0},
            "IsContainer": True,
            "IsCorpse": False,
            "OnGround": False,
            "Name": "backpack",
            "Properties": ["backpack", "Weight: 12 stones"],
            "Contains": [],
        }
        katana_item = {
            "Serial": 0x40000201,
            "SerialHex": "0x40000201",
            "ItemID": 0x13FF,
            "Hue": 0x0480,
            "Amount": 1,
            "Container": 0x40000100,
            "Layer": 1,
            "X": 1400,
            "Y": 1600,
            "Z": 0,
            "Map": 0,
            "Position": {"X": 1400, "Y": 1600, "Z": 0, "Map": 0},
            "IsContainer": False,
            "IsCorpse": False,
            "OnGround": False,
            "Name": "a katana",
            "Properties": ["a katana", "Luck 120", "Damage Increase 40%"],
            "Contains": [],
        }
        ground_corpse = {
            "Serial": 0x40000999,
            "SerialHex": "0x40000999",
            "ItemID": 0x2006,
            "Hue": 0,
            "Amount": 1,
            "Container": 0,
            "Layer": 0,
            "X": 1402,
            "Y": 1600,
            "Z": 0,
            "Map": 0,
            "Position": {"X": 1402, "Y": 1600, "Z": 0, "Map": 0},
            "IsContainer": True,
            "IsCorpse": True,
            "OnGround": True,
            "Name": "an orc corpse",
            "Properties": ["an orc corpse"],
            "Contains": [],
        }
        orc_mob = {
            "Serial": 0x00005001,
            "SerialHex": "0x00005001",
            "Body": 0x0011,
            "MobileID": 0x0011,
            "Hue": 0,
            "Name": "an orc",
            "Hits": 42,
            "HitsMax": 100,
            "Mana": 10,
            "ManaMax": 20,
            "Stam": 30,
            "StamMax": 50,
            "Notoriety": 3,
            "Direction": 2,
            "X": 1405,
            "Y": 1602,
            "Z": 0,
            "Map": 0,
            "Position": {"X": 1405, "Y": 1602, "Z": 0, "Map": 0},
            "Distance": 5,
            "Backpack": 0x40000501,
            "Poisoned": False,
            "Paralyzed": False,
            "Paralized": False,
            "YellowHits": False,
            "WarMode": True,
            "Hidden": False,
            "Dead": False,
            "IsHuman": False,
            "Female": False,
            "Properties": ["an orc"],
        }
        goblin_mob = dict(orc_mob, Serial=0x00005002, SerialHex="0x00005002", Name="a goblin", Hits=15, Distance=2)

        # Player
        if key == ("Player", "Hits"):
            return {"ok": True, "result": 95}
        if key == ("Player", "HitsMax"):
            return {"ok": True, "result": 100}
        if key == ("Player", "Mana"):
            return {"ok": True, "result": 55}
        if key == ("Player", "ManaMax"):
            return {"ok": True, "result": 80}
        if key == ("Player", "Stam"):
            return {"ok": True, "result": 70}
        if key == ("Player", "StamMax"):
            return {"ok": True, "result": 90}
        if key == ("Player", "Weight"):
            return {"ok": True, "result": 180}
        if key == ("Player", "MaxWeight"):
            return {"ok": True, "result": 400}
        if key == ("Player", "Gold"):
            return {"ok": True, "result": 12500}
        if key == ("Player", "Poisoned"):
            return {"ok": True, "result": False}
        if key == ("Player", "YellowHits"):
            return {"ok": True, "result": False}
        if key in (("Player", "Paralized"), ("Player", "Paralyzed")):
            return {"ok": True, "result": False}
        if key == ("Player", "WarMode"):
            return {"ok": True, "result": self.war_mode}
        if key == ("Player", "SetWarMode"):
            self.war_mode = bool(args[0])
            return {"ok": True, "result": self.war_mode}
        if key == ("Player", "Serial"):
            # QuickJS OF_TEXT_SERIAL returns hex string!
            return {"ok": True, "result": "0x00001234"}
        if key == ("Player", "Name"):
            return {"ok": True, "result": "Lord British"}
        if key == ("Player", "Position"):
            return {"ok": True, "result": {"X": 1400, "Y": 1600, "Z": 0, "Map": 0}}
        if key == ("Player", "Map"):
            return {"ok": True, "result": 0}
        if key == ("Player", "Backpack"):
            return {"ok": True, "result": backpack_item}
        if key == ("Player", "Dead"):
            return {"ok": True, "result": False}
        if key == ("Player", "Hidden"):
            return {"ok": True, "result": False}
        if key == ("Player", "InParty"):
            return {"ok": False, "error": "Player.InParty not registered", "interrupted": False}
        if key == ("Player", "GetLayerItem"):
            return {"ok": True, "result": katana_item if args and args[0] == "RightHand" else None}
        if key == ("Player", "BuffsExist"):
            return {"ok": True, "result": args[0] == "Bless"}
        if key == ("Player", "UseSkill"):
            return {"ok": True, "result": None}
        if key in (("Player", "Walk"), ("Player", "Run"), ("Player", "PathFindTo")):
            return {"ok": True, "result": True}

        # Client (delegated from Player.Connected / ChatSay / HeadMessage / Spells)
        if key == ("Client", "Connected"):
            return {"ok": True, "result": True}
        if key in (("Client", "Say"), ("Client", "CharPrint"), ("Client", "Cast")):
            return {"ok": True, "result": None}

        # Items
        if key == ("Items", "FindBySerial"):
            s = args[0]
            if s == 0x40000201:
                return {"ok": True, "result": katana_item}
            if s == 0x40000999:
                return {"ok": True, "result": ground_corpse}
            return {"ok": True, "result": None}
        if key == ("Items", "FindByID"):
            return {"ok": True, "result": katana_item}
        if key == ("Items", "FindAllByID"):
            container = args[2] if len(args) > 2 else -1
            if container == "ground":
                return {"ok": True, "result": [ground_corpse]}
            return {"ok": True, "result": [katana_item]}
        if key == ("Items", "BackpackCount"):
            return {"ok": True, "result": 7}
        if key in (
            ("Items", "UseItem"),
            ("Items", "UseItemByID"),
            ("Items", "Move"),
            ("Items", "MoveOnGround"),
            ("Items", "WaitForContents"),
        ):
            return {"ok": True, "result": True}
        if key == ("Items", "GetPropValue"):
            return {"ok": True, "result": 120.0 if args[1] == "Luck" else 0.0}
        if key == ("Items", "GetPropStringList"):
            return {"ok": True, "result": katana_item["Properties"]}

        # Mobiles
        if key == ("Mobiles", "FindBySerial"):
            return {"ok": True, "result": orc_mob if args[0] == 0x00005001 else None}
        if key == ("Mobiles", "ApplyFilter"):
            return {"ok": True, "result": [orc_mob, goblin_mob]}
        if key == ("Mobiles", "Select"):
            cands = args[0]
            sel = str(args[1]).lower() if len(args) > 1 else "nearest"
            if sel == "nearest":
                best = min(cands, key=lambda m: m.get("Distance", 999))
            elif sel == "weakest":
                best = min(cands, key=lambda m: m.get("Hits", 999))
            else:
                best = cands[0]
            return {"ok": True, "result": best}
        if key in (("Mobiles", "UseMobile"), ("Mobiles", "WaitForStats")):
            return {"ok": True, "result": True}

        # Target
        if key in (
            ("Target", "HasTarget"),
            ("Target", "WaitForTarget"),
            ("Target", "TargetExecute"),
            ("Target", "Self"),
            ("Target", "Last"),
            ("Target", "Cancel"),
        ):
            return {"ok": True, "result": True}
        if key == ("Target", "SetLast"):
            self.last_target = int(args[0])
            return {"ok": True, "result": True}
        if key == ("Target", "GetLast"):
            return {"ok": True, "result": self.last_target}
        if key == ("Target", "PromptTarget"):
            return {"ok": True, "result": 0x00005001}

        # Journal
        if key == ("Journal", "Search"):
            return {"ok": True, "result": any(args[0].lower() in line.lower() for line in self.journal)}
        if key == ("Journal", "SearchByName"):
            return {"ok": True, "result": True}
        if key == ("Journal", "WaitJournal"):
            return {"ok": True, "result": True}
        if key == ("Journal", "Clear"):
            self.journal.clear()
            return {"ok": True, "result": None}
        if key in (("Journal", "GetTextBySerial"), ("Journal", "GetTextByColor"), ("Journal", "GetTextByType")):
            return {"ok": True, "result": list(self.journal)}
        if key == ("Journal", "GetLineText"):
            for line in self.journal:
                if args[0].lower() in line.lower():
                    return {"ok": True, "result": line}
            return {"ok": True, "result": ""}

        # Gumps
        if key in (("Gumps", "HasGump"), ("Gumps", "WaitForGump"), ("Gumps", "SendAction"), ("Gumps", "SendAdvancedAction"), ("Gumps", "CloseGump")):
            return {"ok": True, "result": True}
        if key == ("Gumps", "CurrentGump"):
            return {"ok": True, "result": 0x38920ABD}
        if key == ("Gumps", "LastGumpGetLineList"):
            return {"ok": True, "result": list(self.gump_lines)}
        if key == ("Gumps", "LastGumpGetLine"):
            idx = int(args[0])
            return {"ok": True, "result": self.gump_lines[idx] if 0 <= idx < len(self.gump_lines) else ""}
        if key == ("Gumps", "LastGumpTextExist"):
            return {"ok": True, "result": any(args[0].lower() in l.lower() for l in self.gump_lines)}

        # Timer
        if key == ("Timer", "Create"):
            self.timers[str(args[0])] = int(args[1])
            return {"ok": True, "result": None}
        if key == ("Timer", "Check"):
            return {"ok": True, "result": str(args[0]) in self.timers}
        if key == ("Timer", "Remaining"):
            return {"ok": True, "result": self.timers.get(str(args[0]), 0)}
        if key == ("Timer", "Remove"):
            return {"ok": True, "result": self.timers.pop(str(args[0]), None) is not None}
        if key == ("Timer", "Clear"):
            self.timers.clear()
            return {"ok": True, "result": None}

        # Misc
        if key in (("Misc", "Pause"), ("Misc", "SendMessage"), ("Misc", "NoOperation")):
            return {"ok": True, "result": None}
        if key == ("Misc", "SetSharedValue"):
            self.shared[str(args[0])] = args[1]
            return {"ok": True, "result": None}
        if key == ("Misc", "ReadSharedValue"):
            return {"ok": True, "result": self.shared.get(str(args[0]), None)}
        if key == ("Misc", "CheckSharedValue"):
            return {"ok": True, "result": str(args[0]) in self.shared}
        if key == ("Misc", "RemoveSharedValue"):
            return {"ok": True, "result": self.shared.pop(str(args[0]), None) is not None}
        if key == ("Misc", "AllSharedValue"):
            return {"ok": True, "result": sorted(self.shared.keys())}
        if key == ("Misc", "IgnoreObject"):
            self.ignored.add(int(args[0]))
            return {"ok": True, "result": None}
        if key == ("Misc", "UnIgnoreObject"):
            self.ignored.discard(int(args[0]))
            return {"ok": True, "result": None}
        if key == ("Misc", "CheckIgnoreObject"):
            return {"ok": True, "result": int(args[0]) in self.ignored}
        if key == ("Misc", "ClearIgnore"):
            self.ignored.clear()
            return {"ok": True, "result": None}
        if key == ("Misc", "CurrentScriptDirectory"):
            return {"ok": True, "result": "/tmp/mock-scripts"}
        if key == ("Misc", "ScriptStatus"):
            return {"ok": True, "result": args[0] == "active.cs"}

        # Agents
        if key == ("AutoLoot", "Start"):
            self.autoloot_running = True
            return {"ok": True, "result": True}
        if key == ("AutoLoot", "Stop"):
            self.autoloot_running = False
            return {"ok": True, "result": True}
        if key == ("AutoLoot", "Status"):
            return {"ok": True, "result": self.autoloot_running}
        if key == ("AutoLoot", "RunOnce"):
            return {"ok": True, "result": True}
        if key == ("AutoLoot", "ChangeList"):
            return {"ok": True, "result": None}

        if key == ("BandageHeal", "Start"):
            self.bandage_running = True
            return {"ok": True, "result": True}
        if key == ("BandageHeal", "Stop"):
            self.bandage_running = False
            return {"ok": True, "result": True}
        if key == ("BandageHeal", "Status"):
            return {"ok": True, "result": self.bandage_running}

        if ns in ("Organizer", "Restock", "Scavenger"):
            if fn in ("ChangeList", "ResetIgnore"):
                return {"ok": True, "result": None}
            return {"ok": True, "result": True}

        # PathFinding
        if key in (("PathFinding", "Go"), ("PathFinding", "RunRoute"), ("PathFinding", "PathFindTo"), ("PathFinding", "IsWalking")):
            return {"ok": True, "result": True}
        if key == ("PathFinding", "Stop"):
            return {"ok": True, "result": None}

        return {"ok": False, "error": f"Unhandled mock RPC {ns}.{fn}", "interrupted": False}


class CSharpRuntimeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.assertTrue(RUNNER_SCRIPT.is_file(), f"Missing runner: {RUNNER_SCRIPT}")
        self.assertTrue(
            WORKSPACE_DOTNET.is_file(),
            f"Workspace dotnet SDK expected at {WORKSPACE_DOTNET}",
        )
        self.cache_tmp = tempfile.TemporaryDirectory(prefix="xuo-cs-test-cache-")
        self.cache_dir = Path(self.cache_tmp.name)

    def tearDown(self) -> None:
        self.cache_tmp.cleanup()

    def test_full_razor_enhanced_api_script(self) -> None:
        script = r"""
namespace RazorEnhanced
{
    public class Script
    {
        public void Run()
        {
            // Player properties & methods
            if (Player.Hits != 95 || Player.HitsMax != 100) throw new Exception("Hits mismatch");
            if (Player.Mana != 55 || Player.ManaMax != 80) throw new Exception("Mana mismatch");
            if (Player.Stam != 70 || Player.StamMax != 90) throw new Exception("Stam mismatch");
            if (Player.Weight != 180 || Player.MaxWeight != 400) throw new Exception("Weight mismatch");
            if (Player.Gold != 12500) throw new Exception("Gold mismatch");
            if (Player.Poisoned || Player.YellowHits || Player.Paralized || Player.Paralyzed) throw new Exception("Flag mismatch");
            if (Player.Serial != 0x00001234) throw new Exception($"Serial mismatch: {Player.Serial:X}");
            if (Player.Name != "Lord British") throw new Exception("Name mismatch");
            var pos = Player.Position;
            if (pos.X != 1400 || pos.Y != 1600 || pos.Z != 0) throw new Exception("Position mismatch");
            if (Player.Map != 0 || Player.IsGhost || !Player.Visible || !Player.Connected) throw new Exception("State mismatch");
            if (Player.InParty) throw new Exception("InParty fallback should be false");
            var bp = Player.Backpack;
            if (bp == null || bp.Serial != 0x40000100 || !bp.IsContainer) throw new Exception("Backpack mismatch");
            var weapon = Player.GetItemOnLayer("RightHand");
            if (weapon == null || weapon.ItemID != 0x13FF) throw new Exception("GetItemOnLayer mismatch");
            if (!Player.BuffsExist("Bless")) throw new Exception("BuffsExist mismatch");
            Player.UseSkill("Meditation");
            Player.SetWarMode(true);
            if (!Player.WarMode) throw new Exception("WarMode did not toggle");
            if (!Player.Walk("North") || !Player.Run("East") || !Player.PathFindTo(1410, 1610, 0))
                throw new Exception("Movement mismatch");
            Player.ChatSay(0x0044, "Hail Britannia");
            Player.HeadMessage(0x0035, "Ready");

            // Items API + Items.Filter + Items.Select + Property
            var found = Items.FindBySerial(0x40000201);
            if (found == null || found.Properties.Count < 2) throw new Exception("FindBySerial mismatch");
            string firstProp = found.Properties[0];
            if (firstProp != "a katana") throw new Exception("Property conversion mismatch");
            var byId = Items.FindByID(0x13FF, -1, bp.Serial, true);
            if (byId == null || byId.Serial != 0x40000201) throw new Exception("FindByID mismatch");
            var allById = Items.FindAllByID(0x13FF);
            if (allById.Count != 1) throw new Exception("FindAllByID mismatch");
            if (Items.BackpackCount(0x0E21) != 7) throw new Exception("BackpackCount mismatch");
            Items.UseItem(found);
            Items.UseItemByID(0x0E21);
            Items.Move(found, bp, 1);
            Items.MoveOnGround(found, 1, 1401, 1601, 0);
            if (Items.GetPropValue(found, "Luck") != 120.0) throw new Exception("GetPropValue mismatch");
            if (Items.GetPropStringList(found).Count != 3) throw new Exception("GetPropStringList mismatch");
            if (!Items.WaitForContents(bp, 500)) throw new Exception("WaitForContents mismatch");

            var itemFilter = new Items.Filter();
            itemFilter.Enabled = true;
            itemFilter.OnGround = 1;
            itemFilter.IsCorpse = 1;
            itemFilter.RangeMax = 5;
            var corpses = Items.ApplyFilter(itemFilter);
            if (corpses.Count != 1 || corpses[0].Serial != 0x40000999) throw new Exception("Items.ApplyFilter mismatch");
            var nearestCorpse = Items.Select(corpses, "Nearest");
            if (nearestCorpse == null || nearestCorpse.Serial != 0x40000999) throw new Exception("Items.Select mismatch");

            // Mobiles API + Mobiles.Filter + Select
            var mobFilter = new Mobiles.Filter();
            mobFilter.Enabled = true;
            mobFilter.RangeMax = 10;
            mobFilter.Notorieties = new List<byte> { 3, 4, 5, 6 };
            mobFilter.IsHuman = 0;
            var mobs = Mobiles.ApplyFilter(mobFilter);
            if (mobs.Count != 2) throw new Exception("Mobiles.ApplyFilter count mismatch");
            var nearestMob = Mobiles.Select(mobs, "Nearest");
            if (nearestMob == null || nearestMob.Serial != 0x00005002) throw new Exception("Mobiles.Select mismatch");
            var orc = Mobiles.FindBySerial(0x00005001);
            if (orc == null || orc.Name != "an orc") throw new Exception("Mobiles.FindBySerial mismatch");
            Mobiles.UseMobile(orc);
            Mobiles.WaitForStats(orc, 500);

            // Target API
            if (!Target.HasTarget() || !Target.WaitForTarget(500)) throw new Exception("Target wait mismatch");
            Target.TargetExecute(orc);
            Target.TargetExecute(1400, 1600, 0);
            Target.Self();
            Target.SetLast(orc);
            if (Target.GetLast() != 0x00005001) throw new Exception("Target.GetLast mismatch");
            Target.Last();
            Target.Cancel();
            if (Target.PromptTarget("Pick foe") != 0x00005001) throw new Exception("PromptTarget mismatch");

            // Journal API
            if (!Journal.Search("orc")) throw new Exception("Journal.Search mismatch");
            if (!Journal.SearchByName("orc", "System")) throw new Exception("Journal.SearchByName mismatch");
            if (!Journal.WaitJournal("Britannia", 200)) throw new Exception("Journal.WaitJournal mismatch");
            if (!Journal.GetLineText("Britannia").Contains("Britannia")) throw new Exception("Journal.GetLineText mismatch");
            if (Journal.GetTextBySerial(0).Count == 0) throw new Exception("Journal.GetTextBySerial mismatch");
            if (Journal.GetTextByColor(0).Count == 0) throw new Exception("Journal.GetTextByColor mismatch");
            if (Journal.GetTextByType("System").Count == 0) throw new Exception("Journal.GetTextByType mismatch");
            Journal.Clear();
            if (Journal.Search("orc")) throw new Exception("Journal.Clear did not clear");

            // Gumps API
            if (!Gumps.HasGump() || Gumps.CurrentGump() != 0x38920ABD) throw new Exception("Gumps id mismatch");
            if (!Gumps.WaitForGump(0x38920ABD, 500)) throw new Exception("WaitForGump mismatch");
            if (!Gumps.LastGumpTextExist("Katana")) throw new Exception("LastGumpTextExist mismatch");
            if (Gumps.LastGumpGetLine(1) != "Katana") throw new Exception("LastGumpGetLine mismatch");
            if (Gumps.LastGumpGetLineList().Count != 3) throw new Exception("LastGumpGetLineList mismatch");
            Gumps.SendAction(0x38920ABD, 21);
            Gumps.SendAdvancedAction(0x38920ABD, 21, new List<int> { 1 }, new List<int> { 0 }, new List<string> { "hello" });
            Gumps.CloseGump(0x38920ABD);

            // Timer API
            Timer.Create("heal_cd", 2500);
            if (!Timer.Check("heal_cd") || Timer.Remaining("heal_cd") != 2500) throw new Exception("Timer mismatch");
            if (!Timer.Remove("heal_cd") || Timer.Check("heal_cd")) throw new Exception("Timer.Remove mismatch");
            Timer.Clear();

            // Misc API
            Misc.Pause(5);
            Misc.SendMessage("All systems nominal", 68);
            Misc.NoOperation();
            Misc.SetSharedValue("kills", 42);
            if (!Misc.CheckSharedValue("kills") || (int)Misc.ReadSharedValue("kills") != 42)
                throw new Exception("SharedValue mismatch");
            if (!Misc.AllSharedValue().Contains("kills")) throw new Exception("AllSharedValue mismatch");
            if (!Misc.RemoveSharedValue("kills") || Misc.CheckSharedValue("kills"))
                throw new Exception("RemoveSharedValue mismatch");
            Misc.IgnoreObject(0x40000999);
            if (!Misc.CheckIgnoreObject(0x40000999)) throw new Exception("IgnoreObject mismatch");
            Misc.UnIgnoreObject(0x40000999);
            if (Misc.CheckIgnoreObject(0x40000999)) throw new Exception("UnIgnoreObject mismatch");
            Misc.ClearIgnore();
            if (Misc.CurrentScriptDirectory() != "/tmp/mock-scripts") throw new Exception("ScriptDir mismatch");
            if (!Misc.ScriptStatus("active.cs")) throw new Exception("ScriptStatus mismatch");

            // Agents
            AutoLoot.ChangeList("default");
            AutoLoot.Start();
            if (!AutoLoot.Status()) throw new Exception("AutoLoot.Status mismatch");
            AutoLoot.RunOnce();
            AutoLoot.Stop();

            BandageHeal.Start();
            if (!BandageHeal.Status()) throw new Exception("BandageHeal.Status mismatch");
            BandageHeal.Stop();

            Organizer.ChangeList("gems");
            Organizer.FStart();
            Organizer.FStop();
            Restock.ChangeList("regs");
            Restock.FStart();
            Restock.FStop();
            Scavenger.ChangeList("gold");
            Scavenger.Start();
            Scavenger.ResetIgnore();
            Scavenger.Stop();

            // PathFinding + Route
            var route = new PathFinding.Route();
            route.X = 1420;
            route.Y = 1620;
            route.Distance = 2;
            route.StopIfStuck = true;
            route.MaxRetry = 2;
            route.AddWaypoint(1410, 1610, 0);
            if (!PathFinding.Go(route)) throw new Exception("PathFinding.Go mismatch");
            if (!PathFinding.RunRoute(route)) throw new Exception("PathFinding.RunRoute mismatch");
            PathFinding.Stop();

            // Spells
            Spells.Cast("Greater Heal");
            Spells.CastMagery("Energy Bolt", orc);
        }
    }
}
"""
        state = MockGameState()
        rc, reqs, _, stderr, _ = run_cs_with_mock_rpc(script, state.handle, self.cache_dir)
        self.assertEqual(rc, 0, f"Script failed with stderr:\n{stderr}")
        self.assertEqual(stderr.strip(), "", f"Unexpected stderr:\n{stderr}")

        called_pairs = {(r["ns"], r["fn"]) for r in reqs}
        expected_subset = {
            ("Player", "Hits"),
            ("Player", "GetLayerItem"),
            ("Player", "SetWarMode"),
            ("Client", "Say"),
            ("Client", "CharPrint"),
            ("Client", "Cast"),
            ("Items", "FindBySerial"),
            ("Items", "FindAllByID"),
            ("Mobiles", "ApplyFilter"),
            ("Mobiles", "Select"),
            ("Target", "PromptTarget"),
            ("Journal", "Search"),
            ("Gumps", "SendAdvancedAction"),
            ("Timer", "Create"),
            ("Misc", "SetSharedValue"),
            ("AutoLoot", "Start"),
            ("BandageHeal", "Start"),
            ("PathFinding", "Go"),
            ("PathFinding", "RunRoute"),
            ("PathFinding", "Stop"),
        }
        self.assertTrue(
            expected_subset.issubset(called_pairs),
            f"Missing RPC calls: {expected_subset - called_pairs}",
        )

        # Verify PathFinding.Route serialized waypoints cleanly
        pf_go = next(r for r in reqs if r["ns"] == "PathFinding" and r["fn"] == "Go")
        route_arg = pf_go["args"][0]
        self.assertEqual(route_arg["X"], 1420)
        self.assertEqual(route_arg["Y"], 1620)
        self.assertEqual(route_arg["Waypoints"][0]["X"], 1410)

        # Verify Mobiles.Filter serialized Notorieties as int array
        mob_apply = next(r for r in reqs if r["ns"] == "Mobiles" and r["fn"] == "ApplyFilter")
        self.assertEqual(mob_apply["args"][0]["Notorieties"], [3, 4, 5, 6])

    def test_script_without_namespace_and_with_args(self) -> None:
        script = r"""
public class CustomAutoRunner
{
    public void Run(string[] args)
    {
        if (args.Length != 2 || args[0] != "alpha" || args[1] != "beta")
        {
            throw new Exception($"Unexpected args: {string.Join(",", args)}");
        }
        Misc.SendMessage($"Hello {Player.Name}: {args[0]}/{args[1]}");
    }
}
"""
        state = MockGameState()
        rc, reqs, _, stderr, _ = run_cs_with_mock_rpc(
            script, state.handle, self.cache_dir, script_args=["alpha", "beta"]
        )
        self.assertEqual(rc, 0, f"Script failed:\n{stderr}")
        msg_call = next(r for r in reqs if r["ns"] == "Misc" and r["fn"] == "SendMessage")
        self.assertEqual(msg_call["args"][0], "Hello Lord British: alpha/beta")

    def test_assembly_cache_hit_and_recompile_on_change(self) -> None:
        state = MockGameState()
        script_v1 = r"""
namespace RazorEnhanced
{
    public class Script
    {
        public void Run()
        {
            Misc.SendMessage("v1-" + Player.Hits);
        }
    }
}
"""
        script_v2 = r"""
namespace RazorEnhanced
{
    public class Script
    {
        public void Run()
        {
            Misc.SendMessage("v2-" + Player.Hits);
        }
    }
}
"""
        with tempfile.TemporaryDirectory(prefix="xuo-cs-cache-script-") as tmp:
            script_path = Path(tmp) / "cached.cs"
            script_path.write_text(script_v1, encoding="utf-8")

            # First run: cache miss (compiles)
            t0 = time.monotonic()
            rc1, reqs1, _, stderr1, _ = run_cs_file_with_mock_rpc(
                script_path, state.handle, self.cache_dir
            )
            t1 = time.monotonic()
            self.assertEqual(rc1, 0, stderr1)
            msg1 = next(r for r in reqs1 if r["ns"] == "Misc" and r["fn"] == "SendMessage")
            self.assertEqual(msg1["args"][0], "v1-95")

            entries_after_1 = [p for p in self.cache_dir.iterdir() if p.is_dir() and not p.name.startswith(".tmp-")]
            self.assertEqual(len(entries_after_1), 1)
            dll_v1 = entries_after_1[0] / "ScriptAssembly.dll"
            self.assertTrue(dll_v1.is_file())
            stat_before = dll_v1.stat()

            # Second run: cache hit (must NOT recompile or replace DLL!)
            t2 = time.monotonic()
            rc2, reqs2, _, stderr2, _ = run_cs_file_with_mock_rpc(
                script_path, state.handle, self.cache_dir
            )
            t3 = time.monotonic()
            self.assertEqual(rc2, 0, stderr2)
            msg2 = next(r for r in reqs2 if r["ns"] == "Misc" and r["fn"] == "SendMessage")
            self.assertEqual(msg2["args"][0], "v1-95")

            stat_after = dll_v1.stat()
            self.assertEqual(stat_before.st_ino, stat_after.st_ino)
            self.assertEqual(stat_before.st_mtime_ns, stat_after.st_mtime_ns)
            self.assertLess(
                t3 - t2,
                max(t1 - t0, 1.5),
                f"Expected cache hit ({t3 - t2:.3f}s) to be faster than cold compile ({t1 - t0:.3f}s)",
            )

            # Third run: modify source -> must recompile to a new cache entry
            script_path.write_text(script_v2, encoding="utf-8")
            rc3, reqs3, _, stderr3, _ = run_cs_file_with_mock_rpc(
                script_path, state.handle, self.cache_dir
            )
            self.assertEqual(rc3, 0, stderr3)
            msg3 = next(r for r in reqs3 if r["ns"] == "Misc" and r["fn"] == "SendMessage")
            self.assertEqual(msg3["args"][0], "v2-95")

            entries_after_3 = [p for p in self.cache_dir.iterdir() if p.is_dir() and not p.name.startswith(".tmp-")]
            self.assertEqual(len(entries_after_3), 2)

    def test_console_writeline_stdout_isolation(self) -> None:
        script = r"""
namespace RazorEnhanced
{
    public class Script
    {
        public void Run()
        {
            Console.WriteLine("USER_LOG_BEFORE_RPC");
            int hits = Player.Hits;
            Console.WriteLine($"USER_LOG_HITS={hits}");
            Misc.SendMessage("after_console_write");
            Console.Write("USER_LOG_NO_NEWLINE\n");
        }
    }
}
"""
        state = MockGameState()
        rc, reqs, raw_stdout, stderr, _ = run_cs_with_mock_rpc(script, state.handle, self.cache_dir)
        self.assertEqual(rc, 0, f"Unexpected failure:\n{stderr}")

        # Every single line on stdout MUST be valid JSON-RPC with no user print leakage
        self.assertGreaterEqual(len(raw_stdout), 2)
        for line in raw_stdout:
            parsed = json.loads(line)
            self.assertIn("id", parsed)
            self.assertIn("ns", parsed)
            self.assertIn("fn", parsed)
            self.assertNotIn("USER_LOG", line)

        # User Console.WriteLine output must have been redirected cleanly to stderr
        self.assertIn("USER_LOG_BEFORE_RPC", stderr)
        self.assertIn("USER_LOG_HITS=95", stderr)
        self.assertIn("USER_LOG_NO_NEWLINE", stderr)

    def test_compile_error_diagnostics_to_stderr_only(self) -> None:
        bad_script = r"""
namespace RazorEnhanced
{
    public class Script
    {
        public void Run()
        {
            int broken = "this is not an int";
            NonExistentType.Boom();
        }
    }
}
"""
        state = MockGameState()
        rc, reqs, raw_stdout, stderr, _ = run_cs_with_mock_rpc(
            bad_script, state.handle, self.cache_dir
        )
        self.assertNotEqual(rc, 0, "Compile error must exit non-zero")
        self.assertEqual(raw_stdout, [], f"Compile failure must not write to stdout: {raw_stdout!r}")
        self.assertEqual(reqs, [])
        self.assertIn("CS0029", stderr)
        self.assertIn("CS0103", stderr)

    def test_interrupted_true_clean_abort(self) -> None:
        script = r"""
namespace RazorEnhanced
{
    public class Script
    {
        public void Run()
        {
            Misc.SendMessage("before_interrupt");
            while (true)
            {
                Misc.Pause(50);
            }
        }
    }
}
"""
        calls = {"count": 0}

        def interrupt_handler(ns: str, fn: str, args: List[Any], req_id: int) -> Dict[str, Any]:
            calls["count"] += 1
            if fn == "Pause":
                return {"ok": False, "error": "interrupted", "interrupted": True}
            return {"ok": True, "result": None}

        rc, reqs, _, stderr, _ = run_cs_with_mock_rpc(script, interrupt_handler, self.cache_dir)
        self.assertEqual(rc, 0, f"Interrupted script must exit cleanly with 0, stderr:\n{stderr}")
        self.assertEqual(stderr.strip(), "", f"Clean interrupt should not emit stderr:\n{stderr}")
        self.assertEqual(calls["count"], 2)
        self.assertEqual([r["fn"] for r in reqs], ["SendMessage", "Pause"])

    def test_sigterm_clean_termination(self) -> None:
        script = r"""
namespace RazorEnhanced
{
    public class Script
    {
        public void Run()
        {
            Misc.SendMessage("ready_for_sigterm");
            Thread.Sleep(30000);
        }
    }
}
"""
        with tempfile.TemporaryDirectory(prefix="xuo-cs-sigterm-") as tmp:
            script_path = Path(tmp) / "sigterm_test.cs"
            script_path.write_text(script, encoding="utf-8")

            env = os.environ.copy()
            env["XUO_SCRIPT_PATH"] = str(script_path)
            env["XUO_SCRIPT_DIR"] = str(script_path.parent)
            env["XUO_SCRIPT_NAME"] = script_path.name
            env["XUO_SCRIPT_ARGS_JSON"] = "[]"
            env["XUO_RUNTIME_DIR"] = str(CSHARP_RUNTIME_DIR.parent)
            env["XUO_CACHE_DIR"] = str(self.cache_dir)
            if WORKSPACE_DOTNET.is_file():
                env["XUO_DOTNET_BIN"] = str(WORKSPACE_DOTNET)

            proc = subprocess.Popen(
                [sys.executable, str(RUNNER_SCRIPT)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                env=env,
                bufsize=1,
            )
            assert proc.stdout is not None and proc.stdin is not None
            first_line = proc.stdout.readline()
            req = json.loads(first_line)
            self.assertEqual(req["fn"], "SendMessage")
            proc.stdin.write(json.dumps({"id": req["id"], "ok": True, "result": None}) + "\n")
            proc.stdin.flush()

            # Send SIGTERM directly to the exec'd .NET host process
            proc.send_signal(signal.SIGTERM)
            rc = proc.wait(timeout=10.0)
            proc.stdin.close()
            proc.stdout.close()
            assert proc.stderr is not None
            proc.stderr.close()
            # Clean exit via PosixSignalRegistration (0) or standard SIGTERM (-15 / 143)
            self.assertIn(rc, (0, -signal.SIGTERM, 128 + signal.SIGTERM))

    def test_paths_with_spaces_and_concurrent_cache_compile(self) -> None:
        script = r"""
namespace RazorEnhanced
{
    public class Script
    {
        public void Run()
        {
            Misc.SendMessage("concurrent-" + Player.Hits);
        }
    }
}
"""
        with tempfile.TemporaryDirectory(prefix="xuo cs spaces ") as tmp:
            spaced_cache = Path(tmp) / "cache dir with spaces"
            script_path = Path(tmp) / "my script with spaces.cs"
            script_path.write_text(script, encoding="utf-8")

            results: List[Tuple[int, str, str]] = []
            errors: List[BaseException] = []
            lock = threading.Lock()

            def worker() -> None:
                try:
                    state = MockGameState()
                    rc, reqs, _, stderr, _ = run_cs_file_with_mock_rpc(
                        script_path, state.handle, spaced_cache
                    )
                    msg = next(
                        (r["args"][0] for r in reqs if r["ns"] == "Misc" and r["fn"] == "SendMessage"),
                        "",
                    )
                    with lock:
                        results.append((rc, msg, stderr))
                except BaseException as exc:
                    with lock:
                        errors.append(exc)

            t1 = threading.Thread(target=worker)
            t2 = threading.Thread(target=worker)
            t1.start()
            t2.start()
            t1.join(timeout=30.0)
            t2.join(timeout=30.0)

            self.assertEqual(errors, [])
            self.assertEqual(len(results), 2)
            for rc, msg, stderr in results:
                self.assertEqual(rc, 0, stderr)
                self.assertEqual(msg, "concurrent-95")

            # Verify no lingering temp directories and exactly one published cache entry
            entries = [p for p in spaced_cache.iterdir() if p.is_dir()]
            self.assertEqual(len(entries), 1)
            self.assertFalse(entries[0].name.startswith(".tmp-"))


if __name__ == "__main__":
    unittest.main()
