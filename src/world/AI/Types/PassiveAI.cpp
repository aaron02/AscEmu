/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "PassiveAI.h"
#include "Objects/Units/Creatures/Creature.h"

PassiveAI::PassiveAI(Creature* creature) : CreatureAI(creature)
{
    self->setReactState(REACT_PASSIVE);
}

void PassiveAI::internalLineOfSightMovement(Unit* /*causer*/) { }

void PassiveAI::attackStart(Unit* /*victim*/) { }

void PassiveAI::updateAI(uint32_t diff)
{
    if (self->isInCombat())
        onEnterEvadeMode();
}

int32_t PassiveAI::permissible(Creature const* /*creature*/)
{
    return -1;
}
