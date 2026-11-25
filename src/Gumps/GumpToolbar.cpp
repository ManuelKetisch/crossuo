// AGPLv3 License
// Copyright (c) 2020 Jean-Martin Miljours
// Copyright (c) 2020 CrossUO Team

#include "GumpToolbar.h"

#if defined(XUO_ASSISTANT)
#include <algorithm>
#include <cstdio>
#include <external/gfx/imgui/imgui.h>

#include "../Assistant/config/assistantconfig.h"
#include "../Assistant/config/configio.h"
#include "../Assistant/core/assist.h"
#include "../Assistant/core/inventoryagent.h"
#include "../Assistant/core/itemcounters.h"
#include "../Assistant/core/worldutil.h"
#include "../Config.h"
#include "../CrossUO.h"
#include "../GUI/GUIGumppic.h"
#include "../GameObjects/GamePlayer.h"
#include "../GameObjects/GameWorld.h"
#include "../GameObjects/ObjectOnCursor.h"
#include "../GameWindow.h"
#include "../Globals.h"
#include "../Managers/ClilocManager.h"
#include "../Managers/GumpManager.h"
#include "../Managers/MouseManager.h"
#include "../Managers/SkillsManager.h"
#include "../Misc.h"
#include "../Point.h"
#include "../PressedObject.h"
#include "../Renderer/RenderAPI.h"
#include "../SelectedObject.h"
#include "../Spells.h"
#include "../Sprite.h"
#include "../Target.h"
#include "../ToolTip.h"
#include "GumpAbility.h"
#include "GumpSpell.h"

namespace
{
constexpr int FirstSlotId = 1000;
constexpr int SlotWidth = 42;
constexpr int SlotHeight = 45;
constexpr int HeaderHeight = 22;
constexpr int HorizontalPadding = 6;
constexpr int FooterHeight = 8;
constexpr uint16_t AnyHue = 0xFFFF;
constexpr uint16_t NormalText = 0x0044;
constexpr uint16_t LowText = 0x0026;
constexpr uint16_t ActiveText = 0x0035;
constexpr uint16_t SkillScrollGraphic = 0x0EF3;
constexpr int MaximumAlert = 65535;
constexpr size_t MaximumName = 63;

enum class SlotKind : uint8_t
{
    Item = 0,
    Spell = 1,
    PrimaryAbility = 2,
    SecondaryAbility = 3,
    Skill = 4,
};

struct Slot
{
    uint16_t Graphic = 0;
    uint16_t Hue = AnyHue;
    int Alert = 0;
    astr_t Name;
    SlotKind Kind = SlotKind::Item;
    uint16_t SpellId = 0;
    int SkillIndex = -1;
};

class CGUIGumppicScaled final : public CGUIGumppic
{
    CSize m_Size;

public:
    CGUIGumppicScaled(uint16_t graphic, uint16_t color, int x, int y, int width, int height)
        : CGUIGumppic(graphic, x, y)
        , m_Size(width, height)
    {
        Color = color;
    }

    CSize GetSize() override { return m_Size; }

    void Draw(bool) override
    {
        auto *spr = g_Game.ExecuteGump(GetDrawGraphic());
        if (spr != nullptr && spr->Texture != nullptr && m_Size.Width > 0 && m_Size.Height > 0)
        {
            SetShaderMode();
            const auto textureCmd = DrawQuadCmd{
                spr->Texture->Texture,
                m_X,
                m_Y,
                uint32_t(m_Size.Width),
                uint32_t(m_Size.Height),
            };
            RenderAdd_DrawQuad(g_renderCmdList, textureCmd);
        }
    }
};

const char *SlotKindToIni(SlotKind kind)
{
    switch (kind)
    {
        case SlotKind::Spell: return "spell";
        case SlotKind::PrimaryAbility: return "primary_ability";
        case SlotKind::SecondaryAbility: return "secondary_ability";
        case SlotKind::Skill: return "skill";
        case SlotKind::Item:
        default:
            return "item";
    }
}

SlotKind SlotKindFromIni(const astr_t &value)
{
    const auto lower = str_lower(str_trim(value));
    if (lower == "spell")
    {
        return SlotKind::Spell;
    }
    if (lower == "primary_ability")
    {
        return SlotKind::PrimaryAbility;
    }
    if (lower == "secondary_ability")
    {
        return SlotKind::SecondaryAbility;
    }
    if (lower == "skill")
    {
        return SlotKind::Skill;
    }
    return SlotKind::Item;
}

bool SlotConfigured(const Slot &slot)
{
    switch (slot.Kind)
    {
        case SlotKind::Item:
            return slot.Graphic != 0;
        case SlotKind::Spell:
            return slot.SpellId != 0 && GetSpellById(slot.SpellId) != nullptr;
        case SlotKind::PrimaryAbility:
        case SlotKind::SecondaryAbility:
            return true;
        case SlotKind::Skill:
            return slot.SkillIndex >= 0 &&
                static_cast<uint32_t>(slot.SkillIndex) < g_SkillsManager.Count;
    }
    return false;
}

const char *SpellbookLabel(SPELLBOOK_TYPE type)
{
    switch (type)
    {
        case ST_MAGERY: return "Magery";
        case ST_NECROMANCY: return "Necromancy";
        case ST_CHIVALRY: return "Chivalry";
        case ST_BUSHIDO: return "Bushido";
        case ST_NINJITSU: return "Ninjitsu";
        case ST_SPELLWEAVING: return "Spellweaving";
        case ST_MYSTICISM: return "Mysticism";
        case ST_MASTERY: return "Mastery";
        default: return "Spells";
    }
}

astr_t SlotDisplayLabel(const Slot &slot)
{
    if (!slot.Name.empty())
    {
        return slot.Name;
    }
    switch (slot.Kind)
    {
        case SlotKind::Item:
            return slot.Graphic != 0 ? "Item " + xuoassist::CAssistant::GraphicToText(slot.Graphic) : astr_t{};
        case SlotKind::Spell:
        {
            const auto *spell = GetSpellById(slot.SpellId);
            return spell != nullptr && spell->Name != nullptr ? astr_t(spell->Name) : "Spell";
        }
        case SlotKind::PrimaryAbility:
            return "Primary ability";
        case SlotKind::SecondaryAbility:
            return "Secondary ability";
        case SlotKind::Skill:
        {
            const auto *skill = slot.SkillIndex >= 0 ?
                g_SkillsManager.Get(static_cast<uint32_t>(slot.SkillIndex)) : nullptr;
            return skill != nullptr ? skill->Name : "Skill";
        }
    }
    return {};
}

struct Layout
{
    std::array<Slot, 12> Slots = {{
        { 0x0E21, AnyHue, 10, "Bandages" }, { 0x0F7A, AnyHue, 10, "Black pearl" },
        { 0x0F8C, AnyHue, 10, "Sulfurous ash" }, { 0x0F85, AnyHue, 10, "Ginseng" },
        { 0x0F86, AnyHue, 10, "Mandrake root" }, { 0x0F8D, AnyHue, 10, "Spider silk" },
        { 0x0F84, AnyHue, 10, "Garlic" }, { 0x0F88, AnyHue, 10, "Nightshade" },
        { 0x0F7B, AnyHue, 10, "Blood moss" }, { 0x0F0B, AnyHue, 10, "Refresh potion" },
        { 0x0F0C, AnyHue, 10, "Heal potion" }, { 0x0F09, AnyHue, 10, "Strength potion" },
    }};
    bool Locked = false;
    bool OpenOnLogin = false;
    int X = 20;
    int Y = 140;
};

Layout s_Layout;
fs_path s_ProfilePath{};
uint32_t s_Character = 0;
uint64_t s_ProfileRevision = 0;
int s_SelectedSlot = 0;
int s_TargetSlot = -1;
Slot s_Draft;
std::array<char, MaximumName + 1> s_Name{};
uint16_t s_DraftHue = 0;
bool s_AllHues = true;
bool s_SaveFailed = false;

CGumpToolbar *FindToolbar()
{
    return static_cast<CGumpToolbar *>(g_GumpManager.GetGump(0, 0, GT_ASSISTANT_TOOLBAR));
}

void CancelItemTarget()
{
    s_TargetSlot = -1;
    if (xuoassist::g_TargetHandler == &xuoassist::CAssistant::HandleTargetToolbarItem)
    {
        xuoassist::g_TargetHandler = nullptr;
        if (g_Target.FromAssist)
        {
            g_Target.CancelCursor();
            g_Target.FromAssist = false;
        }
    }
}

void LoadDraft()
{
    s_Draft = s_Layout.Slots[s_SelectedSlot];
    s_AllHues = s_Draft.Hue == AnyHue;
    s_DraftHue = s_AllHues ? 0 : s_Draft.Hue;
    std::snprintf(s_Name.data(), s_Name.size(), "%s", s_Draft.Name.c_str());
}

void OpenToolbar()
{
    if (FindToolbar() == nullptr)
    {
        g_GumpManager.AddGump(new CGumpToolbar(s_Layout.X, s_Layout.Y));
    }
}

bool GameReady()
{
    return g_Config.AssistantEnabled && g_Player != nullptr && xuoassist::g_Connected &&
        g_GameState == GS_GAME;
}

bool HasCurrentProfile()
{
    return GameReady() && s_Character == g_PlayerSerial && !fs_path_empty(s_ProfilePath) &&
        fs_path_equal(s_ProfilePath, xuoassist::g_AssistantConfig.GetProfilePath());
}

bool SaveLayout(const Layout &layout)
{
    if (!HasCurrentProfile())
    {
        return false;
    }
    xuoassist::CIniFile ini;
    ini.SetBool("Toolbar", "locked", layout.Locked);
    ini.SetBool("Toolbar", "open_on_login", layout.OpenOnLogin);
    ini.SetInt("Toolbar", "x", layout.X);
    ini.SetInt("Toolbar", "y", layout.Y);
    for (size_t i = 0; i < layout.Slots.size(); ++i)
    {
        const auto &slot = layout.Slots[i];
        const auto section = xuoassist::IniSectionName("Slot", static_cast<int>(i));
        ini.Set(section, "kind", SlotKindToIni(slot.Kind));
        ini.SetInt(section, "graphic", slot.Graphic);
        ini.SetInt(section, "hue", slot.Hue);
        ini.SetInt(section, "alert", slot.Alert);
        ini.SetInt(section, "spell_id", slot.SpellId);
        ini.SetInt(section, "skill_index", slot.SkillIndex);
        ini.Set(section, "name", slot.Name);
    }
    xuoassist::g_AssistantConfig.EnsureDirectories();
    s_SaveFailed = !ini.Save(fs_path_join(s_ProfilePath, "Toolbar.ini"));
    if (s_SaveFailed)
    {
        return false;
    }
    s_Layout = layout;
    if (auto *toolbar = FindToolbar())
    {
        toolbar->LockMoving = layout.Locked;
        toolbar->NoMove = layout.Locked;
        toolbar->WantUpdateContent = true;
    }
    return true;
}

void LoadProfile()
{
    if (g_Player == nullptr || !xuoassist::g_Connected)
    {
        return;
    }
    const auto path = xuoassist::g_AssistantConfig.GetProfilePath();
    if (s_Character == g_PlayerSerial && fs_path_equal(s_ProfilePath, path))
    {
        return;
    }

    CancelItemTarget();
    ++s_ProfileRevision;
    s_Character = g_PlayerSerial;
    s_ProfilePath = path;
    s_Layout = {};
    s_SaveFailed = false;

    xuoassist::CIniFile ini;
    if (ini.Load(fs_path_join(path, "Toolbar.ini")))
    {
        s_Layout.Locked = ini.GetBool("Toolbar", "locked");
        s_Layout.OpenOnLogin = ini.GetBool("Toolbar", "open_on_login");
        s_Layout.X = std::max(0, std::min(32767, ini.GetInt("Toolbar", "x", s_Layout.X)));
        s_Layout.Y = std::max(0, std::min(32767, ini.GetInt("Toolbar", "y", s_Layout.Y)));
        for (size_t i = 0; i < s_Layout.Slots.size(); ++i)
        {
            auto &slot = s_Layout.Slots[i];
            const auto section = xuoassist::IniSectionName("Slot", static_cast<int>(i));
            const SlotKind kind = ini.Has(section, "kind") ?
                SlotKindFromIni(ini.Get(section, "kind", "item")) : SlotKind::Item;
            if (kind == SlotKind::Item)
            {
                const int graphic = ini.GetInt(section, "graphic", slot.Graphic);
                const int hue = ini.GetInt(section, "hue", slot.Hue);
                if (graphic >= 0 && graphic < 0xFFFF && hue >= 0 && hue <= 0xFFFF)
                {
                    slot.Kind = SlotKind::Item;
                    slot.Graphic = static_cast<uint16_t>(graphic);
                    slot.Hue = static_cast<uint16_t>(hue);
                    slot.Alert = std::max(0, std::min(MaximumAlert, ini.GetInt(section, "alert", slot.Alert)));
                    slot.SpellId = 0;
                    slot.SkillIndex = -1;
                    slot.Name = ini.Get(section, "name", slot.Name).substr(0, MaximumName);
                }
            }
            else if (kind == SlotKind::Spell)
            {
                const int spellId = ini.GetInt(section, "spell_id", 0);
                if (spellId > 0 && spellId <= 0xFFFF)
                {
                    slot.Kind = SlotKind::Spell;
                    slot.Graphic = 0;
                    slot.Hue = AnyHue;
                    slot.Alert = 0;
                    slot.SpellId = static_cast<uint16_t>(spellId);
                    slot.SkillIndex = -1;
                    slot.Name = ini.Get(section, "name", "").substr(0, MaximumName);
                }
            }
            else if (kind == SlotKind::PrimaryAbility || kind == SlotKind::SecondaryAbility)
            {
                slot.Kind = kind;
                slot.Graphic = 0;
                slot.Hue = AnyHue;
                slot.Alert = 0;
                slot.SpellId = 0;
                slot.SkillIndex = -1;
                slot.Name = ini.Get(section, "name", "").substr(0, MaximumName);
            }
            else if (kind == SlotKind::Skill)
            {
                const int skillIndex = ini.GetInt(section, "skill_index", -1);
                if (skillIndex >= 0 && skillIndex < 256)
                {
                    slot.Kind = SlotKind::Skill;
                    slot.Graphic = 0;
                    slot.Hue = AnyHue;
                    slot.Alert = 0;
                    slot.SpellId = 0;
                    slot.SkillIndex = skillIndex;
                    slot.Name = ini.Get(section, "name", "").substr(0, MaximumName);
                }
            }
        }
    }
    LoadDraft();

    if (auto *toolbar = FindToolbar())
    {
        toolbar->SetX(s_Layout.X);
        toolbar->SetY(s_Layout.Y);
        toolbar->LockMoving = s_Layout.Locked;
        toolbar->NoMove = s_Layout.Locked;
        toolbar->WantUpdateContent = true;
    }
    else if (s_Layout.OpenOnLogin)
    {
        OpenToolbar();
    }
}

int ColumnCount()
{
    const int width = g_GameWindow.GetSize().Width;
    const int maximum = static_cast<int>(s_Layout.Slots.size());
    return width > 0 ? std::max(1, std::min(maximum, (width - 2 * HorizontalPadding) / SlotWidth)) : maximum;
}

int SlotIndex(uint32_t serial)
{
    return serial >= FirstSlotId && serial < FirstSlotId + s_Layout.Slots.size() ?
        static_cast<int>(serial - FirstSlotId) : -1;
}

} // namespace

CGumpToolbar::CGumpToolbar(int x, int y) : CGump(GT_ASSISTANT_TOOLBAR, 0, x, y)
{
    LockMoving = s_Layout.Locked;
    NoMove = s_Layout.Locked;
    m_Counts.fill(-1);
    m_SlotActive.fill(false);
    m_AbilityState = { g_Ability[0], g_Ability[1] };
    m_Columns = ColumnCount();
    FitInWindow();
}

void CGumpToolbar::Tick()
{
    if (GameReady())
    {
        LoadProfile();
    }
}

void CGumpToolbar::ResetSession()
{
    CancelItemTarget();
    ++s_ProfileRevision;
    s_ProfilePath = {};
    s_Character = 0;
    s_SelectedSlot = 0;
    s_Layout = {};
    s_SaveFailed = false;
    g_GumpManager.CloseGump(0, 0, GT_ASSISTANT_TOOLBAR);
}

void xuoassist::CAssistant::HandleTargetToolbarItem(uint8_t *buf) const
{
    const int intendedSlot = s_TargetSlot;
    s_TargetSlot = -1;
    if (!HasCurrentProfile() || intendedSlot != s_SelectedSlot || g_World == nullptr)
    {
        return;
    }
    auto *item = g_World->FindWorldItem(unpack32(buf + 7));
    if (item == nullptr || item->Graphic == 0 || item->Graphic == 0xFFFF)
    {
        return;
    }
    s_Draft.Kind = SlotKind::Item;
    s_Draft.Graphic = item->Graphic;
    s_DraftHue = item->Color;
    s_AllHues = false;
    std::snprintf(s_Name.data(), s_Name.size(), "%s", item->GetName().c_str());
}

void CGumpToolbar::DrawSettings()
{
    if (!GameReady())
    {
        ImGui::TextUnformatted("Enter the world to edit the toolbar.");
        return;
    }
    LoadProfile();
    if (FindToolbar() != nullptr)
    {
        if (ImGui::Button("Close toolbar"))
        {
            g_GumpManager.CloseGump(0, 0, GT_ASSISTANT_TOOLBAR);
        }
    }
    else if (ImGui::Button("Open toolbar"))
    {
        OpenToolbar();
    }
    bool locked = s_Layout.Locked;
    if (ImGui::Checkbox("Lock toolbar position", &locked))
    {
        auto updated = s_Layout;
        updated.Locked = locked;
        SaveLayout(updated);
    }
    bool onLogin = s_Layout.OpenOnLogin;
    if (ImGui::Checkbox("Open toolbar on login", &onLogin))
    {
        auto updated = s_Layout;
        updated.OpenOnLogin = onLogin;
        SaveLayout(updated);
    }

    ImGui::Separator();
    char slotLabel[20];
    std::snprintf(slotLabel, sizeof(slotLabel), "Slot %d", s_SelectedSlot + 1);
    if (ImGui::BeginCombo("Edit slot", slotLabel))
    {
        for (size_t i = 0; i < s_Layout.Slots.size(); ++i)
        {
            std::snprintf(slotLabel, sizeof(slotLabel), "Slot %d", static_cast<int>(i + 1));
            if (ImGui::Selectable(slotLabel, static_cast<int>(i) == s_SelectedSlot))
            {
                CancelItemTarget();
                s_SelectedSlot = static_cast<int>(i);
                LoadDraft();
            }
        }
        ImGui::EndCombo();
    }

    int kindIndex = static_cast<int>(s_Draft.Kind);
    if (ImGui::Combo("Slot type", &kindIndex,
            "Item\0Spell\0Primary weapon ability\0Secondary weapon ability\0Skill\0"))
    {
        CancelItemTarget();
        s_Draft.Kind = static_cast<SlotKind>(kindIndex);
        if (s_Draft.Kind == SlotKind::Spell && s_Draft.SpellId == 0)
        {
            s_Draft.SpellId = 4; // Heal
        }
        else if (s_Draft.Kind == SlotKind::Skill && s_Draft.SkillIndex < 0)
        {
            for (uint32_t i = 0; i < g_SkillsManager.Count; ++i)
            {
                const auto *sk = g_SkillsManager.Get(i);
                if (sk != nullptr && sk->Button)
                {
                    s_Draft.SkillIndex = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    bool canSave = false;
    if (s_Draft.Kind == SlotKind::Item)
    {
        ImGui::InputText("Item name", s_Name.data(), s_Name.size());
        ImGui::InputScalar("Graphic", ImGuiDataType_U16, &s_Draft.Graphic, nullptr, nullptr,
            "%04X", ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::Checkbox("Any hue", &s_AllHues);
        ImGui::BeginDisabled(s_AllHues);
        ImGui::InputScalar("Hue", ImGuiDataType_U16, &s_DraftHue, nullptr, nullptr,
            "%04X", ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::EndDisabled();
        ImGui::InputInt("Low stock threshold", &s_Draft.Alert);
        ImGui::TextDisabled("A threshold of 0 turns off the low-stock highlight.");
        ImGui::BeginDisabled(xuoassist::g_TargetHandler != nullptr || g_Target.IsTargeting());
        if (ImGui::Button("Target an item"))
        {
            s_TargetSlot = s_SelectedSlot;
            xuoassist::g_TargetHandler = &xuoassist::CAssistant::HandleTargetToolbarItem;
            xuoassist::g_Assistant.RequestTarget("Select an item for this toolbar slot.");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        canSave = s_Draft.Graphic != 0 && s_Draft.Graphic != 0xFFFF &&
            (s_AllHues || s_DraftHue != AnyHue);
    }
    else if (s_Draft.Kind == SlotKind::Spell)
    {
        const Spell *currentSpell = GetSpellById(s_Draft.SpellId);
        const char *spellPreview = currentSpell != nullptr && currentSpell->Name != nullptr ?
            currentSpell->Name : "Select a spell";
        if (ImGui::BeginCombo("Spell", spellPreview))
        {
            for (int bookIdx = ST_FIRST; bookIdx < ST_COUNT; ++bookIdx)
            {
                const auto &book = GetSpellbook(bookIdx);
                if (book.SpellCount == 0)
                {
                    continue;
                }
                ImGui::SeparatorText(SpellbookLabel(book.Type));
                for (int s = 0; s < book.SpellCount; ++s)
                {
                    const Spell *sp = book.Spells[s];
                    if (sp == nullptr || sp->Name == nullptr)
                    {
                        continue;
                    }
                    ImGui::PushID(sp->Id);
                    const bool selected = (sp->Id == s_Draft.SpellId);
                    if (ImGui::Selectable(sp->Name, selected))
                    {
                        s_Draft.SpellId = sp->Id;
                        std::snprintf(s_Name.data(), s_Name.size(), "%s", sp->Name);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("Label", s_Name.data(), s_Name.size());
        canSave = currentSpell != nullptr;
    }
    else if (s_Draft.Kind == SlotKind::PrimaryAbility || s_Draft.Kind == SlotKind::SecondaryAbility)
    {
        ImGui::InputText("Label", s_Name.data(), s_Name.size());
        ImGui::TextDisabled(s_Draft.Kind == SlotKind::PrimaryAbility ?
            "Toggles your equipped weapon's primary ability." :
            "Toggles your equipped weapon's secondary ability.");
        canSave = true;
    }
    else if (s_Draft.Kind == SlotKind::Skill)
    {
        const auto *currentSkill = s_Draft.SkillIndex >= 0 ?
            g_SkillsManager.Get(static_cast<uint32_t>(s_Draft.SkillIndex)) : nullptr;
        const char *skillPreview = currentSkill != nullptr ? currentSkill->Name.c_str() : "Select a skill";
        if (ImGui::BeginCombo("Skill", skillPreview))
        {
            for (uint32_t i = 0; i < g_SkillsManager.Count; ++i)
            {
                const auto *sk = g_SkillsManager.Get(i);
                if (sk == nullptr || !sk->Button)
                {
                    continue;
                }
                ImGui::PushID(static_cast<int>(i));
                const bool selected = (static_cast<int>(i) == s_Draft.SkillIndex);
                if (ImGui::Selectable(sk->Name.c_str(), selected))
                {
                    s_Draft.SkillIndex = static_cast<int>(i);
                    std::snprintf(s_Name.data(), s_Name.size(), "%s", sk->Name.c_str());
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("Label", s_Name.data(), s_Name.size());
        canSave = currentSkill != nullptr && currentSkill->Button;
    }

    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Save slot"))
    {
        auto updated = s_Layout;
        Slot saved{};
        saved.Kind = s_Draft.Kind;
        saved.Name = s_Name.data();
        if (saved.Kind == SlotKind::Item)
        {
            saved.Graphic = s_Draft.Graphic;
            saved.Hue = s_AllHues ? AnyHue : s_DraftHue;
            saved.Alert = std::max(0, std::min(MaximumAlert, s_Draft.Alert));
        }
        else if (saved.Kind == SlotKind::Spell)
        {
            saved.SpellId = s_Draft.SpellId;
        }
        else if (saved.Kind == SlotKind::Skill)
        {
            saved.SkillIndex = s_Draft.SkillIndex;
        }
        updated.Slots[s_SelectedSlot] = std::move(saved);
        if (SaveLayout(updated))
        {
            LoadDraft();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear slot"))
    {
        CancelItemTarget();
        auto updated = s_Layout;
        updated.Slots[s_SelectedSlot] = {};
        if (SaveLayout(updated))
        {
            LoadDraft();
        }
    }
    if (s_SaveFailed)
    {
        ImGui::TextUnformatted("Could not save the toolbar.");
    }
}

void CGumpToolbar::FitInWindow()
{
    const auto size = g_GameWindow.GetSize();
    if (size.Width <= 0 || size.Height <= 0)
    {
        return;
    }
    const int rows = (SlotCount + m_Columns - 1) / m_Columns;
    const int width = m_Columns * SlotWidth + 2 * HorizontalPadding;
    const int height = HeaderHeight + rows * SlotHeight + FooterHeight;
    SetX(std::max(0, std::min(s_Layout.X, std::max(0, size.Width - width))));
    SetY(std::max(0, std::min(s_Layout.Y, std::max(0, size.Height - height))));
}

void CGumpToolbar::PrepareContent()
{
    if (g_PressedObject.LeftGump != this)
    {
        const int columns = ColumnCount();
        if (m_Columns != columns)
        {
            m_Columns = columns;
            WantUpdateContent = true;
        }
        FitInWindow();
    }
    if (!WantUpdateContent && g_Ticks - m_LastRefresh >= 250)
    {
        RefreshCounts();
    }
}

void CGumpToolbar::UpdateContent()
{
    Clear();
    m_Coverage = -1;
    m_Counts.fill(-2);
    m_SlotActive.fill(false);
    m_AbilityState = { g_Ability[0], g_Ability[1] };
    const int rows = (SlotCount + m_Columns - 1) / m_Columns;
    const int width = m_Columns * SlotWidth + 2 * HorizontalPadding;
    const int height = HeaderHeight + rows * SlotHeight + FooterHeight;
    Add(new CGUIResizepic(0, 0x0A3C, 0, 0, width, height));
    m_CoverageText = static_cast<CGUIText *>(Add(new CGUIText(NormalText, HorizontalPadding, 6)));

    for (int i = 0; i < SlotCount; ++i)
    {
        const int x = HorizontalPadding + (i % m_Columns) * SlotWidth;
        const int y = HeaderHeight + (i / m_Columns) * SlotHeight;
        const auto &slot = s_Layout.Slots[i];
        constexpr int IconSize = 24;
        if (slot.Kind == SlotKind::Item && slot.Graphic != 0)
        {
            const auto *sprite = g_Game.ExecuteStaticArt(slot.Graphic);
            if (sprite != nullptr && sprite->Width > 0 && sprite->Height > 0)
            {
                const int longer = std::max({ IconSize, int(sprite->Width), int(sprite->Height) });
                const int iconWidth = std::max(1, sprite->Width * IconSize / longer);
                const int iconHeight = std::max(1, sprite->Height * IconSize / longer);
                Add(new CGUITilepicScaled(slot.Graphic, slot.Hue == AnyHue ? 0 : slot.Hue,
                    x + (SlotWidth - iconWidth) / 2, y + (IconSize - iconHeight) / 2, iconWidth, iconHeight));
            }
        }
        else if (slot.Kind == SlotKind::Spell)
        {
            const auto *spell = GetSpellById(slot.SpellId);
            if (spell != nullptr && spell->SpellIconGraphic != 0)
            {
                const bool active = CGumpSpell::IsSpellActive(slot.SpellId);
                m_SlotActive[i] = active;
                Add(new CGUIGumppicScaled(spell->SpellIconGraphic, active ? 38 : 0,
                    x + (SlotWidth - 26) / 2, y, 26, 26));
            }
        }
        else if (slot.Kind == SlotKind::PrimaryAbility || slot.Kind == SlotKind::SecondaryAbility)
        {
            const int abIdx = slot.Kind == SlotKind::SecondaryAbility ? 1 : 0;
            const uint8_t raw = g_Ability[abIdx];
            const uint8_t abId = raw & 0x7F;
            const bool active = (raw & 0x80) != 0;
            m_SlotActive[i] = active;
            const uint16_t icon = abId > 0 ? static_cast<uint16_t>(0x5200 + abId - 1) : 0x5200;
            Add(new CGUIGumppicScaled(icon, active ? 38 : 0,
                x + (SlotWidth - 26) / 2, y, 26, 26));
        }
        else if (slot.Kind == SlotKind::Skill && SlotConfigured(slot))
        {
            const auto *sprite = g_Game.ExecuteStaticArt(SkillScrollGraphic);
            if (sprite != nullptr && sprite->Width > 0 && sprite->Height > 0)
            {
                const int longer = std::max({ IconSize, int(sprite->Width), int(sprite->Height) });
                const int iconWidth = std::max(1, sprite->Width * IconSize / longer);
                const int iconHeight = std::max(1, sprite->Height * IconSize / longer);
                Add(new CGUITilepicScaled(SkillScrollGraphic, 0,
                    x + (SlotWidth - iconWidth) / 2, y + (IconSize - iconHeight) / 2, iconWidth, iconHeight));
            }
        }
        m_CountText[i] = static_cast<CGUIText *>(Add(new CGUIText(NormalText, x, y + 28)));
        auto *slotHit = static_cast<CGUIHitBox *>(Add(new CGUIHitBox(FirstSlotId + i, x, y, SlotWidth, SlotHeight)));
        slotHit->MoveOnDrag = true;
    }
    RefreshCounts();
}

void CGumpToolbar::RefreshCounts()
{
    m_LastRefresh = g_Ticks;
    if (m_AbilityState[0] != g_Ability[0] || m_AbilityState[1] != g_Ability[1])
    {
        m_AbilityState = { g_Ability[0], g_Ability[1] };
        WantUpdateContent = true;
        return;
    }

    const auto supplies = xuoassist::ItemCounters_GetSupplies();
    const auto *pack = g_Player != nullptr ? g_Player->FindLayer(OL_BACKPACK) : nullptr;
    const auto coverage = supplies.Coverage;
    const bool coverageChanged = m_Coverage != static_cast<int>(coverage);
    if (coverageChanged)
    {
        m_Coverage = static_cast<int>(coverage);
        const char *text = "Backpack";
        if (coverage == xuoassist::SuppliesCoverage::Partial)
        {
            text = "Partial";
        }
        else if (coverage == xuoassist::SuppliesCoverage::Unknown)
        {
            text = "Unknown";
        }
        m_CoverageText->CreateTextureA(9, text);
        FrameCreated = false;
    }

    for (int i = 0; i < SlotCount; ++i)
    {
        const auto &slot = s_Layout.Slots[i];
        if (slot.Kind == SlotKind::Item)
        {
            int count = -1;
            if (slot.Graphic != 0 && coverage != xuoassist::SuppliesCoverage::Unknown)
            {
                const auto counter = std::find_if(supplies.Entries.begin(), supplies.Entries.end(), [&](const auto &entry) {
                    return entry.Graphic == slot.Graphic && entry.Hue == slot.Hue;
                });
                count = counter != supplies.Entries.end() ? counter->Count :
                    xuoassist::CountItems(pack, slot.Graphic, slot.Hue, true);
            }
            if (coverageChanged || count != m_Counts[i])
            {
                m_Counts[i] = count;
                const bool low = coverage == xuoassist::SuppliesCoverage::Complete && count >= 0 &&
                    slot.Alert > 0 && count < slot.Alert;
                m_CountText[i]->Color = low ? LowText : NormalText;
                const astr_t amount = slot.Graphic == 0 ? astr_t{} : (count < 0 ? "--" : str_from(count));
                m_CountText[i]->CreateTextureA(9, amount, SlotWidth, TS_CENTER, UOFONT_CROPPED);
                FrameCreated = false;
            }
        }
        else if (slot.Kind == SlotKind::Spell)
        {
            const bool active = CGumpSpell::IsSpellActive(slot.SpellId);
            if (active != m_SlotActive[i])
            {
                WantUpdateContent = true;
                return;
            }
            if (m_Counts[i] != -3)
            {
                m_Counts[i] = -3;
                m_CountText[i]->Color = active ? ActiveText : NormalText;
                const astr_t label = SlotConfigured(slot) ? SlotDisplayLabel(slot) : astr_t{};
                m_CountText[i]->CreateTextureA(9, label, SlotWidth, TS_CENTER, UOFONT_CROPPED);
                FrameCreated = false;
            }
        }
        else if (slot.Kind == SlotKind::PrimaryAbility || slot.Kind == SlotKind::SecondaryAbility)
        {
            const int abIdx = slot.Kind == SlotKind::SecondaryAbility ? 1 : 0;
            const bool active = (g_Ability[abIdx] & 0x80) != 0;
            if (m_Counts[i] != -3)
            {
                m_Counts[i] = -3;
                m_CountText[i]->Color = active ? ActiveText : NormalText;
                const astr_t label = !slot.Name.empty() ? slot.Name :
                    (abIdx == 0 ? "Prim" : "Sec");
                m_CountText[i]->CreateTextureA(9, label, SlotWidth, TS_CENTER, UOFONT_CROPPED);
                FrameCreated = false;
            }
        }
        else if (slot.Kind == SlotKind::Skill)
        {
            if (m_Counts[i] != -3)
            {
                m_Counts[i] = -3;
                m_CountText[i]->Color = NormalText;
                const astr_t label = SlotConfigured(slot) ? SlotDisplayLabel(slot) : astr_t{};
                m_CountText[i]->CreateTextureA(9, label, SlotWidth, TS_CENTER, UOFONT_CROPPED);
                FrameCreated = false;
            }
        }
    }
}

void CGumpToolbar::InitToolTip()
{
    const int index = SlotIndex(g_SelectedObject.Serial);
    if (index < 0)
    {
        return;
    }
    const auto &slot = s_Layout.Slots[index];
    if (!SlotConfigured(slot))
    {
        g_ToolTip.Set(L"Configure this slot in Assistant > Display > Toolbar.", 180);
        return;
    }
    if (slot.Kind == SlotKind::Item)
    {
        wstr_t text = wstr_from_utf8(SlotDisplayLabel(slot));
        if (m_Counts[index] < 0)
        {
            text += L"\nBackpack contents are unknown.";
        }
        else if (m_Coverage == static_cast<int>(xuoassist::SuppliesCoverage::Partial))
        {
            text += L"\nAt least " + std::to_wstring(m_Counts[index]) + L" visible in your backpack.";
        }
        else
        {
            text += L"\n" + std::to_wstring(m_Counts[index]) + L" in your backpack.";
        }
        text += L"\nDouble-click to use.";
        g_ToolTip.Set(text, 180);
    }
    else if (slot.Kind == SlotKind::Spell)
    {
        wstr_t text = wstr_from_utf8(SlotDisplayLabel(slot));
        const auto *spell = GetSpellById(slot.SpellId);
        if (spell != nullptr)
        {
            if (spell->PowerWords != nullptr && spell->PowerWords[0] != '\0')
            {
                text += L"\n" + wstr_from_utf8(spell->PowerWords);
            }
            if (spell->ManaCost > 0)
            {
                text += L"\nMana: " + std::to_wstring(spell->ManaCost);
            }
        }
        text += L"\nDouble-click to cast.";
        g_ToolTip.Set(text, 180);
    }
    else if (slot.Kind == SlotKind::PrimaryAbility || slot.Kind == SlotKind::SecondaryAbility)
    {
        const int abIdx = slot.Kind == SlotKind::SecondaryAbility ? 1 : 0;
        const uint8_t raw = g_Ability[abIdx];
        const uint8_t abId = raw & 0x7F;
        const bool active = (raw & 0x80) != 0;
        wstr_t text;
        if (!slot.Name.empty())
        {
            text = wstr_from_utf8(slot.Name);
        }
        else if (abId > 0)
        {
            text = g_ClilocManager.Cliloc(g_Language)->GetW(1028838 + abId - 1, true);
        }
        else
        {
            text = abIdx == 0 ? L"Primary weapon ability" : L"Secondary weapon ability";
        }
        text += active ? L"\nActive" : L"\nReady";
        text += L"\nDouble-click to toggle.";
        g_ToolTip.Set(text, 180);
    }
    else if (slot.Kind == SlotKind::Skill)
    {
        wstr_t text = wstr_from_utf8(SlotDisplayLabel(slot));
        text += L"\nDouble-click to use skill.";
        g_ToolTip.Set(text, 180);
    }
}

void CGumpToolbar::OnLeftMouseButtonDown()
{
    m_DragProfileRevision = HasCurrentProfile() && !WantUpdateContent ? s_ProfileRevision : 0;
    CGump::OnLeftMouseButtonDown();
}

void CGumpToolbar::OnLeftMouseButtonUp()
{
    CGump::OnLeftMouseButtonUp();
    const bool sameProfile = HasCurrentProfile() && m_DragProfileRevision == s_ProfileRevision;
    m_DragProfileRevision = 0;
    const auto offset = g_MouseManager.LeftDroppedOffset();
    if (sameProfile && CanBeMoved() && !g_ObjectInHand.Enabled && (offset.X != 0 || offset.Y != 0))
    {
        auto updated = s_Layout;
        updated.X = std::max(0, GetX());
        updated.Y = std::max(0, GetY());
        if (updated.X != s_Layout.X || updated.Y != s_Layout.Y)
        {
            SaveLayout(updated);
        }
    }
    FitInWindow();
}

bool CGumpToolbar::OnLeftMouseButtonDoubleClick()
{
    const int index = SlotIndex(g_PressedObject.LeftSerial);
    if (index < 0)
    {
        return false;
    }
    // A profile may switch before the following tick has rebuilt what was clicked.
    if (!HasCurrentProfile() || WantUpdateContent || g_Player->IsDead())
    {
        return true;
    }
    const auto &slot = s_Layout.Slots[index];
    if (slot.Kind == SlotKind::Item)
    {
        if (g_Target.IsTargeting() || g_Target.FromAssist ||
            g_ObjectInHand.Enabled || xuoassist::InventoryAgent_MoveInFlight())
        {
            return true;
        }
        if (slot.Graphic != 0)
        {
            auto *pack = g_Player->FindLayer(OL_BACKPACK);
            auto *item = pack != nullptr ? pack->FindItem(slot.Graphic, slot.Hue) : nullptr;
            if (item != nullptr && item->Count > 0 && !item->Dragged)
            {
                g_Game.DoubleClick(item->Serial);
            }
        }
    }
    else if (slot.Kind == SlotKind::Spell)
    {
        if (slot.SpellId != 0 && GetSpellById(slot.SpellId) != nullptr)
        {
            g_Game.CastSpell(slot.SpellId);
        }
    }
    else if (slot.Kind == SlotKind::PrimaryAbility)
    {
        CGumpAbility::OnAbilityUse(0);
    }
    else if (slot.Kind == SlotKind::SecondaryAbility)
    {
        CGumpAbility::OnAbilityUse(1);
    }
    else if (slot.Kind == SlotKind::Skill)
    {
        if (slot.SkillIndex >= 0 && static_cast<uint32_t>(slot.SkillIndex) < g_SkillsManager.Count)
        {
            const auto *skill = g_SkillsManager.Get(static_cast<uint32_t>(slot.SkillIndex));
            if (skill != nullptr && skill->Button)
            {
                g_Game.UseSkill(slot.SkillIndex);
            }
        }
    }
    return true;
}
#endif
