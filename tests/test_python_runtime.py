#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Standalone tests for the CrossUO Razor Enhanced Python script runtime."""

from __future__ import annotations

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
from typing import Any, Callable, Optional

REPO_ROOT = Path(__file__).resolve().parents[1]
HOST_PATH = REPO_ROOT / "src" / "Assistant" / "script" / "runtime" / "python_host.py"


class MockRpcSession:
    """Runs `python_host.py` over real stdin/stdout pipes against a mock RPC handler."""

    def __init__(
        self,
        script_path: Path,
        handler: Callable[[str, str, list[Any], int], dict[str, Any]],
        extra_env: Optional[dict[str, str]] = None,
        args: Optional[list[str]] = None,
    ) -> None:
        self.script_path = script_path
        self.handler = handler
        self.calls: list[tuple[str, str, list[Any]]] = []
        self.raw_stdout_lines: list[str] = []
        self.stderr_text: str = ""
        self.returncode: Optional[int] = None
        self.proc: Optional[subprocess.Popen[str]] = None
        self._extra_env = extra_env or {}
        self._args = args or []
        self._stderr_chunks: list[str] = []
        self._server_error: Optional[BaseException] = None

    def start(self) -> None:
        env = os.environ.copy()
        env["XUO_SCRIPT_PATH"] = str(self.script_path)
        env["XUO_SCRIPT_DIR"] = str(self.script_path.parent)
        env["XUO_SCRIPT_NAME"] = self.script_path.name
        env["XUO_SCRIPT_ARGS_JSON"] = json.dumps(self._args)
        env.update(self._extra_env)

        self.proc = subprocess.Popen(
            [sys.executable, str(HOST_PATH)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
            env=env,
        )

        self._stderr_thread = threading.Thread(target=self._drain_stderr, daemon=True)
        self._stderr_thread.start()
        self._rpc_thread = threading.Thread(target=self._serve_rpc, daemon=True)
        self._rpc_thread.start()

    def _drain_stderr(self) -> None:
        assert self.proc is not None and self.proc.stderr is not None
        for line in self.proc.stderr:
            self._stderr_chunks.append(line)

    def _serve_rpc(self) -> None:
        assert self.proc is not None and self.proc.stdout is not None and self.proc.stdin is not None
        try:
            for line in self.proc.stdout:
                self.raw_stdout_lines.append(line)
                req = json.loads(line)
                req_id = req["id"]
                ns = req["ns"]
                fn = req["fn"]
                args = req["args"]
                self.calls.append((ns, fn, args))
                resp = self.handler(ns, fn, args, req_id)
                resp.setdefault("id", req_id)
                wire = json.dumps(resp, separators=(",", ":")) + "\n"
                try:
                    self.proc.stdin.write(wire)
                    self.proc.stdin.flush()
                except (BrokenPipeError, ValueError, OSError):
                    break
        except BaseException as exc:
            self._server_error = exc
        finally:
            try:
                self.proc.stdin.close()
            except Exception:
                pass

    def wait(self, timeout: float = 10.0) -> int:
        assert self.proc is not None
        try:
            rc = self.proc.wait(timeout=timeout)
            self._rpc_thread.join(timeout=timeout)
            self._stderr_thread.join(timeout=timeout)
        finally:
            for stream in (self.proc.stdin, self.proc.stdout, self.proc.stderr):
                if stream is not None:
                    try:
                        stream.close()
                    except Exception:
                        pass
        self.returncode = rc
        self.stderr_text = "".join(self._stderr_chunks)
        if self._server_error is not None:
            raise self._server_error
        return rc


class TestPythonRuntime(unittest.TestCase):
    def setUp(self) -> None:
        self._tmpdir = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmpdir.name)

    def tearDown(self) -> None:
        self._tmpdir.cleanup()

    def _write_script(self, name: str, source: str) -> Path:
        path = self.tmp / name
        path.write_text(source, encoding="utf-8")
        return path

    def test_player_properties_and_methods(self) -> None:
        script = self._write_script(
            "player_test.py",
            """
assert Player.Hits == 85
assert Player.HitsMax == 100
assert Player.Mana == 40
assert Player.ManaMax == 90
assert Player.Stam == 70
assert Player.StamMax == 80
assert Player.Weight == 210
assert Player.MaxWeight == 400
assert Player.Gold == 12500
assert Player.Poisoned is True
assert Player.YellowHits is False
assert Player.Paralized is False
assert Player.Paralyzed is False
assert Player.WarMode is True
assert Player.Serial == 0x00001234
assert Player.Name == "Arianna"
assert Player.Position.X == 1420
assert Player.Position.Y == 1690
assert Player.Position.Z == 5
assert Player.Map == 1
assert Player.Backpack.Serial == 0x40000010
assert Player.Backpack.Contains[0].ItemID == 0x0E21
assert Player.IsGhost is False
assert Player.InParty is False
assert Player.Visible is True
assert Player.Connected is True

weapon = Player.GetItemOnLayer("RightHand")
assert weapon is not None and weapon.Serial == 0x40000099 and weapon.ItemID == 0x0F5E
assert Player.BuffsExist("Bless") is True
Player.UseSkill("Meditation")
Player.UseSkill("Anatomy", 0x00005555)
assert Player.SetWarMode(False) is False
assert Player.Walk("North") is True
assert Player.Run("East") is True
assert Player.PathFindTo(1430, 1700, 0) is True
Player.ChatSay(33, "Hello Britannia")
Player.ChatSay("Plain chat")
Player.HeadMessage(68, "Heads up!")
""",
        )

        def handler(ns: str, fn: str, args: list[Any], req_id: int) -> dict[str, Any]:
            table: dict[tuple[str, str], Any] = {
                ("Player", "Hits"): 85,
                ("Player", "HitsMax"): 100,
                ("Player", "Mana"): 40,
                ("Player", "ManaMax"): 90,
                ("Player", "Stam"): 70,
                ("Player", "StamMax"): 80,
                ("Player", "Weight"): 210,
                ("Player", "MaxWeight"): 400,
                ("Player", "Gold"): 12500,
                ("Player", "Poisoned"): True,
                ("Player", "YellowHits"): False,
                ("Player", "Paralized"): False,
                ("Player", "Paralyzed"): False,
                ("Player", "WarMode"): True,
                ("Player", "Serial"): "0x00001234",
                ("Player", "Name"): "Arianna",
                ("Player", "Position"): {"X": 1420, "Y": 1690, "Z": 5, "Map": 1},
                ("Player", "Map"): 1,
                ("Player", "Backpack"): {
                    "Serial": 0x40000010,
                    "ItemID": 0x0E75,
                    "Hue": 0,
                    "Amount": 1,
                    "Name": "a backpack",
                    "Container": 0x00001234,
                    "Layer": 21,
                    "IsContainer": True,
                    "Contains": [
                        {
                            "Serial": 0x40000011,
                            "ItemID": 0x0E21,
                            "Hue": 0,
                            "Amount": 50,
                            "Name": "clean bandages",
                            "Container": 0x40000010,
                        }
                    ],
                },
                ("Player", "Dead"): False,
                ("Player", "Hidden"): False,
                ("Client", "Connected"): True,
                ("Player", "GetLayerItem"): {
                    "Serial": 0x40000099,
                    "ItemID": 0x0F5E,
                    "Hue": 0,
                    "Amount": 1,
                    "Layer": 1,
                    "Name": "a broadsword",
                },
                ("Player", "BuffsExist"): args == ["Bless"],
                ("Player", "UseSkill"): None,
                ("Player", "SetWarMode"): args[0] if args else False,
                ("Player", "Walk"): True,
                ("Player", "Run"): True,
                ("Player", "PathFindTo"): True,
                ("Client", "Say"): None,
                ("Client", "CharPrint"): None,
            }
            if (ns, fn) not in table:
                return {"ok": False, "error": f"unexpected {ns}.{fn}", "interrupted": False}
            return {"ok": True, "result": table[(ns, fn)]}

        session = MockRpcSession(script, handler)
        session.start()
        rc = session.wait()
        self.assertEqual(rc, 0, msg=session.stderr_text)
        self.assertIn(("Client", "Say", ["Hello Britannia", 33]), session.calls)
        self.assertIn(("Client", "Say", ["Plain chat"]), session.calls)
        self.assertIn(("Client", "CharPrint", ["self", "0x0044", "Heads up!"]), session.calls)

    def test_ironpython_shims_and_mobiles_filter(self) -> None:
        script = self._write_script(
            "mobiles_test.py",
            """
import clr
clr.AddReference("System.Core")
clr.AddReferenceToFile("RazorEnhanced.dll")
from System.Collections.Generic import List
from System import Byte, Int32, UInt32, Int64, String, Boolean, Array
from RazorEnhanced import Mobiles, Player

arr = Array[Int32]([10, 20, 30])
assert list(arr) == [10, 20, 30] and arr.Length == 3
assert Byte(259) == 3
assert Int32(-5) == -5
assert UInt32(0x40000001) == 0x40000001
assert Int64(10**12) == 10**12
assert String("ok") == "ok"
assert Boolean(1) is True

f = Mobiles.Filter()
f.Enabled = True
f.RangeMin = 0
f.RangeMax = 10
f.Notorieties = List[Byte]([3, 4])
f.Bodies = List[Int32]([0x0190, 0x0191])
f.Bodies.Add(0x0033)
assert f.Bodies.Count == 3
f.Poisoned = False
f.IsHuman = True
f.IsGhost = False
f.Warmode = -1
f.Name = "orc"
f.CheckLineOfSight = True
f.CheckIgnoreObject = True

found = Mobiles.ApplyFilter(f)
assert len(found) == 2
nearest = Mobiles.Select(found, "Nearest")
assert nearest is not None
assert nearest.Serial == 0x00002001
assert nearest.MobileID == 0x0033
assert nearest.Body == 0x0033
assert nearest.Paralized is True
assert nearest.Paralyzed is True
assert nearest.IsGhost is False
assert nearest.DistanceTo(Player.Position) == 2

by_serial = Mobiles.FindBySerial(0x00002001)
assert by_serial is not None and by_serial.Name == "an orc"
assert Mobiles.UseMobile(nearest) is True
assert Mobiles.WaitForStats(nearest, 500) is True
""",
        )

        mobs = [
            {
                "Serial": 0x00002002,
                "Body": 0x0033,
                "Hue": 0,
                "Hits": 80,
                "HitsMax": 100,
                "Mana": 10,
                "ManaMax": 10,
                "Stam": 40,
                "StamMax": 50,
                "Name": "an orc brute",
                "Notoriety": 3,
                "Poisoned": False,
                "Paralyzed": False,
                "YellowHits": False,
                "WarMode": True,
                "IsHuman": False,
                "Dead": False,
                "X": 1428,
                "Y": 1690,
                "Z": 0,
                "Map": 0,
                "Distance": 8,
            },
            {
                "Serial": 0x00002001,
                "Body": 0x0033,
                "Hue": 0,
                "Hits": 50,
                "HitsMax": 100,
                "Mana": 0,
                "ManaMax": 0,
                "Stam": 30,
                "StamMax": 50,
                "Name": "an orc",
                "Notoriety": 4,
                "Poisoned": False,
                "Paralyzed": True,
                "YellowHits": False,
                "WarMode": True,
                "IsHuman": False,
                "Dead": False,
                "X": 1422,
                "Y": 1690,
                "Z": 0,
                "Map": 0,
                "Distance": 2,
            },
        ]

        captured_filter: list[dict[str, Any]] = []

        def handler(ns: str, fn: str, args: list[Any], req_id: int) -> dict[str, Any]:
            if (ns, fn) == ("Mobiles", "ApplyFilter"):
                captured_filter.append(args[0])
                return {"ok": True, "result": mobs}
            if (ns, fn) == ("Mobiles", "Select"):
                return {"ok": True, "result": min(args[0], key=lambda m: m["Distance"])}
            if (ns, fn) == ("Player", "Position"):
                return {"ok": True, "result": {"X": 1420, "Y": 1690, "Z": 0, "Map": 0}}
            if (ns, fn) == ("Mobiles", "FindBySerial"):
                return {"ok": True, "result": mobs[1]}
            if (ns, fn) in (("Mobiles", "UseMobile"), ("Mobiles", "WaitForStats")):
                return {"ok": True, "result": True}
            return {"ok": False, "error": f"unexpected {ns}.{fn}", "interrupted": False}

        session = MockRpcSession(script, handler)
        session.start()
        rc = session.wait()
        self.assertEqual(rc, 0, msg=session.stderr_text)
        self.assertEqual(len(captured_filter), 1)
        self.assertEqual(captured_filter[0]["Notorieties"], [3, 4])
        self.assertEqual(captured_filter[0]["Bodies"], [0x0190, 0x0191, 0x0033])
        self.assertEqual(captured_filter[0]["RangeMax"], 10)
        self.assertTrue(captured_filter[0]["CheckLineOfSight"])
        self.assertIn(("Mobiles", "UseMobile", [0x00002001]), session.calls)

    def test_items_target_journal_gumps_timer_misc_agents_pathfinding_spells(self) -> None:
        script = self._write_script(
            "full_api_test.py",
            """
from System.Collections.Generic import List
from System import Int32

# Items
bandy = Items.FindByID(0x0E21, -1, -1)
assert bandy is not None and bandy.Amount == 75
all_items = Items.FindAllByID([0x0E21, 0x0F0C], -1, -1)
assert len(all_items) == 2
assert Items.FindBySerial(bandy.Serial).Name == "clean bandages"
assert Items.BackpackCount(0x0E21, -1) == 75
assert Items.UseItem(bandy) is True
assert Items.UseItemByID(0x0E21, -1) is True
assert Items.Move(bandy, 0x40000020, 10) is True
assert Items.MoveOnGround(bandy, 5, 1421, 1691, 0) is True
assert Items.GetPropValue(bandy, "Luck") == 120.0
assert Items.GetPropStringList(bandy) == ["clean bandages", "Luck 120"]
assert Items.WaitForContents(0x40000020, 500) is True

it_filter = Items.Filter()
it_filter.OnGround = 1
it_filter.RangeMax = 5
it_filter.Graphics = List[Int32]([0x0EED])
gold_piles = Items.ApplyFilter(it_filter)
assert len(gold_piles) == 1 and gold_piles[0].Serial == 0x40000088
picked_item = Items.Select(gold_piles, "Nearest")
assert picked_item.Serial == 0x40000088

# Target
assert Target.HasTarget() is False
assert Target.WaitForTarget(500) is True
assert Target.TargetExecute(bandy) is True
assert Target.TargetExecute(1420, 1690, 0, 0x051A) is True
assert Target.Self() is True
assert Target.Last() is True
assert Target.Cancel() is True
assert Target.SetLast(0x00002001) is True
assert Target.GetLast() == 0x00002001
assert Target.PromptTarget("Pick chest", 0x03B2, 2000) == 0x40000020

# Journal
Journal.Clear()
assert Journal.Search("You finish") is True
assert Journal.SearchByName("hello", "Arianna") is True
assert Journal.WaitJournal("ready", 250) is True
assert Journal.GetTextBySerial(0x00001234, True) == ["Arianna: hello"]
assert Journal.GetLineText("finish", False) == "You finish applying the bandages."
assert Journal.GetTextByColor(0x03B2, False) == ["System msg"]
assert Journal.GetTextByType("System", False) == ["System msg"]

# Gumps
assert Gumps.HasGump(0x12345678) is True
assert Gumps.CurrentGump() == 0x12345678
assert Gumps.WaitForGump(0x12345678, 500) is True
assert Gumps.SendAction(0x12345678, 1) is True
assert Gumps.SendAdvancedAction(0x12345678, 2, [101], [1], ["hello"]) is True
assert Gumps.LastGumpGetLineList() == ["Craft Menu", "Ingots: 50"]
assert Gumps.LastGumpGetLine(1) == "Ingots: 50"
assert Gumps.LastGumpTextExist("Ingots") is True
assert Gumps.CloseGump(0x12345678) is True

# Timer
Timer.Create("heal_cd", 3000)
assert Timer.Check("heal_cd") is True
assert Timer.Remaining("heal_cd") == 2500
assert Timer.Remove("heal_cd") is True
Timer.Clear()

# Misc
Misc.Pause(5)
Misc.SendMessage("Running OK", 68)
Misc.NoOperation()
Misc.SetSharedValue("counter", 42)
assert Misc.CheckSharedValue("counter") is True
assert Misc.ReadSharedValue("counter") == 42
assert Misc.AllSharedValue() == ["counter"]
assert Misc.RemoveSharedValue("counter") is True
Misc.IgnoreObject(bandy)
assert Misc.CheckIgnoreObject(bandy) is True
Misc.UnIgnoreObject(bandy)
Misc.ClearIgnore()
assert len(Misc.CurrentScriptDirectory()) > 0
assert Misc.ScriptStatus("helper.py") is False

# Agents
AutoLoot.ChangeList("default")
assert AutoLoot.Start() is True
assert AutoLoot.Status() is True
assert AutoLoot.RunOnce() is True
assert AutoLoot.Stop() is True

assert BandageHeal.Start() is True
assert BandageHeal.Status() is True
assert BandageHeal.Stop() is True

Organizer.ChangeList("gems")
assert Organizer.FStart() is True
assert Organizer.Status() is True
assert Organizer.FStop() is True

Restock.ChangeList("regs")
assert Restock.FStart() is True
assert Restock.Status() is True
assert Restock.FStop() is True

Scavenger.ChangeList("reagents")
assert Scavenger.Start() is True
assert Scavenger.Status() is True
assert Scavenger.RunOnce() is True
Scavenger.ResetIgnore()
assert Scavenger.Stop() is True

# PathFinding
route = PathFinding.Route()
route.X = 1430
route.Y = 1700
route.Distance = 2
route.StopIfStuck = True
route.MaxRetry = 2
route.AddWaypoint(1425, 1695)
assert PathFinding.Go(route) is True
assert PathFinding.RunRoute(route) is True
PathFinding.Stop()

# Spells
Spells.Cast("Greater Heal")
Spells.CastMagery("Magic Arrow", 0x00002001)
Spells.CastChivalry("Consecrate Weapon")
Spells.CastNecro("Curse Weapon")
Spells.CastBushido("Confidence")
Spells.CastNinjitsu("Animal Form")
Spells.CastSpellweaving("Gift of Renewal")
Spells.CastMysticism("Healing Stone")
""",
        )

        bandy_snap = {
            "Serial": 0x40000011,
            "ItemID": 0x0E21,
            "Hue": 0,
            "Amount": 75,
            "Name": "clean bandages",
            "Container": 0x40000010,
            "Layer": 0,
            "X": 1420,
            "Y": 1690,
            "Z": 0,
            "Map": 0,
            "OnGround": False,
        }
        gold_snap = {
            "Serial": 0x40000088,
            "ItemID": 0x0EED,
            "Hue": 0,
            "Amount": 500,
            "Name": "gold coins",
            "Container": 0xFFFFFFFF,
            "Layer": 0,
            "X": 1422,
            "Y": 1690,
            "Z": 0,
            "Map": 0,
            "OnGround": True,
        }
        shared: dict[str, Any] = {}

        def handler(ns: str, fn: str, args: list[Any], req_id: int) -> dict[str, Any]:
            if ns == "Items":
                if fn == "FindByID":
                    return {"ok": True, "result": bandy_snap}
                if fn == "FindAllByID":
                    if args[2] == 0:
                        return {"ok": True, "result": [gold_snap]}
                    return {"ok": True, "result": [bandy_snap, gold_snap]}
                if fn == "FindBySerial":
                    return {"ok": True, "result": bandy_snap}
                if fn == "BackpackCount":
                    return {"ok": True, "result": 75}
                if fn == "GetPropValue":
                    return {"ok": True, "result": 120.0}
                if fn == "GetPropStringList":
                    return {"ok": True, "result": ["clean bandages", "Luck 120"]}
                return {"ok": True, "result": True}
            if ns == "Player" and fn == "Position":
                return {"ok": True, "result": {"X": 1420, "Y": 1690, "Z": 0, "Map": 0}}
            if ns == "Target":
                if fn == "HasTarget":
                    return {"ok": True, "result": False}
                if fn == "GetLast":
                    return {"ok": True, "result": 0x00002001}
                if fn == "PromptTarget":
                    return {"ok": True, "result": 0x40000020}
                return {"ok": True, "result": True}
            if ns == "Journal":
                if fn == "Clear":
                    return {"ok": True, "result": None}
                if fn == "GetTextBySerial":
                    return {"ok": True, "result": ["Arianna: hello"]}
                if fn == "GetLineText":
                    return {"ok": True, "result": "You finish applying the bandages."}
                if fn in ("GetTextByColor", "GetTextByType"):
                    return {"ok": True, "result": ["System msg"]}
                return {"ok": True, "result": True}
            if ns == "Gumps":
                if fn == "CurrentGump":
                    return {"ok": True, "result": 0x12345678}
                if fn == "LastGumpGetLineList":
                    return {"ok": True, "result": ["Craft Menu", "Ingots: 50"]}
                if fn == "LastGumpGetLine":
                    return {"ok": True, "result": "Ingots: 50"}
                return {"ok": True, "result": True}
            if ns == "Timer":
                if fn == "Remaining":
                    return {"ok": True, "result": 2500}
                if fn in ("Create", "Clear"):
                    return {"ok": True, "result": None}
                return {"ok": True, "result": True}
            if ns == "Misc":
                if fn == "SetSharedValue":
                    shared[args[0]] = args[1]
                    return {"ok": True, "result": None}
                if fn == "ReadSharedValue":
                    return {"ok": True, "result": shared.get(args[0])}
                if fn == "CheckSharedValue":
                    return {"ok": True, "result": args[0] in shared}
                if fn == "RemoveSharedValue":
                    return {"ok": True, "result": shared.pop(args[0], None) is not None}
                if fn == "AllSharedValue":
                    return {"ok": True, "result": list(shared.keys())}
                if fn == "CheckIgnoreObject":
                    return {"ok": True, "result": True}
                if fn == "CurrentScriptDirectory":
                    return {"ok": True, "result": str(self.tmp)}
                if fn == "ScriptStatus":
                    return {"ok": True, "result": False}
                return {"ok": True, "result": None}
            if ns in ("AutoLoot", "BandageHeal", "Organizer", "Restock", "Scavenger"):
                if fn in ("ChangeList", "ResetIgnore"):
                    return {"ok": True, "result": None}
                return {"ok": True, "result": True}
            if ns == "PathFinding":
                if fn == "Stop":
                    return {"ok": True, "result": None}
                return {"ok": True, "result": True}
            if ns == "Client" and fn in ("Cast", "CastTarget"):
                return {"ok": True, "result": None}
            return {"ok": False, "error": f"unexpected {ns}.{fn}", "interrupted": False}

        session = MockRpcSession(script, handler)
        session.start()
        rc = session.wait()
        self.assertEqual(rc, 0, msg=session.stderr_text)
        self.assertIn(("Client", "Cast", ["Greater Heal"]), session.calls)
        self.assertIn(("Client", "CastTarget", ["Magic Arrow", 0x00002001]), session.calls)
        self.assertIn(("Client", "Cast", ["Consecrate Weapon"]), session.calls)
        self.assertIn(("Client", "Cast", ["Healing Stone"]), session.calls)
        # Verify PathFinding.Go received serialized route dict with Waypoints
        go_calls = [c for c in session.calls if c[0] == "PathFinding" and c[1] == "Go"]
        self.assertEqual(len(go_calls), 1)
        self.assertEqual(go_calls[0][2][0]["X"], 1430)
        self.assertEqual(go_calls[0][2][0]["Waypoints"], [{"X": 1425, "Y": 1695}])

    def test_print_stdout_isolation_and_relative_helper_import(self) -> None:
        self._write_script(
            "my_helper.py",
            """
from RazorEnhanced import Player
def read_hits():
    return Player.Hits
""",
        )
        script = self._write_script(
            "print_test.py",
            """
import os
import sys
import my_helper

print("USER PRINT LINE 1")
sys.stdout.write("USER SYS STDOUT LINE 2\\n")
os.write(1, b"RAW FD 1 WRITE LINE 3\\n")
assert my_helper.read_hits() == 99
assert ScriptName == "print_test.py"
assert ScriptArgs == ["alpha", "beta"]
""",
        )

        def handler(ns: str, fn: str, args: list[Any], req_id: int) -> dict[str, Any]:
            if (ns, fn) == ("Player", "Hits"):
                return {"ok": True, "result": 99}
            return {"ok": False, "error": f"unexpected {ns}.{fn}", "interrupted": False}

        session = MockRpcSession(script, handler, args=["alpha", "beta"])
        session.start()
        rc = session.wait()
        self.assertEqual(rc, 0, msg=session.stderr_text)
        # Every single stdout line must be valid JSON-RPC (zero user print leakage!)
        self.assertEqual(len(session.raw_stdout_lines), 1)
        parsed = json.loads(session.raw_stdout_lines[0])
        self.assertEqual(parsed["ns"], "Player")
        self.assertEqual(parsed["fn"], "Hits")
        # User prints must land cleanly on stderr
        self.assertIn("USER PRINT LINE 1", session.stderr_text)
        self.assertIn("USER SYS STDOUT LINE 2", session.stderr_text)
        self.assertIn("RAW FD 1 WRITE LINE 3", session.stderr_text)

    def test_script_exception_reports_traceback_and_nonzero_exit(self) -> None:
        script = self._write_script(
            "boom.py",
            """
def inner():
    raise ValueError("boom from user script")
inner()
""",
        )

        def handler(ns: str, fn: str, args: list[Any], req_id: int) -> dict[str, Any]:
            return {"ok": True, "result": None}

        session = MockRpcSession(script, handler)
        session.start()
        rc = session.wait()
        self.assertNotEqual(rc, 0)
        self.assertIn("ValueError: boom from user script", session.stderr_text)
        self.assertIn("boom.py", session.stderr_text)

    def test_interrupted_true_cannot_be_caught_by_except_exception(self) -> None:
        script = self._write_script(
            "interrupt_rpc.py",
            """
swallowed = False
try:
    while True:
        Misc.Pause(10)
except Exception:
    swallowed = True
    Misc.SendMessage("SHOULD NEVER RUN")
""",
        )

        pause_count = 0

        def handler(ns: str, fn: str, args: list[Any], req_id: int) -> dict[str, Any]:
            nonlocal pause_count
            if (ns, fn) == ("Misc", "Pause"):
                pause_count += 1
                if pause_count >= 2:
                    return {"ok": False, "error": "interrupted", "interrupted": True}
                return {"ok": True, "result": None}
            return {"ok": False, "error": f"unexpected {ns}.{fn}", "interrupted": False}

        session = MockRpcSession(script, handler)
        session.start()
        rc = session.wait()
        self.assertEqual(rc, 0, msg=session.stderr_text)
        self.assertEqual(pause_count, 2)
        self.assertNotIn(("Misc", "SendMessage", ["SHOULD NEVER RUN", 0x03B2]), session.calls)
        self.assertEqual(session.stderr_text, "")

    def test_sigterm_exits_cleanly_even_inside_except_exception(self) -> None:
        script = self._write_script(
            "sigterm_loop.py",
            """
import time
Misc.NoOperation()
try:
    while True:
        time.sleep(0.05)
except Exception:
    Misc.SendMessage("SWALLOWED SIGTERM")
""",
        )

        ready = threading.Event()

        def handler(ns: str, fn: str, args: list[Any], req_id: int) -> dict[str, Any]:
            if (ns, fn) == ("Misc", "NoOperation"):
                ready.set()
                return {"ok": True, "result": None}
            return {"ok": True, "result": None}

        session = MockRpcSession(script, handler)
        session.start()
        self.assertTrue(ready.wait(timeout=5.0), "script did not reach ready point")
        time.sleep(0.05)
        assert session.proc is not None
        session.proc.send_signal(signal.SIGTERM)
        rc = session.wait(timeout=5.0)
        self.assertEqual(rc, 0, msg=session.stderr_text)
        self.assertEqual(session.stderr_text, "")
        self.assertFalse(any(c[1] == "SendMessage" for c in session.calls))

    def test_rpc_error_raises_catchable_runtime_error_and_advanced_filters(self) -> None:
        script = self._write_script(
            "errors_and_filters.py",
            """
from System.Collections.Generic import List
from System import Int32

# Non-interrupted RPC error must be catchable as Exception
caught = False
try:
    Misc.SendMessage("trigger_error")
except Exception as exc:
    caught = "simulated rpc error" in str(exc)
assert caught is True

# Layer alias normalization + None return
assert Player.GetItemOnLayer("Neck") is None

# Items.Filter disabled, multi-hue, name, corpse, range filtering
disabled = Items.Filter()
disabled.Enabled = False
assert Items.ApplyFilter(disabled) == []

f = Items.Filter()
f.OnGround = 1
f.RangeMin = 1
f.RangeMax = 4
f.Hues = List[Int32]([0x0021, 0x0044])
f.Name = "dragon"
f.IsCorpse = True
matches = Items.ApplyFilter(f)
assert len(matches) == 1 and matches[0].Serial == 0x40000102

# Gumps.SendAdvancedAction dict overload
assert Gumps.SendAdvancedAction(0x1111, 5, [10], {2: "input text"}) is True
""",
        )

        ground_items = [
            {
                "Serial": 0x40000101,
                "ItemID": 0x2006,
                "Hue": 0x0021,
                "Amount": 1,
                "Name": "a dragon corpse",
                "OnGround": True,
                "IsCorpse": True,
                "X": 1420,
                "Y": 1690,
                "Z": 0,
            },  # dist 0 (< RangeMin 1)
            {
                "Serial": 0x40000102,
                "ItemID": 0x2006,
                "Hue": 0x0044,
                "Amount": 1,
                "Name": "a dragon corpse",
                "OnGround": True,
                "IsCorpse": True,
                "X": 1423,
                "Y": 1690,
                "Z": 0,
            },  # dist 3 -> matches!
            {
                "Serial": 0x40000103,
                "ItemID": 0x2006,
                "Hue": 0x0099,
                "Amount": 1,
                "Name": "a dragon corpse",
                "OnGround": True,
                "IsCorpse": True,
                "X": 1422,
                "Y": 1690,
                "Z": 0,
            },  # wrong hue
        ]

        def handler(ns: str, fn: str, args: list[Any], req_id: int) -> dict[str, Any]:
            if (ns, fn) == ("Misc", "SendMessage") and args and args[0] == "trigger_error":
                return {"ok": False, "error": "simulated rpc error", "interrupted": False}
            if (ns, fn) == ("Player", "GetLayerItem"):
                return {"ok": True, "result": None}
            if (ns, fn) == ("Items", "FindAllByID"):
                return {"ok": True, "result": ground_items}
            if (ns, fn) == ("Player", "Position"):
                return {"ok": True, "result": {"X": 1420, "Y": 1690, "Z": 0, "Map": 0}}
            if (ns, fn) == ("Gumps", "SendAdvancedAction"):
                return {"ok": True, "result": True}
            return {"ok": False, "error": f"unexpected {ns}.{fn}", "interrupted": False}

        session = MockRpcSession(script, handler)
        session.start()
        rc = session.wait()
        self.assertEqual(rc, 0, msg=session.stderr_text)
        self.assertIn(("Player", "GetLayerItem", ["Necklace"]), session.calls)
        self.assertIn(
            ("Gumps", "SendAdvancedAction", [0x1111, 5, [10], {"2": "input text"}]),
            session.calls,
        )


if __name__ == "__main__":
    unittest.main()
