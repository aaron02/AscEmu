/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "AI/CreatureAI.h"

class Creature;

class SERVER_DECL TotemAI : public CreatureAI
{
public:
    explicit TotemAI(Creature* creature);

    void internalLineOfSightMovement(Unit* causer) override;
    void attackStart(Unit* victim) override;
    void onEnterEvadeMode() override;

    void updateAI(uint32_t diff) override;
    static int32_t permissible(Creature const* creature);

private:
    uint64_t i_victimGuid;
};
