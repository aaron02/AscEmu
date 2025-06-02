/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "VehicleAI.h"

VehicleAI::VehicleAI(Creature* creature) : CreatureAI(creature)
{
    m_DoDismiss = false;
    m_DismissTimer = 5000;
}

void VehicleAI::attackStart(Unit* victim) { }

void VehicleAI::internalLineOfSightMovement(Unit* causer) { }

void VehicleAI::updateAI(uint32_t diff)
{
    if (m_DoDismiss)
    {
        if (m_DismissTimer < diff)
        {
            m_DoDismiss = false;
            self->despawn(1000);
        }
        else
            m_DismissTimer -= diff;
    }
}

int32_t VehicleAI::permissible(Creature const* creature)
{
    if (creature->isVehicle())
        return 800;

    return -1;
}
