/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "DefaultAI.h"

DefaultAI::DefaultAI(Creature* creature) : CreatureAI(creature)
{
    self->setReactState(REACT_PASSIVE);
}

void DefaultAI::internalLineOfSightMovement(Unit* /*causer*/) { }

void DefaultAI::attackStart(Unit* /*victim*/) { }

void DefaultAI::updateAI(uint32_t /*diff*/) { }

void DefaultAI::onEnterEvadeMode() { }

int32_t DefaultAI::permissible(Creature const* creature)
{
    if (creature->getNpcFlags() & UNIT_NPC_FLAG_SPELLCLICK)
        return 200 + 50;

    if (creature->isTrigger())
        return 200;

    return 1;
}
