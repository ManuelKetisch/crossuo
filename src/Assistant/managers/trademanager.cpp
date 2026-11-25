// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

// Ported from X:UO Assist (managers/trademanager.cpp).

#include "trademanager.h"
#include "../config/assistantconfig.h"
#include "../core/world.h"
#include "../core/assist.h"
#include "../core/gameplayfilters.h"
#include "../core/potionhands_rules.h"
#include "Config.h"
#include "Globals.h"
#include "Managers/GumpManager.h"
#include <algorithm>
#include <xuocore/mappedfile.h>
#include <common/logging/logging.h>

namespace xuoassist {

namespace {

constexpr uint32_t IntentTimeout = 10000;
constexpr uint32_t ContextTimeout = 30000;
constexpr size_t MaxIntents = 16;
constexpr uint32_t TradeEntryCliloc = 1077728;

} // namespace

CTradeManager g_TradeManager;
CTradeManager::CTradeManager() = default;
CTradeManager::~CTradeManager()
{
    m_List.clear();
}

void CTradeManager::OnTradeSent(int action, uint32_t id, bool check)
{
    if (g_World == nullptr)
    {
        return;
    }

    switch ((TRADE_ACTIONS)action)
    {
        case TA_CLOSE:
        {
            for (size_t i = 0; i < m_List.size(); i++)
            {
                CTradeWindow &trade = m_List[i];

                if (trade.ID1 == id)
                {
                    // the client handler removes the trade box objects; the
                    // assistant drops only its own trade-window record
                    m_List.erase(m_List.begin() + i);

                    return;
                }
            }

            Warning(Assistant, "Warning!!! Unknown trade closed\n");

            break;
        }
        case TA_CHECK:
        {
            for (CTradeWindow &trade : m_List)
            {
                if (trade.ID1 == id)
                {
                    // the site passes the state the client actually wrote (the wire
                    // field's high byte reads 0 for either check state)
                    trade.Check1 = check;
                    return;
                }
            }

            Warning(Assistant, "Warning!!! Unknown trade changed\n");

            break;
        }
        default:
        {
            Warning(Assistant, "Warning!!! Unknown trade action\n");
            break;
        }
    }
}

void CTradeManager::OnTradeAction(
    int action, uint32_t serial, uint32_t id1, uint32_t id2, const astr_t &name)
{
    switch ((TRADE_ACTIONS)action)
    {
        case TA_OPEN:
        {
            m_List.emplace_back(serial, id1, id2, name);
            auto &trade = m_List.back();
            const bool fromUs = ConsumeIntent(serial);
            const bool isFriend = g_AssistantConfig.InFriendList(serial);
            const auto decision = tradefilter::DecideTradeWatch(
                GameplayFilters_BlockTradeOffers(),
                GameplayFilters_AllowFriendTrades(),
                fromUs,
                isFriend);
            trade.WatchFirstOffer = (decision == tradefilter::TradeWatchDecision::WatchFirstOffer);
            trade.OpenedAt = g_Ticks;

            break;
        }
        case TA_CLOSE:
        {
            for (size_t i = 0; i < m_List.size(); i++)
            {
                CTradeWindow &trade = m_List[i];

                if (trade.ID1 == serial)
                {
                    // the client handler removes the trade box objects; the
                    // assistant drops only its own trade-window record
                    m_List.erase(m_List.begin() + i);

                    return;
                }
            }

            Warning(Assistant, "Warning!!! Unknown trade closed\n");

            break;
        }
        case TA_CHECK:
        {
            for (CTradeWindow &trade : m_List)
            {
                if (trade.ID1 == serial)
                {
                    trade.Check1 = (bool)id1;
                    trade.Check2 = (bool)id2;
                    return;
                }
            }

            Warning(Assistant, "Warning!!! Unknown trade changed\n");

            break;
        }
        default:
        {
            // SECURE_TRADE_UPDATE_THEIRS / SECURE_TRADE_UPDATE_MINE update the client
            // trade gump directly and do not create or remove assistant trade records.
            break;
        }
    }
}

void CTradeManager::RecordIntent(uint32_t serial)
{
    if (serial == 0 || serial == g_PlayerSerial || serial >= 0x40000000)
    {
        return;
    }

    ConsumeIntent(serial);
    if (m_TradeIntents.size() >= MaxIntents)
    {
        m_TradeIntents.erase(m_TradeIntents.begin());
    }
    m_TradeIntents.push_back({ serial, g_Ticks });
}

bool CTradeManager::ConsumeIntent(uint32_t serial)
{
    bool found = false;
    m_TradeIntents.erase(
        std::remove_if(m_TradeIntents.begin(), m_TradeIntents.end(), [&](const TradeIntent &intent) {
            if (g_Ticks - intent.SentAt >= IntentTimeout)
            {
                return true;
            }
            if (intent.Opponent == serial)
            {
                found = true;
                return true;
            }
            return false;
        }),
        m_TradeIntents.end());
    return found;
}

void CTradeManager::ObserveContextMenu(
    uint32_t serial, const CDataReader &items, bool newClilocs)
{
    m_ContextOpponent = 0;
    if (!newClilocs || !GameplayFilters_BlockTradeOffers())
    {
        return;
    }

    auto reader = items;
    const uint8_t count = reader.ReadUInt8();
    constexpr size_t EntryLength = 8;
    for (int i = 0; i < count && reader.Size - reader.Pos() >= EntryLength; ++i)
    {
        const uint32_t cliloc = reader.ReadUInt32BE();
        const uint16_t index = reader.ReadUInt16BE();
        const uint16_t flags = reader.ReadUInt16BE();
        if (cliloc == TradeEntryCliloc && (flags & 0x01) == 0)
        {
            m_ContextOpponent = serial;
            m_ContextTradeIndex = index;
            m_ContextAt = g_Ticks;
            return;
        }
    }
}

void CTradeManager::ObserveGameSend(uint8_t *data, int size)
{
    if (g_World == nullptr || size <= 0 || !GameplayFilters_BlockTradeOffers())
    {
        return;
    }
    if (data[0] == 0x08 && (size == 14 || size == 15))
    {
        CDataReader reader(data, size);
        RecordIntent(reader.ReadUInt32BE(size - 4));
    }
    else if (data[0] == 0xBF && size == 11 && data[3] == 0 && data[4] == 0x15)
    {
        CDataReader reader(data, size);
        reader.Move(5);
        const uint32_t opponent = reader.ReadUInt32BE();
        const uint16_t index = reader.ReadUInt16BE();
        if (opponent == m_ContextOpponent && index == m_ContextTradeIndex &&
            g_Ticks - m_ContextAt < ContextTimeout)
        {
            RecordIntent(opponent);
        }
        m_ContextOpponent = 0;
    }
}

void CTradeManager::ObserveFirstOffer(uint32_t container, uint16_t graphic)
{
    if (!GameplayFilters_BlockTradeOffers())
    {
        return;
    }

    const auto trade = std::find_if(m_List.begin(), m_List.end(), [container](const CTradeWindow &window) {
        return window.WatchFirstOffer && (window.ID1 == container || window.ID2 == container);
    });
    if (trade == m_List.end())
    {
        return;
    }

    const auto decision = tradefilter::DecideFirstOffer(
        true,
        trade->WatchFirstOffer,
        trade->ID2 == container,
        trade->ID1 == container,
        graphic,
        g_Config.ProtocolClientVersion < CV_704565,
        g_Ticks,
        trade->OpenedAt);

    if (decision == tradefilter::FirstOfferDecision::KeepWatching)
    {
        // Legacy secure trade can send a placeholder ledger (0x14F0) with the same
        // graphic as a bank check; it cannot establish who initiated the trade.
        return;
    }

    trade->WatchFirstOffer = false;
    if (decision == tradefilter::FirstOfferDecision::Decline)
    {
        // Copy fields before closing: SecureTradeClose dispatches OnTradeSent(TA_CLOSE),
        // which erases this entry from m_List.
        const uint32_t ownBox = trade->ID1;
        astr_t opponentName = trade->Name;
        if (opponentName.empty() && g_World != nullptr)
        {
            CGameObject *opponent = g_World->FindWorldObject(trade->Serial);
            if (opponent != nullptr)
            {
                opponentName = opponent->GetName();
            }
        }
        g_GumpManager.SecureTradeClose(ownBox);
        GameplayFilters_ReportTradeDeclined(opponentName);
        if (!opponentName.empty())
        {
            g_Assistant.ClientPrint("Declined incoming trade offer from " + opponentName + ".");
        }
        else
        {
            g_Assistant.ClientPrint("Declined incoming trade offer.");
        }
    }
}

void CTradeManager::Clear()
{
    m_List.clear();
    m_TradeIntents.clear();
    m_ContextOpponent = 0;
}

int CTradeManager::Count()
{
    return (int)m_List.size();
}

CTradeWindow *CTradeManager::GetTrade(const astr_t &serial)
{
    uint32_t index = 0xFFFFFFFF;

    astr_t lower = str_lower(serial);
    if (lower.compare(0, 2, "0x") == 0)
    {
        index = CAssistant::TextToSerial(serial);

        for (size_t i = 0; i < m_List.size(); i++)
        {
            if (m_List[i].Serial == index)
            {
                return &m_List[i];
            }
        }
    }
    else
    {
        index = (uint32_t)atoi(serial.c_str());

        if (index < (uint32_t)m_List.size())
        {
            return &m_List[index];
        }
    }

    return nullptr;
}

} // namespace xuoassist
