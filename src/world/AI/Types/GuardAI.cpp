/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "GuardAI.h"
#include "Movement/MovementManager.h"
#include "Server/World.h"

GuardAI::GuardAI(Creature* creature) : CreatureAI(creature) { }

void GuardAI::updateAI(uint32_t diff)
{
    if (!updateVictim())
        return;

    doMeleeAttackWhenReady();
}

void GuardAI::onEnterEvadeMode()
{
    if (!self->isAlive())
    {
        self->getMovementManager()->moveIdle();
        self->getThreatManager().clearAllThreat();
        self->getCombatHandler().clearCombat();
        return;
    }

    self->removeAllAuras();
    self->getThreatManager().clearAllThreat();
    self->getCombatHandler().clearCombat();

    if (self->getMovementManager()->getCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        self->getMovementManager()->moveTargetedHome();
}

void GuardAI::onDied(Unit* killer)
{
    if (Player* player = killer->getUnitOwnerOrSelf()->ToPlayer())
    {
        auto team = player->getTeam();
        if (team == TEAM_ALLIANCE)
            team = TEAM_HORDE;
        else
            team = TEAM_ALLIANCE;

        const auto area = player->GetArea();
        sWorld.sendZoneUnderAttackMessage(area != nullptr ? area->id : player->getZoneId(), team);
    }
}

int32_t GuardAI::permissible(Creature const* creature)
{
    if (creature->isGuard())
        return 200;

    return -1;
}
