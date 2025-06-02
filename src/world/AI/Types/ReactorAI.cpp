/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "ReactorAI.h"

ReactorAI::ReactorAI(Creature* creature) : CreatureAI(creature) { }

void ReactorAI::internalLineOfSightMovement(Unit* /*causer*/) { }

void ReactorAI::updateAI(uint32_t diff)
{
    if (!updateVictim())
        return;

    doMeleeAttackWhenReady();
}

int32_t ReactorAI::permissible(Creature const* creature)
{
    if (creature->isCivilian() || creature->isNeutralToAll())
        return 100;

    return -1;
}
