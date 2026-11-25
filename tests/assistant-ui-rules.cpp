// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../external/gfx/imgui/imgui.h"
#include "../external/gfx/imgui/bui/imgui_bui_style.h"
#include "../src/Assistant/AssistantWindow_rules.h"

namespace {

int g_Failures = 0;

#define EXPECT_TRUE(cond)                                                                      \
    do                                                                                         \
    {                                                                                          \
        if (!(cond))                                                                           \
        {                                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++g_Failures;                                                                      \
        }                                                                                      \
    } while (0)

struct HeadlessImGui
{
    ImGuiContext *Ctx = nullptr;

    explicit HeadlessImGui(const char *iniText = nullptr)
    {
        Ctx = ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1728.0f, 1117.0f);
        io.DeltaTime = 1.0f / 60.0f;
        bui::StyleBlender();
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
        if (iniText != nullptr)
        {
            ImGui::LoadIniSettingsFromMemory(iniText);
        }
    }

    ~HeadlessImGui()
    {
        ImGui::DestroyContext(Ctx);
    }
};

void TestEmptyGroupAfterBeginReproduces1722RunawayStretch()
{
    HeadlessImGui ui;
    ImVec2 finalSize(0.0f, 0.0f);

    for (int frame = 0; frame < 260; ++frame)
    {
        ImGui::NewFrame();
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 12.0f, viewport->WorkPos.y + 60.0f),
            ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
        if (ImGui::Begin("Assistant display###Item counters", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoDocking))
        {
            ImGui::BeginGroup();
            ImGui::EndGroup();
            ImGui::TextUnformatted("0 gold per hour (net)");
            finalSize = ImGui::GetWindowSize();
        }
        ImGui::End();
        ImGui::EndFrame();
    }

    // Proves the exact 1722px ceiling (1728 - 2*DisplaySafeAreaPadding.x) from xuoassist.ini.
    EXPECT_TRUE(std::fabs(finalSize.x - 1722.0f) < 1.0f);
}

void TestFixedHudRecoversFromPoisonedIniAndNeverStretchesWhenDragged()
{
    const char *poisonedIni =
        "[Window][Item counters]\n"
        "Pos=1590,952\n"
        "Size=1722,51\n"
        "Collapsed=0\n";

    HeadlessImGui ui(poisonedIni);
    ImGuiIO &io = ImGui::GetIO();

    ImVec2 winPos(0.0f, 0.0f);
    ImVec2 winSize(0.0f, 0.0f);

    auto drawHud = [&](bool withCaption, bool withCounters) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 12.0f, viewport->WorkPos.y + 60.0f),
            ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
        if (ImGui::Begin("Assistant display###Item counters", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoDocking))
        {
            if (withCaption)
            {
                ImGui::TextUnformatted("Elijah - HP 85/85 - ST 67/67 - MN 28/28 - Very Long Shard Title Segment");
                ImGui::Separator();
            }
            if (withCounters)
            {
                const char *entries[] = { "BP 24", "BM 34", "GA 22", "GS 21" };
                for (int i = 0; i < 4; ++i)
                {
                    if (i % 3 != 0)
                    {
                        ImGui::SameLine();
                    }
                    ImGui::TextUnformatted(entries[i]);
                }
            }
            ImGui::TextUnformatted("0 gold per hour (net)");
            winPos = ImGui::GetWindowPos();
            winSize = ImGui::GetWindowSize();
        }
        ImGui::End();
    };

    // 1. Auto-recovers from Size=1722,51 immediately and stays compact across 200 frames.
    for (int frame = 0; frame < 200; ++frame)
    {
        ImGui::NewFrame();
        drawHud(false, false);
        ImGui::EndFrame();
    }
    EXPECT_TRUE(winSize.x > 100.0f && winSize.x < 240.0f);
    const float compactWidth = winSize.x;

    // 2. Simulate grabbing the title bar and dragging all the way left across the 1728px viewport.
    const float grabX = winPos.x + 60.0f;
    const float grabY = winPos.y + 10.0f;
    io.AddMousePosEvent(grabX, grabY);
    ImGui::NewFrame();
    drawHud(false, false);
    ImGui::EndFrame();

    io.AddMouseButtonEvent(0, true);
    ImGui::NewFrame();
    drawHud(false, false);
    ImGui::EndFrame();

    for (int step = 1; step <= 40; ++step)
    {
        const float targetX = grabX - (1500.0f * static_cast<float>(step) / 40.0f);
        io.AddMousePosEvent(targetX, 380.0f);
        ImGui::NewFrame();
        drawHud(false, false);
        ImGui::EndFrame();
        EXPECT_TRUE(std::fabs(winSize.x - compactWidth) < 2.0f);
    }

    io.AddMouseButtonEvent(0, false);
    ImGui::NewFrame();
    drawHud(false, false);
    ImGui::EndFrame();

    EXPECT_TRUE(winPos.x < 120.0f);
    EXPECT_TRUE(std::fabs(winSize.x - compactWidth) < 2.0f);

    // 3. Caption + Separator expands cleanly, then turning caption off shrinks back to compactWidth.
    for (int frame = 0; frame < 5; ++frame)
    {
        ImGui::NewFrame();
        drawHud(true, false);
        ImGui::EndFrame();
    }
    EXPECT_TRUE(winSize.x > compactWidth + 100.0f && winSize.x < 700.0f);

    for (int frame = 0; frame < 5; ++frame)
    {
        ImGui::NewFrame();
        drawHud(false, false);
        ImGui::EndFrame();
    }
    EXPECT_TRUE(std::fabs(winSize.x - compactWidth) < 2.0f);

    // 4. Enabling counters lays out compactly and stays stable across frames.
    for (int frame = 0; frame < 20; ++frame)
    {
        ImGui::NewFrame();
        drawHud(false, true);
        ImGui::EndFrame();
    }
    EXPECT_TRUE(winSize.x > 100.0f && winSize.x < 300.0f);
}

void TestOverlayInputAndCursorPolicy()
{
    using namespace xuoassist;

    // Standalone Assistant display HUD (shell closed): hovering shows OS cursor & captures mouse,
    // without swallowing keyboard input for chat/hotkeys.
    AssistantOverlayInputState hudHovered{};
    hudHovered.HudVisible = true;
    hudHovered.WantCaptureMouse = true;
    hudHovered.WantCaptureKeyboard = true;
    EXPECT_TRUE(AssistantOverlay_ShouldShowSystemCursor(hudHovered));
    EXPECT_TRUE(AssistantOverlay_ShouldCaptureMouse(hudHovered));
    EXPECT_TRUE(!AssistantOverlay_ShouldCaptureKeyboard(hudHovered));

    // Standalone HUD visible, mouse over game world: hide OS cursor & pass through to game.
    AssistantOverlayInputState hudNotHovered{};
    hudNotHovered.HudVisible = true;
    hudNotHovered.WantCaptureMouse = false;
    EXPECT_TRUE(!AssistantOverlay_ShouldShowSystemCursor(hudNotHovered));
    EXPECT_TRUE(!AssistantOverlay_ShouldCaptureMouse(hudNotHovered));
    EXPECT_TRUE(!AssistantOverlay_ShouldCaptureKeyboard(hudNotHovered));

    // Asset Viewer open with shell closed: typing stays captured even when mouse moved away.
    AssistantOverlayInputState viewerTyping{};
    viewerTyping.AssetViewerOpen = true;
    viewerTyping.WantCaptureMouse = false;
    viewerTyping.WantCaptureKeyboard = true;
    viewerTyping.WantTextInput = true;
    EXPECT_TRUE(!AssistantOverlay_ShouldShowSystemCursor(viewerTyping));
    EXPECT_TRUE(!AssistantOverlay_ShouldCaptureMouse(viewerTyping));
    EXPECT_TRUE(AssistantOverlay_ShouldCaptureKeyboard(viewerTyping));

    // No surfaces visible: never show system cursor or capture mouse.
    AssistantOverlayInputState nothingVisible{};
    nothingVisible.WantCaptureMouse = true;
    nothingVisible.WantCaptureKeyboard = true;
    EXPECT_TRUE(!AssistantOverlay_ShouldShowSystemCursor(nothingVisible));
    EXPECT_TRUE(!AssistantOverlay_ShouldCaptureMouse(nothingVisible));
    EXPECT_TRUE(!AssistantOverlay_ShouldCaptureKeyboard(nothingVisible));

    // Main shell open: captures both mouse and keyboard when ImGui requests them.
    AssistantOverlayInputState shellOpen{};
    shellOpen.ShellVisible = true;
    shellOpen.WantCaptureMouse = true;
    shellOpen.WantCaptureKeyboard = true;
    EXPECT_TRUE(AssistantOverlay_ShouldShowSystemCursor(shellOpen));
    EXPECT_TRUE(AssistantOverlay_ShouldCaptureMouse(shellOpen));
    EXPECT_TRUE(AssistantOverlay_ShouldCaptureKeyboard(shellOpen));
}

void TestReadabilityScaleAndOpacityRules()
{
    using namespace xuoassist;

    // Legacy 0/missing opacity must default to 100% rather than 30% ghost transparency.
    EXPECT_TRUE(AssistantOverlay_SanitizeOpacity(0) == 100);
    EXPECT_TRUE(AssistantOverlay_SanitizeOpacity(-5) == 100);
    EXPECT_TRUE(AssistantOverlay_SanitizeOpacity(12) == 30);
    EXPECT_TRUE(AssistantOverlay_SanitizeOpacity(30) == 30);
    EXPECT_TRUE(AssistantOverlay_SanitizeOpacity(85) == 85);
    EXPECT_TRUE(AssistantOverlay_SanitizeOpacity(140) == 100);

    // UI scale defaults cleanly to 100% and clamps within 85..150%.
    EXPECT_TRUE(AssistantOverlay_ClampUiScale(0) == 100);
    EXPECT_TRUE(AssistantOverlay_ClampUiScale(-10) == 100);
    EXPECT_TRUE(AssistantOverlay_ClampUiScale(50) == 85);
    EXPECT_TRUE(AssistantOverlay_ClampUiScale(115) == 115);
    EXPECT_TRUE(AssistantOverlay_ClampUiScale(250) == 150);

    // Pure black or transparent text becomes light gray (#E6E6E6).
    EXPECT_TRUE(AssistantOverlay_ReadableTextOnDark(0x00000000u) == 0xFFE6E6E6u);
    EXPECT_TRUE(AssistantOverlay_ReadableTextOnDark(0xFF000000u) == 0xFFE6E6E6u);

    // Dark red (e.g. hue 0x0021 dark crimson) gets lifted to >= 50% Rec.709 luma while staying red-tinted.
    const uint32_t liftedRed = AssistantOverlay_ReadableTextOnDark(IM_COL32(90, 12, 12, 255));
    const uint32_t rr = liftedRed & 0xFFu;
    const uint32_t rg = (liftedRed >> 8) & 0xFFu;
    const uint32_t rb = (liftedRed >> 16) & 0xFFu;
    const uint32_t rLuma = 213u * rr + 715u * rg + 72u * rb;
    EXPECT_TRUE((liftedRed >> 24) == 0xFFu);
    EXPECT_TRUE(rLuma >= 127000u);
    EXPECT_TRUE(rr > rg && rr > rb);

    // Dark blue gets lifted above readability floor while staying blue-tinted.
    const uint32_t liftedBlue = AssistantOverlay_ReadableTextOnDark(IM_COL32(16, 28, 120, 255));
    const uint32_t br = liftedBlue & 0xFFu;
    const uint32_t bg = (liftedBlue >> 8) & 0xFFu;
    const uint32_t bb = (liftedBlue >> 16) & 0xFFu;
    const uint32_t bLuma = 213u * br + 715u * bg + 72u * bb;
    EXPECT_TRUE(bLuma >= 127000u);
    EXPECT_TRUE(bb > br && bb > bg);

    // Already bright color passes through unchanged (with opaque alpha).
    const uint32_t brightGold = IM_COL32(240, 210, 90, 255);
    EXPECT_TRUE(AssistantOverlay_ReadableTextOnDark(brightGold) == brightGold);
}

void TestShellMinSizeExpandsLegacyIniAndHotkeysKeepButtonsVisible()
{
    const char *legacyIni =
        "[Window][X:UO Assist]\n"
        "Pos=100,100\n"
        "Size=650,400\n"
        "Collapsed=0\n";

    HeadlessImGui ui(legacyIni);
    ImVec2 shellSize(0.0f, 0.0f);
    ImVec2 childMax(0.0f, 0.0f);
    ImVec2 addButtonMax(0.0f, 0.0f);

    for (int frame = 0; frame < 5; ++frame)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(900.0f, 640.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(760.0f, 480.0f), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::Begin("X:UO Assist"))
        {
            shellSize = ImGui::GetWindowSize();
        }
        ImGui::End();

        ImGui::SetNextWindowSize(ImVec2(980.0f, 660.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(860.0f, 520.0f), ImVec2(FLT_MAX, FLT_MAX));
        if (ImGui::Begin("Hotkeys"))
        {
            ImGui::BeginChild("##list", ImVec2(500.0f, 0.0f));
            childMax = ImGui::GetWindowPos();
            childMax.x += ImGui::GetWindowSize().x;
            childMax.y += ImGui::GetWindowSize().y;
            const float buttonRowHeight = ImGui::GetFrameHeightWithSpacing() + 4.0f;
            float tableHeight = ImGui::GetContentRegionAvail().y - buttonRowHeight;
            if (tableHeight < 80.0f)
            {
                tableHeight = 80.0f;
            }
            if (ImGui::BeginTable("##hotkeys_table", 3, ImGuiTableFlags_ScrollY, ImVec2(0.0f, tableHeight)))
            {
                for (int r = 0; r < 40; ++r)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("Row %d", r);
                }
                ImGui::EndTable();
            }
            ImGui::Button("Add");
            addButtonMax = ImGui::GetItemRectMax();
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::EndFrame();
    }

    EXPECT_TRUE(shellSize.x >= 760.0f);
    EXPECT_TRUE(shellSize.y >= 480.0f);
    EXPECT_TRUE(addButtonMax.y <= childMax.y + 1.0f);
}

} // namespace

int main()
{
    TestEmptyGroupAfterBeginReproduces1722RunawayStretch();
    TestFixedHudRecoversFromPoisonedIniAndNeverStretchesWhenDragged();
    TestOverlayInputAndCursorPolicy();
    TestReadabilityScaleAndOpacityRules();
    TestShellMinSizeExpandsLegacyIniAndHotkeysKeepButtonsVisible();
    if (g_Failures != 0)
    {
        std::fprintf(stderr, "assistant-ui-rules: %d failure(s)\n", g_Failures);
        return 1;
    }
    std::printf("assistant-ui-rules: all tests passed\n");
    return 0;
}
