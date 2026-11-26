// SPDX-License-Identifier: AGPL-3.0-or-later

#include "potionhands.h"
#include "potionhands_rules.h"

#include "../config/assistantconfig.h"
#include "../config/configio.h"
#include "../macros/macroexecutor.h"
#include "assist.h"
#include "dressagent.h"
#include "gameplayfilters.h"
#include "inventoryagent.h"
#include "shopagent.h"
#include "world.h"
#include "Config.h"
#include "CrossUO.h"
#include "GameObjects/ObjectOnCursor.h"
#include "Globals.h"
#include "Managers/ClilocManager.h"
#include "Managers/ObjectPropertiesManager.h"
#include "Network/Packets.h"
#include "Target.h"
#include <external/gfx/imgui/imgui.h>
#include <algorithm>
#include <unordered_map>

namespace xuoassist {
namespace {

using namespace potionhands;

constexpr size_t MaxSpellChannelingCache = 2048;

enum class Mode
{
    None,
    Potion,
    Spell
};

enum class Phase
{
    Idle,
    AwaitFreeHandReply,
    PrepareToStow,
    ConfirmStowed,
    PrepareToDrink,
    ConfirmDrink,
    PrepareToCast,
    AwaitSpellFinish,
    PrepareToRestore,
    ConfirmRestored
};

struct Sequence
{
    Mode Kind = Mode::None;
    Phase Stage = Phase::Idle;
    uint32_t PhaseSince = 0;
    uint32_t Character = 0;
    uint8_t Facet = 0;
    uint32_t Pack = 0;
    HandPlan Plan{};
    size_t StowIndex = 0;
    size_t RestoreIndex = 0;

    // Potion state
    uint32_t Potion = 0;
    uint32_t PotionParent = 0;
    uint32_t PotionCount = 0;
    uint16_t PotionGraphic = 0;
    uint16_t PotionHue = 0;
    bool DrankPotion = false;
    bool UsedMacroMove = false;
    bool ForceClassicMove = false;
    uint8_t DrinkRetries = 0;
    bool WaitingDrinkRetry = false;

    // Spell state
    int SpellIndex = 0;
    uint32_t SpellBook = 0;
    uint32_t SpellTarget = 0;
    bool IsTargetedSpell = false;
    uint16_t InitialMana = 0;
    uint32_t CastSentAt = 0;
    bool WasParalyzedBeforeCast = false;
    bool SawCastFreeze = false;
    bool PostCastObserved = false;
    uint32_t PostCastSince = 0;
    bool SawCursor = false;
    bool CursorActive = false;
    bool CursorEnded = false;
    uint32_t SpellCursorId = 0;
    uint32_t CursorSince = 0;
    bool SpellFizzled = false;
};

Sequence s_Sequence;
astr_t s_Profile;
bool s_Enabled = false;
bool s_SpellHandsEnabled = false;
bool s_InternalSend = false;
bool s_HasAction = false;
uint32_t s_LastAction = 0;
bool s_MacroMovesSupported = true;
bool s_MacroMoveConfirmed = false;
const char *s_Status = "Ready.";
std::unordered_map<uint32_t, bool> s_SpellChanneling;
std::unordered_map<uint32_t, bool> s_Balanced;

struct InternalSend
{
    InternalSend() { s_InternalSend = true; }
    ~InternalSend() { s_InternalSend = false; }
};

bool active_player()
{
    return g_Connected && g_GameState == GS_GAME && g_World != nullptr && g_Player != nullptr &&
        !g_Player->IsDead();
}

bool potion_permitted()
{
    return g_Config.AssistantEnabled && EnabledCommandMoveItems() &&
        (g_ServerAssistantFeatures & ASF_AUTO_POTION_EQUIP) == 0;
}

bool spell_permitted()
{
    return g_Config.AssistantEnabled && EnabledCommandMoveItems() &&
        (g_ServerAssistantFeatures & ASF_DEQUIP_ON_CAST) == 0;
}

bool common_action_busy()
{
    return g_ObjectInHand.Enabled || g_Target.FromAssist || g_TargetHandler != nullptr ||
        InventoryAgent_MoveInFlight() || g_DressAgent.Active() || g_ShopAgent.IsActiveShopping() ||
        g_MacroExecutor.IsRecording();
}

bool potion_action_busy()
{
    return common_action_busy() || g_Target.IsTargeting() ||
        (g_Target.HookTargeting() && g_Target.CursorType < 3) || g_MacroExecutor.IsPlaying();
}

bool spell_move_busy()
{
    return common_action_busy() || g_Target.IsTargeting() ||
        (g_Target.HookTargeting() && g_Target.CursorType < 3);
}

void finish(const char *status, bool announce = false)
{
    s_Sequence = {};
    s_Status = status;
    if (announce && active_player())
    {
        g_Assistant.ClientPrint(status);
    }
}

void interrupt()
{
    if (s_Sequence.Stage >= Phase::ConfirmStowed)
    {
        const char *msg = s_Sequence.Kind == Mode::Spell
            ? "Spell hand switch stopped. Check your hands and backpack."
            : "Potion switch stopped. Check your hands and backpack.";
        finish(msg, true);
    }
    else
    {
        finish("Ready.");
    }
}

bool load_profile()
{
    const auto path = g_AssistantConfig.GetProfilePath();
    const auto key = fs_path_str(path);
    if (key == s_Profile)
    {
        return true;
    }
    const bool hadSequence = PotionHands_Busy();
    interrupt();
    s_Profile = key;
    CIniFile ini;
    if (ini.Load(fs_path_join(path, "PotionHands.ini")))
    {
        s_Enabled = ini.GetBool("PotionHands", "enabled");
        s_SpellHandsEnabled = ini.GetBool("PotionHands", "spellhands");
    }
    else
    {
        s_Enabled = false;
        s_SpellHandsEnabled = false;
    }
    return !hadSequence;
}

bool save_settings(bool potionEnabled, bool spellEnabled)
{
    CIniFile ini;
    ini.SetBool("PotionHands", "enabled", potionEnabled);
    ini.SetBool("PotionHands", "spellhands", spellEnabled);
    g_AssistantConfig.EnsureDirectories();
    if (!ini.Save(fs_path_join(g_AssistantConfig.GetProfilePath(), "PotionHands.ini")))
    {
        s_Status = "Could not save hand management settings.";
        return false;
    }
    s_Enabled = potionEnabled;
    s_SpellHandsEnabled = spellEnabled;
    if ((!s_Enabled && s_Sequence.Kind == Mode::Potion) ||
        (!s_SpellHandsEnabled && s_Sequence.Kind == Mode::Spell))
    {
        interrupt();
    }
    return true;
}

void enter(Phase next, const char *status)
{
    s_Sequence.Stage = next;
    s_Sequence.PhaseSince = g_Ticks;
    s_Status = status;
}

uint32_t item_serial(const CGameItem *item)
{
    return item != nullptr ? item->Serial : 0;
}

uint16_t hue(const CGameItem &item)
{
    return item.Color & 0x3FFF;
}

bool item_allows_equipped_cast(const CGameItem &item)
{
    if (IsSpellbookGraphic(item.Graphic))
    {
        return true;
    }
    const auto cached = s_SpellChanneling.find(item.Serial);
    if (cached != s_SpellChanneling.end())
    {
        return cached->second;
    }

    const astr_t oplData = g_ObjectPropertiesManager.GetData(item.Serial);
    if (oplData.empty())
    {
        return false;
    }
    astr_t localized;
    CCliloc *cliloc = g_ClilocManager.Cliloc(g_Language);
    if (cliloc != nullptr)
    {
        localized = cliloc->GetA(static_cast<int>(SpellChannelingCliloc), true);
    }
    return HasSpellChannelingText(oplData.c_str(), localized.c_str());
}

bool item_is_balanced(const CGameItem &item)
{
    const auto cached = s_Balanced.find(item.Serial);
    if (cached != s_Balanced.end())
    {
        return cached->second;
    }

    const astr_t oplData = g_ObjectPropertiesManager.GetData(item.Serial);
    if (oplData.empty())
    {
        return false;
    }
    astr_t localized;
    CCliloc *cliloc = g_ClilocManager.Cliloc(g_Language);
    if (cliloc != nullptr)
    {
        localized = cliloc->GetA(static_cast<int>(BalancedCliloc), true);
    }
    return HasBalancedText(oplData.c_str(), localized.c_str());
}

bool build_hand_item(CGameItem *item, uint8_t layer, HandItem &out, bool *oplKnownOut = nullptr)
{
    out = {};
    if (oplKnownOut != nullptr)
    {
        *oplKnownOut = false;
    }
    if (item == nullptr)
    {
        return true;
    }
    if (item->Count > 1 || item->Dragged || item->GetStaticData() == nullptr)
    {
        return false;
    }
    out.Serial = item->Serial;
    out.Layer = layer;
    out.BaseLayer = item->GetStaticData()->Layer;
    out.Graphic = item->Graphic;
    out.Hue = hue(*item);
    out.SpellChanneling = item_allows_equipped_cast(*item);
    out.Balanced = item_is_balanced(*item);

    const CObjectProperty *prop = g_ObjectPropertiesManager.Find(item->Serial);
    if (prop != nullptr && !prop->Empty())
    {
        if (oplKnownOut != nullptr)
        {
            *oplKnownOut = true;
        }
        bool hasWeaponOpl = false;
        for (const auto &row : prop->Properties)
        {
            if (IsWeaponOplCliloc(row.first))
            {
                hasWeaponOpl = true;
            }
            if (row.first == BalancedCliloc)
            {
                out.Balanced = true;
            }
        }
        out.IsWeapon = hasWeaponOpl;
    }
    else
    {
        // OPL not loaded yet; fall back to tiledata flag until properties arrive.
        out.IsWeapon = item->IsWeapon();
    }
    return true;
}

bool visible_pack_container(uint32_t serial, uint32_t pack)
{
    for (int depth = 0; serial != 0 && serial != 0xFFFFFFFF && depth < 16; ++depth)
    {
        const auto *object = g_World->FindWorldItem(serial);
        if (object == nullptr || object->Dragged || !object->Opened)
        {
            return false;
        }
        if (serial == pack)
        {
            return true;
        }
        serial = object->Container;
    }
    return false;
}

bool same_potion(const CGameItem &item)
{
    return item.Container == s_Sequence.PotionParent && !item.Dragged &&
        item.Graphic == s_Sequence.PotionGraphic && hue(item) == s_Sequence.PotionHue;
}

bool pack_contains_planned_item(const HandItem &expected)
{
    const auto *item = g_World->FindWorldItem(expected.Serial);
    return item != nullptr && item->Container == s_Sequence.Pack && item->Count == 1 &&
        !item->Dragged && HandMatches(expected, item->Serial, item->Graphic, hue(*item));
}

ObservedHands observe_hands()
{
    ObservedHands obs;
    const auto *main = g_Player->FindLayer(OL_1_HAND);
    if (main != nullptr)
    {
        obs.OneHandSerial = main->Serial;
        obs.OneHandGraphic = main->Graphic;
        obs.OneHandHue = hue(*main);
    }
    const auto *off = g_Player->FindLayer(OL_2_HAND);
    if (off != nullptr)
    {
        obs.TwoHandSerial = off->Serial;
        obs.TwoHandGraphic = off->Graphic;
        obs.TwoHandHue = hue(*off);
    }
    for (size_t i = 0; i < s_Sequence.Plan.Count; ++i)
    {
        obs.StowedInPack[i] = pack_contains_planned_item(s_Sequence.Plan.StowItem(i));
    }
    return obs;
}

bool can_send_action()
{
    const int configured = std::max(g_AssistantConfig.MoveItemsDelay(), g_AssistantConfig.UseItemsDelay());
    const auto delay = static_cast<uint32_t>(std::max<int>(MinActionDelay, std::min(configured, 60000)));
    return !s_HasAction || DeadlineReached(g_Ticks, s_LastAction, delay);
}

bool macro_moves_available()
{
    return !s_Sequence.ForceClassicMove &&
        ChooseMoveTransport(g_Config.ProtocolClientVersion >= CV_60142, s_MacroMovesSupported) ==
            MoveTransport::MacroPacket;
}

bool can_send_move_now()
{
    return CanSendImmediately(
        macro_moves_available() ? MoveTransport::MacroPacket : MoveTransport::ClassicLift,
        can_send_action());
}

bool can_send_drink_now()
{
    if (s_Sequence.WaitingDrinkRetry)
    {
        return DeadlineReached(g_Ticks, s_Sequence.PhaseSince, DrinkRetryDelay);
    }
    return can_send_action();
}

void send_stow();
void send_drink();
void send_spell_cast();
void send_restore();

bool begin_potion_sequence(uint32_t serial)
{
    if (s_InternalSend || serial == 0 || !active_player() || !load_profile())
    {
        return false;
    }
    if (!s_Enabled || !potion_permitted())
    {
        return false;
    }
    if (s_Sequence.Kind == Mode::Potion && s_Sequence.Potion == serial && PotionHands_Busy())
    {
        // Swallow duplicate rapid double-clicks on the same in-flight potion so
        // they do not abort the sequence and strand the stowed weapon in pack.
        return true;
    }
    if (PotionHands_Busy())
    {
        return false;
    }
    if (s_Sequence.Stage == Phase::AwaitFreeHandReply)
    {
        finish("Ready.");
    }
    if (potion_action_busy())
    {
        return false;
    }

    const auto *potion = g_World->FindWorldItem(serial);
    const auto *pack = g_Player->FindLayer(OL_BACKPACK);
    if (potion == nullptr || potion->Dragged || potion->Count == 0 || !StandardDrink(potion->Graphic, hue(*potion)) ||
        pack == nullptr || !visible_pack_container(potion->Container, pack->Serial))
    {
        return false;
    }

    HandItem oneHand;
    HandItem twoHand;
    bool twoHandOplKnown = false;
    if (!build_hand_item(g_Player->FindLayer(OL_1_HAND), HandLayerOne, oneHand) ||
        !build_hand_item(g_Player->FindLayer(OL_2_HAND), HandLayerTwo, twoHand, &twoHandOplKnown))
    {
        return false;
    }

    if (!CanProactivelyFreeHand(oneHand, twoHand, twoHandOplKnown))
    {
        return false;
    }

    const HandPlan plan = PlanPotionHands(oneHand, twoHand);
    if (plan.Empty())
    {
        return false;
    }

    s_Sequence = {};
    s_Sequence.Kind = Mode::Potion;
    s_Sequence.Character = g_PlayerSerial;
    s_Sequence.Facet = g_CurrentMap;
    s_Sequence.Pack = pack->Serial;
    s_Sequence.Plan = plan;
    s_Sequence.Potion = potion->Serial;
    s_Sequence.PotionParent = potion->Container;
    s_Sequence.PotionCount = potion->Count;
    s_Sequence.PotionGraphic = potion->Graphic;
    s_Sequence.PotionHue = hue(*potion);
    enter(Phase::PrepareToStow, "Freeing a hand.");
    if (can_send_move_now())
    {
        send_stow();
    }
    return true;
}

void observe_original_use(uint32_t serial)
{
    if (potion_action_busy())
    {
        return;
    }

    const auto *potion = g_World->FindWorldItem(serial);
    const auto *pack = g_Player->FindLayer(OL_BACKPACK);
    if (potion == nullptr || potion->Dragged || potion->Count == 0 || !StandardDrink(potion->Graphic, hue(*potion)) ||
        pack == nullptr || !visible_pack_container(potion->Container, pack->Serial))
    {
        return;
    }

    HandItem oneHand;
    HandItem twoHand;
    if (!build_hand_item(g_Player->FindLayer(OL_1_HAND), HandLayerOne, oneHand) ||
        !build_hand_item(g_Player->FindLayer(OL_2_HAND), HandLayerTwo, twoHand))
    {
        return;
    }

    const HandPlan plan = PlanPotionHands(oneHand, twoHand);
    if (plan.Empty())
    {
        return;
    }

    s_Sequence = {};
    s_Sequence.Kind = Mode::Potion;
    s_Sequence.Character = g_PlayerSerial;
    s_Sequence.Facet = g_CurrentMap;
    s_Sequence.Pack = pack->Serial;
    s_Sequence.Plan = plan;
    s_Sequence.Potion = potion->Serial;
    s_Sequence.PotionParent = potion->Container;
    s_Sequence.PotionCount = potion->Count;
    s_Sequence.PotionGraphic = potion->Graphic;
    s_Sequence.PotionHue = hue(*potion);
    enter(Phase::AwaitFreeHandReply, "Ready.");
}

bool begin_spell_sequence(int spellIndex, uint32_t bookSerial, uint32_t targetSerial, bool targeted)
{
    if (s_InternalSend || spellIndex < 0 || !active_player() || !load_profile())
    {
        return false;
    }
    if (!s_SpellHandsEnabled || !spell_permitted())
    {
        return false;
    }
    if (PotionHands_Busy())
    {
        interrupt();
        return false;
    }
    if (s_Sequence.Stage == Phase::AwaitFreeHandReply)
    {
        finish("Ready.");
    }
    if (spell_move_busy())
    {
        return false;
    }

    const auto *pack = g_Player->FindLayer(OL_BACKPACK);
    if (pack == nullptr)
    {
        return false;
    }

    HandItem oneHand;
    HandItem twoHand;
    if (!build_hand_item(g_Player->FindLayer(OL_1_HAND), HandLayerOne, oneHand) ||
        !build_hand_item(g_Player->FindLayer(OL_2_HAND), HandLayerTwo, twoHand))
    {
        return false;
    }

    const HandPlan plan = PlanSpellHands(oneHand, twoHand);
    if (plan.Empty())
    {
        return false;
    }

    s_Sequence = {};
    s_Sequence.Kind = Mode::Spell;
    s_Sequence.Character = g_PlayerSerial;
    s_Sequence.Facet = g_CurrentMap;
    s_Sequence.Pack = pack->Serial;
    s_Sequence.Plan = plan;
    s_Sequence.SpellIndex = spellIndex;
    s_Sequence.SpellBook = bookSerial;
    s_Sequence.SpellTarget = targetSerial;
    s_Sequence.IsTargetedSpell = targeted;
    enter(Phase::PrepareToStow, "Freeing hands for spell cast.");
    if (can_send_move_now())
    {
        send_stow();
    }
    return true;
}

void send_stow()
{
    const HandItem &item = s_Sequence.Plan.StowItem(s_Sequence.StowIndex);
    const bool useMacro = macro_moves_available();
    s_Sequence.UsedMacroMove = useMacro;
    enter(Phase::ConfirmStowed, "Waiting for the backpack move.");
    InternalSend guard;
    if (useMacro)
    {
        CPacketUnequipMacro({ static_cast<uint16_t>(item.Layer) }).Send();
    }
    else
    {
        CPacketPickupRequest(item.Serial, 1).Send();
        if (g_Config.ProtocolClientVersion >= CV_6017)
        {
            CPacketDropRequestNew(item.Serial, 0xFFFF, 0xFFFF, 0, 0, s_Sequence.Pack).Send();
        }
        else
        {
            CPacketDropRequestOld(item.Serial, 0xFFFF, 0xFFFF, 0, s_Sequence.Pack).Send();
        }
    }
}

void send_drink()
{
    s_Sequence.WaitingDrinkRetry = false;
    enter(Phase::ConfirmDrink, "Waiting for the potion result.");
    InternalSend guard;
    g_Game.DoubleClick(s_Sequence.Potion);
}

void send_spell_cast()
{
    s_Sequence.InitialMana = g_Player->Mana;
    s_Sequence.CastSentAt = g_Ticks;
    s_Sequence.WasParalyzedBeforeCast = g_Player->IsParalyzed();
    s_Sequence.SawCastFreeze = false;
    s_Sequence.PostCastObserved = false;
    s_Sequence.PostCastSince = 0;
    s_Sequence.SawCursor = false;
    s_Sequence.CursorActive = false;
    s_Sequence.CursorEnded = false;
    s_Sequence.SpellCursorId = 0;
    s_Sequence.CursorSince = 0;
    s_Sequence.SpellFizzled = false;
    enter(Phase::AwaitSpellFinish, "Casting spell.");

    InternalSend guard;
    if (s_Sequence.IsTargetedSpell)
    {
        GameplayFilters_BeginCast(0);
        CPacketTargetCastSpell(static_cast<uint16_t>(s_Sequence.SpellIndex), s_Sequence.SpellTarget).Send();
    }
    else if (s_Sequence.SpellBook != 0)
    {
        GameplayFilters_BeginCast(s_Sequence.SpellIndex);
        CPacketCastSpellFromBook(s_Sequence.SpellIndex, s_Sequence.SpellBook).Send();
    }
    else
    {
        GameplayFilters_BeginCast(s_Sequence.SpellIndex);
        CPacketCastSpell(s_Sequence.SpellIndex).Send();
    }
}

void send_restore()
{
    const HandItem &item = s_Sequence.Plan.RestoreItem(s_Sequence.RestoreIndex);
    const bool useMacro = macro_moves_available();
    s_Sequence.UsedMacroMove = useMacro;
    enter(Phase::ConfirmRestored, "Waiting for equipment to return.");
    InternalSend guard;
    if (useMacro)
    {
        CPacketEquipMacro({ item.Serial }).Send();
    }
    else
    {
        CPacketPickupRequest(item.Serial, 1).Send();
        CPacketEquipRequest(item.Serial, item.Layer, s_Sequence.Character).Send();
    }
}

} // namespace

bool PotionHands_Busy()
{
    return s_Sequence.Stage >= Phase::PrepareToStow;
}

bool PotionHands_BlocksItemMove()
{
    return s_Sequence.Stage == Phase::ConfirmStowed || s_Sequence.Stage == Phase::ConfirmRestored;
}

void PotionHands_OnPropertyList(
    uint32_t serial, uint32_t /*revision*/, const PotionHandsPropertyRows &properties)
{
    if (serial < 0x40000000)
    {
        return;
    }
    bool spellChanneling = false;
    bool balanced = false;
    for (const auto &row : properties)
    {
        if (row.first == SpellChannelingCliloc)
        {
            spellChanneling = true;
        }
        else if (row.first == BalancedCliloc)
        {
            balanced = true;
        }
    }
    if (s_SpellChanneling.size() >= MaxSpellChannelingCache &&
        s_SpellChanneling.find(serial) == s_SpellChanneling.end())
    {
        s_SpellChanneling.clear();
    }
    if (s_Balanced.size() >= MaxSpellChannelingCache &&
        s_Balanced.find(serial) == s_Balanced.end())
    {
        s_Balanced.clear();
    }
    s_SpellChanneling[serial] = spellChanneling;
    s_Balanced[serial] = balanced;
}

bool PotionHands_BeginPotionUse(uint32_t serial)
{
    return begin_potion_sequence(serial);
}

bool PotionHands_BeginSpellCast(int spellIndex, uint32_t bookSerial)
{
    return begin_spell_sequence(spellIndex, bookSerial, 0, false);
}

bool PotionHands_BeginTargetedSpellCast(int spellIndex, uint32_t targetSerial)
{
    return begin_spell_sequence(spellIndex, 0, targetSerial, true);
}

void PotionHands_OnTargetCursor(uint32_t cursorId, uint8_t /*type*/)
{
    if (s_Sequence.Kind == Mode::Spell && s_Sequence.Stage == Phase::AwaitSpellFinish)
    {
        s_Sequence.SawCursor = true;
        s_Sequence.CursorActive = true;
        s_Sequence.SpellCursorId = cursorId;
        s_Sequence.CursorSince = g_Ticks;
    }
}

void PotionHands_OnTargetCursorEnd(uint32_t cursorId)
{
    if (s_Sequence.Kind == Mode::Spell && s_Sequence.Stage == Phase::AwaitSpellFinish &&
        s_Sequence.CursorActive &&
        (cursorId == 0 || s_Sequence.SpellCursorId == 0 || cursorId == s_Sequence.SpellCursorId))
    {
        s_Sequence.CursorActive = false;
        s_Sequence.CursorEnded = true;
        enter(Phase::PrepareToRestore, "Spell finished. Restoring your equipment.");
    }
}

void PotionHands_OnGameSend(const uint8_t *data, size_t size)
{
    if (TriggersServerActionDelay(data, size))
    {
        s_LastAction = g_Ticks;
        s_HasAction = true;
    }
    if (!ActionPacket(data, size))
    {
        return;
    }
    if (s_InternalSend || !active_player() || !load_profile())
    {
        return;
    }

    if (s_Sequence.Kind == Mode::Spell && s_Sequence.Stage == Phase::AwaitSpellFinish)
    {
        if (!InterruptsSpellWait(data, size))
        {
            if (s_Sequence.CursorActive || s_Sequence.SawCursor)
            {
                s_Sequence.CursorActive = false;
                s_Sequence.CursorEnded = true;
            }
            return;
        }
        interrupt();
        return;
    }

    const bool wasBusy = PotionHands_Busy();
    if (s_Sequence.Stage != Phase::Idle)
    {
        interrupt();
    }
    const auto serial = ItemUseSerial(data, size);
    if (!wasBusy && s_Enabled && potion_permitted() && serial != 0)
    {
        observe_original_use(serial);
    }
}

void PotionHands_OnCliloc(uint32_t cliloc)
{
    if (s_Sequence.Kind == Mode::Potion)
    {
        if (s_Sequence.Stage == Phase::AwaitFreeHandReply && cliloc == FreeHandMessage &&
            !DeadlineReached(g_Ticks, s_Sequence.PhaseSince, InitialReplyTimeout))
        {
            if (active_player() && potion_permitted() && !potion_action_busy() &&
                s_Sequence.Character == g_PlayerSerial)
            {
                enter(Phase::PrepareToStow, "Freeing a hand.");
            }
            else
            {
                finish("Ready.");
            }
        }
        else if (s_Sequence.Stage == Phase::ConfirmDrink && cliloc == ActionDelayMessage &&
                 s_Sequence.DrinkRetries < MaxDrinkRetries)
        {
            ++s_Sequence.DrinkRetries;
            s_Sequence.WaitingDrinkRetry = true;
            enter(Phase::PrepareToDrink, "Waiting for action cooldown.");
        }
        else if (s_Sequence.Stage == Phase::ConfirmDrink && DrinkRefused(cliloc))
        {
            s_Sequence.RestoreIndex = 0;
            enter(Phase::PrepareToRestore, "The shard refused the potion. Restoring your equipment.");
        }
        else if (s_Sequence.Stage == Phase::AwaitFreeHandReply && DrinkRefused(cliloc))
        {
            finish("Ready.");
        }
    }
    else if (s_Sequence.Kind == Mode::Spell && s_Sequence.Stage == Phase::AwaitSpellFinish &&
             SpellFinishedOrFailedCliloc(cliloc))
    {
        s_Sequence.SpellFizzled = true;
        if (!s_Sequence.CursorActive)
        {
            s_Sequence.RestoreIndex = 0;
            enter(Phase::PrepareToRestore, "Spell stopped. Restoring your equipment.");
        }
    }
}

void PotionHands_OnMoveRejected()
{
    if (PotionHands_BlocksItemMove())
    {
        finish("The shard refused the move. Check your hands and backpack.", true);
    }
}

void PotionHands_Tick()
{
    if (!active_player())
    {
        interrupt();
        return;
    }
    load_profile();
    if (s_Sequence.Stage == Phase::Idle)
    {
        return;
    }

    const auto *pack = g_Player->FindLayer(OL_BACKPACK);
    const bool modeEnabled = s_Sequence.Kind == Mode::Spell
        ? (s_SpellHandsEnabled && spell_permitted())
        : (s_Enabled && potion_permitted());
    const bool busy = s_Sequence.Kind == Mode::Spell
        ? (s_Sequence.Stage == Phase::AwaitSpellFinish ? common_action_busy() : spell_move_busy())
        : potion_action_busy();
    if (!modeEnabled || busy || s_Sequence.Character != g_PlayerSerial ||
        s_Sequence.Facet != g_CurrentMap || item_serial(pack) != s_Sequence.Pack)
    {
        interrupt();
        return;
    }

    const ObservedHands obs = observe_hands();
    const CGameItem *potion = nullptr;
    bool parentAccessible = false;
    bool potionUnchanged = false;
    if (s_Sequence.Kind == Mode::Potion)
    {
        potion = g_World->FindWorldItem(s_Sequence.Potion);
        parentAccessible = visible_pack_container(s_Sequence.PotionParent, s_Sequence.Pack);
        potionUnchanged = parentAccessible && potion != nullptr && same_potion(*potion) &&
            potion->Count == s_Sequence.PotionCount;
    }

    switch (s_Sequence.Stage)
    {
        case Phase::Idle:
            break;
        case Phase::AwaitFreeHandReply:
            if (!potionUnchanged || !VerifyStowState(s_Sequence.Plan, 0, obs) ||
                DeadlineReached(g_Ticks, s_Sequence.PhaseSince, InitialReplyTimeout))
            {
                finish("Ready.");
            }
            break;
        case Phase::PrepareToStow:
            if ((s_Sequence.Kind == Mode::Potion && !potionUnchanged) ||
                !VerifyStowState(s_Sequence.Plan, s_Sequence.StowIndex, obs))
            {
                interrupt();
            }
            else if (can_send_move_now())
            {
                send_stow();
            }
            break;
        case Phase::ConfirmStowed:
            if (VerifyStowState(s_Sequence.Plan, s_Sequence.StowIndex + 1, obs))
            {
                if (s_Sequence.UsedMacroMove)
                {
                    s_MacroMoveConfirmed = true;
                    s_Sequence.UsedMacroMove = false;
                }
                s_Sequence.ForceClassicMove = false;
                ++s_Sequence.StowIndex;
                if (s_Sequence.StowIndex < s_Sequence.Plan.Count)
                {
                    enter(Phase::PrepareToStow, "Freeing your second hand.");
                    if (can_send_move_now())
                    {
                        send_stow();
                    }
                }
                else if (s_Sequence.Kind == Mode::Spell)
                {
                    enter(Phase::PrepareToCast, "Hands are free. Casting spell.");
                    send_spell_cast();
                }
                else
                {
                    enter(Phase::PrepareToDrink, "Your hand is free.");
                    if (potionUnchanged && can_send_drink_now())
                    {
                        send_drink();
                    }
                }
            }
            else if (s_Sequence.UsedMacroMove &&
                     VerifyStowState(s_Sequence.Plan, s_Sequence.StowIndex, obs) &&
                     DeadlineReached(g_Ticks, s_Sequence.PhaseSince, MacroFallbackTimeout))
            {
                if (!s_MacroMoveConfirmed)
                {
                    s_MacroMovesSupported = false;
                }
                s_Sequence.ForceClassicMove = true;
                s_Sequence.UsedMacroMove = false;
                enter(Phase::PrepareToStow, "Freeing a hand.");
            }
            else if (!VerifyStowInFlight(s_Sequence.Plan, s_Sequence.StowIndex, obs) ||
                     DeadlineReached(g_Ticks, s_Sequence.PhaseSince, MoveReplyTimeout))
            {
                finish("The backpack move could not be confirmed. Check your equipment.", true);
            }
            break;
        case Phase::PrepareToDrink:
            if (!VerifyStowState(s_Sequence.Plan, s_Sequence.Plan.Count, obs))
            {
                interrupt();
            }
            else if (!potionUnchanged)
            {
                s_Sequence.RestoreIndex = 0;
                enter(Phase::PrepareToRestore, "The potion changed. Restoring your equipment.");
                if (VerifyRestoreState(s_Sequence.Plan, 0, obs) && can_send_move_now())
                {
                    send_restore();
                }
            }
            else if (can_send_drink_now())
            {
                send_drink();
            }
            break;
        case Phase::ConfirmDrink:
        {
            if (!VerifyStowState(s_Sequence.Plan, s_Sequence.Plan.Count, obs))
            {
                interrupt();
                break;
            }
            const auto result = ObserveDrink(parentAccessible, potion != nullptr,
                potion != nullptr && same_potion(*potion), s_Sequence.PotionCount,
                potion != nullptr ? potion->Count : 0);
            if (result == DrinkResult::Consumed)
            {
                s_Sequence.DrankPotion = true;
                s_Sequence.RestoreIndex = 0;
                enter(Phase::PrepareToRestore, "Potion used. Restoring your equipment.");
                if (VerifyRestoreState(s_Sequence.Plan, 0, obs) && can_send_move_now())
                {
                    send_restore();
                }
            }
            else if (result == DrinkResult::Changed)
            {
                interrupt();
            }
            else if (DeadlineReached(g_Ticks, s_Sequence.PhaseSince, DrinkReplyTimeout))
            {
                // The same potion is still present. Recover only the positively
                // confirmed item(s) in the original backpack and untouched slots.
                s_Sequence.RestoreIndex = 0;
                enter(Phase::PrepareToRestore, "Potion not confirmed. Restoring your equipment.");
                if (VerifyRestoreState(s_Sequence.Plan, 0, obs) && can_send_move_now())
                {
                    send_restore();
                }
            }
            break;
        }
        case Phase::PrepareToCast:
            if (!VerifyStowState(s_Sequence.Plan, s_Sequence.Plan.Count, obs))
            {
                interrupt();
            }
            else
            {
                send_spell_cast();
            }
            break;
        case Phase::AwaitSpellFinish:
        {
            if (!VerifyStowState(s_Sequence.Plan, s_Sequence.Plan.Count, obs))
            {
                interrupt();
                break;
            }
            const bool paralyzedNow = g_Player->IsParalyzed();
            if (!s_Sequence.WasParalyzedBeforeCast)
            {
                if (paralyzedNow)
                {
                    s_Sequence.SawCastFreeze = true;
                }
                else if (s_Sequence.SawCastFreeze && !s_Sequence.PostCastObserved)
                {
                    s_Sequence.PostCastObserved = true;
                    s_Sequence.PostCastSince = g_Ticks;
                }
            }
            if (g_Player->Mana < s_Sequence.InitialMana && !s_Sequence.PostCastObserved)
            {
                s_Sequence.PostCastObserved = true;
                s_Sequence.PostCastSince = g_Ticks;
            }
            if (s_Sequence.CursorActive && !g_Target.IsTargeting())
            {
                s_Sequence.CursorActive = false;
                s_Sequence.CursorEnded = true;
            }
            const bool castFrozenNow = !s_Sequence.WasParalyzedBeforeCast && paralyzedNow;
            const auto outcome = EvaluateSpellWait(
                s_Sequence.SpellFizzled,
                s_Sequence.CursorActive,
                s_Sequence.SawCursor,
                s_Sequence.CursorEnded,
                s_Sequence.PostCastObserved,
                castFrozenNow,
                g_Ticks,
                s_Sequence.CastSentAt,
                s_Sequence.CursorSince,
                s_Sequence.PostCastSince);
            if (outcome == SpellWaitOutcome::RestoreNow)
            {
                s_Sequence.RestoreIndex = 0;
                enter(Phase::PrepareToRestore, "Spell finished. Restoring your equipment.");
            }
            else if (outcome == SpellWaitOutcome::Abort)
            {
                interrupt();
            }
            break;
        }
        case Phase::PrepareToRestore:
            if (!VerifyRestoreState(s_Sequence.Plan, s_Sequence.RestoreIndex, obs))
            {
                interrupt();
            }
            else if (can_send_move_now())
            {
                send_restore();
            }
            break;
        case Phase::ConfirmRestored:
            if (VerifyRestoreState(s_Sequence.Plan, s_Sequence.RestoreIndex + 1, obs))
            {
                if (s_Sequence.UsedMacroMove)
                {
                    s_MacroMoveConfirmed = true;
                    s_Sequence.UsedMacroMove = false;
                }
                s_Sequence.ForceClassicMove = false;
                ++s_Sequence.RestoreIndex;
                if (s_Sequence.RestoreIndex < s_Sequence.Plan.Count)
                {
                    enter(Phase::PrepareToRestore, "Restoring your off hand.");
                    if (can_send_move_now())
                    {
                        send_restore();
                    }
                }
                else if (s_Sequence.Kind == Mode::Spell)
                {
                    finish(s_Sequence.SpellFizzled ? "Spell stopped and equipment restored."
                                                   : "Spell finished and equipment restored.");
                }
                else
                {
                    finish(s_Sequence.DrankPotion ? "Potion used and equipment restored."
                                                  : "Equipment restored.");
                }
            }
            else if (s_Sequence.UsedMacroMove &&
                     VerifyRestoreState(s_Sequence.Plan, s_Sequence.RestoreIndex, obs) &&
                     DeadlineReached(g_Ticks, s_Sequence.PhaseSince, MacroFallbackTimeout))
            {
                if (!s_MacroMoveConfirmed)
                {
                    s_MacroMovesSupported = false;
                }
                s_Sequence.ForceClassicMove = true;
                s_Sequence.UsedMacroMove = false;
                enter(Phase::PrepareToRestore, "Restoring your equipment.");
            }
            else if (!VerifyRestoreInFlight(s_Sequence.Plan, s_Sequence.RestoreIndex, obs) ||
                     DeadlineReached(g_Ticks, s_Sequence.PhaseSince, MoveReplyTimeout))
            {
                finish("Equipment could not be restored. Check your hands and backpack.", true);
            }
            break;
    }
}

void PotionHands_ResetSession()
{
    finish("Ready.");
    s_Enabled = false;
    s_SpellHandsEnabled = false;
    s_Profile.clear();
    s_HasAction = false;
    s_LastAction = 0;
    s_MacroMovesSupported = true;
    s_MacroMoveConfirmed = false;
    s_SpellChanneling.clear();
    s_Balanced.clear();
}

void PotionHands_Draw()
{
    load_profile();
    bool potionEnabled = s_Enabled;
    bool spellEnabled = s_SpellHandsEnabled;
    bool changed = false;

    if (ImGui::Checkbox("Free a hand for potions", &potionEnabled))
    {
        changed = true;
    }
    ImGui::TextWrapped("Use a standard potion from your backpack. CrossUO frees a hand and restores the exact item.");
    if (potionEnabled && !potion_permitted())
    {
        ImGui::TextUnformatted("Potion hand switching is unavailable on this shard.");
    }

    ImGui::Separator();

    if (ImGui::Checkbox("Free hands before casting spells", &spellEnabled))
    {
        changed = true;
    }
    ImGui::TextWrapped("Unequip non-Spell Channeling weapons and shields before casting, then restore them after the spell finishes.");
    if (spellEnabled && !spell_permitted())
    {
        ImGui::TextUnformatted("Spell hand switching is unavailable on this shard.");
    }

    if (changed)
    {
        save_settings(potionEnabled, spellEnabled);
    }

    if (s_Enabled || s_SpellHandsEnabled)
    {
        ImGui::Separator();
        ImGui::TextUnformatted(s_Status);
    }
}

} // namespace xuoassist
