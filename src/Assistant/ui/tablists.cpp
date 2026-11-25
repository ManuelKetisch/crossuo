// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

// The six Lists sub-tabs: Types, Objects, Find, Ignore, Friends, Enemies.
// All bind straight to the g_AssistantConfig containers; every structural
// change persists immediately, and the save icon re-saves.

#include "assistantui.h"

#if defined(XUO_ASSISTANT)

#include <external/gfx/imgui/imgui.h>

#include "widgets.h"
#include "assistanteventsui.h"
#include "../config/assistantconfig.h"
#include "../config/configio.h"
#include "../core/assist.h"
#include "../core/assistantevents.h"
#include "../core/global.h"
#include "../core/targetassist_rules.h"

#include <array>
#include <cstdio>
#include <memory>

#if !defined(XUO_EMSC)
#include <external/tinyfiledialogs.h>
#endif

namespace xuoassist {

namespace {


// NameList label accessors (plain functions keep the callbacks captureless)
static const char *TypeNameOf(const CTypeEntry &e) { return e.Name.c_str(); }
static void TypeSetNameOf(CTypeEntry &e, const char *n) { e.Name = n; }
static const char *ObjectNameOf(const CObjectEntry &e) { return e.Name.c_str(); }
static void ObjectSetNameOf(CObjectEntry &e, const char *n) { e.Name = n; }
static const char *FindNameOf(const CFindListEntry &e) { return e.Name.c_str(); }
static void FindSetNameOf(CFindListEntry &e, const char *n) { e.Name = n; }
static const char *IgnoreNameOf(const CIgnoreListEntry &e) { return e.Name.c_str(); }
static void IgnoreSetNameOf(CIgnoreListEntry &e, const char *n) { e.Name = n; }
static const char *WorldObjectNameOf(const CWorldObjectEntry &e) { return e.Name.c_str(); }
static void WorldObjectSetNameOf(CWorldObjectEntry &e, const char *n) { e.Name = n; }

const ImVec4 HINT_COLOR = ImVec4(1.0f, 0.45f, 0.42f, 1.0f);
const ImVec4 DRESS_BAG_SET = ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
const ImVec4 DRESS_BAG_UNSET = ImVec4(0.95f, 0.42f, 0.40f, 1.0f);

// case-insensitive name collision check
bool name_in_use(const vector<CTypeEntry> &items, const char *name, int skip)
{
    for (int i = (int)items.size() - 1; i >= 0; i--)
    {
        if (i != skip && ListNamesEqual(items[i].Name, name))
        {
            return true;
        }
    }
    return false;
}

// --- Types (tabliststypes.cpp) ------------------------------------------------

// The from-target flow: the button arms g_TargetHandler + RequestTarget; the
// next outgoing 0x6C (assistant-requested cursor) is intercepted in
// CTarget::SendTarget and forwarded here through AssistantUIEvents::OnFromTarget.
// Each tab polls
// the result slot with its own pending flag so a tab switch mid-request stays
// correct.
static void from_target_request(
    bool &pending,
    uint32_t &generation,
    void (CAssistant::*handler)(uint8_t *) const,
    const char *prompt)
{
    pending = true;
    g_TargetHandler = handler;
    g_Assistant.RequestTarget(prompt);
}

static bool from_target_poll(
    const bool &pending, uint32_t &generation, int kind, XA_FROM_TARGET_RESULT &out)
{
    if (!pending)
    {
        return false;
    }
    XA_FROM_TARGET_RESULT &slot = CAssistantUIEvents::FromTargetResult();
    if (slot.Generation == generation || slot.Kind != kind)
    {
        return false;
    }
    generation = slot.Generation;
    out = slot;
    return true;
}

// The per-tab arm states (types/objects/find/ignore/friends/enemies);
// file-scope so TabLists_CancelPendingFromTargets can drop them on shutdown.
static bool s_FtPending[6] = { false, false, false, false, false, false };

void draw_types_tab()
{
    static int selected = -1;
    static int lastSelected = -1;
    static char typeName[64] = { 0 };
    static char typeGraphic[16] = { 0 };
    static char hint[128] = { 0 };
    static bool &ftPending = s_FtPending[0];
    static uint32_t ftGeneration = 0;
    XA_FROM_TARGET_RESULT ftResult;

    vector<CTypeEntry> &items = g_AssistantConfig.m_Types;
    const int buttons = ListButtons("types");
    if ((buttons & XALB_TARGET) != 0)
    {
        from_target_request(ftPending, ftGeneration, &CAssistant::HandleTargetType,
                            "Select a object for obtain graphic");
    }
    if (from_target_poll(ftPending, ftGeneration, XA_FROM_TARGET_TYPE, ftResult))
    {
        // SetTypeFromTarget: fill the graphic field
        snprintf(
            typeGraphic, sizeof(typeGraphic), "%s", CAssistant::GraphicToText(ftResult.Graphic).c_str());
    }

    // the list pane left, the editor fields right
    ImGui::BeginChild("##types_pane", ImVec2(170.0f, 0.0f), ImGuiChildFlags_None);
    bool changed = NameList("##types", items, &selected, &TypeNameOf, &TypeSetNameOf, false, -1.0f);
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##types_form", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    if (selected != lastSelected)
    {
        lastSelected = selected;
        hint[0] = '\0';
        if (selected >= 0 && selected < (int)items.size())
        {
            snprintf(typeName, sizeof(typeName), "%s", items[selected].Name.c_str());
            snprintf(typeGraphic, sizeof(typeGraphic), "%s", CAssistant::GraphicToText(items[selected].Type).c_str());
        }
    }

    // the static edit buffers are read directly (draw path: no scratch copies)
    if ((buttons & XALB_ADD) != 0)
    {
        if (typeName[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the type name!");
        }
        else if (typeGraphic[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the type graphic!");
        }
        else if (name_in_use(items, typeName, -1))
        {
            snprintf(hint, sizeof(hint), "Type name is already exists!");
        }
        else
        {
            CTypeEntry entry;
            entry.Name = typeName;
            entry.Type = (uint16_t)CAssistant::TextToUInt(typeGraphic);
            items.push_back(entry);
            selected = lastSelected = (int)items.size() - 1;
            changed = true;
            hint[0] = '\0';
        }
    }
    if ((buttons & XALB_SAVE) != 0)
    {
        if (typeName[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the type name!");
        }
        else if (typeGraphic[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the type graphic!");
        }
        else if (selected < 0 || selected >= (int)items.size())
        {
            snprintf(hint, sizeof(hint), "Type is not selected!");
        }
        else if (name_in_use(items, typeName, selected))
        {
            snprintf(hint, sizeof(hint), "Type name is already exists (not this item)!");
        }
        else
        {
            items[selected].Name = typeName;
            items[selected].Type = (uint16_t)CAssistant::TextToUInt(typeGraphic);
            changed = true;
            hint[0] = '\0';
        }
    }
    if ((buttons & XALB_REMOVE) != 0 && selected >= 0 && selected < (int)items.size())
    {
        items.erase(items.begin() + selected);
        selected = lastSelected = -1;
        changed = true;
    }

    if (changed)
    {
        g_AssistantConfig.SaveTypes(); // the original saved on every mutation and drop
    }

    ImGui::TextUnformatted("Type:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    HexEditField("##type_graphic", typeGraphic, sizeof(typeGraphic));
    ImGui::TextUnformatted("Name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputText("##type_name", typeName, sizeof(typeName));
    if (hint[0] != '\0')
    {
        ImGui::TextColored(HINT_COLOR, "%s", hint);
    }
    ImGui::EndChild();
}

// --- Objects (tablistsobjects.cpp) ----------------------------------------------

void draw_objects_tab()
{
    static int selected = -1;
    static int lastSelected = -1;
    static char objectName[64] = { 0 };
    static char objectSerial[16] = { 0 };
    static char hint[128] = { 0 };
    static bool &ftPending = s_FtPending[1];
    static uint32_t ftGeneration = 0;
    XA_FROM_TARGET_RESULT ftResult;

    vector<CObjectEntry> &items = g_AssistantConfig.m_Objects;
    const int buttons = ListButtons("objects");
    if ((buttons & XALB_TARGET) != 0)
    {
        from_target_request(ftPending, ftGeneration, &CAssistant::HandleTargetObject,
                            "Select a object for obtain serial");
    }
    if (from_target_poll(ftPending, ftGeneration, XA_FROM_TARGET_OBJECT, ftResult))
    {
        // SetObjectFromTarget
        snprintf(
            objectSerial, sizeof(objectSerial), "%s", CAssistant::SerialToText(ftResult.Serial).c_str());
    }

    // the list pane left, the editor fields right
    ImGui::BeginChild("##objects_pane", ImVec2(170.0f, 0.0f), ImGuiChildFlags_None);
    bool changed = NameList("##objects", items, &selected, &ObjectNameOf, &ObjectSetNameOf, false, -1.0f);
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##objects_form", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    if (selected != lastSelected)
    {
        lastSelected = selected;
        hint[0] = '\0';
        if (selected >= 0 && selected < (int)items.size())
        {
            snprintf(objectName, sizeof(objectName), "%s", items[selected].Name.c_str());
            snprintf(objectSerial, sizeof(objectSerial), "%s", CAssistant::SerialToText(items[selected].Serial).c_str());
        }
    }

    // the static edit buffers are read directly (draw path: no scratch copies)
    if ((buttons & XALB_ADD) != 0)
    {
        if (objectName[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the object name!");
        }
        else if (objectSerial[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the object serial!");
        }
        else
        {
            CObjectEntry entry;
            entry.Name = objectName;
            entry.Serial = CAssistant::TextToSerial(objectSerial);
            items.push_back(entry);
            selected = lastSelected = (int)items.size() - 1;
            changed = true;
            hint[0] = '\0';
        }
    }
    if ((buttons & XALB_SAVE) != 0)
    {
        if (objectName[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the object name!");
        }
        else if (selected < 0 || selected >= (int)items.size())
        {
            snprintf(hint, sizeof(hint), "Object is not selected!");
        }
        else
        {
            items[selected].Name = objectName;
            items[selected].Serial = CAssistant::TextToSerial(objectSerial);
            changed = true;
            hint[0] = '\0';
        }
    }
    if ((buttons & XALB_REMOVE) != 0 && selected >= 0 && selected < (int)items.size())
    {
        items.erase(items.begin() + selected);
        selected = lastSelected = -1;
        changed = true;
    }

    if (changed)
    {
        g_AssistantConfig.SaveObjects();
    }

    // Name / Serial fields
    ImGui::TextUnformatted("Name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputText("##object_name", objectName, sizeof(objectName));
    ImGui::TextUnformatted("Serial:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    HexEditField("##object_serial", objectSerial, sizeof(objectSerial));
    if (hint[0] != '\0')
    {
        ImGui::TextColored(HINT_COLOR, "%s", hint);
    }

    ImGui::Separator();

    // dress bag row
    if (ImGui::Button("Set as dress bag"))
    {
        const uint32_t serial = selected >= 0 && selected < (int)items.size()
            ? items[selected].Serial
            : CAssistant::TextToSerial(objectSerial);
        if (serial == 0u)
        {
            snprintf(hint, sizeof(hint), "Select the object to set as dress bag!");
        }
        else
        {
            g_Assistant.SetDressBag(serial);
            hint[0] = '\0';
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Unset dress bag"))
    {
        g_Assistant.SetDressBag(0);
    }
    ImGui::SameLine();
    // rebuild the text only when the value changed (draw path: no per-frame
    // SerialToText allocation)
    static uint32_t lastDressBag = 0;
    static astr_t dressBagText = CAssistant::SerialToText(lastDressBag);
    if (g_DressBag != lastDressBag)
    {
        lastDressBag = g_DressBag;
        dressBagText = CAssistant::SerialToText(g_DressBag);
    }
    ImGui::TextColored(
        g_DressBag != 0u ? DRESS_BAG_SET : DRESS_BAG_UNSET,
        "%s",
        dressBagText.c_str());
    ImGui::EndChild();
}

// --- Find (tablistsfind.cpp, dual list) ------------------------------------------

void draw_find_tab()
{

    static int selectedList = -1;
    static int lastSelectedList = -1;
    static int selectedItem = -1;
    static int lastSelectedItem = -1;
    static char findName[64] = { 0 };
    static char itemGraphic[16] = { 0 };
    static char itemColor[16] = { 0 };
    static char itemComment[64] = { 0 };
    static char hint[128] = { 0 };

    vector<CFindListEntry> &lists = g_AssistantConfig.m_FindLists;

    // left: the named find lists + their fields; right: the selected list's
    // content (Graphic/Color/Comment rows)
    const int listButtons = ListButtons("find_list");

    ImGui::BeginChild("##find_lists_pane", ImVec2(170.0f, 0.0f), ImGuiChildFlags_None);
    bool changed = NameList("##find", lists, &selectedList, &FindNameOf, &FindSetNameOf, false, -1.0f);
    if (selectedList != lastSelectedList)
    {
        lastSelectedList = selectedList;
        selectedItem = lastSelectedItem = -1;
        hint[0] = '\0';
        if (selectedList >= 0 && selectedList < (int)lists.size())
        {
            snprintf(findName, sizeof(findName), "%s", lists[selectedList].Name.c_str());
        }
    }

    // the static edit buffer is read directly (draw path: no scratch copy)
    if ((listButtons & XALB_ADD) != 0)
    {
        if (findName[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the find list name!");
        }
        else
        {
            bool exists = false;
            for (int i = (int)lists.size() - 1; i >= 0; i--)
            {
                if (ListNamesEqual(lists[i].Name, findName))
                {
                    exists = true;
                    break;
                }
            }
            if (exists)
            {
                snprintf(hint, sizeof(hint), "Find list name is already exists!");
            }
            else
            {
                CFindListEntry entry;
                entry.Name = findName;
                lists.push_back(entry);
                selectedList = lastSelectedList = (int)lists.size() - 1;
                changed = true;
                hint[0] = '\0';
            }
        }
    }
    if ((listButtons & XALB_SAVE) != 0)
    {
        if (findName[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the find list name!");
        }
        else if (selectedList < 0 || selectedList >= (int)lists.size())
        {
            snprintf(hint, sizeof(hint), "List is not selected!");
        }
        else
        {
            lists[selectedList].Name = findName;
            changed = true;
            hint[0] = '\0';
        }
    }
    if ((listButtons & XALB_REMOVE) != 0 && selectedList >= 0 && selectedList < (int)lists.size())
    {
        lists.erase(lists.begin() + selectedList);
        selectedList = lastSelectedList = -1;
        selectedItem = lastSelectedItem = -1;
        changed = true;
    }

    if (changed)
    {
        g_AssistantConfig.SaveFindList();
    }

    ImGui::TextUnformatted("Find list name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("##find_name", findName, sizeof(findName));
    if (hint[0] != '\0')
    {
        ImGui::TextColored(HINT_COLOR, "%s", hint);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##find_content_pane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    ImGui::TextUnformatted("Selected find list content:");
    if (selectedList < 0 || selectedList >= (int)lists.size())
    {
        ImGui::TextDisabled("(select a find list)");
    }
    else
    {
        vector<CFindItem> &content = lists[selectedList].m_Items;
        const int itemButtons = ListButtons("find_content");
        static bool &ftPending = s_FtPending[2];
        static uint32_t ftGeneration = 0;
        if ((itemButtons & XALB_TARGET) != 0)
        {
            from_target_request(ftPending, ftGeneration, &CAssistant::HandleTargetFindList,
                                "Select a object for obtain graphic and color");
        }
        XA_FROM_TARGET_RESULT ftResult;
        if (from_target_poll(ftPending, ftGeneration, XA_FROM_TARGET_FIND_LIST, ftResult))
        {
            // AddFindListItemInList
            snprintf(itemGraphic, sizeof(itemGraphic), "%s", CAssistant::GraphicToText(ftResult.Graphic).c_str());
            snprintf(itemColor, sizeof(itemColor), "%s", CAssistant::GraphicToText(ftResult.Color).c_str());
            snprintf(itemComment, sizeof(itemComment), "%s", ftResult.Name.c_str());
        }
        // the content pane swaps to another container with the same row id
        // when the selected list changes: kill any pending rename on it
        // (same-count lists would otherwise let a stale rename resume)
        static int lastContentList = -2;
        if (selectedList != lastContentList)
        {
            lastContentList = selectedList;
            ListGenerationBump(ImGui::GetID("##find_content"));
        }
        bool contentChanged = FindContentList("##find_content", content, &selectedItem, -1.0f);
        if (selectedItem != lastSelectedItem)
        {
            lastSelectedItem = selectedItem;
            if (selectedItem >= 0 && selectedItem < (int)content.size())
            {
                snprintf(itemGraphic, sizeof(itemGraphic), "%s", content[selectedItem].Graphic.c_str());
                snprintf(itemColor, sizeof(itemColor), "%s", content[selectedItem].Color.c_str());
                snprintf(itemComment, sizeof(itemComment), "%s", content[selectedItem].Comment.c_str());
            }
        }

        // the static edit buffers are read directly (draw path: no scratch copies)
        if ((itemButtons & XALB_ADD) != 0)
        {
            if (itemGraphic[0] == '\0')
            {
                snprintf(hint, sizeof(hint), "Enter the content graphic!");
            }
            else
            {
                CFindItem item(itemGraphic, itemColor);
                item.Comment = itemComment;
                content.push_back(item);
                selectedItem = lastSelectedItem = (int)content.size() - 1;
                contentChanged = true;
                hint[0] = '\0';
            }
        }
        if ((itemButtons & XALB_SAVE) != 0 && selectedItem >= 0 && selectedItem < (int)content.size())
        {
            content[selectedItem].SetGraphic(itemGraphic);
            content[selectedItem].SetColor(itemColor);
            content[selectedItem].Comment = itemComment;
            contentChanged = true;
            hint[0] = '\0';
        }
        if ((itemButtons & XALB_REMOVE) != 0 && selectedItem >= 0 && selectedItem < (int)content.size())
        {
            content.erase(content.begin() + selectedItem);
            selectedItem = lastSelectedItem = -1;
            contentChanged = true;
        }
        if (contentChanged)
        {
            g_AssistantConfig.SaveFindList();
        }

        ImGui::TextUnformatted("Graphic:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        HexEditField("##find_graphic", itemGraphic, sizeof(itemGraphic));
        ImGui::TextUnformatted("Color:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        HexEditField("##find_color", itemColor, sizeof(itemColor));
        ImGui::TextUnformatted("Comment:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("##find_comment", itemComment, sizeof(itemComment));
    }
    ImGui::EndChild();
}

// --- Ignore (tablistsignore.cpp, dual list) ----------------------------------------

void draw_ignore_tab()
{

    static int selectedList = -1;
    static int lastSelectedList = -1;
    static int selectedItem = -1;
    static int lastSelectedItem = -1;
    static char ignoreName[64] = { 0 };
    static char itemSerial[16] = { 0 };
    static char itemGraphic[16] = { 0 };
    static char itemColor[16] = { 0 };
    static char itemComment[64] = { 0 };
    static char hint[128] = { 0 };

    vector<CIgnoreListEntry> &lists = g_AssistantConfig.m_IgnoreLists;

    const int listButtons = ListButtons("ignore_list");

    ImGui::BeginChild("##ignore_lists_pane", ImVec2(170.0f, 0.0f), ImGuiChildFlags_None);
    bool changed = NameList("##ignore", lists, &selectedList, &IgnoreNameOf, &IgnoreSetNameOf, false, -1.0f);
    if (selectedList != lastSelectedList)
    {
        lastSelectedList = selectedList;
        selectedItem = lastSelectedItem = -1;
        hint[0] = '\0';
        if (selectedList >= 0 && selectedList < (int)lists.size())
        {
            snprintf(ignoreName, sizeof(ignoreName), "%s", lists[selectedList].Name.c_str());
        }
    }

    // the static edit buffers are read directly (draw path: no scratch copies)
    if ((listButtons & XALB_ADD) != 0)
    {
        if (ignoreName[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the ignore list name!");
        }
        else
        {
            bool exists = false;
            for (int i = (int)lists.size() - 1; i >= 0; i--)
            {
                if (ListNamesEqual(lists[i].Name, ignoreName))
                {
                    exists = true;
                    break;
                }
            }
            if (exists)
            {
                snprintf(hint, sizeof(hint), "Ignore list name is already exists!");
            }
            else
            {
                CIgnoreListEntry entry;
                entry.Name = ignoreName;
                lists.push_back(entry);
                selectedList = lastSelectedList = (int)lists.size() - 1;
                changed = true;
                hint[0] = '\0';
            }
        }
    }
    if ((listButtons & XALB_SAVE) != 0)
    {
        if (ignoreName[0] == '\0')
        {
            snprintf(hint, sizeof(hint), "Enter the ignore list name!");
        }
        else if (selectedList < 0 || selectedList >= (int)lists.size())
        {
            snprintf(hint, sizeof(hint), "List is not selected!");
        }
        else
        {
            lists[selectedList].Name = ignoreName;
            changed = true;
            hint[0] = '\0';
        }
    }
    if ((listButtons & XALB_REMOVE) != 0 && selectedList >= 0 && selectedList < (int)lists.size())
    {
        lists.erase(lists.begin() + selectedList);
        selectedList = lastSelectedList = -1;
        selectedItem = lastSelectedItem = -1;
        changed = true;
    }

    if (changed)
    {
        g_AssistantConfig.SaveIgnoreList();
    }

    ImGui::TextUnformatted("Ignore list name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("##ignore_name", ignoreName, sizeof(ignoreName));
    if (hint[0] != '\0')
    {
        ImGui::TextColored(HINT_COLOR, "%s", hint);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##ignore_content_pane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    ImGui::TextUnformatted("Selected ignore list content:");
    if (selectedList < 0 || selectedList >= (int)lists.size())
    {
        ImGui::TextDisabled("(select an ignore list)");
    }
    else
    {
        vector<CIgnoreItem> &content = lists[selectedList].m_Items;
        const int itemButtons = ListButtons("ignore_content");
        static bool &ftPending = s_FtPending[3];
        static uint32_t ftGeneration = 0;
        if ((itemButtons & XALB_TARGET) != 0)
        {
            from_target_request(ftPending, ftGeneration, &CAssistant::HandleTargetIgnoreList,
                                "Select a object for obtain graphic and color");
        }
        XA_FROM_TARGET_RESULT ftResult;
        if (from_target_poll(ftPending, ftGeneration, XA_FROM_TARGET_IGNORE_LIST, ftResult))
        {
            snprintf(itemGraphic, sizeof(itemGraphic), "%s", CAssistant::GraphicToText(ftResult.Graphic).c_str());
            snprintf(itemColor, sizeof(itemColor), "%s", CAssistant::GraphicToText(ftResult.Color).c_str());
            snprintf(itemComment, sizeof(itemComment), "%s", ftResult.Name.c_str());
        }
        // kill any pending rename on the content pane (same as ##find_content)
        static int lastContentList = -2;
        if (selectedList != lastContentList)
        {
            lastContentList = selectedList;
            ListGenerationBump(ImGui::GetID("##ignore_content"));
        }
        bool contentChanged = IgnoreContentList("##ignore_content", content, &selectedItem, -1.0f);
        if (selectedItem != lastSelectedItem)
        {
            lastSelectedItem = selectedItem;
            if (selectedItem >= 0 && selectedItem < (int)content.size())
            {
                snprintf(itemSerial, sizeof(itemSerial), "%s", content[selectedItem].Serial.c_str());
                snprintf(itemGraphic, sizeof(itemGraphic), "%s", content[selectedItem].Graphic.c_str());
                snprintf(itemColor, sizeof(itemColor), "%s", content[selectedItem].Color.c_str());
                snprintf(itemComment, sizeof(itemComment), "%s", content[selectedItem].Comment.c_str());
            }
        }

        // the static edit buffers are read directly (draw path: no scratch copies)
        if ((itemButtons & XALB_ADD) != 0)
        {
            if (itemSerial[0] == '\0')
            {
                snprintf(hint, sizeof(hint), "Enter the content serial!");
            }
            else
            {
                CIgnoreItem item(itemSerial, itemGraphic, itemColor);
                item.Comment = itemComment;
                content.push_back(item);
                selectedItem = lastSelectedItem = (int)content.size() - 1;
                contentChanged = true;
                hint[0] = '\0';
            }
        }
        if ((itemButtons & XALB_SAVE) != 0 && selectedItem >= 0 && selectedItem < (int)content.size())
        {
            content[selectedItem].Serial = itemSerial;
            content[selectedItem].SetGraphic(itemGraphic);
            content[selectedItem].SetColor(itemColor);
            content[selectedItem].Comment = itemComment;
            contentChanged = true;
            hint[0] = '\0';
        }
        if ((itemButtons & XALB_REMOVE) != 0 && selectedItem >= 0 && selectedItem < (int)content.size())
        {
            content.erase(content.begin() + selectedItem);
            selectedItem = lastSelectedItem = -1;
            contentChanged = true;
        }
        if (contentChanged)
        {
            g_AssistantConfig.SaveIgnoreList();
        }

        ImGui::TextUnformatted("Serial:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        HexEditField("##ignore_serial", itemSerial, sizeof(itemSerial));
        ImGui::TextUnformatted("Graphic:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        HexEditField("##ignore_graphic", itemGraphic, sizeof(itemGraphic));
        ImGui::TextUnformatted("Color:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        HexEditField("##ignore_color", itemColor, sizeof(itemColor));
        ImGui::TextUnformatted("Comment:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("##ignore_comment", itemComment, sizeof(itemComment));
    }
    ImGui::EndChild();
}

// --- Friends / Enemies (tablistsfriends.cpp / tablistsenemies.cpp) ------------------

void draw_world_object_tab(
    const char *listId,
    vector<CWorldObjectEntry> &items,
    bool checkColumn,
    int *selected,
    int *lastSelected,
    char *name,
    size_t nameSize,
    char *serial,
    size_t serialSize,
    char *hint,
    bool &changed,
    int fromTargetKind,
    bool &ftPending,
    uint32_t &ftGeneration)
{
    const int buttons = ListButtons(listId);
    if ((buttons & XALB_TARGET) != 0)
    {
        from_target_request(
            ftPending,
            ftGeneration,
            fromTargetKind == XA_FROM_TARGET_FRIEND ? &CAssistant::HandleTargetFriendObject :
                                                      &CAssistant::HandleTargetEnemyObject,
            "Select a object for obtain serial");
    }
    XA_FROM_TARGET_RESULT ftResult;
    if (from_target_poll(
            ftPending,
            ftGeneration,
            fromTargetKind,
            ftResult))
    {
        // SetFriend/EnemyObjectFromTarget: serial + name when known
        snprintf(serial, serialSize, "%s", CAssistant::SerialToText(ftResult.Serial).c_str());
        if (!ftResult.Name.empty())
        {
            snprintf(name, nameSize, "%s", ftResult.Name.c_str());
        }
    }
    // the list pane left, the editor fields right
    char paneId[48];
    snprintf(paneId, sizeof(paneId), "%s_pane", listId);
    char formId[48];
    snprintf(formId, sizeof(formId), "%s_form", listId);
    ImGui::BeginChild(paneId, ImVec2(170.0f, 0.0f), ImGuiChildFlags_None);
    changed = NameList(listId, items, selected, &WorldObjectNameOf, &WorldObjectSetNameOf, checkColumn, -1.0f);
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild(formId, ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    if (*selected != *lastSelected)
    {
        *lastSelected = *selected;
        hint[0] = '\0';
        if (*selected >= 0 && *selected < (int)items.size())
        {
            snprintf(name, nameSize, "%s", items[*selected].Name.c_str());
            snprintf(serial, serialSize, "%s", CAssistant::SerialToText(items[*selected].Serial).c_str());
        }
    }

    // the edit buffers are read directly (draw path: no scratch copies)
    if ((buttons & XALB_ADD) != 0)
    {
        if (name[0] == '\0')
        {
            snprintf(hint, 128, "Enter the name!");
        }
        else if (serial[0] == '\0')
        {
            snprintf(hint, 128, "Enter the serial!");
        }
        else
        {
            CWorldObjectEntry entry;
            entry.Name = name;
            entry.Serial = CAssistant::TextToSerial(serial);
            entry.Enabled = true;
            items.push_back(entry);
            *selected = *lastSelected = (int)items.size() - 1;
            changed = true;
            hint[0] = '\0';
        }
    }
    if ((buttons & XALB_SAVE) != 0)
    {
        if (name[0] == '\0')
        {
            snprintf(hint, 128, "Enter the name!");
        }
        else if (*selected < 0 || *selected >= (int)items.size())
        {
            snprintf(hint, 128, "Object is not selected!");
        }
        else
        {
            items[*selected].Name = name;
            items[*selected].Serial = CAssistant::TextToSerial(serial);
            changed = true;
            hint[0] = '\0';
        }
    }
    if ((buttons & XALB_REMOVE) != 0 && *selected >= 0 && *selected < (int)items.size())
    {
        items.erase(items.begin() + *selected);
        *selected = *lastSelected = -1;
        changed = true;
    }

    ImGui::TextUnformatted("Name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("##wo_name", name, nameSize);
    ImGui::TextUnformatted("Serial:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    HexEditField("##wo_serial", serial, serialSize);
    if (hint[0] != '\0')
    {
        ImGui::TextColored(HINT_COLOR, "%s", hint);
    }
    ImGui::EndChild();
}

struct FriendGroupsState
{
    astr_t ProfileKey;
    std::vector<FriendGroupRecord> Groups;
    int SelectedGroup = -1;
    std::array<char, FriendGroupMaxNameLength + 1> GroupName{};
    std::array<char, FriendGroupMaxNameLength + 1> GuildName{};
    int ExportFormat = 0; // 0 = JSON, 1 = INI
    bool ApplyAfterImport = false;
    std::array<char, 32768> ExchangeText{};
    astr_t Status;
};

FriendGroupsState s_FriendGroups;

int find_friend_group(const std::string &name)
{
    const std::string key = TargetRules_ToLower(TargetRules_Trim(name));
    for (size_t i = 0; i < s_FriendGroups.Groups.size(); ++i)
    {
        if (TargetRules_ToLower(s_FriendGroups.Groups[i].Name) == key)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool save_friend_groups()
{
    g_AssistantConfig.EnsureDirectories();
    const std::string text = FriendRules_ExportIni(s_FriendGroups.Groups);
    return AtomicFileWrite(
        fs_path_join(g_AssistantConfig.GetProfilePath(), "FriendGroups.ini"),
        text.data(),
        text.size());
}

void sync_friend_group_fields()
{
    if (s_FriendGroups.SelectedGroup >= 0 &&
        s_FriendGroups.SelectedGroup < static_cast<int>(s_FriendGroups.Groups.size()))
    {
        std::snprintf(
            s_FriendGroups.GroupName.data(),
            s_FriendGroups.GroupName.size(),
            "%s",
            s_FriendGroups.Groups[s_FriendGroups.SelectedGroup].Name.c_str());
    }
    else
    {
        s_FriendGroups.GroupName[0] = '\0';
    }
}

void load_friend_groups_for_profile()
{
    const fs_path path = g_AssistantConfig.GetProfilePath();
    const astr_t key = fs_path_str(path);
    if (key == s_FriendGroups.ProfileKey)
    {
        return;
    }

    s_FriendGroups.ProfileKey = key;
    s_FriendGroups.Groups.clear();
    s_FriendGroups.SelectedGroup = -1;
    s_FriendGroups.GroupName[0] = '\0';
    s_FriendGroups.GuildName[0] = '\0';
    s_FriendGroups.Status.clear();

    const fs_path filePath = fs_path_join(path, "FriendGroups.ini");
    if (fs_path_is_file(filePath))
    {
        const std::unique_ptr<FILE, decltype(&fs_close)> file(fs_open(filePath, FS_READ), fs_close);
        if (file && std::fseek(file.get(), 0, SEEK_END) == 0)
        {
            const long bytes = std::ftell(file.get());
            if (bytes > 0 && bytes <= static_cast<long>(FriendGroupMaxImportBytes) &&
                std::fseek(file.get(), 0, SEEK_SET) == 0)
            {
                std::string content(static_cast<size_t>(bytes), '\0');
                if (std::fread(&content[0], 1, content.size(), file.get()) == content.size())
                {
                    const auto parsed = FriendRules_ParseImport(content, "Default");
                    if (parsed.Ok)
                    {
                        s_FriendGroups.Groups = parsed.Groups;
                    }
                }
            }
        }
    }
    if (!s_FriendGroups.Groups.empty())
    {
        s_FriendGroups.SelectedGroup = 0;
        sync_friend_group_fields();
    }
}

void apply_friend_group_to_active(const FriendGroupRecord &group)
{
    g_AssistantConfig.m_Friends.clear();
    for (const auto &player : group.Players)
    {
        CWorldObjectEntry entry;
        entry.Serial = player.Serial;
        entry.Name = player.Name;
        entry.Enabled = player.Enabled;
        g_AssistantConfig.m_Friends.push_back(std::move(entry));
    }
    g_AssistantConfig.m_FeelPartyMembersAsFriends = group.IncludeParty;
    g_AssistantConfig.SaveFriends();
}

void capture_active_friends_into_group(FriendGroupRecord &group)
{
    group.Players.clear();
    for (const auto &entry : g_AssistantConfig.m_Friends)
    {
        if (entry.Serial == 0)
        {
            continue;
        }
        FriendPlayerRecord rec;
        rec.Serial = entry.Serial;
        rec.Name = FriendRules_SanitizeName(entry.Name);
        if (rec.Name.empty())
        {
            rec.Name = FriendRules_FormatSerial(entry.Serial);
        }
        rec.Enabled = entry.Enabled;
        group.Players.push_back(std::move(rec));
    }
    group.IncludeParty = g_AssistantConfig.m_FeelPartyMembersAsFriends;
}

void import_friend_groups_from_text(const std::string &input)
{
    const std::string fallback =
        (s_FriendGroups.SelectedGroup >= 0 &&
            s_FriendGroups.SelectedGroup < static_cast<int>(s_FriendGroups.Groups.size())) ?
        s_FriendGroups.Groups[s_FriendGroups.SelectedGroup].Name :
        "Imported";
    const auto parsed = FriendRules_ParseImport(input, fallback);
    if (!parsed.Ok)
    {
        s_FriendGroups.Status = "Could not import friend list: " + parsed.Error;
        return;
    }

    auto updated = s_FriendGroups.Groups;
    size_t importedPlayers = 0;
    size_t importedGroups = 0;
    for (const auto &incoming : parsed.Groups)
    {
        int existingIdx = -1;
        const std::string key = TargetRules_ToLower(incoming.Name);
        for (size_t i = 0; i < updated.size(); ++i)
        {
            if (TargetRules_ToLower(updated[i].Name) == key)
            {
                existingIdx = static_cast<int>(i);
                break;
            }
        }
        if (existingIdx < 0)
        {
            if (updated.size() >= FriendGroupMaxGroups)
            {
                continue;
            }
            updated.push_back(incoming);
            importedPlayers += incoming.Players.size();
            ++importedGroups;
        }
        else
        {
            auto &target = updated[existingIdx];
            target.IncludeParty = incoming.IncludeParty;
            for (const auto &p : incoming.Players)
            {
                bool found = false;
                for (auto &ep : target.Players)
                {
                    if (ep.Serial == p.Serial)
                    {
                        ep.Name = p.Name;
                        ep.Enabled = p.Enabled;
                        found = true;
                        break;
                    }
                }
                if (!found && target.Players.size() < FriendGroupMaxEntriesPerGroup)
                {
                    target.Players.push_back(p);
                }
                ++importedPlayers;
            }
            for (const auto &g : incoming.Guilds)
            {
                bool found = false;
                const std::string gKey = TargetRules_ToLower(g.Name);
                for (auto &eg : target.Guilds)
                {
                    if (TargetRules_ToLower(eg.Name) == gKey)
                    {
                        eg.Enabled = g.Enabled;
                        found = true;
                        break;
                    }
                }
                if (!found && target.Guilds.size() < FriendGroupMaxEntriesPerGroup)
                {
                    target.Guilds.push_back(g);
                }
            }
            ++importedGroups;
        }
    }

    auto backup = s_FriendGroups.Groups;
    s_FriendGroups.Groups = std::move(updated);
    if (!save_friend_groups())
    {
        s_FriendGroups.Groups = std::move(backup);
        s_FriendGroups.Status = "Could not save imported friend groups.";
        return;
    }

    if (!parsed.Groups.empty())
    {
        const int idx = find_friend_group(parsed.Groups.front().Name);
        if (idx >= 0)
        {
            s_FriendGroups.SelectedGroup = idx;
            sync_friend_group_fields();
            if (s_FriendGroups.ApplyAfterImport)
            {
                apply_friend_group_to_active(s_FriendGroups.Groups[idx]);
            }
        }
    }

    char msg[160];
    if (parsed.SkippedEntries > 0)
    {
        std::snprintf(msg, sizeof(msg), "Imported %zu friends across %zu groups (%zu skipped).",
            importedPlayers, importedGroups, parsed.SkippedEntries);
    }
    else
    {
        std::snprintf(msg, sizeof(msg), "Imported %zu friends across %zu groups.",
            importedPlayers, importedGroups);
    }
    s_FriendGroups.Status = msg;
}

void draw_friend_groups_page()
{
    load_friend_groups_for_profile();

    const char *currentGroup =
        (s_FriendGroups.SelectedGroup >= 0 &&
            s_FriendGroups.SelectedGroup < static_cast<int>(s_FriendGroups.Groups.size())) ?
        s_FriendGroups.Groups[s_FriendGroups.SelectedGroup].Name.c_str() :
        "No group selected";

    if (ImGui::BeginCombo("Friend group", currentGroup))
    {
        for (size_t i = 0; i < s_FriendGroups.Groups.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            const bool isSel = (s_FriendGroups.SelectedGroup == static_cast<int>(i));
            if (ImGui::Selectable(s_FriendGroups.Groups[i].Name.c_str(), isSel))
            {
                s_FriendGroups.SelectedGroup = static_cast<int>(i);
                sync_friend_group_fields();
                s_FriendGroups.Status.clear();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    ImGui::InputText("Group name", s_FriendGroups.GroupName.data(), s_FriendGroups.GroupName.size());

    if (ImGui::Button("Create from active list"))
    {
        const std::string name = FriendRules_SanitizeName(s_FriendGroups.GroupName.data());
        if (name.empty())
        {
            s_FriendGroups.Status = "Enter a group name.";
        }
        else if (find_friend_group(name) >= 0)
        {
            s_FriendGroups.Status = "A friend group with that name already exists.";
        }
        else if (s_FriendGroups.Groups.size() >= FriendGroupMaxGroups)
        {
            s_FriendGroups.Status = "Remove a friend group before adding another.";
        }
        else
        {
            FriendGroupRecord rec;
            rec.Name = name;
            capture_active_friends_into_group(rec);
            s_FriendGroups.Groups.push_back(std::move(rec));
            if (save_friend_groups())
            {
                s_FriendGroups.SelectedGroup = static_cast<int>(s_FriendGroups.Groups.size()) - 1;
                sync_friend_group_fields();
                s_FriendGroups.Status = "Friend group created.";
            }
            else
            {
                s_FriendGroups.Groups.pop_back();
                s_FriendGroups.Status = "Could not save friend groups.";
            }
        }
    }

    const bool hasGroup = s_FriendGroups.SelectedGroup >= 0 &&
        s_FriendGroups.SelectedGroup < static_cast<int>(s_FriendGroups.Groups.size());

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasGroup);
    if (ImGui::Button("Load into active list"))
    {
        apply_friend_group_to_active(s_FriendGroups.Groups[s_FriendGroups.SelectedGroup]);
        s_FriendGroups.Status = "Loaded group into active friends list.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Update from active list"))
    {
        auto &group = s_FriendGroups.Groups[s_FriendGroups.SelectedGroup];
        auto backup = group;
        capture_active_friends_into_group(group);
        if (save_friend_groups())
        {
            s_FriendGroups.Status = "Saved active friends list into group.";
        }
        else
        {
            group = std::move(backup);
            s_FriendGroups.Status = "Could not save friend groups.";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete group"))
    {
        auto backup = s_FriendGroups.Groups;
        s_FriendGroups.Groups.erase(s_FriendGroups.Groups.begin() + s_FriendGroups.SelectedGroup);
        if (save_friend_groups())
        {
            s_FriendGroups.SelectedGroup = s_FriendGroups.Groups.empty() ? -1 : 0;
            sync_friend_group_fields();
            s_FriendGroups.Status = "Friend group removed.";
        }
        else
        {
            s_FriendGroups.Groups = std::move(backup);
            s_FriendGroups.Status = "Could not save friend groups.";
        }
    }
    ImGui::EndDisabled();

    if (hasGroup)
    {
        auto &group = s_FriendGroups.Groups[s_FriendGroups.SelectedGroup];
        ImGui::TextDisabled("%zu players, %zu guilds in this group.",
            group.Players.size(), group.Guilds.size());
        bool includeParty = group.IncludeParty;
        if (ImGui::Checkbox("Include party members in this group", &includeParty))
        {
            group.IncludeParty = includeParty;
            save_friend_groups();
        }
    }

    ImGui::SeparatorText("Import and export");
    ImGui::Combo("Export format", &s_FriendGroups.ExportFormat, "Razor Enhanced JSON\0Text (INI)\0");
    ImGui::Checkbox("Apply first imported group to active friends list", &s_FriendGroups.ApplyAfterImport);

    auto build_export = [&](bool allGroups) -> std::string {
        std::vector<FriendGroupRecord> payload;
        if (allGroups && !s_FriendGroups.Groups.empty())
        {
            payload = s_FriendGroups.Groups;
        }
        else if (hasGroup)
        {
            payload.push_back(s_FriendGroups.Groups[s_FriendGroups.SelectedGroup]);
        }
        else
        {
            FriendGroupRecord active;
            active.Name = "Friends";
            capture_active_friends_into_group(active);
            payload.push_back(std::move(active));
        }
        return s_FriendGroups.ExportFormat == 0 ?
            FriendRules_ExportJson(payload) :
            FriendRules_ExportIni(payload);
    };

    if (ImGui::Button("Prepare export text"))
    {
        const std::string exported = build_export(true);
        std::snprintf(s_FriendGroups.ExchangeText.data(), s_FriendGroups.ExchangeText.size(), "%s", exported.c_str());
        s_FriendGroups.Status = "Export text ready below.";
    }
#if !defined(XUO_EMSC)
    ImGui::SameLine();
    if (ImGui::Button("Export to file..."))
    {
        const bool isJson = (s_FriendGroups.ExportFormat == 0);
        const char *patterns[] = { isJson ? "*.json" : "*.ini" };
        const char *defaultName = isJson ? "friends.json" : "friends.ini";
        const char *picked = tinyfd_saveFileDialog(
            "Export friend groups", defaultName, 1, patterns,
            isJson ? "Friend group JSON" : "Friend group INI");
        if (picked != nullptr)
        {
            const std::string exported = build_export(true);
            if (AtomicFileWrite(fs_path_from(picked), exported.data(), exported.size()))
            {
                std::snprintf(s_FriendGroups.ExchangeText.data(), s_FriendGroups.ExchangeText.size(), "%s", exported.c_str());
                s_FriendGroups.Status = "Friend groups exported.";
            }
            else
            {
                s_FriendGroups.Status = "Could not write the export file.";
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Import from file..."))
    {
        static const char *PATTERNS[] = { "*.json", "*.ini", "*.txt", "*.FRIENDS_PLAYERS", "*.FRIENDS_LISTS" };
        const char *picked = tinyfd_openFileDialog(
            "Import friend list", "", IM_ARRAYSIZE(PATTERNS), PATTERNS, "Friend list file", 0);
        if (picked != nullptr)
        {
            const fs_path filePath = fs_path_from(picked);
            const std::unique_ptr<FILE, decltype(&fs_close)> file(fs_open(filePath, FS_READ), fs_close);
            if (!file || std::fseek(file.get(), 0, SEEK_END) != 0)
            {
                s_FriendGroups.Status = "Could not open the selected file.";
            }
            else
            {
                const long bytes = std::ftell(file.get());
                if (bytes < 0 || bytes > static_cast<long>(FriendGroupMaxImportBytes) ||
                    std::fseek(file.get(), 0, SEEK_SET) != 0)
                {
                    s_FriendGroups.Status = "Choose a readable file of up to 256 KiB.";
                }
                else
                {
                    std::string content(static_cast<size_t>(bytes), '\0');
                    if (content.empty() || std::fread(&content[0], 1, content.size(), file.get()) == content.size())
                    {
                        import_friend_groups_from_text(content);
                    }
                    else
                    {
                        s_FriendGroups.Status = "Could not read the selected file.";
                    }
                }
            }
        }
    }
#endif
    ImGui::SameLine();
    if (ImGui::Button("Import from text"))
    {
        import_friend_groups_from_text(s_FriendGroups.ExchangeText.data());
    }

    ImGui::TextDisabled("Accepts Razor Enhanced JSON, CrossUO INI, or plain serial/name lines.");
    ImGui::InputTextMultiline(
        "##friend_exchange_text",
        s_FriendGroups.ExchangeText.data(),
        s_FriendGroups.ExchangeText.size(),
        ImVec2(-1.0f, 120.0f));

    if (!s_FriendGroups.Status.empty())
    {
        ImGui::TextWrapped("%s", s_FriendGroups.Status.c_str());
    }
}

void draw_friends_tab()
{
    static int selected = -1;
    static int lastSelected = -1;
    static char name[64] = { 0 };
    static char serial[16] = { 0 };
    static char hint[128] = { 0 };
    static bool &ftPending = s_FtPending[4];
    static uint32_t ftGeneration = 0;

    if (ImGui::BeginTabBar("##friends_subtabs", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("Active friends"))
        {
            if (ImGui::Checkbox("Party members as friends", &g_AssistantConfig.m_FeelPartyMembersAsFriends))
            {
                g_AssistantConfig.SaveFriends(); // the attribute rides Friends.ini
            }
            bool changed = false;
            draw_world_object_tab(
                "##friends", g_AssistantConfig.m_Friends, true, &selected, &lastSelected,
                name, sizeof(name), serial, sizeof(serial), hint, changed,
                XA_FROM_TARGET_FRIEND, ftPending, ftGeneration);
            if (changed)
            {
                g_AssistantConfig.SaveFriends();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Groups & Import/Export"))
        {
            draw_friend_groups_page();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void draw_enemies_tab()
{
    static int selected = -1;
    static int lastSelected = -1;
    static char name[64] = { 0 };
    static char serial[16] = { 0 };
    static char hint[128] = { 0 };
    static bool &ftPending = s_FtPending[5];
    static uint32_t ftGeneration = 0;

    bool changed = false;
    draw_world_object_tab(
        "##enemies", g_AssistantConfig.m_Enemies, false, &selected, &lastSelected,
        name, sizeof(name), serial, sizeof(serial), hint, changed,
        XA_FROM_TARGET_ENEMY, ftPending, ftGeneration);
    if (changed)
    {
        g_AssistantConfig.SaveEnemies();
    }
}

} // namespace

void TabLists_CancelPendingFromTargets()
{
    for (bool &pending : s_FtPending)
    {
        pending = false;
    }
}

void TabLists_Draw(int subTab)
{
    switch (subTab)
    {
        case TAB_LISTS_TYPES:
            draw_types_tab();
            break;
        case TAB_LISTS_OBJECTS:
            draw_objects_tab();
            break;
        case TAB_LISTS_FIND:
            draw_find_tab();
            break;
        case TAB_LISTS_IGNORE:
            draw_ignore_tab();
            break;
        case TAB_LISTS_FRIENDS:
            draw_friends_tab();
            break;
        case TAB_LISTS_ENEMIES:
            draw_enemies_tab();
            break;
        default:
            break;
    }
}

} // namespace xuoassist

#endif // XUO_ASSISTANT
