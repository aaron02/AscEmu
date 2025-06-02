/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#ifndef UNIX
#include <cmath>
#endif

#include "CreatureAI.h"
#include "Objects/Units/Creatures/Vehicle.hpp"
#include "Movement/MovementManager.h"
#include "Objects/Units/Creatures/Summons/Summon.hpp"

#include "Map/AreaBoundary.hpp"
#include "Utilities/TimeTracker.hpp"

AISpellInfo* BaseAI::mAISpellInfo;
AISpellInfo* getAISpellInfo(uint32 i) { return &BaseAI::mAISpellInfo[i]; }

CreatureAI::CreatureAI(Creature* creature) : BaseAI(creature),
self(creature),
_negateBoundary(false),
m_MoveInLineOfSight_locked(false)
{
    _boundary.clear();
}

CreatureAI::~CreatureAI()
{
}

void CreatureAI::setZoneWideCombat(Creature* creature, float maxRangeToNearestTarget)
{
    if (!creature)
        creature = self;

    WorldMap* map = creature->getWorldMap();
    if (!map || !map->getBaseMap() || !map->getBaseMap()->isDungeon())
        return;

    // Find us a Victim
    if (!creature->hasReactState(REACT_PASSIVE) && !creature->getVictim())
    {
        if (Unit* nearTarget = self->selectNearestTarget(maxRangeToNearestTarget))
        {
            creature->getAI()->attackStart(nearTarget);
        }
        else if (creature->isSummon())
        {
            if (Unit* summoner = creature->ToSummon()->getUnitOwner())
            {
                Unit* target = summoner->getTargetForPet();
                if (!target && summoner->getThreatManager().canHaveThreatList() && !summoner->getThreatManager().isThreatListEmpty())
                    target = summoner->getThreatManager().getCurrentVictim();
                if (target && (creature->isFriendlyTo(summoner) || creature->isHostileTo(target)))
                    creature->getAI()->attackStart(target);
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

            creature->getCombatHandler().onHostileAction(plr);
            plr->getCombatHandler().onHostileAction(creature);

            creature->getThreatManager().addThreat(plr, 0.0f);
        }
    }
}

void CreatureAI::internalLineOfSightMovement(Unit* causer)
{
    if (m_MoveInLineOfSight_locked)
        return;

    m_MoveInLineOfSight_locked = true;
    internalLineOfSightMovement(causer);
    m_MoveInLineOfSight_locked = false;
}

bool CreatureAI::checkBoundary(LocationVector const* pos) const
{
    if (!_boundary.empty())
        return true;

    return (CreatureAI::isInBounds(&_boundary, pos) != _negateBoundary);
}

bool CreatureAI::checkBoundary()
{
    if (!isInBoundary(self->GetPosition()))
    {
        onEnterEvadeMode();
        return false;
    }

    return true;
}

void CreatureAI::addBoundary(AreaBoundary const* boundary, bool overrideDefault, bool negativeBoundaries)
{
    if (boundary == nullptr)
        return;

    if (overrideDefault && !m_disableDynamicBoundary)
    {
        // On first custom boundary, clear existing default/dynamic boundaries
        clearBoundary();
        m_disableDynamicBoundary = true;
    }

    _boundary.push_back(boundary);
    _negateBoundary = negativeBoundaries;
    doImmediateBoundaryCheck();
}

void CreatureAI::setDefaultBoundary()
{
    if (m_disableDynamicBoundary)
        return;

    // Do net set default boundaries to creatures in raids or dungeons
    // Mobs and bosses will chase players to instance portal unless custom boundaries are set
    if (self->getWorldMap()->getBaseMap()->getMapInfo()->isInstanceMap())
        return;

    if (self->isPet() || self->isSummon())
        return;

    // Clear existing boundaries
    clearBoundary();

    // Default boundary 50 yards
    addBoundary(new CircleBoundary(self->GetPosition(), 50.0f));
}

void CreatureAI::clearBoundary()
{
    for (auto& boundaryItr : _boundary)
        delete boundaryItr;

    _boundary.clear();
}

/*static*/ bool CreatureAI::isInBounds(CreatureBoundary const* boundary, LocationVector const* pos)
{
    for (AreaBoundary const* areaBoundary : *boundary)
        if (!areaBoundary->isWithinBoundary(pos))
            return false;

    return true;
}

bool CreatureAI::isInBoundary(LocationVector pos) const
{
    if (_boundary.empty())
        return true;

    return AIInterface::isInBounds(&_boundary, pos) != _negateBoundary;
}

bool CreatureAI::isInBoundary() const
{
    if (_boundary.empty())
        return true;

    return AIInterface::isInBounds(&_boundary, self->GetPosition()) != _negateBoundary;
}

void CreatureAI::doImmediateBoundaryCheck()
{
    m_boundaryCheckTime->resetInterval(0);
}

void CreatureAI::onLineOfSightReaction(Unit* causer)
{
    if (self->getVictim())
        return;

    if (self->hasReactState(REACT_AGGRESSIVE) && self->getAIInterface2()->canStartAttack(self, false))
        attackStart(self);
}

void CreatureAI::internalOnOwnerCombatInteraction(Unit* target)
{
    if (!target || !self->isAlive())
        return;

    if (!self->hasReactState(REACT_PASSIVE) && self->getAIInterface2()->canStartAttack(target, true))
    {
        if (self->isInCombat())
            self->getThreatManager().addThreat(target, 0.0f);
        else
            attackStart(target);
    }
}

void CreatureAI::onAlertReaction(Unit* causer)
{
    // If there's no target, or target isn't a player do nothing
    if (!causer || causer->isPlayer())
        return;

    // If this unit isn't an NPC, is already distracted, is in combat, is confused, stunned or fleeing, do nothing
    if (self->getObjectTypeId() != TYPEID_UNIT || self->isInCombat() || self->hasUnitStateFlag(UNIT_STATE_CONFUSED | UNIT_STATE_STUNNED | UNIT_STATE_FLEEING | UNIT_STATE_DISTRACTED))
        return;

    // Only alert for hostiles!
    if (self->isCivilian() || self->hasReactState(REACT_PASSIVE) || !self->isHostileTo(causer) || !self->getAIInterface2()->isTargetAcceptable(causer))
        return;

    // Send alert sound (if any) for this creature
    self->SendAIReaction(AI_REACTION_ALERT);

    // Face the unit (stealthed player) and set distracted state for 5 seconds
    self->getMovementManager()->moveDistract(5 * IN_MILLISECONDS, self->getRelativeAngle(causer));
    self->stopMoving();
}

void CreatureAI::onEnterEvadeMode()
{
    if (!internalEnterEvadeMode())
        return;

    if (!self->getVehicle())
    {
        if (Unit* owner = self->getUnitOwner())
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

    reset();

    if (self->isVehicle())
        self->getVehicleKit()->initialize();
}

bool CreatureAI::internalEnterEvadeMode()
{
    if (!self->isAlive())
        return false;

    self->removeAllAuras();
    self->getThreatManager().clearAllThreat();
    self->getThreatManager().removeMeFromThreatLists();
    self->getCombatHandler().clearCombat();
    self->setTaggerGuid(0);
    self->setCannotReachTarget(false);
    self->doNotReacquireTarget();

    if (self->isInEvadeMode())
        return false;

    return true;
}

bool CreatureAI::updateVictim()
{
    if (!self->isInCombat())
        return false;

    if (!self->hasReactState(REACT_PASSIVE))
    {
        if (Unit* victim = self->selectVictim())
            if (!self->isFocusing(nullptr, true) && victim != self->getVictim())
                attackStart(victim);

        return self->getVictim() != nullptr;
    }
    else if (self->getThreatManager().isThreatListEmpty())
    {
        onEnterEvadeMode();
        return false;
    }

    return true;
}

Creature* CreatureAI::summonCreature(uint32_t entry, LocationVector const& pos, uint32_t despawnTime, CreatureSummonDespawnType summonType)
{
    return self->summonCreature(entry, pos, summonType, despawnTime);
}

Creature* CreatureAI::summonCreature(uint32_t entry, Object* obj, float radius, uint32_t despawnTime, CreatureSummonDespawnType summonType)
{
    LocationVector pos = obj->GetPosition();

    float angle = static_cast<float>(rand()) / RAND_MAX * 2.0f * M_PI;
    float distance = static_cast<float>(rand()) / RAND_MAX * radius;

    pos.x += cos(angle) * distance;
    pos.y += sin(angle) * distance;
    pos.z = obj->getMapHeight(pos);

    return self->summonCreature(entry, pos, summonType, despawnTime);
}

