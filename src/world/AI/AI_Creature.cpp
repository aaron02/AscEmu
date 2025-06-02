/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#ifndef UNIX
#include <cmath>
#endif

#include "AI_Creature.h"
#include "Objects/Units/Creatures/Vehicle.hpp"
#include "Movement/MovementManager.h"

AI_Creature::AI_Creature(Creature* creature) : AI_Base(creature),
self(creature),
_boundary(nullptr),
_negateBoundary(false),
m_MoveInLineOfSight_locked(false)
{
}

AI_Creature::~AI_Creature()
{
}

void AI_Creature::setZoneWideCombat(Creature* creature, float maxRangeToNearestTarget)
{
    if (!creature)
        creature = self;

    WorldMap* map = creature->getWorldMap();
    if (!map || !map->getBaseMap() || !map->getBaseMap()->isDungeon())
        return;

    // Find us a Victim
    if (!creature->hasReactState(REACT_PASSIVE) && !creature->getVictim())
    {
        if (Unit* nearTarget = selectNearestTarget(maxRangeToNearestTarget))
            creature->getAI()->AttackStart(nearTarget);
        else if (creature->isSummon())
        {
            if (Unit* summoner = creature->ToTempSummon()->GetSummoner())
            {
                Unit* target = summoner->getAttackerForHelper();
                if (!target && summoner->canHaveThreatList() && !summoner->getThreatManager().isThreatListEmpty())
                    target = summoner->getThreatManager().getHostilTarget();
                if (target && (creature->IsFriendlyTo(summoner) || creature->IsHostileTo(target)))
                    creature->getAI()->AttackStart(target);
            }
        }
    }

    if (!map->hasPlayers())
        return;

    for (auto player : map->getPlayers())
    {
        if (Player* plr = player.second)
        {
            if (!plr->isAlive() || !creature->canBeginCombat(plr))
                continue;

            creature->setInCombatWith(plr);
            plr->setInCombatWith(creature);
            creature->addThreat(plr, 0.0f);
        }
    }
}

void AI_Creature::internalLineOfSightMovement(Unit* causer)
{
    if (m_MoveInLineOfSight_locked)
        return;

    m_MoveInLineOfSight_locked = true;
    internalLineOfSightMovement(causer);
    m_MoveInLineOfSight_locked = false;
}

void AI_Creature::onLineOfSightReaction(Unit* causer)
{
    if (self->getVictim())
        return;

    if (self->hasReactState(REACT_AGGRESSIVE) && self->canStartAttack(self, false))
        AttackStart(self);
}

void AI_Creature::internalOnOwnerCombatInteraction(Unit* target)
{
    if (!target || !self->isAlive())
        return;

    if (!self->hasReactState(REACT_PASSIVE) && self->canStartAttack(target, true))
    {
        if (self->isInCombat())
            self->addThreat(target, 0.0f);
        else
            AttackStart(target);
    }
}

void AI_Creature::onAlertReaction(Unit const* causer) const
{
    // If there's no target, or target isn't a player do nothing
    if (!who || who->GetTypeId() != TYPEID_PLAYER)
        return;

    // If this unit isn't an NPC, is already distracted, is in combat, is confused, stunned or fleeing, do nothing
    if (me->GetTypeId() != TYPEID_UNIT || me->IsInCombat() || me->HasUnitState(UNIT_STATE_CONFUSED | UNIT_STATE_STUNNED | UNIT_STATE_FLEEING | UNIT_STATE_DISTRACTED))
        return;

    // Only alert for hostiles!
    if (me->IsCivilian() || me->HasReactState(REACT_PASSIVE) || !me->IsHostileTo(who) || !me->_IsTargetAcceptable(who))
        return;

    // Send alert sound (if any) for this creature
    me->SendAIReaction(AI_REACTION_ALERT);

    // Face the unit (stealthed player) and set distracted state for 5 seconds
    me->GetMotionMaster()->MoveDistract(5 * IN_MILLISECONDS);
    me->StopMoving();
    me->SetFacingTo(me->GetAngle(who));
}

void AI_Creature::onEnterEvadeMode()
{
    if (!internalEnterEvadeMode())
        return;

    if (!self->getVehicle())
    {
        if (Unit* owner = self->getCharmerOrOwner())
        {
            self->getMovementManager()->clear();
            self->getMovementManager()->moveFollow(owner, PET_FOLLOW_DIST, self->getFollowAngle(), MOTION_SLOT_ACTIVE);
        }
        else
        {
            // Required to prevent attacking creatures that are evading and cause them to reenter combat
            // Does not apply to MoveFollow
            self->addUnitStateFlag(UNIT_STATE_EVADING);
            self->getMovementManager()->moveTargetedHome();
        }
    }

    Reset();

    if (self->isVehicle())
        self->getVehicleKit()->initialize();
}

bool AI_Creature::internalEnterEvadeMode()
{
    if (!self->isAlive())
        return false;

    self->RemoveAurasOnEvade();

    // sometimes bosses stuck in combat?
    self->DeleteThreatList();
    self->CombatStop(true);
    self->SetLootRecipient(nullptr);
    self->ResetPlayerDamageReq();
    self->SetLastDamagedTime(0);
    self->SetCannotReachTarget(false);
    self->DoNotReacquireTarget();

    if (self->isInEvadeMode())
        return false;

    return true;
}

bool AI_Creature::updateVictim()
{
    if (!self->isInCombat())
        return false;

    if (!self->hasReactState(REACT_PASSIVE))
    {
        if (Unit* victim = self->selectVictim())
            if (!self->isFocusing(nullptr, true) && victim != self->getVictim())
                AttackStart(victim);

        return self->getVictim() != nullptr;
    }
    else if (self->getThreatManager().isThreatListEmpty())
    {
        onEnterEvadeMode();
        return false;
    }

    return true;
}



