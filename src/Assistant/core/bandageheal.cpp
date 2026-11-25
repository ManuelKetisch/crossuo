// SPDX-License-Identifier: AGPL-3.0-or-later

#include "bandageheal.h"

#include "../config/assistantconfig.h"
#include "../config/configio.h"
#include "../macros/macroexecutor.h"
#include "assist.h"
#include "bandageheal_rules.h"
#include "dressagent.h"
#include "inventoryagent.h"
#include "shopagent.h"
#include "world.h"
#include "Config.h"
#include "GameObjects/ObjectOnCursor.h"
#include "Globals.h"
#include "Misc.h"
#include "Network/Packets.h"
#include "Party.h"
#include "Target.h"
#include <external/gfx/imgui/imgui.h>
#include <algorithm>

namespace xuoassist {
namespace {

constexpr uint16_t DefaultBandage = 0x0E21;
constexpr uint16_t AnyHue = 0xFFFF;
constexpr uint16_t MortalStrike = 0x0403;
constexpr uint32_t HealResponseGrace = 2000;
// Confirmed heals wait for the result even when a custom shard takes longer than the estimate.
constexpr uint32_t ConfirmedHealTimeout = 20000;

struct Settings
{
    BandagePatientMode PatientMode = BandagePatientMode::Self;
    bool AutoStart = false;
    bool AutomaticDelay = true;
    bool TreatPoison = false;
    bool PauseHidden = true;
    bool PauseMortal = true;
    bool Announce = false;
    bool SpecifiedBandage = false;
    int HitPercent = 85;
    int Distance = 2;
    int Delay = 6000;
    int Graphic = DefaultBandage;
    int Hue = AnyHue;
};

Settings s_Settings;
astr_t s_Profile;
uint32_t s_StartedCharacter = 0;
uint32_t s_SelectionCharacter = 0;
uint32_t s_SelectedPatient = 0;
uint32_t s_AttemptDelay = 0;
BandageTimer s_OtherAction;
BandageTimer s_Attempt;
BandageTimer s_Healing;
BandageTimer s_Mortal;
BandageTimer s_Refusal;
bool s_MortalUntilRemoved = false;
bool s_AwaitingStart = false;
bool s_StoppedManually = false;
bool s_SaveFailed = false;
const char *s_Status = "Stopped.";

int clamped(int value, int low, int high)
{
    return std::max(low, std::min(value, high));
}

bool active_character()
{
    return g_Connected && g_GameState == GS_GAME && g_World != nullptr &&
        g_Player != nullptr && !g_Player->IsDead();
}

bool supported()
{
    // The bandage macro includes both serials in one standard 0xBF request.
    // A cursor-based fallback could consume an unrelated target reply.
    return g_Config.AssistantEnabled && g_Config.ProtocolClientVersion >= CV_7000 &&
        EnabledFeatureLoopedMacro() && (g_ServerAssistantFeatures & ASF_LOOPED_MACROS) == 0;
}

bool approved_patient(uint32_t serial)
{
    return serial != 0 && (g_Party.Contains(serial) || g_AssistantConfig.InFriendList(serial));
}

void load_profile()
{
    const auto path = g_AssistantConfig.GetProfilePath();
    const auto key = fs_path_str(path);
    if (key == s_Profile)
    {
        return;
    }
    s_Profile = key;
    BandageHeal_Stop();
    s_StoppedManually = false;
    s_SelectedPatient = 0;
    s_Settings = {};
    CIniFile ini;
    if (ini.Load(fs_path_join(path, "BandageHeal.ini")))
    {
        constexpr const char *section = "BandageHeal";
        if (ini.Has(section, "patient_mode"))
        {
            s_Settings.PatientMode = static_cast<BandagePatientMode>(
                clamped(ini.GetInt(section, "patient_mode", 0), 0, 2));
        }
        else
        {
            s_Settings.PatientMode = ini.GetBool(section, "selected_patient")
                ? BandagePatientMode::SelectedFriend
                : BandagePatientMode::Self;
        }
        s_Settings.AutoStart = ini.GetBool(section, "auto_start", false);
        s_Settings.AutomaticDelay = ini.GetBool(section, "automatic_delay", true);
        s_Settings.TreatPoison = ini.GetBool(section, "treat_poison");
        s_Settings.PauseHidden = ini.GetBool(section, "pause_hidden", true);
        s_Settings.PauseMortal = ini.GetBool(section, "pause_mortal", true);
        s_Settings.Announce = ini.GetBool(section, "announce");
        s_Settings.SpecifiedBandage = ini.GetBool(section, "specified_bandage");
        s_Settings.HitPercent = clamped(ini.GetInt(section, "hit_percent", 85), 1, 100);
        s_Settings.Distance = clamped(ini.GetInt(section, "distance", 2), 1, 2);
        s_Settings.Delay = clamped(ini.GetInt(section, "delay", 6000), BandageActionInterval, 60000);
        s_Settings.Graphic = clamped(ini.GetInt(section, "graphic", DefaultBandage), 1, 0xFFFF);
        s_Settings.Hue = clamped(ini.GetInt(section, "hue", AnyHue), 0, 0xFFFF);
    }
    s_SaveFailed = false;
}

bool save_settings(const Settings &settings)
{
    constexpr const char *section = "BandageHeal";
    CIniFile ini;
    ini.SetInt(section, "patient_mode", static_cast<int>(settings.PatientMode));
    ini.SetBool(section, "selected_patient", settings.PatientMode == BandagePatientMode::SelectedFriend);
    ini.SetBool(section, "auto_start", settings.AutoStart);
    ini.SetBool(section, "automatic_delay", settings.AutomaticDelay);
    ini.SetBool(section, "treat_poison", settings.TreatPoison);
    ini.SetBool(section, "pause_hidden", settings.PauseHidden);
    ini.SetBool(section, "pause_mortal", settings.PauseMortal);
    ini.SetBool(section, "announce", settings.Announce);
    ini.SetBool(section, "specified_bandage", settings.SpecifiedBandage);
    ini.SetInt(section, "hit_percent", settings.HitPercent);
    ini.SetInt(section, "distance", settings.Distance);
    ini.SetInt(section, "delay", settings.Delay);
    ini.SetInt(section, "graphic", settings.Graphic);
    ini.SetInt(section, "hue", settings.Hue);
    g_AssistantConfig.EnsureDirectories();
    return ini.Save(fs_path_join(g_AssistantConfig.GetProfilePath(), "BandageHeal.ini"));
}

bool action_busy()
{
    return g_Target.IsTargeting() || (g_Target.HookTargeting() && g_Target.CursorType < 3) || g_Target.FromAssist ||
        g_TargetHandler != nullptr || g_ObjectInHand.Enabled || InventoryAgent_MoveInFlight() ||
        g_DressAgent.Active() || g_ShopAgent.IsActiveShopping() ||
        g_MacroExecutor.IsPlaying() || g_MacroExecutor.IsRecording();
}

bool self_mortal_paused()
{
    return s_Settings.PauseMortal && (s_MortalUntilRemoved || s_Mortal.Remaining(g_Ticks) != 0);
}

CGameCharacter *find_patient()
{
    if (s_Settings.PatientMode == BandagePatientMode::Self)
    {
        return g_Player;
    }
    if (s_Settings.PatientMode == BandagePatientMode::SelectedFriend)
    {
        if (!approved_patient(s_SelectedPatient))
        {
            return nullptr;
        }
        return g_World->FindWorldCharacter(s_SelectedPatient);
    }

    // LowestPartyOrFriend: consider self and all visible in-range alive party
    // members / friends who currently need a bandage.
    CGameCharacter *best = nullptr;
    BandagePatientCandidate bestInfo{};
    auto consider = [&](CGameCharacter *mob, bool isSelf) {
        if (mob == nullptr || mob->IsDead() || mob->MapIndex != g_Player->MapIndex)
        {
            return;
        }
        const int dist = isSelf ? 0 : GetDistance(g_Player, mob);
        if (!isSelf && dist > s_Settings.Distance)
        {
            return;
        }
        if (isSelf && self_mortal_paused())
        {
            return;
        }
        if (mob->MaxHits == 0 ||
            !BandageNeeded(mob->Hits, mob->MaxHits, s_Settings.HitPercent,
                           mob->IsPoisoned(), s_Settings.TreatPoison))
        {
            return;
        }
        const BandagePatientCandidate candidate{
            mob->Serial, mob->Hits, mob->MaxHits, dist, isSelf, mob->IsPoisoned()
        };
        if (best == nullptr || BandagePatientBetter(candidate, bestInfo, s_Settings.TreatPoison))
        {
            best = mob;
            bestInfo = candidate;
        }
    };

    consider(g_Player, true);
    if (g_World != nullptr)
    {
        for (auto *obj = g_World->m_Items; obj != nullptr; obj = static_cast<CGameObject *>(obj->m_Next))
        {
            if (obj->NPC && obj->Serial != g_PlayerSerial && approved_patient(obj->Serial))
            {
                consider(static_cast<CGameCharacter *>(obj), false);
            }
        }
    }
    return best != nullptr ? best : g_Player;
}

CGameItem *find_bandage()
{
    auto *pack = g_Player->FindLayer(OL_BACKPACK);
    if (pack == nullptr)
    {
        return nullptr;
    }
    const uint16_t graphic = s_Settings.SpecifiedBandage ? s_Settings.Graphic : DefaultBandage;
    const uint16_t hue = s_Settings.SpecifiedBandage ? s_Settings.Hue : AnyHue;
    auto *item = pack->FindItem(graphic, hue);
    return item != nullptr && item->Count > 0 && !item->Dragged ? item : nullptr;
}

void cancel_selection()
{
    s_SelectionCharacter = 0;
    if (g_TargetHandler == &CAssistant::HandleTargetBandagePatient)
    {
        g_TargetHandler = nullptr;
        if (g_Target.FromAssist)
        {
            g_Target.CancelCursor();
            g_Target.FromAssist = false;
        }
    }
}

} // namespace

bool BandageHeal_Start()
{
    load_profile();
    s_StoppedManually = false;
    if (BandageHeal_Running())
    {
        return true;
    }
    if (!active_character() || !supported())
    {
        s_Status = "Bandage Heal is unavailable here.";
        return false;
    }
    if (s_Settings.PatientMode == BandagePatientMode::SelectedFriend && !approved_patient(s_SelectedPatient))
    {
        s_Status = "Select a party member or friend first.";
        return false;
    }
    s_StartedCharacter = g_PlayerSerial;
    s_Status = "Watching health.";
    return true;
}

void BandageHeal_Stop()
{
    s_StartedCharacter = 0;
    s_StoppedManually = true;
    s_Status = "Stopped.";
    cancel_selection();
}

bool BandageHeal_Running()
{
    return s_StartedCharacter != 0 && s_StartedCharacter == g_PlayerSerial && active_character();
}

const char *BandageHeal_Status()
{
    return s_Status != nullptr ? s_Status : "Stopped.";
}

void BandageHeal_ResetSession()
{
    BandageHeal_Stop();
    s_StoppedManually = false;
    s_SelectedPatient = 0;
    s_AttemptDelay = 0;
    s_OtherAction = {};
    s_Attempt = {};
    s_Healing = {};
    s_Mortal = {};
    s_Refusal = {};
    s_MortalUntilRemoved = false;
    s_AwaitingStart = false;
}

void BandageHeal_OnGameSend(const uint8_t *data, std::size_t size)
{
    if (!active_character())
    {
        return;
    }
    s_OtherAction.Extend(g_Ticks, BandageCompetingAction(data, size));
    if (BandageTargetedUse(data, size))
    {
        InventoryAgent_OnItemActionSent();
    }
}

void BandageHeal_OnCliloc(uint32_t message)
{
    if (g_Player == nullptr)
    {
        return;
    }
    switch (message)
    {
        case 500956: // begins applying a bandage, including manual use
        {
            const auto duration = s_AwaitingStart ? std::max(s_AttemptDelay + HealResponseGrace, ConfirmedHealTimeout) :
                ConfirmedHealTimeout;
            s_Healing = { g_Ticks, duration };
            s_AwaitingStart = false;
            break;
        }
        case 500962: // no longer alive
        case 500963: // moved out of range
        case 500965: // resurrection succeeds
        case 500966: // resurrection fails
        case 500967: // heals the remaining damage
        case 500968: // healing mostly fails
        case 500969: // finished
        case 1060088: // bleeding stopped
        case 503255: // creature resurrection succeeds
        case 503256: // creature resurrection fails
            s_Healing = {};
            s_AwaitingStart = false;
            break;
        case 1005000: // self is mortally wounded
        case 1010398: // patient is mortally wounded
            if (s_Healing.Remaining(g_Ticks) != 0 || s_AwaitingStart)
            {
                s_Healing = {};
                s_AwaitingStart = false;
                s_Refusal.Extend(g_Ticks, 6000);
            }
            break;
    }
}

void BandageHeal_OnBuff(uint16_t icon, bool added, uint16_t seconds)
{
    if (icon == MortalStrike)
    {
        s_MortalUntilRemoved = added && seconds == 0;
        s_Mortal = { g_Ticks, added ? 1000u * seconds : 0 };
    }
}

void BandageHeal_Tick()
{
    load_profile();
    if (s_SelectionCharacter != 0 && (!active_character() || s_SelectionCharacter != g_PlayerSerial))
    {
        cancel_selection();
    }
    if (s_StartedCharacter == 0)
    {
        if (s_Settings.AutoStart && !s_StoppedManually && active_character() && supported() &&
            (s_Settings.PatientMode != BandagePatientMode::SelectedFriend || approved_patient(s_SelectedPatient)))
        {
            s_StartedCharacter = g_PlayerSerial;
            s_Status = "Watching health.";
        }
        else
        {
            return;
        }
    }
    if (!BandageHeal_Running() || !supported())
    {
        s_StartedCharacter = 0;
        s_Status = "Stopped.";
        cancel_selection();
        return;
    }
    if (s_Healing.Remaining(g_Ticks) != 0 || s_Attempt.Remaining(g_Ticks) != 0)
    {
        s_Status = "Waiting for the last bandage.";
        return;
    }
    s_AwaitingStart = false;
    if (s_Refusal.Remaining(g_Ticks) != 0)
    {
        s_Status = "The patient cannot be healed yet.";
        return;
    }
    if (s_Settings.PauseHidden && g_Player->Hidden())
    {
        s_Status = "Paused while hidden.";
        return;
    }

    auto *patient = find_patient();
    if (patient == nullptr || patient->IsDead() || patient->MapIndex != g_Player->MapIndex ||
        GetDistance(g_Player, patient) > s_Settings.Distance)
    {
        s_Status = "Waiting for the patient to come into range.";
        return;
    }
    const bool self = patient == g_Player;
    if (self && self_mortal_paused())
    {
        s_Status = "Paused during Mortal Strike.";
        return;
    }
    if (patient->MaxHits == 0)
    {
        s_Status = "Open the patient's status bar to read their health.";
        return;
    }
    if (!BandageNeeded(patient->Hits, patient->MaxHits, s_Settings.HitPercent,
                       patient->IsPoisoned(), s_Settings.TreatPoison))
    {
        s_Status = "Watching health.";
        return;
    }
    if (action_busy() || s_OtherAction.Remaining(g_Ticks) != 0)
    {
        s_Status = "Waiting for the current action.";
        return;
    }

    const auto *bandage = find_bandage();
    if (bandage == nullptr)
    {
        s_Status = "No matching bandages in loaded backpack bags.";
        return;
    }
    s_AttemptDelay = s_Settings.AutomaticDelay ? BandageEstimatedInterval(g_Player->Dex, self, !patient->IsHuman()) :
        static_cast<uint32_t>(s_Settings.Delay);
    s_Attempt = { g_Ticks, s_AttemptDelay };
    s_AwaitingStart = true;
    s_Status = "Bandage sent.";
    CPacketTargetSelectedObject(bandage->Serial, patient->Serial).Send();
    if (s_Settings.Announce)
    {
        g_Assistant.ClientPrint("Bandage sent. Estimated wait " + str_from(s_AttemptDelay) + " ms.");
    }
}

void CAssistant::HandleTargetBandagePatient(uint8_t *buf) const
{
    const auto character = s_SelectionCharacter;
    s_SelectionCharacter = 0;
    if (!active_character() || character != g_PlayerSerial || s_Profile != fs_path_str(g_AssistantConfig.GetProfilePath()))
    {
        return;
    }
    const auto serial = unpack32(buf + 7);
    if (!approved_patient(serial) || g_World->FindWorldCharacter(serial) == nullptr)
    {
        s_Status = "Select a visible party member or friend.";
        return;
    }
    s_SelectedPatient = serial;
    s_Status = "Patient selected.";
}

void BandageHeal_Draw()
{
    load_profile();
    const bool running = BandageHeal_Running();
    if (running)
    {
        if (ImGui::Button("Stop healing"))
        {
            BandageHeal_Stop();
        }
    }
    else
    {
        ImGui::BeginDisabled(!active_character() || !supported());
        if (ImGui::Button("Start healing"))
        {
            BandageHeal_Start();
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::TextColored(
        running ? ImVec4(0.35f, 0.88f, 0.48f, 1.0f) : ImVec4(0.70f, 0.70f, 0.70f, 1.0f),
        running ? "ACTIVE" : "STOPPED");
    ImGui::SameLine();
    ImGui::TextUnformatted(s_Status);
    if (running)
    {
        const auto remaining = s_Attempt.Remaining(g_Ticks);
        if (remaining != 0)
        {
            ImGui::TextDisabled("Next check in %.1f s", remaining / 1000.0);
        }
    }
    if (!supported())
    {
        ImGui::TextDisabled("Requires a compatible shard that allows repeating assistant actions.");
    }
    if (s_SaveFailed)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.42f, 1.0f), "Could not save healing settings.");
    }

    ImGui::Separator();
    auto settings = s_Settings;
    bool changed = ImGui::Checkbox("Start automatically on login", &settings.AutoStart);
    int patientMode = static_cast<int>(settings.PatientMode);
    changed |= ImGui::Combo(
        "Patient",
        &patientMode,
        "Self\0Selected party member or friend\0Self, party, and friends (lowest HP)\0");
    settings.PatientMode = static_cast<BandagePatientMode>(clamped(patientMode, 0, 2));
    if (settings.PatientMode == BandagePatientMode::SelectedFriend)
    {
        ImGui::BeginDisabled(!active_character() || g_Target.IsTargeting() || g_TargetHandler != nullptr);
        if (ImGui::Button("Select patient"))
        {
            s_SelectionCharacter = g_PlayerSerial;
            g_TargetHandler = &CAssistant::HandleTargetBandagePatient;
            g_Assistant.RequestTarget("Select a party member or friend.");
        }
        ImGui::EndDisabled();
        if (s_SelectedPatient != 0)
        {
            auto *patient = g_World != nullptr ? g_World->FindWorldCharacter(s_SelectedPatient) : nullptr;
            auto label = patient != nullptr ? patient->GetName() : astr_t{};
            if (label.empty())
            {
                label = CAssistant::SerialToText(s_SelectedPatient);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(label.c_str());
        }
    }
    if (settings.PatientMode != BandagePatientMode::Self)
    {
        changed |= ImGui::SliderInt("Range in tiles", &settings.Distance, 1, 2);
    }
    changed |= ImGui::SliderInt("Heal at or below", &settings.HitPercent, 1, 100, "%d%% health");
    changed |= ImGui::Checkbox("Treat poison", &settings.TreatPoison);
    changed |= ImGui::Checkbox("Pause while hidden", &settings.PauseHidden);
    changed |= ImGui::Checkbox("Pause on my Mortal Strike debuff", &settings.PauseMortal);
    changed |= ImGui::Checkbox("Announce each bandage in game", &settings.Announce);
    changed |= ImGui::Checkbox("Estimate delay from Dexterity", &settings.AutomaticDelay);
    if (!settings.AutomaticDelay)
    {
        changed |= ImGui::SliderInt("Minimum delay in ms", &settings.Delay, BandageActionInterval, 60000);
    }
    changed |= ImGui::Checkbox("Use a specific item graphic and hue", &settings.SpecifiedBandage);
    if (settings.SpecifiedBandage)
    {
        changed |= ImGui::InputScalar("Item graphic", ImGuiDataType_S32, &settings.Graphic, nullptr, nullptr, "%04X",
            ImGuiInputTextFlags_CharsHexadecimal);
        changed |= ImGui::InputScalar("Item hue (FFFF allows any)", ImGuiDataType_S32, &settings.Hue, nullptr, nullptr,
            "%04X", ImGuiInputTextFlags_CharsHexadecimal);
        settings.Graphic = clamped(settings.Graphic, 1, 0xFFFF);
        settings.Hue = clamped(settings.Hue, 0, 0xFFFF);
    }
    ImGui::TextDisabled("Tip: You can also bind Auto-heal toggle/start/stop in Hotkeys -> Bandage healing.");
    if (changed)
    {
        s_SaveFailed = !save_settings(settings);
        if (!s_SaveFailed)
        {
            s_Settings = settings;
        }
    }
}

} // namespace xuoassist
