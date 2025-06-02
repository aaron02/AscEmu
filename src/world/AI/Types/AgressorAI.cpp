/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "AgressorAI.h"

AggressorAI::AggressorAI(Creature* creature) : CreatureAI(creature) { }

void AggressorAI::updateAI(uint32_t)
{
    if (!updateVictim())
        return;

    doMeleeAttackWhenReady();
}

int32_t AggressorAI::permissible(Creature const* creature)
{
    // have some hostile factions, it will be selected by isHostileTo check at MoveInLineOfSight
    if (!creature->isCivilian() && !creature->isNeutralToAll())
        return 100;

    return -1;
}
