/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "AI/CreatureAI.h"

class Creature;

class SERVER_DECL DefaultAI : public CreatureAI
{
public:
    explicit DefaultAI(Creature* creature);

    void internalLineOfSightMovement(Unit* /*causer*/) override;
    void attackStart(Unit* /*victim*/) override;
    void updateAI(uint32_t /*diff*/) override;
    void onEnterEvadeMode() override;

    static int32_t permissible(Creature const* creature);
};
