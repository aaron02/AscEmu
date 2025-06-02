/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "TotemAI.h"

TotemAI::TotemAI(Creature* creature) : CreatureAI(creature), i_victimGuid() { }

void TotemAI::internalLineOfSightMovement(Unit* causer) { }

void TotemAI::attackStart(Unit* victim) { }

void TotemAI::onEnterEvadeMode()
{
    self->getCombatHandler().clearCombat();
}

void TotemAI::updateAI(uint32_t diff)
{
    
}

int32_t TotemAI::permissible(Creature const* creature)
{
    if (creature->isTotem())
        return 200;

    return -1;
}
