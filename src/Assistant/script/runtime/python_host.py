#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""CrossUO Razor Enhanced-compatible Python (.py) external script host.

Runs a user `.py` script in an isolated child process and bridges Razor Enhanced
API calls back to the CrossUO host over newline-delimited JSON-RPC on raw
fd 1 (requests) and raw fd 0 (responses).

Stdout is immediately detached from raw fd 1 and redirected to stderr so user
`print()` calls can never corrupt JSON-RPC framing.
"""

from __future__ import annotations

import io
import json
import os
import random
import signal
import sys
import threading
import traceback
import types
from typing import Any, Callable, Iterable, Optional, Sequence


# ---------------------------------------------------------------------------
# Exceptions (ScriptInterrupted inherits from BaseException so user
# `except Exception:` blocks cannot swallow a Stop / SIGTERM request)
# ---------------------------------------------------------------------------


class ScriptInterrupted(BaseException):
    """Raised when CrossUO stops or interrupts the running script."""


class ScriptRuntimeError(RuntimeError):
    """Raised when a JSON-RPC call fails on the CrossUO host."""


# ---------------------------------------------------------------------------
# Value conversion helpers
# ---------------------------------------------------------------------------


def _parse_int(value: Any, default: int = 0) -> int:
    if value is None:
        return default
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return int(value)
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str):
        text = value.strip()
        if not text:
            return default
        try:
            return int(text, 0)
        except ValueError:
            try:
                return int(float(text))
            except ValueError:
                return default
    if hasattr(value, "Serial"):
        return _parse_int(getattr(value, "Serial"), default)
    return default


def _parse_tristate(value: Any) -> Optional[bool]:
    """Normalize Razor Enhanced tri-state filter fields (-1/None = any, 0/False, 1/True)."""
    if value is None:
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        if value < 0:
            return None
        return value != 0
    return None


def _serial_arg(value: Any) -> Any:
    """Convert Item/Mobile/int/str into a JSON-RPC serial argument."""
    if value is None:
        return 0
    if hasattr(value, "Serial"):
        return int(getattr(value, "Serial"))
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return int(value)
    return str(value)


def _to_wire_value(value: Any) -> Any:
    """Convert Python / IronPython wrapper values into pure JSON-safe structures."""
    if value is None or isinstance(value, (bool, str, float)):
        return value
    if isinstance(value, int):
        return int(value)
    if isinstance(value, (Item, Mobile)):
        return int(value.Serial)
    if hasattr(value, "to_dict") and callable(value.to_dict):
        return _to_wire_value(value.to_dict())
    if isinstance(value, dict):
        return {str(k): _to_wire_value(v) for k, v in value.items()}
    if isinstance(value, (list, tuple, set)):
        return [_to_wire_value(v) for v in value]
    return str(value)


# ---------------------------------------------------------------------------
# IronPython / .NET compatibility shims
# ---------------------------------------------------------------------------


class Byte(int):
    """IronPython System.Byte shim (unsigned 8-bit integer)."""

    def __new__(cls, value: Any = 0) -> "Byte":
        return super().__new__(cls, _parse_int(value) & 0xFF)


class Int32(int):
    """IronPython System.Int32 shim (signed 32-bit integer)."""

    def __new__(cls, value: Any = 0) -> "Int32":
        raw = _parse_int(value) & 0xFFFFFFFF
        if raw & 0x80000000:
            raw -= 0x100000000
        return super().__new__(cls, raw)


class UInt32(int):
    """IronPython System.UInt32 shim (unsigned 32-bit integer)."""

    def __new__(cls, value: Any = 0) -> "UInt32":
        return super().__new__(cls, _parse_int(value) & 0xFFFFFFFF)


class Int64(int):
    """IronPython System.Int64 shim (signed 64-bit integer)."""

    def __new__(cls, value: Any = 0) -> "Int64":
        raw = _parse_int(value) & 0xFFFFFFFFFFFFFFFF
        if raw & 0x8000000000000000:
            raw -= 0x10000000000000000
        return super().__new__(cls, raw)


String = str
Boolean = bool
Double = float
Single = float


def _coerce_type(item_type: Any, value: Any) -> Any:
    if item_type is None or item_type is object:
        return value
    try:
        return item_type(value)
    except Exception:
        return value


class _DotNetList(list):
    """IronPython System.Collections.Generic.List[T] compatible list."""

    _item_type: Any = None
    _type_cache: dict[Any, type] = {}

    def __init__(self, iterable: Optional[Iterable[Any]] = None) -> None:
        super().__init__()
        if iterable is not None:
            self.AddRange(iterable)

    def __class_getitem__(cls, item_type: Any) -> type:
        cached = cls._type_cache.get(item_type)
        if cached is not None:
            return cached
        type_name = getattr(item_type, "__name__", str(item_type))
        sub = type(f"List[{type_name}]", (cls,), {"_item_type": item_type})
        cls._type_cache[item_type] = sub
        return sub

    def _coerce(self, value: Any) -> Any:
        return _coerce_type(self._item_type, value)

    def Add(self, item: Any) -> None:
        self.append(self._coerce(item))

    def AddRange(self, items: Iterable[Any]) -> None:
        for item in items:
            self.append(self._coerce(item))

    def Remove(self, item: Any) -> bool:
        coerced = self._coerce(item)
        try:
            super().remove(coerced)
            return True
        except ValueError:
            return False

    def RemoveAt(self, index: int) -> None:
        del self[index]

    def Clear(self) -> None:
        self.clear()

    def Contains(self, item: Any) -> bool:
        return self._coerce(item) in self

    def IndexOf(self, item: Any) -> int:
        coerced = self._coerce(item)
        try:
            return self.index(coerced)
        except ValueError:
            return -1

    def ToArray(self) -> list[Any]:
        return list(self)

    @property
    def Count(self) -> int:
        return len(self)


class _DotNetArray(list):
    """IronPython System.Array[T] shim."""

    _item_type: Any = None
    _type_cache: dict[Any, type] = {}

    def __init__(self, iterable: Optional[Iterable[Any]] = None) -> None:
        super().__init__()
        if isinstance(iterable, int):
            default = _coerce_type(self._item_type, 0) if self._item_type in (Byte, Int32, UInt32, Int64, int, float) else None
            super().extend([default] * max(0, iterable))
        elif iterable is not None:
            super().extend(_coerce_type(self._item_type, x) for x in iterable)

    def __class_getitem__(cls, item_type: Any) -> type:
        cached = cls._type_cache.get(item_type)
        if cached is not None:
            return cached
        type_name = getattr(item_type, "__name__", str(item_type))
        sub = type(f"Array[{type_name}]", (cls,), {"_item_type": item_type})
        cls._type_cache[item_type] = sub
        return sub

    @staticmethod
    def CreateInstance(item_type: Any, length: int) -> "_DotNetArray":
        return _DotNetArray[item_type](int(length))

    @property
    def Length(self) -> int:
        return len(self)


class _DotNetDictionary(dict):
    """IronPython System.Collections.Generic.Dictionary[K, V] shim."""

    _type_cache: dict[Any, type] = {}

    def __class_getitem__(cls, key_val_types: Any) -> type:
        cached = cls._type_cache.get(key_val_types)
        if cached is not None:
            return cached
        sub = type(f"Dictionary[{key_val_types}]", (cls,), {})
        cls._type_cache[key_val_types] = sub
        return sub

    def Add(self, key: Any, value: Any) -> None:
        self[key] = value

    def ContainsKey(self, key: Any) -> bool:
        return key in self

    def Remove(self, key: Any) -> bool:
        if key in self:
            del self[key]
            return True
        return False

    def Clear(self) -> None:
        self.clear()

    @property
    def Count(self) -> int:
        return len(self)


class _DotNetHashSet(set):
    """IronPython System.Collections.Generic.HashSet[T] shim."""

    _item_type: Any = None
    _type_cache: dict[Any, type] = {}

    def __init__(self, iterable: Optional[Iterable[Any]] = None) -> None:
        super().__init__()
        if iterable is not None:
            for x in iterable:
                self.Add(x)

    def __class_getitem__(cls, item_type: Any) -> type:
        cached = cls._type_cache.get(item_type)
        if cached is not None:
            return cached
        sub = type(f"HashSet[{item_type}]", (cls,), {"_item_type": item_type})
        cls._type_cache[item_type] = sub
        return sub

    def Add(self, item: Any) -> bool:
        coerced = _coerce_type(self._item_type, item)
        if coerced in self:
            return False
        self.add(coerced)
        return True

    def Remove(self, item: Any) -> bool:
        coerced = _coerce_type(self._item_type, item)
        if coerced in self:
            self.remove(coerced)
            return True
        return False

    def Contains(self, item: Any) -> bool:
        return _coerce_type(self._item_type, item) in self

    def Clear(self) -> None:
        self.clear()

    @property
    def Count(self) -> int:
        return len(self)


def install_dotnet_shims() -> None:
    """Install IronPython/CLR compatibility modules into sys.modules."""
    clr_mod = types.ModuleType("clr")
    clr_mod.References = []  # type: ignore[attr-defined]

    def _noop_ref(*_args: Any, **_kwargs: Any) -> None:
        return None

    clr_mod.AddReference = _noop_ref  # type: ignore[attr-defined]
    clr_mod.AddReferenceToFile = _noop_ref  # type: ignore[attr-defined]
    clr_mod.AddReferenceToFileAndPath = _noop_ref  # type: ignore[attr-defined]
    clr_mod.AddReferenceByName = _noop_ref  # type: ignore[attr-defined]
    clr_mod.AddReferenceByPartialName = _noop_ref  # type: ignore[attr-defined]
    clr_mod.LoadAssemblyFromFile = _noop_ref  # type: ignore[attr-defined]

    generic_mod = types.ModuleType("System.Collections.Generic")
    generic_mod.List = _DotNetList  # type: ignore[attr-defined]
    generic_mod.Dictionary = _DotNetDictionary  # type: ignore[attr-defined]
    generic_mod.HashSet = _DotNetHashSet  # type: ignore[attr-defined]

    collections_mod = types.ModuleType("System.Collections")
    collections_mod.Generic = generic_mod  # type: ignore[attr-defined]
    collections_mod.ArrayList = _DotNetList  # type: ignore[attr-defined]

    system_mod = types.ModuleType("System")
    system_mod.Byte = Byte  # type: ignore[attr-defined]
    system_mod.Int32 = Int32  # type: ignore[attr-defined]
    system_mod.UInt32 = UInt32  # type: ignore[attr-defined]
    system_mod.Int64 = Int64  # type: ignore[attr-defined]
    system_mod.String = String  # type: ignore[attr-defined]
    system_mod.Boolean = Boolean  # type: ignore[attr-defined]
    system_mod.Double = Double  # type: ignore[attr-defined]
    system_mod.Single = Single  # type: ignore[attr-defined]
    system_mod.Array = _DotNetArray  # type: ignore[attr-defined]
    system_mod.Collections = collections_mod  # type: ignore[attr-defined]

    sys.modules["clr"] = clr_mod
    sys.modules["System"] = system_mod
    sys.modules["System.Collections"] = collections_mod
    sys.modules["System.Collections.Generic"] = generic_mod


# ---------------------------------------------------------------------------
# JSON-RPC transport
# ---------------------------------------------------------------------------


class _RpcClient:
    """Thread-safe newline-delimited JSON-RPC client over raw parent pipes."""

    def __init__(self, in_stream: io.TextIOBase, out_stream: io.TextIOBase) -> None:
        self._in = in_stream
        self._out = out_stream
        self._lock = threading.Lock()
        self._next_id = 0
        self._interrupted = False

    def mark_interrupted(self) -> None:
        self._interrupted = True

    def check_interrupted(self) -> None:
        if self._interrupted:
            raise ScriptInterrupted("Script interrupted")

    def call(self, ns: str, fn: str, args: Optional[Sequence[Any]] = None) -> Any:
        self.check_interrupted()
        wire_args = [_to_wire_value(a) for a in (args or ())]
        with self._lock:
            self.check_interrupted()
            self._next_id += 1
            req_id = self._next_id
            payload = {
                "id": req_id,
                "ns": ns,
                "fn": fn,
                "args": wire_args,
            }
            line = json.dumps(payload, separators=(",", ":"), ensure_ascii=False) + "\n"
            try:
                self._out.write(line)
                self._out.flush()
            except (BrokenPipeError, ValueError, OSError) as exc:
                self._interrupted = True
                raise ScriptInterrupted("Host pipe closed") from exc

            try:
                raw_resp = self._in.readline()
            except (ValueError, OSError) as exc:
                self._interrupted = True
                raise ScriptInterrupted("Host pipe closed") from exc

            self.check_interrupted()
            if not raw_resp:
                self._interrupted = True
                raise ScriptInterrupted("Host closed connection")

        try:
            resp = json.loads(raw_resp)
        except json.JSONDecodeError as exc:
            raise ScriptRuntimeError(f"Malformed JSON-RPC response from host: {exc}") from exc

        if not isinstance(resp, dict):
            raise ScriptRuntimeError("Invalid JSON-RPC response payload")

        if resp.get("interrupted") is True:
            self._interrupted = True
            raise ScriptInterrupted(str(resp.get("error") or "interrupted"))

        if resp.get("id") != req_id:
            raise ScriptRuntimeError(
                f"JSON-RPC id mismatch: expected {req_id}, got {resp.get('id')}"
            )

        if not resp.get("ok", False):
            raise ScriptRuntimeError(str(resp.get("error") or f"RPC {ns}.{fn} failed"))

        return resp.get("result")

    def close(self) -> None:
        for stream in (self._out, self._in):
            try:
                stream.close()
            except Exception:
                pass


# ---------------------------------------------------------------------------
# Data wrappers: Point3D, Item, Mobile
# ---------------------------------------------------------------------------


class Point3D:
    """3D world coordinate wrapper with `.X`, `.Y`, `.Z`, `.Map`."""

    __slots__ = ("X", "Y", "Z", "Map")

    def __init__(self, x: int = 0, y: int = 0, z: int = 0, map_id: int = 0) -> None:
        self.X = int(x)
        self.Y = int(y)
        self.Z = int(z)
        self.Map = int(map_id)

    @classmethod
    def from_dict(cls, data: Any) -> "Point3D":
        if isinstance(data, Point3D):
            return Point3D(data.X, data.Y, data.Z, data.Map)
        if not isinstance(data, dict):
            return cls(0, 0, 0, 0)
        return cls(
            x=_parse_int(data.get("X", data.get("x", 0))),
            y=_parse_int(data.get("Y", data.get("y", 0))),
            z=_parse_int(data.get("Z", data.get("z", 0))),
            map_id=_parse_int(data.get("Map", data.get("map", 0))),
        )

    @property
    def x(self) -> int:
        return self.X

    @property
    def y(self) -> int:
        return self.Y

    @property
    def z(self) -> int:
        return self.Z

    @property
    def map(self) -> int:
        return self.Map

    def to_dict(self) -> dict[str, int]:
        return {"X": self.X, "Y": self.Y, "Z": self.Z, "Map": self.Map}

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, Point3D):
            return (self.X, self.Y, self.Z) == (other.X, other.Y, other.Z)
        return NotImplemented

    def __repr__(self) -> str:
        return f"Point3D(X={self.X}, Y={self.Y}, Z={self.Z})"


def _extract_xy(target: Any) -> Optional[tuple[int, int]]:
    if isinstance(target, Point3D):
        return (target.X, target.Y)
    if hasattr(target, "Position"):
        pos = getattr(target, "Position")
        if isinstance(pos, Point3D):
            return (pos.X, pos.Y)
        if isinstance(pos, dict):
            p = Point3D.from_dict(pos)
            return (p.X, p.Y)
    if isinstance(target, dict) and ("X" in target or "x" in target):
        p = Point3D.from_dict(target)
        return (p.X, p.Y)
    return None


def _chebyshev_distance(a: Point3D, b: tuple[int, int]) -> int:
    return max(abs(a.X - b[0]), abs(a.Y - b[1]))


class Item:
    """Razor Enhanced Item snapshot wrapper."""

    def __init__(self, data: dict[str, Any]) -> None:
        self._raw = dict(data)
        self.Serial: int = _parse_int(data.get("Serial", 0))
        self.ItemID: int = _parse_int(data.get("ItemID", data.get("Graphic", 0)))
        self.Graphic: int = self.ItemID
        self.TypeID: int = self.ItemID
        self.Hue: int = _parse_int(data.get("Hue", data.get("Color", 0)))
        self.Color: int = self.Hue
        self.Amount: int = _parse_int(data.get("Amount", data.get("Count", 1)), 1)
        self.Count: int = self.Amount
        self.Name: str = str(data.get("Name") or "")
        self.Container: int = _parse_int(data.get("Container", 0xFFFFFFFF), 0xFFFFFFFF)
        self.RootContainer: int = _parse_int(data.get("RootContainer", self.Container), self.Container)
        self.Layer: int = _parse_int(data.get("Layer", 0))
        pos_raw = data.get("Position")
        if isinstance(pos_raw, dict):
            self.Position: Point3D = Point3D.from_dict(pos_raw)
        else:
            self.Position = Point3D(
                _parse_int(data.get("X", 0)),
                _parse_int(data.get("Y", 0)),
                _parse_int(data.get("Z", 0)),
                _parse_int(data.get("Map", 0)),
            )
        self.X: int = self.Position.X
        self.Y: int = self.Position.Y
        self.Z: int = self.Position.Z
        self.Map: int = self.Position.Map
        self.IsContainer: bool = bool(data.get("IsContainer", False))
        self.IsCorpse: bool = bool(data.get("IsCorpse", False))
        self.OnGround: bool = bool(data.get("OnGround", self.Container == 0xFFFFFFFF))
        props = data.get("Properties")
        self.Properties: list[str] = [str(p) for p in props] if isinstance(props, list) else []
        contains = data.get("Contains")
        self.Contains: list[Item] = (
            [Item(c) for c in contains if isinstance(c, dict)]
            if isinstance(contains, list)
            else []
        )

    def DistanceTo(self, other: Any) -> int:
        xy = _extract_xy(other)
        if xy is None:
            return 100500
        return _chebyshev_distance(self.Position, xy)

    def to_dict(self) -> dict[str, Any]:
        return dict(self._raw)

    def __int__(self) -> int:
        return self.Serial

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, Item):
            return self.Serial == other.Serial
        if isinstance(other, int):
            return self.Serial == other
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self.Serial)

    def __repr__(self) -> str:
        return f"Item(Serial=0x{self.Serial:08X}, ItemID=0x{self.ItemID:04X}, Name={self.Name!r})"


class Mobile:
    """Razor Enhanced Mobile snapshot wrapper."""

    def __init__(self, data: dict[str, Any]) -> None:
        self._raw = dict(data)
        self.Serial: int = _parse_int(data.get("Serial", 0))
        self.Body: int = _parse_int(data.get("Body", data.get("MobileID", data.get("Graphic", 0))))
        self.MobileID: int = self.Body
        self.Graphic: int = self.Body
        self.Hue: int = _parse_int(data.get("Hue", data.get("Color", 0)))
        self.Color: int = self.Hue
        self.Hits: int = _parse_int(data.get("Hits", 0))
        self.HitsMax: int = _parse_int(data.get("HitsMax", data.get("MaxHits", 0)))
        self.MaxHits: int = self.HitsMax
        self.Mana: int = _parse_int(data.get("Mana", 0))
        self.ManaMax: int = _parse_int(data.get("ManaMax", data.get("MaxMana", 0)))
        self.MaxMana: int = self.ManaMax
        self.Stam: int = _parse_int(data.get("Stam", 0))
        self.StamMax: int = _parse_int(data.get("StamMax", data.get("MaxStam", 0)))
        self.MaxStam: int = self.StamMax
        self.Name: str = str(data.get("Name") or "")
        self.Notoriety: int = _parse_int(data.get("Notoriety", 0))
        self.Direction: int = _parse_int(data.get("Direction", 0))
        self.Poisoned: bool = bool(data.get("Poisoned", False))
        self.YellowHits: bool = bool(data.get("YellowHits", data.get("Mortaled", False)))
        self.Mortaled: bool = self.YellowHits
        paralyzed = bool(data.get("Paralyzed", data.get("Paralized", False)))
        self.Paralized: bool = paralyzed
        self.Paralyzed: bool = paralyzed
        self.WarMode: bool = bool(data.get("WarMode", False))
        self.Hidden: bool = bool(data.get("Hidden", False))
        self.Visible: bool = not self.Hidden
        self.IsHuman: bool = bool(data.get("IsHuman", False))
        self.IsGhost: bool = bool(data.get("IsGhost", data.get("Dead", False)))
        self.Dead: bool = self.IsGhost
        self.Female: bool = bool(data.get("Female", False))
        bp_raw = data.get("Backpack")
        if isinstance(bp_raw, dict):
            self.Backpack: Any = Item(bp_raw)
        else:
            self.Backpack = _parse_int(bp_raw, 0)
        pos_raw = data.get("Position")
        if isinstance(pos_raw, dict):
            self.Position: Point3D = Point3D.from_dict(pos_raw)
        else:
            self.Position = Point3D(
                _parse_int(data.get("X", 0)),
                _parse_int(data.get("Y", 0)),
                _parse_int(data.get("Z", 0)),
                _parse_int(data.get("Map", 0)),
            )
        self.X: int = self.Position.X
        self.Y: int = self.Position.Y
        self.Z: int = self.Position.Z
        self.Map: int = self.Position.Map
        self.Distance: int = _parse_int(data.get("Distance", 100500), 100500)
        props = data.get("Properties")
        self.Properties: list[str] = [str(p) for p in props] if isinstance(props, list) else []

    def DistanceTo(self, other: Any) -> int:
        xy = _extract_xy(other)
        if xy is None:
            return self.Distance
        return _chebyshev_distance(self.Position, xy)

    def to_dict(self) -> dict[str, Any]:
        return dict(self._raw)

    def __int__(self) -> int:
        return self.Serial

    def __eq__(self, other: Any) -> bool:
        if isinstance(other, Mobile):
            return self.Serial == other.Serial
        if isinstance(other, int):
            return self.Serial == other
        return NotImplemented

    def __hash__(self) -> int:
        return hash(self.Serial)

    def __repr__(self) -> str:
        return f"Mobile(Serial=0x{self.Serial:08X}, Body=0x{self.Body:04X}, Name={self.Name!r})"


# ---------------------------------------------------------------------------
# Razor Enhanced API Namespaces
# ---------------------------------------------------------------------------


class _PlayerAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    # --- Stat / vital properties ---
    @property
    def Hits(self) -> int:
        return _parse_int(self._rpc.call("Player", "Hits"))

    @property
    def HitsMax(self) -> int:
        return _parse_int(self._rpc.call("Player", "HitsMax"))

    @property
    def MaxHits(self) -> int:
        return _parse_int(self._rpc.call("Player", "MaxHits"))

    @property
    def Mana(self) -> int:
        return _parse_int(self._rpc.call("Player", "Mana"))

    @property
    def ManaMax(self) -> int:
        return _parse_int(self._rpc.call("Player", "ManaMax"))

    @property
    def MaxMana(self) -> int:
        return _parse_int(self._rpc.call("Player", "MaxMana"))

    @property
    def Stam(self) -> int:
        return _parse_int(self._rpc.call("Player", "Stam"))

    @property
    def StamMax(self) -> int:
        return _parse_int(self._rpc.call("Player", "StamMax"))

    @property
    def MaxStam(self) -> int:
        return _parse_int(self._rpc.call("Player", "MaxStam"))

    @property
    def Str(self) -> int:
        return _parse_int(self._rpc.call("Player", "Str"))

    @property
    def Dex(self) -> int:
        return _parse_int(self._rpc.call("Player", "Dex"))

    @property
    def Int(self) -> int:
        return _parse_int(self._rpc.call("Player", "Int"))

    @property
    def Weight(self) -> int:
        return _parse_int(self._rpc.call("Player", "Weight"))

    @property
    def MaxWeight(self) -> int:
        return _parse_int(self._rpc.call("Player", "MaxWeight"))

    @property
    def Gold(self) -> int:
        return _parse_int(self._rpc.call("Player", "Gold"))

    @property
    def Armor(self) -> int:
        return _parse_int(self._rpc.call("Player", "Armor"))

    @property
    def AR(self) -> int:
        return self.Armor

    @property
    def Followers(self) -> int:
        return _parse_int(self._rpc.call("Player", "Followers"))

    @property
    def FollowersMax(self) -> int:
        return _parse_int(self._rpc.call("Player", "MaxFollowers"))

    @property
    def MaxFollowers(self) -> int:
        return self.FollowersMax

    @property
    def StatsCap(self) -> int:
        return _parse_int(self._rpc.call("Player", "StatsCap"))

    @property
    def StatCap(self) -> int:
        return self.StatsCap

    @property
    def Luck(self) -> int:
        return _parse_int(self._rpc.call("Player", "Luck"))

    @property
    def FireResistance(self) -> int:
        return _parse_int(self._rpc.call("Player", "FireResistance"))

    @property
    def ColdResistance(self) -> int:
        return _parse_int(self._rpc.call("Player", "ColdResistance"))

    @property
    def PoisonResistance(self) -> int:
        return _parse_int(self._rpc.call("Player", "PoisonResistance"))

    @property
    def EnergyResistance(self) -> int:
        return _parse_int(self._rpc.call("Player", "EnergyResistance"))

    @property
    def MinDamage(self) -> int:
        return _parse_int(self._rpc.call("Player", "MinDamage"))

    @property
    def MaxDamage(self) -> int:
        return _parse_int(self._rpc.call("Player", "MaxDamage"))

    @property
    def TithingPoints(self) -> int:
        return _parse_int(self._rpc.call("Player", "TithingPoints"))

    @property
    def StealthSteps(self) -> int:
        return _parse_int(self._rpc.call("Player", "StealthSteps"))

    # --- Status flags ---
    @property
    def Poisoned(self) -> bool:
        return bool(self._rpc.call("Player", "Poisoned"))

    @property
    def YellowHits(self) -> bool:
        return bool(self._rpc.call("Player", "YellowHits"))

    @property
    def Mortaled(self) -> bool:
        return bool(self._rpc.call("Player", "Mortaled"))

    @property
    def Paralized(self) -> bool:
        return bool(self._rpc.call("Player", "Paralized"))

    @property
    def Paralyzed(self) -> bool:
        return bool(self._rpc.call("Player", "Paralyzed"))

    @property
    def Frozen(self) -> bool:
        return bool(self._rpc.call("Player", "Frozen"))

    @property
    def WarMode(self) -> bool:
        return bool(self._rpc.call("Player", "WarMode"))

    @property
    def IsGhost(self) -> bool:
        return bool(self._rpc.call("Player", "Dead"))

    @property
    def Dead(self) -> bool:
        return self.IsGhost

    @property
    def IsHuman(self) -> bool:
        return bool(self._rpc.call("Player", "IsHuman"))

    @property
    def Female(self) -> bool:
        return bool(self._rpc.call("Player", "Female"))

    @property
    def Flying(self) -> bool:
        return bool(self._rpc.call("Player", "Flying"))

    @property
    def Hidden(self) -> bool:
        return bool(self._rpc.call("Player", "Hidden"))

    @property
    def Visible(self) -> bool:
        return not bool(self._rpc.call("Player", "Hidden"))

    @property
    def InParty(self) -> bool:
        return False

    @property
    def Connected(self) -> bool:
        return bool(self._rpc.call("Client", "Connected"))

    # --- Identity & spatial properties ---
    @property
    def Serial(self) -> int:
        return _parse_int(self._rpc.call("Player", "Serial"))

    @property
    def Name(self) -> str:
        return str(self._rpc.call("Player", "Name") or "")

    @property
    def Body(self) -> int:
        return _parse_int(self._rpc.call("Player", "Graphic"))

    @property
    def MobileID(self) -> int:
        return self.Body

    @property
    def Graphic(self) -> int:
        return self.Body

    @property
    def Hue(self) -> int:
        return _parse_int(self._rpc.call("Player", "Color"))

    @property
    def Color(self) -> int:
        return self.Hue

    @property
    def Direction(self) -> int:
        return _parse_int(self._rpc.call("Player", "Direction"))

    @property
    def Notoriety(self) -> int:
        return _parse_int(self._rpc.call("Player", "Notoriety"))

    @property
    def Position(self) -> Point3D:
        return Point3D.from_dict(self._rpc.call("Player", "Position"))

    @property
    def X(self) -> int:
        return _parse_int(self._rpc.call("Player", "X"))

    @property
    def Y(self) -> int:
        return _parse_int(self._rpc.call("Player", "Y"))

    @property
    def Z(self) -> int:
        return _parse_int(self._rpc.call("Player", "Z"))

    @property
    def Map(self) -> int:
        return _parse_int(self._rpc.call("Player", "Map"))

    @property
    def Backpack(self) -> Optional[Item]:
        raw = self._rpc.call("Player", "Backpack")
        return Item(raw) if isinstance(raw, dict) else None

    # --- Methods ---
    _LAYER_ALIASES: dict[str, str] = {
        "neck": "Necklace",
        "head": "Helmet",
        "outertorso": "Robe",
        "outerlegs": "Legs",
        "innerlegs": "Pants",
        "onehanded": "RightHand",
        "twohanded": "LeftHand",
    }

    def _normalize_layer(self, layer: Any) -> Any:
        if isinstance(layer, str):
            return self._LAYER_ALIASES.get(layer.strip().lower(), layer)
        return layer

    def GetItemOnLayer(self, layer: Any) -> Optional[Item]:
        raw = self._rpc.call("Player", "GetLayerItem", [self._normalize_layer(layer)])
        return Item(raw) if isinstance(raw, dict) else None

    def GetLayerItem(self, layer: Any) -> Optional[Item]:
        return self.GetItemOnLayer(layer)

    def CheckLayer(self, layer: Any) -> bool:
        return self.GetItemOnLayer(layer) is not None

    def BuffsExist(self, name: str, _keyword_only: bool = False) -> bool:
        return bool(self._rpc.call("Player", "BuffsExist", [str(name)]))

    def UseSkill(self, skill: Any, target: Any = None, _wait: bool = False) -> None:
        if target is None:
            self._rpc.call("Player", "UseSkill", [skill])
        else:
            self._rpc.call("Player", "UseSkill", [skill, _serial_arg(target)])

    def GetSkillValue(self, skill: Any) -> float:
        res = self._rpc.call("Client", "SkillValue", [skill, "real"])
        return float(res or 0)

    def GetRealSkillValue(self, skill: Any) -> float:
        return self.GetSkillValue(skill)

    def SetWarMode(self, state: bool) -> bool:
        return bool(self._rpc.call("Player", "SetWarMode", [bool(state)]))

    def Walk(self, direction: Any) -> bool:
        return bool(self._rpc.call("Player", "Walk", [direction]))

    def Run(self, direction: Any) -> bool:
        return bool(self._rpc.call("Player", "Run", [direction]))

    def PathFindTo(
        self,
        x: Any,
        y: Optional[int] = None,
        z: Optional[int] = None,
        tolerance: Optional[int] = None,
    ) -> bool:
        if isinstance(x, Point3D):
            args: list[Any] = [x.X, x.Y, x.Z]
            if y is not None:
                args.append(int(y))
        else:
            args = [int(x), int(y if y is not None else 0)]
            if z is not None:
                args.append(int(z))
                if tolerance is not None:
                    args.append(int(tolerance))
            elif tolerance is not None:
                args.append(None)
                args.append(int(tolerance))
        return bool(self._rpc.call("Player", "PathFindTo", args))

    def _say_helper(self, fn: str, color_or_msg: Any, msg: Optional[str] = None) -> None:
        if msg is None:
            self._rpc.call("Client", fn, [str(color_or_msg)])
        else:
            self._rpc.call("Client", fn, [str(msg), _parse_int(color_or_msg)])

    def ChatSay(self, color_or_msg: Any, msg: Optional[str] = None) -> None:
        self._say_helper("Say", color_or_msg, msg)

    def ChatYell(self, color_or_msg: Any, msg: Optional[str] = None) -> None:
        self._say_helper("SayYell", color_or_msg, msg)

    def ChatWhisper(self, color_or_msg: Any, msg: Optional[str] = None) -> None:
        self._say_helper("SayWhisper", color_or_msg, msg)

    def ChatEmote(self, color_or_msg: Any, msg: Optional[str] = None) -> None:
        self._say_helper("SayEmote", color_or_msg, msg)

    def ChatParty(self, msg: str) -> None:
        self._rpc.call("Client", "SayParty", [str(msg)])

    def ChatGuild(self, msg: str) -> None:
        self._rpc.call("Client", "SayGuild", [str(msg)])

    def ChatAlliance(self, msg: str) -> None:
        self._rpc.call("Client", "SayAlliance", [str(msg)])

    def HeadMessage(self, color_or_msg: Any, msg: Optional[str] = None) -> None:
        if msg is None:
            hue = 0x03B2
            text = str(color_or_msg)
        else:
            hue = _parse_int(color_or_msg, 0x03B2) & 0xFFFF
            text = str(msg)
        self._rpc.call("Client", "CharPrint", ["self", f"0x{hue:04X}", text])

    def Attack(self, serial: Any) -> None:
        self._rpc.call("Client", "Attack", [_serial_arg(serial)])

    def WeaponPrimarySA(self) -> None:
        self._rpc.call("Client", "UseAbility", ["primary"])

    def WeaponSecondarySA(self) -> None:
        self._rpc.call("Client", "UseAbility", ["secondary"])

    def InvokeVirtue(self, virtue: str) -> None:
        self._rpc.call("Client", "InvokeVirtue", [str(virtue)])

    def EquipItem(self, serial: Any) -> None:
        self._rpc.call("Client", "Equip", [_serial_arg(serial)])

    def UnEquipItemByLayer(self, layer: Any, _wait: bool = False) -> None:
        self._rpc.call("Client", "Unequip", [str(self._normalize_layer(layer))])

    def DistanceTo(self, other: Any) -> int:
        xy = _extract_xy(other)
        if xy is None:
            return 100500
        return _chebyshev_distance(self.Position, xy)


class _ItemsFilter:
    """Razor Enhanced Items.Filter builder."""

    def __init__(self) -> None:
        self.Enabled: bool = True
        self.Graphics: _DotNetList = _DotNetList[Int32]()
        self.Hues: _DotNetList = _DotNetList[Int32]()
        self.RangeMin: int = -1
        self.RangeMax: int = -1
        self.OnGround: Any = -1
        self.IsContainer: Any = -1
        self.IsCorpse: Any = -1
        self.Movable: Any = -1
        self.Name: str = ""
        self.CheckIgnoreObject: bool = True
        self.Layers: _DotNetList = _DotNetList[String]()
        self.Serials: _DotNetList = _DotNetList[Int32]()
        self.Container: Any = -1


class _ItemsAPI:
    Filter = _ItemsFilter

    def __init__(self, rpc: _RpcClient, player: _PlayerAPI) -> None:
        self._rpc = rpc
        self._player = player

    def FindBySerial(self, serial: Any) -> Optional[Item]:
        raw = self._rpc.call("Items", "FindBySerial", [_serial_arg(serial)])
        return Item(raw) if isinstance(raw, dict) else None

    def FindByID(
        self,
        itemid: Any,
        color: int = -1,
        container: Any = -1,
        range_or_recursive: Any = True,
        ignorecheck: bool = False,
    ) -> Optional[Item]:
        cid = _serial_arg(container) if hasattr(container, "Serial") else container
        raw = self._rpc.call(
            "Items",
            "FindByID",
            [itemid, color, cid, range_or_recursive, bool(ignorecheck)],
        )
        return Item(raw) if isinstance(raw, dict) else None

    def FindAllByID(
        self,
        itemids: Any,
        color: int = -1,
        container: Any = -1,
        range_or_recursive: Any = True,
        ignorecheck: bool = False,
    ) -> list[Item]:
        cid = _serial_arg(container) if hasattr(container, "Serial") else container
        raw = self._rpc.call(
            "Items",
            "FindAllByID",
            [itemids, color, cid, range_or_recursive, bool(ignorecheck)],
        )
        if not isinstance(raw, list):
            return []
        return [Item(entry) for entry in raw if isinstance(entry, dict)]

    def BackpackCount(self, itemid: Any, color: int = -1) -> int:
        return _parse_int(self._rpc.call("Items", "BackpackCount", [itemid, color]))

    def UseItem(self, serial: Any, target: Any = None) -> bool:
        args: list[Any] = [_serial_arg(serial)]
        if target is not None:
            args.append(_serial_arg(target))
        return bool(self._rpc.call("Items", "UseItem", args))

    def UseItemByID(self, itemid: Any, color: int = -1) -> bool:
        return bool(self._rpc.call("Items", "UseItemByID", [itemid, color]))

    def Move(
        self,
        source: Any,
        destination: Any = -1,
        amount: int = 0,
        x: int = -1,
        y: int = -1,
    ) -> bool:
        dst = _serial_arg(destination) if hasattr(destination, "Serial") else destination
        return bool(
            self._rpc.call(
                "Items",
                "Move",
                [_serial_arg(source), dst, int(amount), int(x), int(y)],
            )
        )

    def MoveOnGround(
        self,
        source: Any,
        amount: int,
        x: int,
        y: int,
        z: Optional[int] = None,
    ) -> bool:
        args: list[Any] = [_serial_arg(source), int(amount), int(x), int(y)]
        if z is not None:
            args.append(int(z))
        return bool(self._rpc.call("Items", "MoveOnGround", args))

    def GetPropValue(self, serial: Any, name: str) -> float:
        res = self._rpc.call("Items", "GetPropValue", [_serial_arg(serial), str(name)])
        return float(res or 0.0)

    def GetPropStringList(self, serial: Any) -> list[str]:
        res = self._rpc.call("Items", "GetPropStringList", [_serial_arg(serial)])
        return [str(line) for line in res] if isinstance(res, list) else []

    def GetPropStringByIndex(self, serial: Any, index: int) -> str:
        lines = self.GetPropStringList(serial)
        if 0 <= index < len(lines):
            return lines[index]
        return ""

    def WaitForContents(self, serial: Any, delay: int = 1000) -> bool:
        return bool(self._rpc.call("Items", "WaitForContents", [_serial_arg(serial), int(delay)]))

    def ApplyFilter(self, filter_obj: _ItemsFilter) -> list[Item]:
        if filter_obj is None or not getattr(filter_obj, "Enabled", True):
            return []

        graphics = [_parse_int(g) for g in getattr(filter_obj, "Graphics", ())]
        hues = [_parse_int(h) for h in getattr(filter_obj, "Hues", ())]
        serials = {_parse_int(s) for s in getattr(filter_obj, "Serials", ())}
        on_ground_tri = _parse_tristate(getattr(filter_obj, "OnGround", -1))
        is_container_tri = _parse_tristate(getattr(filter_obj, "IsContainer", -1))
        is_corpse_tri = _parse_tristate(getattr(filter_obj, "IsCorpse", -1))
        range_min = _parse_int(getattr(filter_obj, "RangeMin", -1), -1)
        range_max = _parse_int(getattr(filter_obj, "RangeMax", -1), -1)
        name_needle = str(getattr(filter_obj, "Name", "") or "").lower()
        check_ignore = bool(getattr(filter_obj, "CheckIgnoreObject", True))
        explicit_container = getattr(filter_obj, "Container", -1)

        if explicit_container not in (-1, None):
            container = explicit_container
        elif on_ground_tri is True or range_max >= 0 or range_min >= 0:
            container = 0  # ground/world
        else:
            container = -1  # backpack

        wire_color = hues[0] if len(hues) == 1 else -1
        wire_ids: Any = graphics if graphics else -1
        range_or_rec: Any = range_max if (container == 0 and range_max >= 0) else True

        items = self.FindAllByID(wire_ids, wire_color, container, range_or_rec, check_ignore)
        if not items:
            return []

        player_pos: Optional[Point3D] = None
        if range_min >= 0 or range_max >= 0:
            player_pos = self._player.Position

        hue_set = set(hues) if len(hues) > 1 else None
        out: list[Item] = []
        for item in items:
            if hue_set is not None and item.Hue not in hue_set:
                continue
            if serials and item.Serial not in serials:
                continue
            if on_ground_tri is not None and item.OnGround != on_ground_tri:
                continue
            if is_container_tri is not None and item.IsContainer != is_container_tri:
                continue
            if is_corpse_tri is not None and item.IsCorpse != is_corpse_tri:
                continue
            if name_needle and name_needle not in item.Name.lower():
                continue
            if player_pos is not None:
                dist = _chebyshev_distance(player_pos, (item.X, item.Y))
                if range_min >= 0 and dist < range_min:
                    continue
                if range_max >= 0 and dist > range_max:
                    continue
            out.append(item)
        return out

    def Select(self, items: Sequence[Item], selector: str = "Nearest") -> Optional[Item]:
        if not items:
            return None
        sel = str(selector or "nearest").strip().lower()
        if sel == "random":
            return random.choice(list(items))
        if sel in ("weakest", "lowest"):
            return min(items, key=lambda it: it.Amount)
        if sel == "strongest":
            return max(items, key=lambda it: it.Amount)

        player_pos = self._player.Position
        if sel in ("farthest", "furthest"):
            return max(items, key=lambda it: _chebyshev_distance(player_pos, (it.X, it.Y)))
        return min(items, key=lambda it: _chebyshev_distance(player_pos, (it.X, it.Y)))


class _MobilesFilter:
    """Razor Enhanced Mobiles.Filter builder."""

    def __init__(self) -> None:
        self.Enabled: bool = True
        self.RangeMin: int = -1
        self.RangeMax: int = -1
        self.Notorieties: _DotNetList = _DotNetList[Byte]()
        self.Bodies: _DotNetList = _DotNetList[Int32]()
        self.Poisoned: Any = -1
        self.IsHuman: Any = -1
        self.IsGhost: Any = -1
        self.Warmode: Any = -1
        self.Friend: Any = -1
        self.Female: Any = -1
        self.Blessed: Any = -1
        self.Paralized: Any = -1
        self.Name: str = ""
        self.CheckLineOfSight: bool = False
        self.CheckIgnoreObject: bool = True
        self.Selector: str = ""

    @property
    def WarMode(self) -> Any:
        return self.Warmode

    @WarMode.setter
    def WarMode(self, value: Any) -> None:
        self.Warmode = value

    def to_dict(self) -> dict[str, Any]:
        return {
            "Enabled": bool(self.Enabled),
            "RangeMin": _parse_int(self.RangeMin, -1),
            "RangeMax": _parse_int(self.RangeMax, -1),
            "Notorieties": [_parse_int(n) if not isinstance(n, str) else n for n in self.Notorieties],
            "Bodies": [_parse_int(b) if not isinstance(b, str) else b for b in self.Bodies],
            "Poisoned": self.Poisoned if isinstance(self.Poisoned, bool) else _parse_int(self.Poisoned, -1),
            "IsHuman": self.IsHuman if isinstance(self.IsHuman, bool) else _parse_int(self.IsHuman, -1),
            "IsGhost": self.IsGhost if isinstance(self.IsGhost, bool) else _parse_int(self.IsGhost, -1),
            "Warmode": self.Warmode if isinstance(self.Warmode, bool) else _parse_int(self.Warmode, -1),
            "Name": str(self.Name or ""),
            "CheckLineOfSight": bool(self.CheckLineOfSight),
            "CheckIgnoreObject": bool(self.CheckIgnoreObject),
            "Selector": str(self.Selector or ""),
        }


class _MobilesAPI:
    Filter = _MobilesFilter

    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def FindBySerial(self, serial: Any) -> Optional[Mobile]:
        raw = self._rpc.call("Mobiles", "FindBySerial", [_serial_arg(serial)])
        return Mobile(raw) if isinstance(raw, dict) else None

    def ApplyFilter(self, filter_obj: Any) -> list[Mobile]:
        payload = filter_obj.to_dict() if hasattr(filter_obj, "to_dict") else filter_obj
        raw = self._rpc.call("Mobiles", "ApplyFilter", [payload])
        if not isinstance(raw, list):
            return []
        return [Mobile(entry) for entry in raw if isinstance(entry, dict)]

    def Select(self, mobiles: Sequence[Any], selector: str = "Nearest") -> Optional[Mobile]:
        if not mobiles:
            return None
        raw_list = [m.to_dict() if isinstance(m, Mobile) else m for m in mobiles]
        raw = self._rpc.call("Mobiles", "Select", [raw_list, str(selector)])
        return Mobile(raw) if isinstance(raw, dict) else None

    def UseMobile(self, serial: Any) -> bool:
        return bool(self._rpc.call("Mobiles", "UseMobile", [_serial_arg(serial)]))

    def WaitForStats(self, serial: Any, delay: int = 1000) -> bool:
        return bool(self._rpc.call("Mobiles", "WaitForStats", [_serial_arg(serial), int(delay)]))

    def Message(self, serial: Any, hue: int, message: str, _wait: bool = False) -> None:
        color = _parse_int(hue, 0x03B2) & 0xFFFF
        self._rpc.call("Client", "CharPrint", [_serial_arg(serial), f"0x{color:04X}", str(message)])


class _TargetAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def HasTarget(self) -> bool:
        return bool(self._rpc.call("Target", "HasTarget"))

    def WaitForTarget(self, timeout: int = 1000, _noshow: bool = False) -> bool:
        return bool(self._rpc.call("Target", "WaitForTarget", [int(timeout)]))

    def TargetExecute(
        self,
        x_or_serial: Any,
        y: Optional[int] = None,
        z: Optional[int] = None,
        graphic: Optional[int] = None,
    ) -> bool:
        if y is not None and z is not None:
            args: list[Any] = [int(x_or_serial), int(y), int(z)]
            if graphic is not None:
                args.append(int(graphic))
            return bool(self._rpc.call("Target", "TargetExecute", args))
        if isinstance(x_or_serial, Point3D):
            return bool(
                self._rpc.call(
                    "Target", "TargetExecute", [x_or_serial.X, x_or_serial.Y, x_or_serial.Z]
                )
            )
        return bool(self._rpc.call("Target", "TargetExecute", [_serial_arg(x_or_serial)]))

    def Self(self) -> bool:
        return bool(self._rpc.call("Target", "Self"))

    def Last(self) -> bool:
        return bool(self._rpc.call("Target", "Last"))

    def Cancel(self) -> bool:
        return bool(self._rpc.call("Target", "Cancel"))

    def SetLast(self, serial: Any) -> bool:
        return bool(self._rpc.call("Target", "SetLast", [_serial_arg(serial)]))

    def GetLast(self) -> int:
        return _parse_int(self._rpc.call("Target", "GetLast"))

    def PromptTarget(
        self,
        message: str = "Select a target",
        color: int = 0x03B2,
        timeout: int = 15000,
    ) -> int:
        return _parse_int(
            self._rpc.call("Target", "PromptTarget", [str(message), int(color), int(timeout)])
        )


class _JournalAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def Search(self, text: str) -> bool:
        return bool(self._rpc.call("Journal", "Search", [str(text)]))

    def SearchByName(self, text: str, name: str) -> bool:
        return bool(self._rpc.call("Journal", "SearchByName", [str(text), str(name)]))

    def WaitJournal(self, text: str, delay: int = 1000) -> bool:
        return bool(self._rpc.call("Journal", "WaitJournal", [str(text), int(delay)]))

    def Clear(self) -> None:
        self._rpc.call("Journal", "Clear")

    def GetTextBySerial(self, serial: Any, addname: bool = False) -> list[str]:
        res = self._rpc.call("Journal", "GetTextBySerial", [_serial_arg(serial), bool(addname)])
        return [str(x) for x in res] if isinstance(res, list) else []

    def GetLineText(self, text: str, addname: bool = False) -> str:
        res = self._rpc.call("Journal", "GetLineText", [str(text), bool(addname)])
        return str(res or "")

    def GetTextByColor(self, color: int, addname: bool = False) -> list[str]:
        res = self._rpc.call("Journal", "GetTextByColor", [int(color), bool(addname)])
        return [str(x) for x in res] if isinstance(res, list) else []

    def GetTextByType(self, msg_type: str, addname: bool = False) -> list[str]:
        res = self._rpc.call("Journal", "GetTextByType", [str(msg_type), bool(addname)])
        return [str(x) for x in res] if isinstance(res, list) else []


class _GumpsAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def HasGump(self, gumpid: int = 0) -> bool:
        return bool(self._rpc.call("Gumps", "HasGump", [int(gumpid)]))

    def CurrentGump(self) -> int:
        return _parse_int(self._rpc.call("Gumps", "CurrentGump"))

    def WaitForGump(self, gumpid: int = 0, delay: int = 1000) -> bool:
        return bool(self._rpc.call("Gumps", "WaitForGump", [int(gumpid), int(delay)]))

    def SendAction(self, gumpid: int, buttonid: int) -> bool:
        return bool(self._rpc.call("Gumps", "SendAction", [int(gumpid), int(buttonid)]))

    def SendAdvancedAction(
        self,
        gumpid: int,
        buttonid: int,
        switches: Optional[Sequence[int]] = None,
        text_ids: Any = None,
        text_values: Optional[Sequence[str]] = None,
    ) -> bool:
        sw_list = [int(s) for s in (switches or ())]
        if text_values is not None and text_ids is not None:
            args: list[Any] = [
                int(gumpid),
                int(buttonid),
                sw_list,
                [str(i) for i in text_ids],
                [str(t) for t in text_values],
            ]
        elif isinstance(text_ids, dict):
            args = [
                int(gumpid),
                int(buttonid),
                sw_list,
                {str(k): str(v) for k, v in text_ids.items()},
            ]
        else:
            args = [int(gumpid), int(buttonid), sw_list]
        return bool(self._rpc.call("Gumps", "SendAdvancedAction", args))

    def LastGumpGetLineList(self) -> list[str]:
        res = self._rpc.call("Gumps", "LastGumpGetLineList")
        return [str(line) for line in res] if isinstance(res, list) else []

    def LastGumpGetLine(self, line_num: int) -> str:
        res = self._rpc.call("Gumps", "LastGumpGetLine", [int(line_num)])
        return str(res or "")

    def LastGumpTextExist(self, text: str) -> bool:
        return bool(self._rpc.call("Gumps", "LastGumpTextExist", [str(text)]))

    def CloseGump(self, gumpid: int = 0) -> bool:
        return bool(self._rpc.call("Gumps", "CloseGump", [int(gumpid)]))

    def ResetGump(self) -> bool:
        return self.CloseGump(0)


class _TimerAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def Create(self, name: str, delay_ms: int, _message: Optional[str] = None) -> None:
        self._rpc.call("Timer", "Create", [str(name), int(delay_ms)])

    def Check(self, name: str) -> bool:
        return bool(self._rpc.call("Timer", "Check", [str(name)]))

    def Remaining(self, name: str) -> int:
        return _parse_int(self._rpc.call("Timer", "Remaining", [str(name)]))

    def Remove(self, name: str) -> bool:
        return bool(self._rpc.call("Timer", "Remove", [str(name)]))

    def Clear(self) -> None:
        self._rpc.call("Timer", "Clear")


class _MiscAPI:
    def __init__(self, rpc: _RpcClient, script_dir: str) -> None:
        self._rpc = rpc
        self._script_dir = script_dir

    def Pause(self, ms: int) -> None:
        self._rpc.call("Misc", "Pause", [int(ms)])

    def SendMessage(self, msg: Any, color: int = 0x03B2) -> None:
        self._rpc.call("Misc", "SendMessage", [str(msg), int(color)])

    def NoOperation(self) -> None:
        self._rpc.call("Misc", "NoOperation")

    def ReadSharedValue(self, name: str) -> Any:
        return self._rpc.call("Misc", "ReadSharedValue", [str(name)])

    def SetSharedValue(self, name: str, value: Any) -> None:
        self._rpc.call("Misc", "SetSharedValue", [str(name), value])

    def CheckSharedValue(self, name: str) -> bool:
        return bool(self._rpc.call("Misc", "CheckSharedValue", [str(name)]))

    def RemoveSharedValue(self, name: str) -> bool:
        return bool(self._rpc.call("Misc", "RemoveSharedValue", [str(name)]))

    def AllSharedValue(self) -> list[str]:
        res = self._rpc.call("Misc", "AllSharedValue")
        return [str(k) for k in res] if isinstance(res, list) else []

    def IgnoreObject(self, serial: Any) -> None:
        self._rpc.call("Misc", "IgnoreObject", [_serial_arg(serial)])

    def UnIgnoreObject(self, serial: Any) -> None:
        self._rpc.call("Misc", "UnIgnoreObject", [_serial_arg(serial)])

    def CheckIgnoreObject(self, serial: Any) -> bool:
        return bool(self._rpc.call("Misc", "CheckIgnoreObject", [_serial_arg(serial)]))

    def ClearIgnore(self) -> None:
        self._rpc.call("Misc", "ClearIgnore")

    def CurrentScriptDirectory(self) -> str:
        res = self._rpc.call("Misc", "CurrentScriptDirectory")
        text = str(res or "")
        if not text or text == ".":
            return self._script_dir or "."
        return text

    def ScriptStatus(self, name: str) -> bool:
        return bool(self._rpc.call("Misc", "ScriptStatus", [str(name)]))

    def ScriptRun(self, name: str) -> None:
        self._rpc.call("Client", "Exec", [str(name)])

    def ScriptStop(self, name: str) -> None:
        self._rpc.call("Client", "Terminate", [str(name)])


class _AutoLootAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def Start(self) -> bool:
        return bool(self._rpc.call("AutoLoot", "Start"))

    def Stop(self) -> bool:
        return bool(self._rpc.call("AutoLoot", "Stop"))

    def Status(self) -> bool:
        return bool(self._rpc.call("AutoLoot", "Status"))

    def RunOnce(self, target_or_list: Any = "") -> bool:
        args = [_serial_arg(target_or_list)] if target_or_list not in ("", None) else []
        return bool(self._rpc.call("AutoLoot", "RunOnce", args))

    def ChangeList(self, name: str) -> None:
        self._rpc.call("AutoLoot", "ChangeList", [str(name)])


class _BandageHealAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def Start(self) -> bool:
        return bool(self._rpc.call("BandageHeal", "Start"))

    def Stop(self) -> bool:
        return bool(self._rpc.call("BandageHeal", "Stop"))

    def Status(self) -> bool:
        return bool(self._rpc.call("BandageHeal", "Status"))


class _OrganizerAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def FStart(self) -> bool:
        return bool(self._rpc.call("Organizer", "FStart"))

    def Start(self) -> bool:
        return bool(self._rpc.call("Organizer", "Start"))

    def FStop(self) -> bool:
        return bool(self._rpc.call("Organizer", "FStop"))

    def Stop(self) -> bool:
        return bool(self._rpc.call("Organizer", "Stop"))

    def Status(self) -> bool:
        return bool(self._rpc.call("Organizer", "Status"))

    def ChangeList(self, name: str) -> None:
        self._rpc.call("Organizer", "ChangeList", [str(name)])


class _RestockAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def FStart(self) -> bool:
        return bool(self._rpc.call("Restock", "FStart"))

    def Start(self) -> bool:
        return bool(self._rpc.call("Restock", "Start"))

    def FStop(self) -> bool:
        return bool(self._rpc.call("Restock", "FStop"))

    def Stop(self) -> bool:
        return bool(self._rpc.call("Restock", "Stop"))

    def Status(self) -> bool:
        return bool(self._rpc.call("Restock", "Status"))

    def ChangeList(self, name: str) -> None:
        self._rpc.call("Restock", "ChangeList", [str(name)])


class _ScavengerAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def Start(self) -> bool:
        return bool(self._rpc.call("Scavenger", "Start"))

    def Stop(self) -> bool:
        return bool(self._rpc.call("Scavenger", "Stop"))

    def Status(self) -> bool:
        return bool(self._rpc.call("Scavenger", "Status"))

    def RunOnce(self) -> bool:
        return bool(self._rpc.call("Scavenger", "RunOnce"))

    def ResetIgnore(self) -> None:
        self._rpc.call("Scavenger", "ResetIgnore")

    def ChangeList(self, name: str) -> None:
        self._rpc.call("Scavenger", "ChangeList", [str(name)])


class _PathFindingRoute:
    """Razor Enhanced PathFinding.Route builder."""

    def __init__(self, waypoints: Optional[Iterable[Any]] = None) -> None:
        self.X: int = 0
        self.Y: int = 0
        self.Z: int = 0
        self.Distance: int = 1
        self.StopIfStuck: bool = False
        self.MaxRetry: int = 3
        self.Timeout: int = 15000
        self.Run: bool = True
        self.DebugMessage: bool = False
        self.UseResync: bool = True
        self.IgnoreMobile: bool = False
        self.Waypoints: list[Any] = list(waypoints) if waypoints is not None else []

    def AddWaypoint(self, x: int, y: int, z: Optional[int] = None) -> None:
        if z is None:
            self.Waypoints.append({"X": int(x), "Y": int(y)})
        else:
            self.Waypoints.append({"X": int(x), "Y": int(y), "Z": int(z)})

    def _normalize_waypoint(self, wp: Any) -> Any:
        if isinstance(wp, Point3D):
            return {"X": wp.X, "Y": wp.Y, "Z": wp.Z}
        if isinstance(wp, (tuple, list)):
            return [int(v) for v in wp]
        if isinstance(wp, dict):
            return dict(wp)
        return wp

    def to_dict(self) -> dict[str, Any]:
        return {
            "X": int(self.X),
            "Y": int(self.Y),
            "Z": int(self.Z),
            "Distance": int(self.Distance),
            "StopIfStuck": bool(self.StopIfStuck),
            "MaxRetry": int(self.MaxRetry),
            "Timeout": int(self.Timeout),
            "Waypoints": [self._normalize_waypoint(w) for w in self.Waypoints],
        }


class _PathFindingAPI:
    Route = _PathFindingRoute

    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def Go(
        self,
        route_or_x: Any,
        y: Optional[int] = None,
        z: Optional[int] = None,
        options: Optional[dict[str, Any]] = None,
    ) -> bool:
        if isinstance(route_or_x, _PathFindingRoute):
            args: list[Any] = [route_or_x.to_dict()]
            if isinstance(y, dict):
                args.append(y)
            elif options is not None:
                args.append(options)
            return bool(self._rpc.call("PathFinding", "Go", args))

        if isinstance(route_or_x, (list, tuple, dict)):
            args = [route_or_x]
            if isinstance(y, dict):
                args.append(y)
            elif options is not None:
                args.append(options)
            return bool(self._rpc.call("PathFinding", "Go", args))

        args = [int(route_or_x), int(y if y is not None else 0)]
        if z is not None:
            args.append(int(z))
        if options is not None:
            args.append(options)
        return bool(self._rpc.call("PathFinding", "Go", args))

    def RunRoute(self, route: Any, options: Optional[dict[str, Any]] = None) -> bool:
        payload = route.to_dict() if hasattr(route, "to_dict") else route
        args: list[Any] = [payload]
        if options is not None:
            args.append(options)
        return bool(self._rpc.call("PathFinding", "RunRoute", args))

    def PathFindTo(
        self,
        x: Any,
        y: Optional[int] = None,
        z: Optional[int] = None,
        tolerance: Optional[int] = None,
    ) -> bool:
        if isinstance(x, Point3D):
            args: list[Any] = [x.X, x.Y, x.Z]
            if y is not None:
                args.append(int(y))
        else:
            args = [int(x), int(y if y is not None else 0)]
            if z is not None:
                args.append(int(z))
                if tolerance is not None:
                    args.append(int(tolerance))
            elif tolerance is not None:
                args.append(None)
                args.append(int(tolerance))
        return bool(self._rpc.call("PathFinding", "PathFindTo", args))

    def IsWalking(self) -> bool:
        return bool(self._rpc.call("PathFinding", "IsWalking"))

    def Stop(self) -> None:
        self._rpc.call("PathFinding", "Stop")


class _SpellsAPI:
    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc
        self._last_spell: Any = None

    def Cast(self, spell: Any, target: Any = None, _wait_after: bool = True) -> None:
        self._last_spell = spell
        if target is None:
            self._rpc.call("Client", "Cast", [spell])
        else:
            self._rpc.call("Client", "CastTarget", [spell, _serial_arg(target)])

    def CastMagery(self, spell: Any, target: Any = None) -> None:
        self.Cast(spell, target)

    def CastChivalry(self, spell: Any, target: Any = None) -> None:
        self.Cast(spell, target)

    def CastNecro(self, spell: Any, target: Any = None) -> None:
        self.Cast(spell, target)

    def CastBushido(self, spell: Any, target: Any = None) -> None:
        self.Cast(spell, target)

    def CastNinjitsu(self, spell: Any, target: Any = None) -> None:
        self.Cast(spell, target)

    def CastSpellweaving(self, spell: Any, target: Any = None) -> None:
        self.Cast(spell, target)

    def CastMysticism(self, spell: Any, target: Any = None) -> None:
        self.Cast(spell, target)

    def CastMastery(self, spell: Any, target: Any = None) -> None:
        self.Cast(spell, target)

    def CastLastSpell(self, target: Any = None) -> None:
        if self._last_spell is not None:
            self.Cast(self._last_spell, target)

    def Interrupt(self) -> None:
        self._rpc.call("Misc", "NoOperation")


class _ClientProxy:
    """Dynamic proxy exposing CrossUO's `Client.*` functions to Python scripts."""

    def __init__(self, rpc: _RpcClient) -> None:
        self._rpc = rpc

    def __getattr__(self, name: str) -> Callable[..., Any]:
        if name.startswith("_"):
            raise AttributeError(name)

        def _invoke(*args: Any) -> Any:
            return self._rpc.call("Client", name, list(args))

        _invoke.__name__ = name
        return _invoke


# ---------------------------------------------------------------------------
# Runtime assembly & script execution
# ---------------------------------------------------------------------------


def build_api_namespace(
    rpc: _RpcClient,
    script_dir: str,
    script_name: str,
    script_args: list[str],
) -> dict[str, Any]:
    """Instantiate all Razor Enhanced API namespaces and register `RazorEnhanced`."""
    player = _PlayerAPI(rpc)
    items = _ItemsAPI(rpc, player)
    mobiles = _MobilesAPI(rpc)
    target = _TargetAPI(rpc)
    journal = _JournalAPI(rpc)
    gumps = _GumpsAPI(rpc)
    timer = _TimerAPI(rpc)
    misc = _MiscAPI(rpc, script_dir)
    autoloot = _AutoLootAPI(rpc)
    bandage_heal = _BandageHealAPI(rpc)
    organizer = _OrganizerAPI(rpc)
    restock = _RestockAPI(rpc)
    scavenger = _ScavengerAPI(rpc)
    pathfinding = _PathFindingAPI(rpc)
    spells = _SpellsAPI(rpc)
    client = _ClientProxy(rpc)

    api: dict[str, Any] = {
        "Player": player,
        "Items": items,
        "Mobiles": mobiles,
        "Target": target,
        "Journal": journal,
        "Gumps": gumps,
        "Timer": timer,
        "Misc": misc,
        "AutoLoot": autoloot,
        "BandageHeal": bandage_heal,
        "Organizer": organizer,
        "Restock": restock,
        "Scavenger": scavenger,
        "PathFinding": pathfinding,
        "Spells": spells,
        "Client": client,
        "Item": Item,
        "Mobile": Mobile,
        "Point3D": Point3D,
        "ScriptInterrupted": ScriptInterrupted,
        "ScriptRuntimeError": ScriptRuntimeError,
        "ScriptName": script_name,
        "ScriptArgs": list(script_args),
    }

    re_mod = types.ModuleType("RazorEnhanced")
    for k, v in api.items():
        setattr(re_mod, k, v)
    sys.modules["RazorEnhanced"] = re_mod

    return api


def _parse_script_args(raw_json: str) -> list[str]:
    if not raw_json:
        return []
    try:
        parsed = json.loads(raw_json)
    except json.JSONDecodeError:
        return []
    if isinstance(parsed, list):
        return [str(x) for x in parsed]
    return []


def main() -> int:
    # 1. Immediately detach JSON-RPC transport from fd 1 / fd 0 and redirect
    #    both OS fd 1 and sys.stdout to stderr so user `print()` can NEVER
    #    corrupt JSON-RPC framing.
    rpc_out_fd = os.dup(1)
    rpc_in_fd = os.dup(0)
    os.dup2(2, 1)
    sys.stdout = sys.stderr
    sys.stdin = io.StringIO("")

    rpc_out = os.fdopen(rpc_out_fd, "w", encoding="utf-8", buffering=1)
    rpc_in = os.fdopen(rpc_in_fd, "r", encoding="utf-8")
    rpc = _RpcClient(rpc_in, rpc_out)

    # 2. Install clean SIGTERM / SIGINT handlers raising ScriptInterrupted
    #    (a BaseException subclass that user `except Exception:` cannot catch).
    def _on_signal(signum: int, _frame: Any) -> None:
        rpc.mark_interrupted()
        raise ScriptInterrupted(f"Interrupted by signal {signum}")

    signal.signal(signal.SIGTERM, _on_signal)
    signal.signal(signal.SIGINT, _on_signal)

    try:
        script_path = os.environ.get("XUO_SCRIPT_PATH", "").strip()
        if not script_path and len(sys.argv) > 1:
            script_path = sys.argv[1]
        if not script_path:
            sys.stderr.write("python_host: missing XUO_SCRIPT_PATH\n")
            sys.stderr.flush()
            return 1

        script_path = os.path.abspath(script_path)
        script_dir = os.environ.get("XUO_SCRIPT_DIR", "").strip() or os.path.dirname(script_path)
        script_name = os.environ.get("XUO_SCRIPT_NAME", "").strip() or os.path.basename(script_path)
        script_args = _parse_script_args(os.environ.get("XUO_SCRIPT_ARGS_JSON", ""))

        if script_dir and script_dir not in sys.path:
            sys.path.insert(0, script_dir)
        sys.argv = [script_path, *script_args]

        install_dotnet_shims()
        api_globals = build_api_namespace(rpc, script_dir, script_name, script_args)

        with open(script_path, "r", encoding="utf-8-sig") as f:
            source = f.read()

        code = compile(source, script_path, "exec")
        script_globals: dict[str, Any] = {
            "__name__": "__main__",
            "__file__": script_path,
            "__package__": None,
            "__builtins__": __builtins__,
        }
        script_globals.update(api_globals)

        exec(code, script_globals, script_globals)
        return 0
    except ScriptInterrupted:
        return 0
    except SystemExit as exc:
        code_val = exc.code
        if code_val is None:
            return 0
        if isinstance(code_val, int):
            return code_val
        sys.stderr.write(f"{code_val}\n")
        sys.stderr.flush()
        return 1
    except BaseException:
        traceback.print_exc(file=sys.stderr)
        sys.stderr.flush()
        return 1
    finally:
        rpc.close()


if __name__ == "__main__":
    raise SystemExit(main())
