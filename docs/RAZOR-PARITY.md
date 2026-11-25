# Razor and Razor Enhanced workflows in CrossUO

CrossUO's **Assistant** button opens the built-in assistant. **Dock in game
window** keeps it with the game. The launcher, gameplay assistant, and
JavaScript runtime run in the client process; Python (`.py`) and C# (`.cs`)
scripts run in local child processes over private stdin/stdout JSON-RPC pipes
on macOS and Linux.

## Where to find the tools

| Workflow | CrossUO entry point |
| --- | --- |
| Shard, game-data, and login profiles | Embedded launcher |
| Create, clone, archive, and switch characters’ settings profiles | Assistant → Main |
| Dress, undress, party, vendor lists and Sell Hot Bag | Assistant → Agents |
| Organizer, Restock, Scavenger, Use Once, and AutoLoot | Assistant → Agents → Inventory |
| Bandage Heal | Assistant → Agents → Bandage Heal |
| Automatic hand switching for standard potions | Assistant → Agents → Potion Hands |
| Damage totals, hits, and damage per second by target | Assistant → Display → Damage meter |
| Item counters and icons, container labels, low-stock alerts, bandage and gold tracking, buffs, and display rules | Assistant → Display and Expanded Status Bar → Supplies |
| Smart or queued targets, friend checks, and named targeting presets | Assistant → Main → Targeting |
| Customizable item toolbar with alerts and item use | Assistant → Display → Toolbar |
| Bind an inventory preset or combat action to a key | Assistant → Hotkeys |
| Record and play macros | Assistant → Macros |
| Edit and run JavaScript (`.js`), Python (`.py`), and C# (`.cs`) scripts | Assistant → Scripts |
| Preview a Razor script, recorded macro, UOSteam script, or supported Classic and Enhanced profiles | Assistant → Scripts → Razor Import |
| Message, sound, creature, healing and trade filters | Assistant → Filters |
| Map and ship controls | Assistant → Main and the map and ship hotkeys |
| Screenshot destination, size, format, delay, and death capture | Assistant → Tools |
| Profile backup, packet capture, and ModernUO decoration and door actions | Assistant → Tools |

## Inventory agents

Open the source and destination containers so the client can see their
contents. Choose an agent, target its containers and item rules, and select
**Run**. Organizer moves matching items. Restock tops each rule up to the
saved quantity. Use Once sends the next owned item-use request. Scavenger
only tries visible matching ground items within reach and remembers the
items attempted until its cache is cleared.

The client waits for confirmation through the normal inventory update
packets before starting a second move. Confirmed transfers and ordinary
combat or use actions respect the shard's item timing. Stop, logout, and
character changes clear active work. Saved agent definitions belong to
the assistant profile and character on the selected shard.

**AutoLoot** matches items in opened corpses by graphic, hue, and optional
numeric property ranges. Each property can have an inclusive minimum and
maximum. The item must meet all configured ranges on a rule. If an item property
is missing or the shard does not return it, the property rule skips the item.
Each rule can use the profile destination or override it with its own backpack
container. Start a named preset to keep scanning or choose Sweep once for the
current open corpses. It can include open bags inside a corpse, optionally
auto-open nearby unopened creature corpses within two tiles while respecting
item-action cooldowns and tracking opened corpses, and import or export named
rule lists in native INI, native JSON, or Razor Enhanced formats. It skips
standard playable bodies and worn items. Shards supply corpse body art over this
protocol, so the body filter is a heuristic. The shard decides each lift and
applies its loot rules.

**Bandage Heal** starts on request each session. It can apply bandages to you or
a selected friend or party member. Set the health threshold, range, bandage type,
and optional poison or pause rules in its tab. It waits for other item actions
and uses shard confirmations to track healing.

**Potion Hands** can free a hand for standard undyed potions and recover custom
dual-wield setups (stowing off-hand then main-hand and restoring in reverse
order). Turn it on in the active Assistant profile. When the shard asks for a
free hand, the client moves the shield or weapon(s) to your backpack, retries
the potion, and restores the exact item(s) if your hands and backpack still
match. Optional **Spell Hand Handling** can also unequip non-Spell-Channeling
weapons or shields before casting a spell and restore them after cast
completion, fizzle, or timeout while keeping Spell Channeling gear equipped.
Manual item changes, death, or logout cancel pending recovery. Trade filtering
can decline unsolicited item offers and optionally allow trades from friends;
empty trade windows still lack an initiator in the standard protocol.

**Damage meter** records the damage reports delivered by the shard. It shows the
recipient, amount, hits, largest hit, and the average over recorded time. Pause
freezes elapsed time and skips events until you resume. The protocol supplies the
recipient and amount, so its other-target totals can include hits from pets or
other sources the shard shows you.

**Sell Hot Bag** restricts named sell rules to the selected bag in the current
character's backpack. Open the bag before a sale so the client can verify item
ownership and location.

**Target presets** select one visible mobile by name, one or more body IDs, hue,
distance, notoriety, status filters (poisoned, blessed, paralyzed, war mode),
friend inclusion or exclusion, ignore-list filtering, and ordering mode
(nearest, lowest health percentage, farthest, highest health percentage, random,
or cycle next). A preset can set Last Target or explicitly use the current
server cursor, and preserves friend protection on harmful cursors. Under
**Lists → Friends**, named friend groups can be managed, applied, and imported
or exported as Razor Enhanced JSON, INI, or plain text.

**Toolbar** starts with 12 customizable slots supporting item counters, spells,
primary and secondary weapon abilities, and skills. Choose slot actions,
graphics, hues, and alert thresholds, then optionally lock the layout or open it
at login. Item counts use the same backpack coverage as Supplies, and
double-clicking a slot uses the item, casts the spell, toggles the weapon
ability, or uses the skill.

**Supplies and Counters (Expanded Status Bar)** integrates **Display → Counters**
directly into your character's expanded status bar. Its **Supplies** drawer
remembers whether you leave it open, scales reagent and resource icons from
their visible opaque artwork bounds for crisp larger tiles, highlights reagents
used by an active spell cast in crimson, and shows a live status strip when
Bandage, Gold per hour, Gate, Criminal, or active-cast readouts are running.
Double-click any supply cell to use a matching item from your backpack, or click
**Counters** in the header to jump straight to **Assistant → Display → Counters**.
Counts refresh immediately in memory whenever items enter or leave your backpack
or opened sub-bags with zero extra network traffic. When enabled, automatic
recount paces opening unopened sub-bags on login, when new bags arrive, or when
tracked items enter an unopened bag, and skips pouches (`0x0E79`) by default so
trapped pouches stay safe. The in-game colored title caption applies automatic
low/mid/good threshold tinting on `{hp}`, `{mana}`/`{mp}`, `{stam}`/`{sp}`, and
inverted `{weight}`/`{weight_maxweight}` (preserving explicit `~#RRGGBB`
overrides while keeping the OS window title plain text), and renders inline
16×16 static item icons, low-stock tinting, and crimson spell-reagent cast
highlights directly inside the title format string.

## Import and compatibility

The reference is the [classic Razor help](https://www.razorce.com/help/), its
[script guide](https://www.razorce.com/guide/commands/), and the
[Razor Enhanced API](https://razorenhanced.readthedocs.io/index.html).

Classic import translates supported script commands, expanded recorded `.macro`
actions, and 120 built-in hotkey IDs (targeting, attack, bandage, potions, 64
Magery spells, last skill/spell/object, dress/undress, inventory agents, and
war/peace) into native JavaScript. XML profile import previews supported settings (including `AutoSearch`,
`NoSearchPouches`, `ShowImages`, `ShowNotoHue`, `TitleBarDisplay`, and
`TitleBarText`), built-in hotkeys (with per-key pass-through), and saved agents
(Organizer, Restock, Scavenger, Use Once, Dress, Buy/Sell including Sell Hot
Bag, and Friends). Attach Classic `counters.xml` to carry over selected counter
definitions, icon preferences, and warning thresholds.

UOSteam (`.uos`) import translates common UOSteam commands (`useobject`,
`usetype`, `cast`, `target`, `waitfortarget`, `pause`, `sysmsg`, `headmsg`,
`moveitem`, `findtype`, `injournal`, `clearjournal`, `dress`, `undress`,
`organizer`, `autoloot`), control flow (`if`/`elseif`/`else`/`endif`,
`while`/`endwhile`, `for`/`endfor`), and aliases (`self`, `backpack`, `last`,
`enemy`, `friend`, `found`) into native JavaScript with line-numbered issue
reporting for unsupported lines.

Enhanced import can read a profile folder or any of 14 supported table files:
`GENERAL`, `HOTKEYS`, `TOOLBAR_ITEMS`, `AUTOLOOT`, `SCAVENGER`, `ORGANIZER`,
`RESTOCK`, `DRESS`, `BUY`, `SELL`, `BANDAGE_HEAL`, `TARGETS`, `FRIENDS`, and
`SCRIPTS`. Accepted entries publish to a new inactive profile without autostart,
and the preview reports imported and skipped counts. Credentials, connection
overrides, unsafe paths, and custom chat commands stay in the original files.

Native JavaScript preserves existing `Client.*` and `Player.*` helpers (such as
`Client.AutoLootOnce(name)`, `Client.RunInventoryAgent(name)`,
`Client.StopInventoryAgent()`, `Client.StartBandageHeal()`,
`Client.StopBandageHeal()`, `Client.StartDamageMeter()`,
`Client.PauseDamageMeter()`, `Client.ResumeDamageMeter()`,
`Client.StopDamageMeter()`, `Client.GetRecordedDamage(serial)`,
`Client.RunTargetPreset(name, useCursor)`, and
`Client.ExecuteRazorHotkey(id)`) and adds Razor Enhanced-compatible namespaces
for `Player`, `Items`, `Mobiles`, `Target`, `Journal`, `Gumps`, `Timer`,
`Misc`, `AutoLoot`, `BandageHeal`, `Organizer`, `Restock`, `Scavenger`, and
`PathFinding`. `Target.PromptTarget()` captures self-clicks, mobile and item
clicks, tile clicks, and ESC/cancel locally without sending stray target
packets to the shard.

**Assistant → Scripts** also loads and runs Python (`.py`) and C# (`.cs`)
scripts directly on macOS and Linux over private local stdin/stdout JSON-RPC
pipes backed by the same Razor Enhanced API boundary. Script standard output is
isolated from the RPC stream, pause/resume is enforced across RPC calls, and
Stop tears down the script's process group cleanly. Python scripts run on a
Python 3 standard-library host with IronPython/.NET shims (`clr`, `System`, and
`System.Collections.Generic.List[T]`). C# scripts compile offline with the .NET
SDK Roslyn compiler (`csc.dll`) into a SHA-256-keyed assembly cache with no
NuGet or network access.

## Remaining integration work

| Workstream | Remaining capabilities |
| --- | --- |
| External apps | The legacy Windows UOAssist interface needs a separate platform adapter. |
| Windows script runtimes | External `.py` and `.cs` process execution currently targets POSIX (macOS and Linux); Windows process-bridge support remains future work if needed. |
| Live shard playtesting | Hands-on shard verification of the newly added AutoLoot, Spell Hand Handling, Toolbar/Target/Friend-group, Profile/UOSteam import, and JavaScript, Python, and C# Razor Enhanced script workflows. |

[Enhanced source](https://github.com/UltimaTools/RazorEnhanced/tree/1baf653b21eedf7e624b7ac21ae2c4e41180d938/Razor/RazorEnhanced)
uses separate engines for Python, C#, and UOSteam. Its Python API includes a .NET
host, so compatibility extends beyond syntax translation.

## Server behavior

The assistant sends the same game actions as the client. The shard
validates inventory, targeting and staff actions, and can advertise
disabled assistant features. ModernUO accepts both ASCII and binary-prefixed
`0xBE` assistant version packets, parses and enforces `KickOnFailure` only when
enabled, aligns `0xF0` feature-mask and MapUO party/guild packets with CrossUO,
and provides the `[Assistants]` staff command to inspect connected assistant
versions and manage disallowed features. The packet capture control works during
gameplay. It records packet identifiers and sizes for authentication,
speech, chat, and prompt opcodes without their message payloads.

Each profile backup stores its registered scripts under `Scripts/` and
records the original paths in `ScriptSources.ini`. When recovering a script
into a different folder, re-add it from that folder in the Scripts tab.

## Verification

The ARM64 client build and Assistant harness pass. A disposable ModernUO
shard verified all four inventory agents before and after a world save and
restart. Restock moved 15 bandages across two lifts, split a stack, and left
dyed bandages in place. Use Once respected a preceding item action and used
one item. Automatic death capture wrote one 800×600 game-view PNG.

The Enhanced integration was exercised on a disposable ModernUO shard through
CrossUO. AutoLoot moved the matching hue only. Bandage Heal consumed a bandage
and restored health. The damage meter ignored events while paused. A saved
target preset selected the expected mobile, and a toolbar double-click used a
backpack bandage. A named vendor rule sold three bottles from the designated
bag and left the other stack alone. The Assistant harness and focused parser,
selection, timing, and packet tests passed.

Separate verifiers tested the next three changes on disposable shards. A Luck
range moved the matching dagger and kept lower and missing values in the corpse.
Disabled property lists and a malformed bound also kept those daggers in place,
while a graphic-only rule still moved pearls. Potion Hands restored the exact
sword and shield after a successful heal and a full-health refusal. A manual
item use canceled the pending potion retry. Classic and Enhanced profiles stayed
inactive upon import and loaded their expected settings after a client restart.
Invalid input and a staged write failure left no partial profiles.

The latest parity wave passed automated verification across both forks: the
ARM64 CrossUO client build (`crossuo`), the `xuoassist-script-test` harness
(including the Razor Enhanced API, interrupt-safety, IPC security/lifecycle,
and live end-to-end `.py` and `.cs` script execution checks), `16/16` runtime
unit tests (`test_python_runtime.py` and `test_csharp_runtime.py`), all eight
standalone C++14 unit test suites (`item-counters-rules`,
`inventory-agent-plan`, `potionhands-rules`, `target-assist-rules`,
`bandageheal-rules`, `damage-meter-state`, `razorimport_test`, and
`razorprofiles_test`), ModernUO's `UOContent.Tests` (`20/20` `Assistant*`
tests), and the workspace checker (`check_workspace.py`). Hands-on shard
playtesting for this wave remains to be run.
