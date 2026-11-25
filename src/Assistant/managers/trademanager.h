// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2020 Danny Angelo Carminati Grein

// (lists are std::vector).

#pragma once

#include "core/global.h"
#include "model/commonitems/tradewindow.h"

struct CDataReader;

namespace xuoassist {

typedef vector<CTradeWindow> TRADE_LIST;

class CTradeManager
{
private:
    struct TradeIntent
    {
        uint32_t Opponent;
        uint32_t SentAt;
    };

    TRADE_LIST m_List;
    vector<TradeIntent> m_TradeIntents;
    uint32_t m_ContextOpponent = 0;
    uint32_t m_ContextAt = 0;
    uint16_t m_ContextTradeIndex = 0;

    void RecordIntent(uint32_t serial);
    bool ConsumeIntent(uint32_t serial);

public:
    CTradeManager();
    virtual ~CTradeManager();

    CTradeManager(const CTradeManager &) = delete;
    CTradeManager &operator=(const CTradeManager &) = delete;

    // trade bookkeeping over the client handler's parsed values
    void OnTradeAction(int action, uint32_t serial, uint32_t id1, uint32_t id2, const astr_t &name);

    // the client's 0x6F send sites report the user's trade answer here;
    // check is the sender's own check state (the TA_CHECK wire field)
    void OnTradeSent(int action, uint32_t id, bool check);
    void ObserveContextMenu(uint32_t serial, const CDataReader &items, bool newClilocs);
    void ObserveGameSend(uint8_t *data, int size);
    void ObserveFirstOffer(uint32_t container, uint16_t graphic);

    void Clear();

    int Count();

    CTradeWindow *GetTrade(const astr_t &serial);
};

extern CTradeManager g_TradeManager;

} // namespace xuoassist
