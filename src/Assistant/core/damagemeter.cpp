// SPDX-License-Identifier: AGPL-3.0-or-later

#include "damagemeter.h"
#include "damagemeter_state.h"

#include <cstdio>
#include <vector>
#include <external/gfx/imgui/imgui.h>

#include "Config.h"
#include "Globals.h"
#include "global.h"

namespace xuoassist {
namespace {

DamageMeterState s_Meter;

bool CanRecord()
{
    return g_Config.AssistantEnabled && g_Connected && g_PlayerSerial != 0 &&
        (g_GameState == GS_GAME || g_GameState == GS_GAME_BLOCKED);
}

void DrawTotals(const char *label, const DamageTotals &totals, uint64_t elapsed)
{
    ImGui::Text("%s  %llu damage  %.1f/s", label, static_cast<unsigned long long>(totals.Amount),
        totals.PerSecond(elapsed));
}

void DrawRow(const char *label, const DamageTotals &totals, uint64_t elapsed)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%llu", static_cast<unsigned long long>(totals.Amount));
    ImGui::TableSetColumnIndex(2);
    ImGui::Text("%llu", static_cast<unsigned long long>(totals.Hits));
    ImGui::TableSetColumnIndex(3);
    ImGui::Text("%d", totals.Largest);
    ImGui::TableSetColumnIndex(4);
    ImGui::Text("%.1f", totals.PerSecond(elapsed));
}

} // namespace

void DamageMeter_OnDamage(uint32_t serial, const astr_t &name, int amount)
{
    if (CanRecord())
    {
        s_Meter.Record(serial, name, amount);
    }
}

bool DamageMeter_Start()
{
    if (!CanRecord())
    {
        return false;
    }
    s_Meter.Start(g_Ticks, g_PlayerSerial);
    return true;
}

void DamageMeter_Pause() { s_Meter.Pause(g_Ticks); }
void DamageMeter_Resume() { if (CanRecord()) s_Meter.Resume(g_Ticks); }
void DamageMeter_Stop() { s_Meter.Stop(g_Ticks); }
void DamageMeter_Reset() { s_Meter = {}; }
bool DamageMeter_Running() { return s_Meter.GetStatus() == DamageMeterState::Status::Running; }
uint64_t DamageMeter_GetDamage(uint32_t serial) { return s_Meter.DamageFor(serial); }

void DamageMeter_Draw()
{
    using Status = DamageMeterState::Status;
    const auto state = s_Meter.GetStatus();
    ImGui::BeginDisabled(!CanRecord());
    if (state == Status::Running)
    {
        if (ImGui::Button("Pause")) DamageMeter_Pause();
    }
    else if (state == Status::Paused)
    {
        if (ImGui::Button("Resume")) DamageMeter_Resume();
    }
    else if (ImGui::Button("Start"))
    {
        DamageMeter_Start();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state != Status::Running && state != Status::Paused);
    if (ImGui::Button("Stop")) DamageMeter_Stop();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state == Status::Idle);
    if (ImGui::Button("Clear")) DamageMeter_Reset();
    ImGui::EndDisabled();

    const auto elapsed = s_Meter.Elapsed(g_Ticks);
    ImGui::TextDisabled("%.1f seconds recorded", elapsed / 1000.0);
    ImGui::TextWrapped("Counts the damage numbers your shard shows you.");
    DrawTotals("Damage to you", s_Meter.Taken(), elapsed);
    DrawTotals("Damage to other targets", s_Meter.Others(), elapsed);
    if (s_Meter.Targets().empty())
    {
        return;
    }

    std::vector<uint32_t> ranked;
    ranked.reserve(s_Meter.Targets().size());
    for (const auto &target : s_Meter.Targets())
    {
        ranked.push_back(target.first);
    }
    std::sort(ranked.begin(), ranked.end(), [](uint32_t a, uint32_t b) {
        const auto first = s_Meter.DamageFor(a);
        const auto second = s_Meter.DamageFor(b);
        return first != second ? first > second : a < b;
    });

    if (ImGui::BeginTable("##damage_targets", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
    {
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Damage", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Hits", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Biggest hit", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Damage/s", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (const auto serial : ranked)
        {
            const auto &target = s_Meter.Targets().find(serial)->second;
            char fallback[20];
            std::snprintf(fallback, sizeof(fallback), "0x%08X", serial);
            DrawRow(target.Name.empty() ? fallback : target.Name.c_str(), target.Totals, elapsed);
        }
        if (s_Meter.Unlisted().Hits != 0)
        {
            DrawRow("Additional targets", s_Meter.Unlisted(), elapsed);
        }
        ImGui::EndTable();
    }
}

} // namespace xuoassist
