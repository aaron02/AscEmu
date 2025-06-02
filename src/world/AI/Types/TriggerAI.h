/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "AI/Types/DefaultAI.h"

class Creature;

class SERVER_DECL TriggerAI : public DefaultAI
{
public:
    explicit TriggerAI(Creature* creature);

    void onSummonedBy(Unit* summoner) override;

    static int32_t permissible(Creature const* creature);
};
