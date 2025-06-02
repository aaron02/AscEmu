/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "PetAI.h"
#include "Objects/Units/Creatures/Summons/Summon.hpp"

PetAI::PetAI(Creature* creature) : CreatureAI(creature) { }

void PetAI::updateAI(uint32_t diff)
{
}

int32_t PetAI::permissible(Creature const* creature)
{
    if (creature->hasTypeMask(CREATURE_TYPE_MASK_CONTROLABLE_GUARDIAN))
    {
        if (reinterpret_cast<GuardianSummon const*>(creature)->getUnitOwner()->getObjectTypeId() == TYPEID_PLAYER)
            return 200;
        return 100;
    }

    return -1;
}
