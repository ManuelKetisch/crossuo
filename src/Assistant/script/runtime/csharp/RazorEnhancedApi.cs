using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;

namespace RazorEnhanced
{
    /// <summary>
    /// Raised when the host requests script termination or the RPC channel closes.
    /// Caught at the top level of the script host so the process exits cleanly with code 0.
    /// </summary>
    public sealed class ScriptInterruptedException : Exception
    {
        public ScriptInterruptedException() : base("Script interrupted") { }
        public ScriptInterruptedException(string message) : base(message) { }
    }

    internal static class RpcClient
    {
        private static readonly object SyncLock = new object();
        private static readonly JsonSerializerOptions SerializerOptions = new JsonSerializerOptions
        {
            WriteIndented = false,
            Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
        };

        private static StreamReader _reader;
        private static StreamWriter _writer;
        private static long _nextId = 1;
        private static volatile bool _interrupted;

        internal static void Initialize(StreamReader reader, StreamWriter writer)
        {
            lock (SyncLock)
            {
                _reader = reader;
                _writer = writer;
                _nextId = 1;
                _interrupted = false;
            }
        }

        internal static void MarkInterrupted()
        {
            _interrupted = true;
        }

        internal static JsonElement Call(string ns, string fn, params object[] args)
        {
            if (_interrupted)
            {
                throw new ScriptInterruptedException("Script interrupted");
            }

            lock (SyncLock)
            {
                if (_interrupted)
                {
                    throw new ScriptInterruptedException("Script interrupted");
                }
                if (_reader == null || _writer == null)
                {
                    throw new InvalidOperationException("RPC transport is not initialized");
                }

                long requestId = _nextId++;
                string payload = SerializeRequest(requestId, ns, fn, args ?? Array.Empty<object>());

                try
                {
                    _writer.WriteLine(payload);
                }
                catch (IOException ex)
                {
                    _interrupted = true;
                    throw new ScriptInterruptedException("RPC stdout closed: " + ex.Message);
                }
                catch (ObjectDisposedException ex)
                {
                    _interrupted = true;
                    throw new ScriptInterruptedException("RPC stdout disposed: " + ex.Message);
                }

                string line;
                try
                {
                    line = _reader.ReadLine();
                }
                catch (IOException ex)
                {
                    _interrupted = true;
                    throw new ScriptInterruptedException("RPC stdin closed: " + ex.Message);
                }
                catch (ObjectDisposedException ex)
                {
                    _interrupted = true;
                    throw new ScriptInterruptedException("RPC stdin disposed: " + ex.Message);
                }

                if (line == null)
                {
                    _interrupted = true;
                    throw new ScriptInterruptedException("RPC stream reached EOF");
                }

                using (JsonDocument doc = JsonDocument.Parse(line))
                {
                    JsonElement root = doc.RootElement;
                    if (root.ValueKind != JsonValueKind.Object)
                    {
                        throw new InvalidOperationException("Invalid JSON-RPC response object");
                    }

                    if (root.TryGetProperty("interrupted", out JsonElement intEl) &&
                        intEl.ValueKind == JsonValueKind.True)
                    {
                        string msg = root.TryGetProperty("error", out JsonElement errEl) &&
                            errEl.ValueKind == JsonValueKind.String
                            ? errEl.GetString()
                            : "interrupted";
                        _interrupted = true;
                        throw new ScriptInterruptedException(msg ?? "interrupted");
                    }

                    if (root.TryGetProperty("id", out JsonElement idEl) &&
                        idEl.ValueKind == JsonValueKind.Number &&
                        idEl.TryGetInt64(out long respId) &&
                        respId != requestId)
                    {
                        throw new InvalidOperationException(
                            $"JSON-RPC response id mismatch: expected {requestId}, got {respId}");
                    }

                    bool ok = root.TryGetProperty("ok", out JsonElement okEl) &&
                        okEl.ValueKind == JsonValueKind.True;
                    if (!ok)
                    {
                        string errMsg = root.TryGetProperty("error", out JsonElement errEl) &&
                            errEl.ValueKind == JsonValueKind.String
                            ? errEl.GetString()
                            : $"RPC call {ns}.{fn} failed";
                        throw new InvalidOperationException(errMsg ?? $"RPC call {ns}.{fn} failed");
                    }

                    if (root.TryGetProperty("result", out JsonElement resultEl))
                    {
                        return resultEl.Clone();
                    }

                    using (JsonDocument nullDoc = JsonDocument.Parse("null"))
                    {
                        return nullDoc.RootElement.Clone();
                    }
                }
            }
        }

        private static string SerializeRequest(long id, string ns, string fn, object[] args)
        {
            using (var ms = new MemoryStream())
            {
                using (var w = new Utf8JsonWriter(ms, new JsonWriterOptions
                {
                    Indented = false,
                    Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
                }))
                {
                    w.WriteStartObject();
                    w.WriteNumber("id", id);
                    w.WriteString("ns", ns);
                    w.WriteString("fn", fn);
                    w.WritePropertyName("args");
                    w.WriteStartArray();
                    foreach (object arg in args)
                    {
                        JsonSerializer.Serialize(w, arg, SerializerOptions);
                    }
                    w.WriteEndArray();
                    w.WriteEndObject();
                }
                return Encoding.UTF8.GetString(ms.ToArray());
            }
        }

        internal static bool CallBool(string ns, string fn, params object[] args)
        {
            return ToBool(Call(ns, fn, args));
        }

        internal static bool TryCallBool(string ns, string fn, bool fallback, params object[] args)
        {
            try
            {
                return ToBool(Call(ns, fn, args));
            }
            catch (ScriptInterruptedException)
            {
                throw;
            }
            catch (InvalidOperationException)
            {
                return fallback;
            }
        }

        internal static int CallInt(string ns, string fn, params object[] args)
        {
            return ToInt(Call(ns, fn, args));
        }

        internal static uint CallUInt(string ns, string fn, params object[] args)
        {
            return ToUInt(Call(ns, fn, args));
        }

        internal static double CallDouble(string ns, string fn, params object[] args)
        {
            return ToDouble(Call(ns, fn, args));
        }

        internal static string CallString(string ns, string fn, params object[] args)
        {
            return ToStringValue(Call(ns, fn, args));
        }

        internal static List<string> CallStringList(string ns, string fn, params object[] args)
        {
            return ToStringList(Call(ns, fn, args));
        }

        internal static void CallVoid(string ns, string fn, params object[] args)
        {
            Call(ns, fn, args);
        }

        internal static bool ToBool(JsonElement el)
        {
            switch (el.ValueKind)
            {
                case JsonValueKind.True:
                    return true;
                case JsonValueKind.False:
                case JsonValueKind.Null:
                case JsonValueKind.Undefined:
                    return false;
                case JsonValueKind.Number:
                    return el.TryGetInt64(out long lv) ? lv != 0 : el.GetDouble() != 0.0;
                case JsonValueKind.String:
                    string s = el.GetString() ?? "";
                    if (bool.TryParse(s, out bool bv)) return bv;
                    if (int.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out int iv)) return iv != 0;
                    return !string.IsNullOrEmpty(s);
                default:
                    return false;
            }
        }

        internal static int ToInt(JsonElement el)
        {
            switch (el.ValueKind)
            {
                case JsonValueKind.Number:
                    if (el.TryGetInt32(out int i32)) return i32;
                    if (el.TryGetUInt32(out uint u32)) return unchecked((int)u32);
                    if (el.TryGetInt64(out long i64)) return unchecked((int)i64);
                    return (int)el.GetDouble();
                case JsonValueKind.String:
                    return ParseSerialOrInt(el.GetString());
                case JsonValueKind.True:
                    return 1;
                default:
                    return 0;
            }
        }

        internal static uint ToUInt(JsonElement el)
        {
            switch (el.ValueKind)
            {
                case JsonValueKind.Number:
                    if (el.TryGetUInt32(out uint u32)) return u32;
                    if (el.TryGetInt64(out long i64)) return unchecked((uint)i64);
                    return unchecked((uint)(long)el.GetDouble());
                case JsonValueKind.String:
                    return unchecked((uint)ParseSerialOrInt(el.GetString()));
                default:
                    return 0;
            }
        }

        internal static double ToDouble(JsonElement el)
        {
            switch (el.ValueKind)
            {
                case JsonValueKind.Number:
                    return el.GetDouble();
                case JsonValueKind.String:
                    return double.TryParse(el.GetString(), NumberStyles.Float, CultureInfo.InvariantCulture, out double dv)
                        ? dv
                        : 0.0;
                default:
                    return 0.0;
            }
        }

        internal static string ToStringValue(JsonElement el)
        {
            switch (el.ValueKind)
            {
                case JsonValueKind.String:
                    return el.GetString() ?? "";
                case JsonValueKind.Null:
                case JsonValueKind.Undefined:
                    return "";
                default:
                    return el.ToString();
            }
        }

        internal static List<string> ToStringList(JsonElement el)
        {
            var list = new List<string>();
            if (el.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement item in el.EnumerateArray())
                {
                    list.Add(ToStringValue(item));
                }
            }
            return list;
        }

        internal static object ToClrObject(JsonElement el)
        {
            switch (el.ValueKind)
            {
                case JsonValueKind.Null:
                case JsonValueKind.Undefined:
                    return null;
                case JsonValueKind.True:
                    return true;
                case JsonValueKind.False:
                    return false;
                case JsonValueKind.String:
                    return el.GetString();
                case JsonValueKind.Number:
                    if (el.TryGetInt32(out int i32)) return i32;
                    if (el.TryGetInt64(out long i64)) return i64;
                    return el.GetDouble();
                case JsonValueKind.Array:
                    var arr = new List<object>();
                    foreach (JsonElement item in el.EnumerateArray())
                    {
                        arr.Add(ToClrObject(item));
                    }
                    return arr;
                case JsonValueKind.Object:
                    var dict = new Dictionary<string, object>();
                    foreach (JsonProperty prop in el.EnumerateObject())
                    {
                        dict[prop.Name] = ToClrObject(prop.Value);
                    }
                    return dict;
                default:
                    return null;
            }
        }

        internal static int ParseSerialOrInt(string text)
        {
            if (string.IsNullOrWhiteSpace(text))
            {
                return 0;
            }
            string trimmed = text.Trim();
            if (trimmed.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            {
                if (uint.TryParse(trimmed.Substring(2), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint hexVal))
                {
                    return unchecked((int)hexVal);
                }
            }
            if (int.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out int decVal))
            {
                return decVal;
            }
            if (uint.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out uint uDecVal))
            {
                return unchecked((int)uDecVal);
            }
            return 0;
        }

        internal static string FormatSerialHex(int serial)
        {
            return $"0x{unchecked((uint)serial):X8}";
        }

        internal static bool TryGetProp(JsonElement obj, out JsonElement value, params string[] names)
        {
            if (obj.ValueKind == JsonValueKind.Object)
            {
                foreach (string name in names)
                {
                    if (obj.TryGetProperty(name, out value))
                    {
                        return true;
                    }
                }
            }
            value = default;
            return false;
        }

        internal static int GetIntProp(JsonElement obj, int fallback, params string[] names)
        {
            return TryGetProp(obj, out JsonElement v, names) ? ToInt(v) : fallback;
        }

        internal static bool GetBoolProp(JsonElement obj, bool fallback, params string[] names)
        {
            return TryGetProp(obj, out JsonElement v, names) ? ToBool(v) : fallback;
        }

        internal static string GetStringProp(JsonElement obj, string fallback, params string[] names)
        {
            return TryGetProp(obj, out JsonElement v, names) ? ToStringValue(v) : fallback;
        }

        internal static Point3D ParsePoint3D(JsonElement el)
        {
            if (el.ValueKind != JsonValueKind.Object)
            {
                return new Point3D(0, 0, 0, 0);
            }
            int x = GetIntProp(el, 0, "X", "x");
            int y = GetIntProp(el, 0, "Y", "y");
            int z = GetIntProp(el, 0, "Z", "z");
            int map = GetIntProp(el, 0, "Map", "map");
            return new Point3D(x, y, z, map);
        }

        internal static List<Property> ParseProperties(JsonElement el)
        {
            var props = new List<Property>();
            if (el.ValueKind != JsonValueKind.Array)
            {
                return props;
            }
            foreach (JsonElement entry in el.EnumerateArray())
            {
                if (entry.ValueKind == JsonValueKind.String)
                {
                    props.Add(new Property(0, entry.GetString() ?? ""));
                }
                else if (entry.ValueKind == JsonValueKind.Object)
                {
                    int num = GetIntProp(entry, 0, "Number", "number");
                    string args = GetStringProp(entry, "", "Args", "Text", "text", "args");
                    props.Add(new Property(num, args));
                }
                else
                {
                    props.Add(new Property(0, ToStringValue(entry)));
                }
            }
            return props;
        }

        internal static Item ParseItem(JsonElement el)
        {
            if (el.ValueKind != JsonValueKind.Object)
            {
                return null;
            }
            var item = new Item
            {
                Serial = GetIntProp(el, 0, "Serial", "serial"),
                SerialHex = GetStringProp(el, "", "SerialHex", "serialHex"),
                ItemID = GetIntProp(el, 0, "ItemID", "Graphic", "itemID", "graphic"),
                Hue = GetIntProp(el, 0, "Hue", "Color", "hue", "color"),
                Amount = GetIntProp(el, 0, "Amount", "Count", "amount", "count"),
                Container = GetIntProp(el, 0, "Container", "container"),
                Layer = GetIntProp(el, 0, "Layer", "layer"),
                X = GetIntProp(el, 0, "X", "x"),
                Y = GetIntProp(el, 0, "Y", "y"),
                Z = GetIntProp(el, 0, "Z", "z"),
                Map = GetIntProp(el, 0, "Map", "map"),
                IsContainer = GetBoolProp(el, false, "IsContainer", "isContainer"),
                IsCorpse = GetBoolProp(el, false, "IsCorpse", "isCorpse"),
                OnGround = GetBoolProp(el, false, "OnGround", "onGround"),
                Name = GetStringProp(el, "", "Name", "name")
            };

            if (string.IsNullOrEmpty(item.SerialHex))
            {
                item.SerialHex = FormatSerialHex(item.Serial);
            }

            if (TryGetProp(el, out JsonElement posEl, "Position", "position") &&
                posEl.ValueKind == JsonValueKind.Object)
            {
                item.Position = ParsePoint3D(posEl);
                if (!TryGetProp(el, out _, "X", "x")) item.X = item.Position.X;
                if (!TryGetProp(el, out _, "Y", "y")) item.Y = item.Position.Y;
                if (!TryGetProp(el, out _, "Z", "z")) item.Z = item.Position.Z;
                if (!TryGetProp(el, out _, "Map", "map")) item.Map = item.Position.Map;
            }
            else
            {
                item.Position = new Point3D(item.X, item.Y, item.Z, item.Map);
            }

            item.RootContainer = GetIntProp(el, item.Container, "RootContainer", "rootContainer");

            if (TryGetProp(el, out JsonElement propsEl, "Properties", "properties"))
            {
                item.Properties = ParseProperties(propsEl);
            }
            else
            {
                item.Properties = new List<Property>();
            }

            item.Contains = new List<Item>();
            if (TryGetProp(el, out JsonElement containsEl, "Contains", "contains") &&
                containsEl.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement child in containsEl.EnumerateArray())
                {
                    Item parsedChild = ParseItem(child);
                    if (parsedChild != null)
                    {
                        item.Contains.Add(parsedChild);
                    }
                }
            }

            return item;
        }

        internal static List<Item> ParseItemList(JsonElement el)
        {
            var list = new List<Item>();
            if (el.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement entry in el.EnumerateArray())
                {
                    Item item = ParseItem(entry);
                    if (item != null)
                    {
                        list.Add(item);
                    }
                }
            }
            return list;
        }

        internal static Mobile ParseMobile(JsonElement el)
        {
            if (el.ValueKind != JsonValueKind.Object)
            {
                return null;
            }
            var mob = new Mobile
            {
                Serial = GetIntProp(el, 0, "Serial", "serial"),
                SerialHex = GetStringProp(el, "", "SerialHex", "serialHex"),
                Body = GetIntProp(el, 0, "Body", "MobileID", "Graphic", "body", "mobileID"),
                Hue = GetIntProp(el, 0, "Hue", "Color", "hue", "color"),
                Name = GetStringProp(el, "", "Name", "name"),
                Hits = GetIntProp(el, 0, "Hits", "hits"),
                HitsMax = GetIntProp(el, 0, "HitsMax", "hitsMax"),
                Mana = GetIntProp(el, 0, "Mana", "mana"),
                ManaMax = GetIntProp(el, 0, "ManaMax", "manaMax"),
                Stam = GetIntProp(el, 0, "Stam", "stam"),
                StamMax = GetIntProp(el, 0, "StamMax", "stamMax"),
                Notoriety = GetIntProp(el, 0, "Notoriety", "notoriety"),
                Direction = GetIntProp(el, 0, "Direction", "direction"),
                X = GetIntProp(el, 0, "X", "x"),
                Y = GetIntProp(el, 0, "Y", "y"),
                Z = GetIntProp(el, 0, "Z", "z"),
                Map = GetIntProp(el, 0, "Map", "map"),
                Distance = GetIntProp(el, 0, "Distance", "distance"),
                Poisoned = GetBoolProp(el, false, "Poisoned", "poisoned"),
                Paralyzed = GetBoolProp(el, false, "Paralyzed", "Paralized", "paralyzed", "paralized"),
                YellowHits = GetBoolProp(el, false, "YellowHits", "Mortaled", "yellowHits"),
                WarMode = GetBoolProp(el, false, "WarMode", "Warmode", "warMode"),
                Hidden = GetBoolProp(el, false, "Hidden", "hidden"),
                Dead = GetBoolProp(el, false, "Dead", "IsGhost", "dead", "isGhost"),
                IsHuman = GetBoolProp(el, false, "IsHuman", "isHuman"),
                Female = GetBoolProp(el, false, "Female", "female")
            };

            if (string.IsNullOrEmpty(mob.SerialHex))
            {
                mob.SerialHex = FormatSerialHex(mob.Serial);
            }

            if (TryGetProp(el, out JsonElement posEl, "Position", "position") &&
                posEl.ValueKind == JsonValueKind.Object)
            {
                mob.Position = ParsePoint3D(posEl);
                if (!TryGetProp(el, out _, "X", "x")) mob.X = mob.Position.X;
                if (!TryGetProp(el, out _, "Y", "y")) mob.Y = mob.Position.Y;
                if (!TryGetProp(el, out _, "Z", "z")) mob.Z = mob.Position.Z;
                if (!TryGetProp(el, out _, "Map", "map")) mob.Map = mob.Position.Map;
            }
            else
            {
                mob.Position = new Point3D(mob.X, mob.Y, mob.Z, mob.Map);
            }

            if (TryGetProp(el, out JsonElement bpEl, "Backpack", "backpack"))
            {
                if (bpEl.ValueKind == JsonValueKind.Object)
                {
                    mob.Backpack = ParseItem(bpEl);
                    mob.BackpackSerial = mob.Backpack?.Serial ?? 0;
                }
                else
                {
                    int bpSerial = ToInt(bpEl);
                    mob.BackpackSerial = bpSerial;
                    if (bpSerial != 0)
                    {
                        mob.Backpack = new Item
                        {
                            Serial = bpSerial,
                            SerialHex = FormatSerialHex(bpSerial),
                            IsContainer = true
                        };
                    }
                }
            }

            if (TryGetProp(el, out JsonElement propsEl, "Properties", "properties"))
            {
                mob.Properties = ParseProperties(propsEl);
            }
            else
            {
                mob.Properties = new List<Property>();
            }

            return mob;
        }

        internal static List<Mobile> ParseMobileList(JsonElement el)
        {
            var list = new List<Mobile>();
            if (el.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement entry in el.EnumerateArray())
                {
                    Mobile mob = ParseMobile(entry);
                    if (mob != null)
                    {
                        list.Add(mob);
                    }
                }
            }
            return list;
        }
    }

    public struct Point3D : IEquatable<Point3D>
    {
        public int X { get; set; }
        public int Y { get; set; }
        public int Z { get; set; }
        public int Map { get; set; }

        public Point3D(int x, int y, int z, int map = 0)
        {
            X = x;
            Y = y;
            Z = z;
            Map = map;
        }

        public int DistanceTo(Point3D other)
        {
            return Math.Max(Math.Abs(X - other.X), Math.Abs(Y - other.Y));
        }

        public bool Equals(Point3D other) => X == other.X && Y == other.Y && Z == other.Z;
        public override bool Equals(object obj) => obj is Point3D p && Equals(p);
        public override int GetHashCode() => HashCode.Combine(X, Y, Z);
        public override string ToString() => $"({X}, {Y}, {Z})";
    }

    public class Property
    {
        public int Number { get; set; }
        public string Args { get; set; }
        public string Text => Args;

        public Property() : this(0, "") { }

        public Property(int number, string args)
        {
            Number = number;
            Args = args ?? "";
        }

        public override string ToString() => Args ?? "";

        public static implicit operator string(Property prop) => prop?.Args ?? "";
    }

    public class Item
    {
        public int Serial { get; set; }
        public string SerialHex { get; set; } = "0x00000000";
        public int ItemID { get; set; }
        public int Graphic => ItemID;
        public int Hue { get; set; }
        public int Color => Hue;
        public int Amount { get; set; }
        public int Count => Amount;
        public int Container { get; set; }
        public int RootContainer { get; set; }
        public int Layer { get; set; }
        public int X { get; set; }
        public int Y { get; set; }
        public int Z { get; set; }
        public int Map { get; set; }
        public Point3D Position { get; set; }
        public bool IsContainer { get; set; }
        public bool IsCorpse { get; set; }
        public bool OnGround { get; set; }
        public bool Visible => true;
        public string Name { get; set; } = "";
        public List<Property> Properties { get; set; } = new List<Property>();
        public List<Item> Contains { get; set; } = new List<Item>();

        public int DistanceTo(Point3D other) => Position.DistanceTo(other);
        public int DistanceTo(Item other) => other == null ? 0 : Position.DistanceTo(other.Position);
        public int DistanceTo(Mobile other) => other == null ? 0 : Position.DistanceTo(other.Position);
        public override string ToString() => $"{Name} ({SerialHex})";
    }

    public class Mobile
    {
        public int Serial { get; set; }
        public string SerialHex { get; set; } = "0x00000000";
        public int Body { get; set; }
        public int MobileID => Body;
        public int Graphic => Body;
        public int Hue { get; set; }
        public int Color => Hue;
        public string Name { get; set; } = "";
        public int Hits { get; set; }
        public int HitsMax { get; set; }
        public int Mana { get; set; }
        public int ManaMax { get; set; }
        public int Stam { get; set; }
        public int StamMax { get; set; }
        public int Notoriety { get; set; }
        public int Direction { get; set; }
        public int X { get; set; }
        public int Y { get; set; }
        public int Z { get; set; }
        public int Map { get; set; }
        public Point3D Position { get; set; }
        public int Distance { get; set; }
        public int BackpackSerial { get; set; }
        public Item Backpack { get; set; }
        public bool Poisoned { get; set; }
        public bool Paralyzed { get; set; }
        public bool Paralized => Paralyzed;
        public bool YellowHits { get; set; }
        public bool Mortaled => YellowHits;
        public bool WarMode { get; set; }
        public bool Hidden { get; set; }
        public bool Visible => !Hidden;
        public bool Dead { get; set; }
        public bool IsGhost => Dead;
        public bool IsHuman { get; set; }
        public bool Female { get; set; }
        public List<Property> Properties { get; set; } = new List<Property>();

        public int DistanceTo(Point3D other) => Position.DistanceTo(other);
        public int DistanceTo(Mobile other) => other == null ? 0 : Position.DistanceTo(other.Position);
        public int DistanceTo(Item other) => other == null ? 0 : Position.DistanceTo(other.Position);
        public override string ToString() => $"{Name} ({SerialHex})";
    }

    public static class Player
    {
        public static int Hits => RpcClient.CallInt("Player", "Hits");
        public static int HitsMax => RpcClient.CallInt("Player", "HitsMax");
        public static int Mana => RpcClient.CallInt("Player", "Mana");
        public static int ManaMax => RpcClient.CallInt("Player", "ManaMax");
        public static int Stam => RpcClient.CallInt("Player", "Stam");
        public static int StamMax => RpcClient.CallInt("Player", "StamMax");
        public static int Weight => RpcClient.CallInt("Player", "Weight");
        public static int MaxWeight => RpcClient.CallInt("Player", "MaxWeight");
        public static int Gold => RpcClient.CallInt("Player", "Gold");
        public static bool Poisoned => RpcClient.CallBool("Player", "Poisoned");
        public static bool YellowHits => RpcClient.CallBool("Player", "YellowHits");
        public static bool Paralized => RpcClient.CallBool("Player", "Paralized");
        public static bool Paralyzed => RpcClient.CallBool("Player", "Paralyzed");
        public static bool WarMode => RpcClient.CallBool("Player", "WarMode");
        public static int Serial => RpcClient.CallInt("Player", "Serial");
        public static string Name => RpcClient.CallString("Player", "Name");
        public static Point3D Position => RpcClient.ParsePoint3D(RpcClient.Call("Player", "Position"));
        public static int Map => RpcClient.CallInt("Player", "Map");
        public static Item Backpack => RpcClient.ParseItem(RpcClient.Call("Player", "Backpack"));
        public static bool IsGhost => RpcClient.CallBool("Player", "Dead");
        public static bool InParty => RpcClient.TryCallBool("Player", "InParty", false);
        public static bool Visible => !RpcClient.CallBool("Player", "Hidden");
        public static bool Connected => RpcClient.CallBool("Client", "Connected");

        // Additional helpful Player stats exposed by apiscript.cpp
        public static int Str => RpcClient.CallInt("Player", "Str");
        public static int Int => RpcClient.CallInt("Player", "Int");
        public static int Dex => RpcClient.CallInt("Player", "Dex");
        public static int Armor => RpcClient.CallInt("Player", "Armor");
        public static int Followers => RpcClient.CallInt("Player", "Followers");
        public static int FollowersMax => RpcClient.CallInt("Player", "MaxFollowers");
        public static int FireResistance => RpcClient.CallInt("Player", "FireResistance");
        public static int ColdResistance => RpcClient.CallInt("Player", "ColdResistance");
        public static int PoisonResistance => RpcClient.CallInt("Player", "PoisonResistance");
        public static int EnergyResistance => RpcClient.CallInt("Player", "EnergyResistance");
        public static int Luck => RpcClient.CallInt("Player", "Luck");
        public static int Direction => RpcClient.CallInt("Player", "Direction");
        public static int Notoriety => RpcClient.CallInt("Player", "Notoriety");
        public static bool Hidden => RpcClient.CallBool("Player", "Hidden");
        public static bool Dead => RpcClient.CallBool("Player", "Dead");
        public static bool Female => RpcClient.CallBool("Player", "Female");

        public static Item GetItemOnLayer(string layer)
        {
            return RpcClient.ParseItem(RpcClient.Call("Player", "GetLayerItem", layer ?? ""));
        }

        public static Item GetItemOnLayer(int layer)
        {
            return RpcClient.ParseItem(RpcClient.Call("Player", "GetLayerItem", layer));
        }

        public static Item GetLayerItem(string layer) => GetItemOnLayer(layer);
        public static Item GetLayerItem(int layer) => GetItemOnLayer(layer);

        public static bool BuffsExist(string buffName, bool okay = true)
        {
            _ = okay;
            return RpcClient.CallBool("Player", "BuffsExist", buffName ?? "");
        }

        public static void UseSkill(string skillName)
        {
            RpcClient.CallVoid("Player", "UseSkill", skillName ?? "");
        }

        public static void UseSkill(string skillName, int targetSerial)
        {
            RpcClient.CallVoid("Player", "UseSkill", skillName ?? "", RpcClient.FormatSerialHex(targetSerial));
        }

        public static void UseSkill(string skillName, Mobile target)
        {
            if (target != null)
            {
                UseSkill(skillName, target.Serial);
            }
            else
            {
                UseSkill(skillName);
            }
        }

        public static void UseSkill(string skillName, Item target)
        {
            if (target != null)
            {
                UseSkill(skillName, target.Serial);
            }
            else
            {
                UseSkill(skillName);
            }
        }

        public static void SetWarMode(bool warFlag)
        {
            RpcClient.CallVoid("Player", "SetWarMode", warFlag);
        }

        public static bool Walk(string direction)
        {
            return RpcClient.CallBool("Player", "Walk", direction ?? "");
        }

        public static bool Run(string direction)
        {
            return RpcClient.CallBool("Player", "Run", direction ?? "");
        }

        public static bool PathFindTo(int x, int y, int z)
        {
            return RpcClient.CallBool("Player", "PathFindTo", x, y, z);
        }

        public static bool PathFindTo(int x, int y)
        {
            return RpcClient.CallBool("Player", "PathFindTo", x, y);
        }

        public static bool PathFindTo(Point3D position)
        {
            return PathFindTo(position.X, position.Y, position.Z);
        }

        public static void ChatSay(int color, string message)
        {
            RpcClient.CallVoid("Client", "Say", message ?? "", color);
        }

        public static void ChatSay(string message)
        {
            RpcClient.CallVoid("Client", "Say", message ?? "");
        }

        public static void HeadMessage(int color, string message)
        {
            RpcClient.CallVoid("Client", "CharPrint", "self", $"0x{color & 0xFFFF:X4}", message ?? "");
        }
    }

    public static class Items
    {
        public class Filter
        {
            public bool Enabled { get; set; } = true;
            public List<int> Serials { get; set; } = new List<int>();
            public List<int> Graphics { get; set; } = new List<int>();
            public List<int> Hues { get; set; } = new List<int>();
            public string Name { get; set; } = "";
            public double RangeMin { get; set; } = -1;
            public double RangeMax { get; set; } = -1;
            public int Movable { get; set; } = -1;
            public int IsCorpse { get; set; } = -1;
            public int IsContainer { get; set; } = -1;
            public int OnGround { get; set; } = -1;
            public bool CheckIgnoreObject { get; set; } = true;
            public List<int> Layers { get; set; } = new List<int>();
        }

        public static Item FindBySerial(int serial)
        {
            return RpcClient.ParseItem(RpcClient.Call("Items", "FindBySerial", serial));
        }

        public static Item FindByID(
            int itemid,
            int color = -1,
            int container = -1,
            bool recursive = true,
            bool ignoreCheck = false)
        {
            return RpcClient.ParseItem(
                RpcClient.Call("Items", "FindByID", itemid, color, container, recursive, ignoreCheck));
        }

        public static Item FindByID(
            List<int> itemids,
            int color = -1,
            int container = -1,
            bool recursive = true,
            bool ignoreCheck = false)
        {
            return RpcClient.ParseItem(
                RpcClient.Call("Items", "FindByID", itemids ?? new List<int>(), color, container, recursive, ignoreCheck));
        }

        public static List<Item> FindAllByID(
            int itemid,
            int color = -1,
            int container = -1,
            bool recursive = true,
            bool ignoreCheck = false)
        {
            return RpcClient.ParseItemList(
                RpcClient.Call("Items", "FindAllByID", itemid, color, container, recursive, ignoreCheck));
        }

        public static List<Item> FindAllByID(
            List<int> itemids,
            int color = -1,
            int container = -1,
            bool recursive = true,
            bool ignoreCheck = false)
        {
            return RpcClient.ParseItemList(
                RpcClient.Call("Items", "FindAllByID", itemids ?? new List<int>(), color, container, recursive, ignoreCheck));
        }

        public static int BackpackCount(int itemid, int color = -1)
        {
            return RpcClient.CallInt("Items", "BackpackCount", itemid, color);
        }

        public static bool UseItem(int serial)
        {
            return RpcClient.CallBool("Items", "UseItem", serial);
        }

        public static bool UseItem(Item item)
        {
            return item != null && UseItem(item.Serial);
        }

        public static bool UseItem(int serial, int targetSerial)
        {
            return RpcClient.CallBool("Items", "UseItem", serial, targetSerial);
        }

        public static bool UseItem(Item item, Item target)
        {
            return item != null && target != null && UseItem(item.Serial, target.Serial);
        }

        public static bool UseItem(Item item, Mobile target)
        {
            return item != null && target != null && UseItem(item.Serial, target.Serial);
        }

        public static bool UseItemByID(int itemid, int color = -1)
        {
            return RpcClient.CallBool("Items", "UseItemByID", itemid, color);
        }

        public static bool Move(int source, int destination, int amount, int x = -1, int y = -1)
        {
            if (x >= 0 || y >= 0)
            {
                return RpcClient.CallBool("Items", "Move", source, destination, amount, x, y);
            }
            return RpcClient.CallBool("Items", "Move", source, destination, amount);
        }

        public static bool Move(Item source, Item destination, int amount, int x = -1, int y = -1)
        {
            if (source == null) return false;
            return Move(source.Serial, destination?.Serial ?? -1, amount, x, y);
        }

        public static bool Move(Item source, int destination, int amount, int x = -1, int y = -1)
        {
            if (source == null) return false;
            return Move(source.Serial, destination, amount, x, y);
        }

        public static bool Move(int source, Item destination, int amount, int x = -1, int y = -1)
        {
            return Move(source, destination?.Serial ?? -1, amount, x, y);
        }

        public static bool MoveOnGround(int source, int amount, int x, int y, int z)
        {
            return RpcClient.CallBool("Items", "MoveOnGround", source, amount, x, y, z);
        }

        public static bool MoveOnGround(Item source, int amount, int x, int y, int z)
        {
            return source != null && MoveOnGround(source.Serial, amount, x, y, z);
        }

        public static double GetPropValue(int serial, string name)
        {
            return RpcClient.CallDouble("Items", "GetPropValue", serial, name ?? "");
        }

        public static double GetPropValue(Item item, string name)
        {
            return item == null ? 0.0 : GetPropValue(item.Serial, name);
        }

        public static List<string> GetPropStringList(int serial)
        {
            return RpcClient.CallStringList("Items", "GetPropStringList", serial);
        }

        public static List<string> GetPropStringList(Item item)
        {
            return item == null ? new List<string>() : GetPropStringList(item.Serial);
        }

        public static bool WaitForContents(int serial, int delay = 1000)
        {
            return RpcClient.CallBool("Items", "WaitForContents", serial, delay);
        }

        public static bool WaitForContents(Item item, int delay = 1000)
        {
            return item != null && WaitForContents(item.Serial, delay);
        }

        public static List<Item> ApplyFilter(Filter filter)
        {
            if (filter == null || !filter.Enabled)
            {
                return new List<Item>();
            }

            var candidates = new List<Item>();
            var seen = new HashSet<int>();

            void AddUnique(IEnumerable<Item> items)
            {
                foreach (Item it in items)
                {
                    if (it != null && seen.Add(it.Serial))
                    {
                        candidates.Add(it);
                    }
                }
            }

            if (filter.Serials != null && filter.Serials.Count > 0)
            {
                foreach (int serial in filter.Serials)
                {
                    Item found = FindBySerial(serial);
                    if (found != null && seen.Add(found.Serial))
                    {
                        candidates.Add(found);
                    }
                }
            }
            else
            {
                object idArg = (filter.Graphics != null && filter.Graphics.Count > 0)
                    ? (object)filter.Graphics
                    : -1;
                int colorArg = (filter.Hues != null && filter.Hues.Count == 1) ? filter.Hues[0] : -1;
                int rangeArg = filter.RangeMax >= 0 ? (int)Math.Ceiling(filter.RangeMax) : -1;

                if (filter.OnGround != 0)
                {
                    AddUnique(RpcClient.ParseItemList(
                        RpcClient.Call("Items", "FindAllByID", idArg, colorArg, "ground", rangeArg, filter.CheckIgnoreObject)));
                }
                if (filter.OnGround <= 0)
                {
                    AddUnique(RpcClient.ParseItemList(
                        RpcClient.Call("Items", "FindAllByID", idArg, colorArg, "backpack", true, filter.CheckIgnoreObject)));
                }
            }

            bool needPlayerPos = filter.RangeMin >= 0 || filter.RangeMax >= 0;
            Point3D playerPos = needPlayerPos ? Player.Position : default;

            var results = new List<Item>();
            foreach (Item it in candidates)
            {
                if (filter.Serials != null && filter.Serials.Count > 0 && !filter.Serials.Contains(it.Serial))
                    continue;
                if (filter.Graphics != null && filter.Graphics.Count > 0 && !filter.Graphics.Contains(it.ItemID))
                    continue;
                if (filter.Hues != null && filter.Hues.Count > 0 && !filter.Hues.Contains(it.Hue))
                    continue;
                if (!string.IsNullOrEmpty(filter.Name) &&
                    (it.Name == null || it.Name.IndexOf(filter.Name, StringComparison.OrdinalIgnoreCase) < 0))
                    continue;
                if (filter.IsCorpse == 0 && it.IsCorpse) continue;
                if (filter.IsCorpse > 0 && !it.IsCorpse) continue;
                if (filter.IsContainer == 0 && it.IsContainer) continue;
                if (filter.IsContainer > 0 && !it.IsContainer) continue;
                if (filter.OnGround == 0 && it.OnGround) continue;
                if (filter.OnGround > 0 && !it.OnGround) continue;
                if (filter.Layers != null && filter.Layers.Count > 0 && !filter.Layers.Contains(it.Layer))
                    continue;
                if (needPlayerPos)
                {
                    int dist = playerPos.DistanceTo(it.Position);
                    if (filter.RangeMin >= 0 && dist < filter.RangeMin) continue;
                    if (filter.RangeMax >= 0 && dist > filter.RangeMax) continue;
                }
                results.Add(it);
            }

            return results;
        }

        public static Item Select(List<Item> items, string selector)
        {
            if (items == null || items.Count == 0)
            {
                return null;
            }
            string sel = (selector ?? "nearest").Trim().ToLowerInvariant();
            if (sel == "random")
            {
                return items[Random.Shared.Next(items.Count)];
            }
            if (sel == "first")
            {
                return items[0];
            }
            if (sel == "last")
            {
                return items[items.Count - 1];
            }

            Point3D playerPos = Player.Position;
            Item best = items[0];
            int bestDist = playerPos.DistanceTo(best.Position);

            for (int i = 1; i < items.Count; i++)
            {
                Item cand = items[i];
                int candDist = playerPos.DistanceTo(cand.Position);
                bool better;
                if (sel == "farthest" || sel == "furthest")
                {
                    better = candDist > bestDist;
                }
                else if (sel == "weakest" || sel == "lowest")
                {
                    better = cand.Amount < best.Amount || (cand.Amount == best.Amount && candDist < bestDist);
                }
                else if (sel == "strongest" || sel == "highest")
                {
                    better = cand.Amount > best.Amount || (cand.Amount == best.Amount && candDist < bestDist);
                }
                else
                {
                    better = candDist < bestDist;
                }

                if (better)
                {
                    best = cand;
                    bestDist = candDist;
                }
            }
            return best;
        }
    }

    public static class Mobiles
    {
        public class Filter
        {
            public bool Enabled { get; set; } = true;
            public double RangeMin { get; set; } = -1;
            public double RangeMax { get; set; } = -1;
            public List<byte> Notorieties { get; set; } = new List<byte>();
            public List<int> Bodies { get; set; } = new List<int>();
            public int Poisoned { get; set; } = -1;
            public int IsHuman { get; set; } = -1;
            public int IsGhost { get; set; } = -1;
            public int Warmode { get; set; } = -1;
            public bool CheckLineOfSight { get; set; } = false;
            public bool CheckIgnoreObject { get; set; } = true;
            public string Name { get; set; } = "";
            public string Selector { get; set; } = "";
            public int Blessed { get; set; } = -1;
            public int Female { get; set; } = -1;
            public int Friend { get; set; } = -1;
            public int Paralized { get; set; } = -1;
            public List<int> Serials { get; set; } = new List<int>();
        }

        public static Mobile FindBySerial(int serial)
        {
            return RpcClient.ParseMobile(RpcClient.Call("Mobiles", "FindBySerial", serial));
        }

        public static List<Mobile> ApplyFilter(Filter filter)
        {
            if (filter == null || !filter.Enabled)
            {
                return new List<Mobile>();
            }

            var payload = new Dictionary<string, object>
            {
                ["Enabled"] = filter.Enabled,
                ["RangeMin"] = filter.RangeMin >= 0 ? (int)Math.Floor(filter.RangeMin) : -1,
                ["RangeMax"] = filter.RangeMax >= 0 ? (int)Math.Ceiling(filter.RangeMax) : -1,
                ["Notorieties"] = (filter.Notorieties ?? new List<byte>()).Select(b => (int)b).ToList(),
                ["Bodies"] = filter.Bodies ?? new List<int>(),
                ["Poisoned"] = filter.Poisoned,
                ["IsHuman"] = filter.IsHuman,
                ["IsGhost"] = filter.IsGhost,
                ["Warmode"] = filter.Warmode,
                ["CheckLineOfSight"] = filter.CheckLineOfSight,
                ["CheckIgnoreObject"] = filter.CheckIgnoreObject,
                ["Name"] = filter.Name ?? "",
                ["Selector"] = filter.Selector ?? ""
            };

            List<Mobile> list = RpcClient.ParseMobileList(RpcClient.Call("Mobiles", "ApplyFilter", payload));
            if (filter.Serials != null && filter.Serials.Count > 0)
            {
                list = list.Where(m => filter.Serials.Contains(m.Serial)).ToList();
            }
            if (filter.Female == 0) list = list.Where(m => !m.Female).ToList();
            else if (filter.Female > 0) list = list.Where(m => m.Female).ToList();
            if (filter.Paralized == 0) list = list.Where(m => !m.Paralyzed).ToList();
            else if (filter.Paralized > 0) list = list.Where(m => m.Paralyzed).ToList();
            return list;
        }

        public static Mobile Select(List<Mobile> mobiles, string selector)
        {
            if (mobiles == null || mobiles.Count == 0)
            {
                return null;
            }
            var dtoList = mobiles.Select(m => new Dictionary<string, object>
            {
                ["Serial"] = m.Serial,
                ["Name"] = m.Name ?? "",
                ["Body"] = m.Body,
                ["Hue"] = m.Hue,
                ["Hits"] = m.Hits,
                ["HitsMax"] = m.HitsMax,
                ["Mana"] = m.Mana,
                ["ManaMax"] = m.ManaMax,
                ["Stam"] = m.Stam,
                ["StamMax"] = m.StamMax,
                ["Notoriety"] = m.Notoriety,
                ["Distance"] = m.Distance,
                ["X"] = m.X,
                ["Y"] = m.Y,
                ["Z"] = m.Z,
                ["Map"] = m.Map,
                ["Poisoned"] = m.Poisoned,
                ["Paralyzed"] = m.Paralyzed,
                ["YellowHits"] = m.YellowHits,
                ["WarMode"] = m.WarMode,
                ["Hidden"] = m.Hidden,
                ["Dead"] = m.Dead,
                ["IsHuman"] = m.IsHuman,
                ["Female"] = m.Female
            }).ToList();

            JsonElement selected = RpcClient.Call("Mobiles", "Select", dtoList, selector ?? "Nearest");
            Mobile parsed = RpcClient.ParseMobile(selected);
            if (parsed == null)
            {
                return null;
            }
            Mobile match = mobiles.FirstOrDefault(m => m.Serial == parsed.Serial);
            return match ?? parsed;
        }

        public static bool UseMobile(int serial)
        {
            return RpcClient.CallBool("Mobiles", "UseMobile", serial);
        }

        public static bool UseMobile(Mobile mobile)
        {
            return mobile != null && UseMobile(mobile.Serial);
        }

        public static bool WaitForStats(int serial, int delay = 1000)
        {
            return RpcClient.CallBool("Mobiles", "WaitForStats", serial, delay);
        }

        public static bool WaitForStats(Mobile mobile, int delay = 1000)
        {
            return mobile != null && WaitForStats(mobile.Serial, delay);
        }
    }

    public static class Target
    {
        public static bool HasTarget()
        {
            return RpcClient.CallBool("Target", "HasTarget");
        }

        public static bool WaitForTarget(int delay = 1000, bool noshow = false)
        {
            _ = noshow;
            return RpcClient.CallBool("Target", "WaitForTarget", delay);
        }

        public static bool TargetExecute(int serial)
        {
            return RpcClient.CallBool("Target", "TargetExecute", serial);
        }

        public static bool TargetExecute(Item item)
        {
            return item != null && TargetExecute(item.Serial);
        }

        public static bool TargetExecute(Mobile mobile)
        {
            return mobile != null && TargetExecute(mobile.Serial);
        }

        public static bool TargetExecute(int x, int y, int z)
        {
            return RpcClient.CallBool("Target", "TargetExecute", x, y, z);
        }

        public static bool TargetExecute(int x, int y, int z, int graphic)
        {
            return RpcClient.CallBool("Target", "TargetExecute", x, y, z, graphic);
        }

        public static bool TargetExecute(Point3D pos)
        {
            return TargetExecute(pos.X, pos.Y, pos.Z);
        }

        public static bool Self()
        {
            return RpcClient.CallBool("Target", "Self");
        }

        public static bool Last()
        {
            return RpcClient.CallBool("Target", "Last");
        }

        public static bool Cancel()
        {
            return RpcClient.CallBool("Target", "Cancel");
        }

        public static bool SetLast(int serial)
        {
            return RpcClient.CallBool("Target", "SetLast", serial);
        }

        public static bool SetLast(Mobile mobile)
        {
            return mobile != null && SetLast(mobile.Serial);
        }

        public static bool SetLast(Item item)
        {
            return item != null && SetLast(item.Serial);
        }

        public static int GetLast()
        {
            return RpcClient.CallInt("Target", "GetLast");
        }

        public static int PromptTarget(string message = "Select a target", int color = 0x03B2, int timeout = 15000)
        {
            return RpcClient.CallInt("Target", "PromptTarget", message ?? "Select a target", color, timeout);
        }
    }

    public static class Journal
    {
        public static bool Search(string text)
        {
            return RpcClient.CallBool("Journal", "Search", text ?? "");
        }

        public static bool SearchByName(string text, string name)
        {
            return RpcClient.CallBool("Journal", "SearchByName", text ?? "", name ?? "");
        }

        public static bool WaitJournal(string text, int delay = 1000)
        {
            return RpcClient.CallBool("Journal", "WaitJournal", text ?? "", delay);
        }

        public static void Clear()
        {
            RpcClient.CallVoid("Journal", "Clear");
        }

        public static List<string> GetTextBySerial(int serial, bool addname = false)
        {
            return RpcClient.CallStringList("Journal", "GetTextBySerial", serial, addname);
        }

        public static string GetLineText(string text, bool addname = false)
        {
            return RpcClient.CallString("Journal", "GetLineText", text ?? "", addname);
        }

        public static List<string> GetTextByColor(int color, bool addname = false)
        {
            return RpcClient.CallStringList("Journal", "GetTextByColor", color, addname);
        }

        public static List<string> GetTextByType(string type, bool addname = false)
        {
            return RpcClient.CallStringList("Journal", "GetTextByType", type ?? "", addname);
        }
    }

    public static class Gumps
    {
        public static bool HasGump()
        {
            return RpcClient.CallBool("Gumps", "HasGump");
        }

        public static bool HasGump(uint gumpId)
        {
            return RpcClient.CallBool("Gumps", "HasGump", gumpId);
        }

        public static bool HasGump(int gumpId)
        {
            return RpcClient.CallBool("Gumps", "HasGump", unchecked((uint)gumpId));
        }

        public static uint CurrentGump()
        {
            return RpcClient.CallUInt("Gumps", "CurrentGump");
        }

        public static bool WaitForGump(uint gumpId, int delay)
        {
            return RpcClient.CallBool("Gumps", "WaitForGump", gumpId, delay);
        }

        public static bool WaitForGump(int gumpId, int delay)
        {
            return RpcClient.CallBool("Gumps", "WaitForGump", unchecked((uint)gumpId), delay);
        }

        public static bool SendAction(uint gumpId, int buttonId)
        {
            return RpcClient.CallBool("Gumps", "SendAction", gumpId, buttonId);
        }

        public static bool SendAction(int gumpId, int buttonId)
        {
            return RpcClient.CallBool("Gumps", "SendAction", unchecked((uint)gumpId), buttonId);
        }

        public static bool SendAdvancedAction(uint gumpId, int buttonId, List<int> switches)
        {
            return RpcClient.CallBool(
                "Gumps", "SendAdvancedAction", gumpId, buttonId, switches ?? new List<int>());
        }

        public static bool SendAdvancedAction(int gumpId, int buttonId, List<int> switches)
        {
            return SendAdvancedAction(unchecked((uint)gumpId), buttonId, switches);
        }

        public static bool SendAdvancedAction(
            uint gumpId,
            int buttonId,
            List<int> switches,
            List<int> textId,
            List<string> textString)
        {
            return RpcClient.CallBool(
                "Gumps",
                "SendAdvancedAction",
                gumpId,
                buttonId,
                switches ?? new List<int>(),
                textId ?? new List<int>(),
                textString ?? new List<string>());
        }

        public static bool SendAdvancedAction(
            int gumpId,
            int buttonId,
            List<int> switches,
            List<int> textId,
            List<string> textString)
        {
            return SendAdvancedAction(unchecked((uint)gumpId), buttonId, switches, textId, textString);
        }

        public static List<string> LastGumpGetLineList()
        {
            return RpcClient.CallStringList("Gumps", "LastGumpGetLineList");
        }

        public static string LastGumpGetLine(int lineNum)
        {
            return RpcClient.CallString("Gumps", "LastGumpGetLine", lineNum);
        }

        public static bool LastGumpTextExist(string text)
        {
            return RpcClient.CallBool("Gumps", "LastGumpTextExist", text ?? "");
        }

        public static bool CloseGump()
        {
            return RpcClient.CallBool("Gumps", "CloseGump");
        }

        public static bool CloseGump(uint gumpId)
        {
            return RpcClient.CallBool("Gumps", "CloseGump", gumpId);
        }

        public static bool CloseGump(int gumpId)
        {
            return RpcClient.CallBool("Gumps", "CloseGump", unchecked((uint)gumpId));
        }
    }

    public static class Timer
    {
        public static void Create(string name, int delay)
        {
            RpcClient.CallVoid("Timer", "Create", name ?? "", delay);
        }

        public static void Create(string name, int delay, string message)
        {
            _ = message;
            RpcClient.CallVoid("Timer", "Create", name ?? "", delay);
        }

        public static bool Check(string name)
        {
            return RpcClient.CallBool("Timer", "Check", name ?? "");
        }

        public static int Remaining(string name)
        {
            return RpcClient.CallInt("Timer", "Remaining", name ?? "");
        }

        public static bool Remove(string name)
        {
            return RpcClient.CallBool("Timer", "Remove", name ?? "");
        }

        public static void Clear()
        {
            RpcClient.CallVoid("Timer", "Clear");
        }
    }

    public static class Misc
    {
        internal static string[] ScriptArgsInternal { get; set; } = Array.Empty<string>();
        public static string[] ScriptArgs => ScriptArgsInternal;

        public static void Pause(int millisec)
        {
            RpcClient.CallVoid("Misc", "Pause", millisec);
        }

        public static void SendMessage(string message)
        {
            RpcClient.CallVoid("Misc", "SendMessage", message ?? "");
        }

        public static void SendMessage(string message, int color)
        {
            RpcClient.CallVoid("Misc", "SendMessage", message ?? "", color);
        }

        public static void SendMessage(object message, int color = 0x03B2, bool wait = true)
        {
            _ = wait;
            RpcClient.CallVoid("Misc", "SendMessage", message?.ToString() ?? "", color);
        }

        public static void NoOperation()
        {
            RpcClient.CallVoid("Misc", "NoOperation");
        }

        public static object ReadSharedValue(string name)
        {
            return RpcClient.ToClrObject(RpcClient.Call("Misc", "ReadSharedValue", name ?? ""));
        }

        public static void SetSharedValue(string name, object value)
        {
            RpcClient.CallVoid("Misc", "SetSharedValue", name ?? "", value);
        }

        public static bool CheckSharedValue(string name)
        {
            return RpcClient.CallBool("Misc", "CheckSharedValue", name ?? "");
        }

        public static bool RemoveSharedValue(string name)
        {
            return RpcClient.CallBool("Misc", "RemoveSharedValue", name ?? "");
        }

        public static List<string> AllSharedValue()
        {
            return RpcClient.CallStringList("Misc", "AllSharedValue");
        }

        public static void IgnoreObject(int serial)
        {
            RpcClient.CallVoid("Misc", "IgnoreObject", serial);
        }

        public static void IgnoreObject(Item item)
        {
            if (item != null) IgnoreObject(item.Serial);
        }

        public static void IgnoreObject(Mobile mobile)
        {
            if (mobile != null) IgnoreObject(mobile.Serial);
        }

        public static void UnIgnoreObject(int serial)
        {
            RpcClient.CallVoid("Misc", "UnIgnoreObject", serial);
        }

        public static void UnIgnoreObject(Item item)
        {
            if (item != null) UnIgnoreObject(item.Serial);
        }

        public static void UnIgnoreObject(Mobile mobile)
        {
            if (mobile != null) UnIgnoreObject(mobile.Serial);
        }

        public static bool CheckIgnoreObject(int serial)
        {
            return RpcClient.CallBool("Misc", "CheckIgnoreObject", serial);
        }

        public static bool CheckIgnoreObject(Item item)
        {
            return item != null && CheckIgnoreObject(item.Serial);
        }

        public static bool CheckIgnoreObject(Mobile mobile)
        {
            return mobile != null && CheckIgnoreObject(mobile.Serial);
        }

        public static void ClearIgnore()
        {
            RpcClient.CallVoid("Misc", "ClearIgnore");
        }

        public static string CurrentScriptDirectory()
        {
            return RpcClient.CallString("Misc", "CurrentScriptDirectory");
        }

        public static bool ScriptStatus(string scriptName)
        {
            return RpcClient.CallBool("Misc", "ScriptStatus", scriptName ?? "");
        }
    }

    public static class AutoLoot
    {
        public static bool Start() => RpcClient.CallBool("AutoLoot", "Start");
        public static bool Stop() => RpcClient.CallBool("AutoLoot", "Stop");
        public static bool Status() => RpcClient.CallBool("AutoLoot", "Status");

        public static bool RunOnce() => RpcClient.CallBool("AutoLoot", "RunOnce");

        public static bool RunOnce(string listName)
        {
            return string.IsNullOrEmpty(listName)
                ? RpcClient.CallBool("AutoLoot", "RunOnce")
                : RpcClient.CallBool("AutoLoot", "RunOnce", listName);
        }

        public static void ChangeList(string listName)
        {
            RpcClient.CallVoid("AutoLoot", "ChangeList", listName ?? "");
        }
    }

    public static class BandageHeal
    {
        public static bool Start() => RpcClient.CallBool("BandageHeal", "Start");
        public static bool Stop() => RpcClient.CallBool("BandageHeal", "Stop");
        public static bool Status() => RpcClient.CallBool("BandageHeal", "Status");
    }

    public static class Organizer
    {
        public static bool FStart() => RpcClient.CallBool("Organizer", "FStart");
        public static bool Start() => RpcClient.CallBool("Organizer", "Start");
        public static bool FStop() => RpcClient.CallBool("Organizer", "FStop");
        public static bool Stop() => RpcClient.CallBool("Organizer", "Stop");
        public static bool Status() => RpcClient.CallBool("Organizer", "Status");

        public static void ChangeList(string listName)
        {
            RpcClient.CallVoid("Organizer", "ChangeList", listName ?? "");
        }
    }

    public static class Restock
    {
        public static bool FStart() => RpcClient.CallBool("Restock", "FStart");
        public static bool Start() => RpcClient.CallBool("Restock", "Start");
        public static bool FStop() => RpcClient.CallBool("Restock", "FStop");
        public static bool Stop() => RpcClient.CallBool("Restock", "Stop");
        public static bool Status() => RpcClient.CallBool("Restock", "Status");

        public static void ChangeList(string listName)
        {
            RpcClient.CallVoid("Restock", "ChangeList", listName ?? "");
        }
    }

    public static class Scavenger
    {
        public static bool Start() => RpcClient.CallBool("Scavenger", "Start");
        public static bool Stop() => RpcClient.CallBool("Scavenger", "Stop");
        public static bool Status() => RpcClient.CallBool("Scavenger", "Status");
        public static bool RunOnce() => RpcClient.CallBool("Scavenger", "RunOnce");
        public static void ResetIgnore() => RpcClient.CallVoid("Scavenger", "ResetIgnore");

        public static void ChangeList(string listName)
        {
            RpcClient.CallVoid("Scavenger", "ChangeList", listName ?? "");
        }
    }

    public static class PathFinding
    {
        public class Route
        {
            public int X { get; set; }
            public int Y { get; set; }
            public int Z { get; set; }
            public int Distance { get; set; } = 1;
            public bool StopIfStuck { get; set; }
            public int MaxRetry { get; set; } = 3;
            public int Timeout { get; set; } = 15000;
            public bool DebugMessage { get; set; }
            public bool Run { get; set; } = true;
            public bool UseResync { get; set; } = true;
            public bool IgnoreMobile { get; set; }
            public List<Point3D> Waypoints { get; set; } = new List<Point3D>();

            public void AddWaypoint(int x, int y, int z = 0)
            {
                Waypoints.Add(new Point3D(x, y, z));
            }

            internal Dictionary<string, object> ToDto()
            {
                var wps = (Waypoints ?? new List<Point3D>())
                    .Select(p => new Dictionary<string, object>
                    {
                        ["X"] = p.X,
                        ["Y"] = p.Y,
                        ["Z"] = p.Z
                    })
                    .ToList();
                return new Dictionary<string, object>
                {
                    ["X"] = X,
                    ["Y"] = Y,
                    ["Z"] = Z,
                    ["Distance"] = Distance,
                    ["StopIfStuck"] = StopIfStuck,
                    ["MaxRetry"] = MaxRetry,
                    ["Timeout"] = Timeout,
                    ["Waypoints"] = wps
                };
            }
        }

        public static bool Go(Route route)
        {
            if (route == null) return false;
            return RpcClient.CallBool("PathFinding", "Go", route.ToDto());
        }

        public static bool Go(int x, int y)
        {
            return RpcClient.CallBool("PathFinding", "Go", x, y);
        }

        public static bool Go(int x, int y, int z)
        {
            return RpcClient.CallBool("PathFinding", "Go", x, y, z);
        }

        public static bool RunRoute(Route route)
        {
            if (route == null) return false;
            return RpcClient.CallBool("PathFinding", "RunRoute", route.ToDto());
        }

        public static bool PathFindTo(int x, int y)
        {
            return RpcClient.CallBool("PathFinding", "PathFindTo", x, y);
        }

        public static bool PathFindTo(int x, int y, int z)
        {
            return RpcClient.CallBool("PathFinding", "PathFindTo", x, y, z);
        }

        public static bool PathFindTo(Point3D pos)
        {
            return PathFindTo(pos.X, pos.Y, pos.Z);
        }

        public static bool IsWalking()
        {
            return RpcClient.CallBool("PathFinding", "IsWalking");
        }

        public static void Stop()
        {
            RpcClient.CallVoid("PathFinding", "Stop");
        }
    }

    public static class Spells
    {
        public static void Cast(string spellName)
        {
            RpcClient.CallVoid("Client", "Cast", spellName ?? "");
        }

        public static void Cast(string spellName, int targetSerial)
        {
            RpcClient.CallVoid("Client", "Cast", spellName ?? "", RpcClient.FormatSerialHex(targetSerial));
        }

        public static void Cast(string spellName, Mobile target)
        {
            if (target != null) Cast(spellName, target.Serial);
            else Cast(spellName);
        }

        public static void Cast(string spellName, Item target)
        {
            if (target != null) Cast(spellName, target.Serial);
            else Cast(spellName);
        }

        public static void CastMagery(string spellName) => Cast(spellName);
        public static void CastMagery(string spellName, int targetSerial) => Cast(spellName, targetSerial);
        public static void CastMagery(string spellName, Mobile target) => Cast(spellName, target);
        public static void CastMagery(string spellName, Item target) => Cast(spellName, target);

        public static void CastChivalry(string spellName) => Cast(spellName);
        public static void CastChivalry(string spellName, int targetSerial) => Cast(spellName, targetSerial);
        public static void CastChivalry(string spellName, Mobile target) => Cast(spellName, target);
        public static void CastChivalry(string spellName, Item target) => Cast(spellName, target);

        public static void CastNecro(string spellName) => Cast(spellName);
        public static void CastNecro(string spellName, int targetSerial) => Cast(spellName, targetSerial);
        public static void CastNecro(string spellName, Mobile target) => Cast(spellName, target);
        public static void CastNecro(string spellName, Item target) => Cast(spellName, target);

        public static void CastBushido(string spellName) => Cast(spellName);
        public static void CastNinjitsu(string spellName) => Cast(spellName);
        public static void CastSpellweaving(string spellName) => Cast(spellName);
        public static void CastSpellweaving(string spellName, int targetSerial) => Cast(spellName, targetSerial);
        public static void CastSpellweaving(string spellName, Mobile target) => Cast(spellName, target);
        public static void CastMysticism(string spellName) => Cast(spellName);
        public static void CastMysticism(string spellName, int targetSerial) => Cast(spellName, targetSerial);
        public static void CastMysticism(string spellName, Mobile target) => Cast(spellName, target);
        public static void CastMastery(string spellName) => Cast(spellName);
        public static void CastLastSpell() => Cast("last");
    }
}
