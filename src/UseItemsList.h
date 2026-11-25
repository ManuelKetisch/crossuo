// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2017 Hotride

#pragma once

#include <cstdint>
#include "UseItemsList_rules.h"

class CUseItemActions
{
private:
    xuo_container_restore::UseItemQueue m_Queue;

public:
    CUseItemActions() = default;
    ~CUseItemActions() = default;

    void Add(int serial);

    void Clear() { m_Queue.Clear(); }

    bool Empty() const { return m_Queue.Empty(); }

    void NoteActionSent();

    void Process();
};

extern CUseItemActions g_UseItemActions;
