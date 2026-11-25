// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

// The shop buy/sell engine: the hook bodies, ProcessShopList and the progress
// countdown as frame-tick state machine steps.

#define LOGGER_MODULE Assistant

#include "shopagent.h"

#if defined(XUO_ASSISTANT)

#include <common/logging/logging.h>
#include <common/str.h>
#include <stdlib.h>
#include <algorithm>
#include <climits>
#include <unordered_set>

#include "assistantevents.h"
#include "assist.h"
#include "inventoryagent.h"
#include "GameObjects/ObjectOnCursor.h"
#include "Network/Packets.h" // the raw vendor-reply builders
#include "../config/assistantconfig.h"
#include "../config/configio.h"
#include "../model/commonitems/finditem.h"
#include "world.h"
#include "Config.h"
#include "Misc.h"
#include "ServerList.h"
#include "Target.h"
#include "TextEngine/GameConsole.h" // ::SendConsoleText (the agent speech)

namespace xuoassist {

CShopAgent g_ShopAgent;

namespace {

constexpr uint32_t OnGround = 0xFFFFFFFF;
constexpr int MaxScopedSaleStacks = 99; // ModernUO rejects sale replies with 100 or more lines.
constexpr const char *SellBagSection = "SellHotBag";

astr_t current_shard()
{
    const auto *server = g_ServerList.GetSelectedServer();
    return server != nullptr ? server->Name : g_Config.ShardName;
}

bool contained_in(uint32_t serial, uint32_t container)
{
    for (int depth = 0; g_World != nullptr && serial != 0 && serial != OnGround && depth < 16; ++depth)
    {
        if (serial == container)
        {
            return true;
        }
        const auto *item = g_World->FindWorldItem(serial);
        serial = item != nullptr ? item->Container : 0;
    }
    return false;
}

CGameItem *owned_bag(uint32_t serial)
{
    auto *backpack = g_Player != nullptr ? g_Player->FindLayer(OL_BACKPACK) : nullptr;
    auto *bag = g_World != nullptr ? g_World->FindWorldItem(serial) : nullptr;
    if (backpack == nullptr || bag == nullptr || bag == backpack || bag->Dragged ||
        bag->GetStaticData() == nullptr || !bag->IsContainer() || bag->IsCorpse() ||
        !contained_in(bag->Container, backpack->Serial))
    {
        return nullptr;
    }
    return bag;
}

CGameItem *scoped_sale_item(uint32_t serial, uint32_t bag)
{
    auto *item = g_World != nullptr ? g_World->FindWorldItem(serial) : nullptr;
    return item != nullptr && !item->Dragged && item->GetStaticData() != nullptr &&
            !item->IsContainer() && !item->IsCorpse() && contained_in(item->Container, bag)
        ? item : nullptr;
}

} // namespace

void CShopAgent::LoadSellHotBag()
{
    const auto path = g_AssistantConfig.GetProfilePath();
    const auto key = fs_path_str(path);
    if (key == m_SellBagProfile)
    {
        return;
    }
    CancelSellHotBagTarget();
    if (!m_SellBagProfile.empty())
    {
        m_AutoSell = false;
        if (m_State == SLWS_SELL)
        {
            m_State = SLWS_NORMAL;
        }
        if (m_ProgressActive && !m_ProgressIsBuy)
        {
            CancelProgress();
        }
    }
    m_SellBagProfile = key;
    m_SellBag = {};
    m_SellBagStatus.clear();
    CIniFile ini;
    const auto settingsPath = fs_path_join(path, "SellHotBag.ini");
    if (ini.Load(settingsPath))
    {
        m_SellBag.Enabled = ini.GetBool(SellBagSection, "enabled");
        m_SellBag.Serial = CAssistant::TextToSerial(ini.Get(SellBagSection, "bag"));
        m_SellBag.Character = CAssistant::TextToSerial(ini.Get(SellBagSection, "character"));
        m_SellBag.Shard = ini.Get(SellBagSection, "shard");
    }
    else if (fs_path_exists(settingsPath))
    {
        m_SellBag.Enabled = true;
        m_SellBagStatus = "Sell bag settings could not be read. Choose a bag again.";
    }
}

bool CShopAgent::SaveSellHotBag(const SellBag &settings)
{
    CIniFile ini;
    ini.SetBool(SellBagSection, "enabled", settings.Enabled);
    ini.Set(SellBagSection, "bag", CAssistant::SerialToText(settings.Serial));
    ini.Set(SellBagSection, "character", CAssistant::SerialToText(settings.Character));
    ini.Set(SellBagSection, "shard", settings.Shard);
    g_AssistantConfig.EnsureDirectories();
    if (!ini.Save(fs_path_join(g_AssistantConfig.GetProfilePath(), "SellHotBag.ini")))
    {
        m_SellBagStatus = "Could not save the sell bag setting.";
        return false;
    }
    m_SellBag = settings;
    if (m_State == SLWS_SELL)
    {
        m_State = SLWS_NORMAL;
    }
    if (m_ProgressActive && !m_ProgressIsBuy)
    {
        CancelProgress();
    }
    m_SellBagStatus.clear();
    return true;
}

uint32_t CShopAgent::ActiveSellHotBag() const
{
    if (m_SellBag.Character != g_PlayerSerial || m_SellBag.Shard != current_shard())
    {
        return 0;
    }
    const auto *bag = owned_bag(m_SellBag.Serial);
    return bag != nullptr && bag->Opened ? bag->Serial : 0;
}

bool CShopAgent::SellHotBagEnabled()
{
    LoadSellHotBag();
    return m_SellBag.Enabled;
}

void CShopAgent::SetSellHotBagEnabled(bool enabled)
{
    LoadSellHotBag();
    auto settings = m_SellBag;
    settings.Enabled = enabled;
    SaveSellHotBag(settings);
}

void CShopAgent::RequestSellHotBag()
{
    LoadSellHotBag();
    if (g_World == nullptr || g_Player == nullptr || g_Player->IsDead() ||
        g_TargetHandler != nullptr || g_Target.IsTargeting())
    {
        m_SellBagStatus = "Finish the current target before choosing a bag.";
        return;
    }
    m_SellBagStatus.clear();
    m_SellBagTargetCharacter = g_PlayerSerial;
    m_SellBagTargetShard = current_shard();
    g_TargetHandler = &CAssistant::HandleTargetSellHotBag;
    g_Assistant.RequestTarget("Select a container inside your backpack.");
}

void CShopAgent::SelectSellHotBag(uint32_t serial)
{
    const uint32_t character = m_SellBagTargetCharacter;
    m_SellBagTargetCharacter = 0;
    if (character == 0 || character != g_PlayerSerial || m_SellBagTargetShard != current_shard() ||
        m_SellBagProfile != fs_path_str(g_AssistantConfig.GetProfilePath()))
    {
        return;
    }
    if (owned_bag(serial) == nullptr)
    {
        m_SellBagStatus = "Choose a container inside your backpack.";
        return;
    }
    auto settings = m_SellBag;
    settings.Serial = serial;
    settings.Character = g_PlayerSerial;
    settings.Shard = current_shard();
    SaveSellHotBag(settings);
}

void CShopAgent::ClearSellHotBag()
{
    LoadSellHotBag();
    CancelSellHotBagTarget();
    auto settings = m_SellBag;
    settings.Serial = 0;
    settings.Character = 0;
    settings.Shard.clear();
    SaveSellHotBag(settings);
}

astr_t CShopAgent::SellHotBagLabel()
{
    LoadSellHotBag();
    if (m_SellBag.Serial == 0 || m_SellBag.Character != g_PlayerSerial || m_SellBag.Shard != current_shard())
    {
        return "No sell bag selected for this character.";
    }
    auto *bag = owned_bag(m_SellBag.Serial);
    if (bag == nullptr || !bag->Opened)
    {
        return "Open the selected sell bag in your backpack.";
    }
    const auto name = bag->GetName();
    return name.empty() ? CAssistant::SerialToText(bag->Serial) : name;
}

void CShopAgent::CancelSellHotBagTarget()
{
    m_SellBagTargetCharacter = 0;
    m_SellBagTargetShard.clear();
    if (g_TargetHandler == &CAssistant::HandleTargetSellHotBag)
    {
        g_TargetHandler = nullptr;
        if (g_Target.FromAssist)
        {
            g_Target.CancelCursor();
            g_Target.FromAssist = false;
        }
    }
}

void CAssistant::HandleTargetSellHotBag(uint8_t *buf) const
{
    g_ShopAgent.SelectSellHotBag(unpack32(buf + 7));
}

bool CShopAgent::ScopedSaleMovePending() const
{
    return g_ObjectInHand.Enabled || InventoryAgent_MoveInFlight() ||
        (m_HasItemMove && g_Ticks - m_LastItemMove < 1200);
}

void CShopAgent::OnItemMoveSent()
{
    m_HasItemMove = true;
    m_LastItemMove = g_Ticks;
    if (m_ProgressActive && m_ProgressSellBag != 0)
    {
        CancelProgress();
        m_SellBagStatus = "Sale canceled because an item was moved.";
        g_Assistant.ClientPrint(m_SellBagStatus);
    }
}

void CShopAgent::Tick()
{
    if (!m_ProgressActive)
    {
        return;
    }

    if ((int)(g_Ticks - m_ProgressStart) < m_ProgressDelay)
    {
        return;
    }

    if (!m_ProgressIsBuy)
    {
        LoadSellHotBag();
        if (!m_ProgressActive)
        {
            return;
        }
    }
    if (m_ProgressSellBag != 0)
    {
        const bool valid = m_SellBag.Enabled && m_SellBagProfile == m_ProgressSellBagProfile &&
            ActiveSellHotBag() == m_ProgressSellBag && g_Player != nullptr && !g_Player->IsDead() &&
            !ScopedSaleMovePending() &&
            EnabledFeatureShopAgent() && (g_ServerAssistantFeatures & ASF_SELL_AGENT) == 0 &&
            std::all_of(m_ProgressList.begin(), m_ProgressList.end(), [this](const auto &entry) {
                const auto *item = scoped_sale_item(entry.first, m_ProgressSellBag);
                return item != nullptr && item->Count >= static_cast<uint32_t>(entry.second);
            });
        if (!valid)
        {
            m_SellBagStatus = "Sale canceled because the bag or item contents changed.";
            g_Assistant.ClientPrint(m_SellBagStatus);
            CancelProgress();
            return;
        }
    }

    g_Assistant.ClientPrintDebugLevel1("Shopping completed!");
    m_ProgressActive = false;

    if (m_ProgressIsBuy)
    {
        ::CPacketBuyRequest(m_ProgressVendorSerial, m_ProgressList).Send();
    }
    else
    {
        ::CPacketSellRequest(m_ProgressVendorSerial, m_ProgressList).Send();
    }
}

bool CShopAgent::IsActiveShopping() const
{
    // waiting for a vendor reply or the
    // queued request countdown still running
    return (
        (m_State != SLWS_NORMAL && m_WaitTimeout >= g_Ticks) || m_ProgressActive);
}

void CShopAgent::Buy(const astr_t &listName)
{
    // "<vendor name> buy" speech
    astr_t text = m_VendorName;

    if (text.length() != 0)
    {
        text += " ";
    }

    text += "buy";

    SendShopRequest(SLWS_BUY, listName, text);
}

void CShopAgent::Sell(const astr_t &listName)
{
    // "<vendor name> sell" speech
    astr_t text = m_VendorName;

    if (text.length() != 0)
    {
        text += " ";
    }

    text += "sell";

    SendShopRequest(SLWS_SELL, listName, text);
}

void CShopAgent::SendShopRequest(int state, const astr_t &listName, const astr_t &text)
{
    // (the buttons always passed shopDelay
    // 0: the per-stack delay comes from the config when the request is built)
    if (g_World == nullptr || !EnabledFeatureShopAgent())
    {
        return;
    }

    if (state == SLWS_SELL)
    {
        LoadSellHotBag();
    }
    if (m_State != SLWS_NORMAL && m_WaitTimeout >= g_Ticks)
    {
        g_Assistant.ClientPrintDebugLevel1("Preveous waiting for shopping cancelled.");
    }

    m_WaitShopDelay = 0;
    m_WaitShopListName = listName;
    m_State = state;
    m_WaitTimeout = g_Ticks + (uint32_t)(g_AssistantConfig.m_ShopTimeout * 1000);
    SendConsoleText(wstr_from(text), 0);
}

void CShopAgent::CancelProgress()
{
    g_Assistant.ClientPrintDebugLevel1("Shopping cancelled by user");
    m_ProgressActive = false;
    m_ProgressList.clear();
    m_ProgressSellBag = 0;
    m_ProgressSellBagProfile.clear();
}

int CShopAgent::ProgressValue() const
{
    if (!m_ProgressActive)
    {
        return 0;
    }

    // ms remaining
    const int elapsed = (int)(g_Ticks - m_ProgressStart);
    return (elapsed >= m_ProgressDelay) ? 0 : (m_ProgressDelay - elapsed);
}

void CShopAgent::Reset()
{
    CancelSellHotBagTarget();
    m_HasItemMove = false;
    m_ProgressSellBag = 0;
    m_ProgressSellBagProfile.clear();
    m_State = SLWS_NORMAL;
    m_WaitTimeout = 0;
    m_ProgressActive = false;
    m_ProgressList.clear();
    m_VendorItems.clear();
    m_VendorSerial = 0;
}

bool CShopAgent::ProcessShopList(const astr_t &listName)
{
    vector<CVendorShopItem> &vendorItems = m_VendorItems;

    if (vendorItems.empty())
    {
        return true;
    }

    LoadSellHotBag();
    const bool isBuyList = m_IsBuyList;
    const bool scoped = !isBuyList && m_SellBag.Enabled;
    const uint32_t sellBag = scoped ? ActiveSellHotBag() : 0;
    if (scoped && ScopedSaleMovePending())
    {
        m_State = SLWS_NORMAL;
        m_SellBagStatus = "Wait for the item move to finish before selling.";
        g_Assistant.ClientPrint(m_SellBagStatus);
        return true;
    }
    if (scoped && sellBag == 0)
    {
        m_State = SLWS_NORMAL;
        m_SellBagStatus = "Open or replace the selected sell bag before using the sell agent.";
        g_Assistant.ClientPrint(m_SellBagStatus);
        return true;
    }

    auto &shopLists = g_AssistantConfig.m_ShopLists;
    const auto soughtName = str_lower(listName);
    auto shopList = std::find_if(shopLists.begin(), shopLists.end(), [&soughtName](const auto &list) {
        return str_lower(list.Name) == soughtName;
    });
    if (shopList == shopLists.end() || shopList->m_Items.empty())
    {
        m_State = SLWS_NORMAL;
        return true;
    }

    const auto &shopItems = shopList->m_Items;
    int maxStacks = g_AssistantConfig.m_MaxShopItems;
    if (scoped)
    {
        maxStacks = maxStacks > 0 ? std::min(maxStacks, MaxScopedSaleStacks) : MaxScopedSaleStacks;
    }
    const int maxSellPrice = isBuyList ? 0 : g_AssistantConfig.m_MaxSellPrice;
    int64_t totalPrice = 0;
    vector<std::pair<uint32_t, int>> result;
    std::unordered_set<uint32_t> scopedItems;

    for (size_t si = 0; si < shopItems.size(); si++)
    {
        const CShopItemEntry &shopItem = shopItems[si];
        CFindItem findItem(shopItem.Graphic, shopItem.Color);
        int checkPrice = shopItem.Price;
        astr_t checkName = str_lower(shopItem.Name);
        bool exit = false;

        for (size_t vi = 0; vi < vendorItems.size(); vi++)
        {
            CVendorShopItem &vendorItem = vendorItems[vi];
            int price = (int)vendorItem.Price;
            const auto *saleItem = scoped ? scoped_sale_item(vendorItem.Serial, sellBag) : nullptr;
            if (scoped && (saleItem == nullptr || price <= 0 || scopedItems.count(vendorItem.Serial) != 0))
            {
                continue;
            }

            if (checkPrice != 0)
            {
                if (isBuyList)
                {
                    // buy: the list price is the maximum acceptable price
                    if (checkPrice < price)
                    {
                        continue;
                    }
                }
                else if (checkPrice > price)
                {
                    // sell: the list price is the minimum acceptable price
                    continue;
                }
            }

            if (findItem.Found(vendorItem.Graphic, vendorItem.Color) &&
                ((checkName.length() == 0) ||
                 checkName == str_lower(vendorItem.Name)))
            {
                int count = shopItem.Count;

                if ((count == 0) || count > (int)vendorItem.Count)
                {
                    count = (int)vendorItem.Count;
                }

                if (count <= 0)
                {
                    continue;
                }
                if (scoped)
                {
                    count = static_cast<int>(std::min(static_cast<uint32_t>(count), saleItem->Count));
                    count = std::min(count, int(INT16_MAX));
                }
                if (maxSellPrice > 0 && price > 0)
                {
                    const int64_t affordable = (maxSellPrice - totalPrice) / price;
                    count = static_cast<int>(std::min<int64_t>(count, affordable));
                }

                if (count > 0)
                {
                    totalPrice += int64_t(count) * price;
                    result.push_back(std::pair<uint32_t, int>(vendorItem.Serial, count));
                    if (scoped)
                    {
                        scopedItems.insert(vendorItem.Serial);
                    }
                    exit = maxSellPrice > 0 && totalPrice >= maxSellPrice;

                    if ((maxStacks != 0) && (int)result.size() >= maxStacks)
                    {
                        exit = true;
                    }

                    if (exit)
                    {
                        break;
                    }
                }
            }
        }

        if (exit)
        {
            break;
        }
    }

    m_State = SLWS_NORMAL;

    if (result.empty())
    {
        if (scoped)
        {
            m_SellBagStatus = "No matching vendor items in the sell bag.";
            g_Assistant.ClientPrint(m_SellBagStatus);
        }
        else if (isBuyList)
        {
            g_Assistant.ClientPrintDebugLevel1("No items to bought. Waiting cancelled.");
        }
        else
        {
            g_Assistant.ClientPrintDebugLevel1("No items to sold. Waiting cancelled.");
        }

        return true;
    }

    // the packet is built from the stored pair when the countdown expires
    m_ProgressIsBuy = isBuyList;
    m_ProgressVendorSerial = m_VendorSerial;
    m_ProgressList = result;
    m_ProgressSellBag = sellBag;
    m_ProgressSellBagProfile = scoped ? m_SellBagProfile : astr_t{};
    if (scoped)
    {
        m_SellBagStatus.clear();
    }

    int delay = m_WaitShopDelay;

    if (delay == 0)
    {
        delay = g_AssistantConfig.m_ShopDelay * (int)result.size();
    }

    m_ProgressDelay = delay;
    m_ProgressStart = g_Ticks;
    m_ProgressActive = true;
    m_ProgressShow = g_AssistantConfig.m_ShowShoppingProgressBar;

    return false;
}

bool CShopAgent::OnOpenContainer(uint32_t serial)
{
    if (g_World == nullptr || !EnabledFeatureShopAgent())
    {
        return true;
    }

    m_ShopListReplayed = false;
    m_IsBuyList = true;
    m_VendorItems.clear();

    bool result = true;

    if (serial == m_VendorSerial)
    {
        CGameCharacter *vendor = g_World->FindWorldCharacter(serial);

        if (vendor != nullptr)
        {
            for (int i = 0; i < 2; i++)
            {
                CGameItem *container = vendor->FindLayer(i + OL_BUY_RESTOCK);

                if (container == nullptr)
                {
                    continue;
                }

                for (CGameItem *obj = (CGameItem *)container->m_Items; obj != nullptr; obj = (CGameItem *)obj->m_Next)
                {
                    CVendorShopItem item;
                    item.Serial = obj->Serial;
                    item.Graphic = obj->Graphic;
                    item.Color = obj->Color;
                    item.Count = obj->Count;
                    item.Price = obj->Price;
                    item.Name = obj->GetName();
                    item.NameFromCliloc = obj->NameFromCliloc;

                    m_VendorItems.push_back(item);
                }
            }

            const bool waiting = m_State == SLWS_BUY && m_WaitTimeout >= g_Ticks;
            if (m_AutoBuy || waiting)
            {
                result = ProcessShopList(waiting ? m_WaitShopListName : m_AutoShopListName);
            }
        }
        else
        {
            Error(Assistant, "shop agent: buy vendor is not found");
        }
    }
    else
    {
        // also covers a stale serial from a previous vendor (serial reuse):
        // the snapshot stays empty, no shopping happens
        Error(Assistant,
            "shop agent: buy vendor serial (0x%08X) is incorrent! Wanted serial: 0x%08X",
            serial,
            m_VendorSerial);
    }

    return result;
}

bool CShopAgent::OnSellList(uint32_t serial, CGameCharacter *vendor, const vector<CVendorShopItem> &items)
{
    // the 0x9E
    // fields arrive mapped from the client parse (vendor serial, lookup
    // result, the sell entries with the client's cliloc names)
    if (g_World == nullptr)
    {
        return true;
    }

    if (!EnabledFeatureShopAgent() || (g_ServerAssistantFeatures & ASF_SELL_AGENT) != 0)
    {
        return true;
    }

    LoadSellHotBag();
    m_ShopListReplayed = false;
    m_IsBuyList = false;
    m_VendorItems.clear();

    m_VendorSerial = serial;

    if (vendor == nullptr)
    {
        Error(Assistant, "shop agent: sell vendor is not found");
        return true;
    }

    m_VendorItems = items;
    if (items.empty())
    {
        Warning(Assistant, "shop agent: sell list is empty");
    }

    const bool waiting = m_State == SLWS_SELL && m_WaitTimeout >= g_Ticks;
    if (m_AutoSell || waiting)
    {
        return ProcessShopList(waiting ? m_WaitShopListName : m_AutoShopListName);
    }

    return true;
}

void CShopAgent::OnBuyReplySent()
{
    if (EnabledFeatureShopAgent())
    {
        m_ShopListReplayed = true;
    }
}

void CShopAgent::OnSellReplySent()
{
    if (EnabledFeatureShopAgent())
    {
        m_ShopListReplayed = true;
    }
}

} // namespace xuoassist

#endif // XUO_ASSISTANT
