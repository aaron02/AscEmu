/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "CritterAI.h"

CritterAI::CritterAI(Creature* creature) : PassiveAI(creature) { }

void CritterAI::onDamageTaken(Unit*, uint32_t&)
{
    if (!self->hasUnitStateFlag(UNIT_STATE_FLEEING))
        self->setControlled(true, UNIT_STATE_FLEEING);
}

void CritterAI::onEnterEvadeMode()
{
    if (self->hasUnitStateFlag(UNIT_STATE_FLEEING))
        self->setControlled(false, UNIT_STATE_FLEEING);

    CreatureAI::onEnterEvadeMode();
}

int32_t CritterAI::permissible(Creature const* creature)
{
    if (creature->isCritter() && !creature->hasTypeMask(CREATURE_TYPE_MASK_GUARDIAN))
        return 200;

    return -1;
}
