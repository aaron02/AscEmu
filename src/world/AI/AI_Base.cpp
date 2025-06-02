/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#ifndef UNIX
#include <cmath>
#endif

#include "AI_BASE.h"

#include "Objects/Units/Unit.hpp"

#include "Movement/MovementManager.h"


void AI_Base::AttackStart(Unit* victim)
{
    if (victim && self->Attack(victim, true))
    {
        // Clear distracted state on attacking
        if (self->hasUnitStateFlag(UNIT_STATE_DISTRACTED))
        {
            self->removeUnitStateFlag(UNIT_STATE_DISTRACTED);
            self->getMovementManager()->clear();
        }
        self->getMovementManager()->moveChase(victim);
    }
}

void AI_Base::AttackStartCaster(Unit* victim, float dist)
{
    if (victim && self->Attack(victim, false))
        self->getMovementManager()->moveChase(victim, dist);
}

void AI_Base::InitializeAI()
{
    if (self->isAlive())
        Reset();
}

Unit* AI_Base::selectTarget(const Unit* unit, const TargetSelector& selector)
{
    if (!unit)
        return nullptr;

    std::list<Unit*> units;
    for (const auto& itr : unit->getInRangeObjectsSet())
    {
        if (itr && itr->isCreatureOrPlayer())
        {
            Unit* inRangeTarget = static_cast<Unit*>(itr);
            units.push_back(inRangeTarget);
        }
    }

    return selectTarget(selector, units);
}

Unit* AI_Base::selectTarget(const TargetSelector& selector, const std::list<Unit*>& units)
{
    Unit* bestTarget = nullptr;
    float bestScore = -1.0f;

    for (Unit* unit : units)
    {
        if (!unit || unit == selector.self)
            continue;

        if (!selector(unit))
            continue; // Check if unit matches the filter

        float score = selector.evaluate(unit);
        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = unit;
        }
    }

    return bestTarget;
}

Unit* AI_Base::selectRandomTarget(const std::list<Unit*>& units)
{
    if (units.empty())
        return nullptr;

    size_t randomIndex = std::rand() % units.size();

    auto it = units.begin();
    std::advance(it, randomIndex);

    return *it;
}

bool AI_Base::doMeleeAttackWhenReady()
{
    if (self->hasUnitStateFlag(UNIT_STATE_CASTING))
        return false;

    Unit* victim = self->getVictim();

    if (!self->isWithinMeleeRange(victim))
        return false;

    //Make sure our attack is ready and we aren't currently casting before checking distance
    if (self->isAttackReady(WeaponDamageType::MELEE))
    {
        self->AttackerStateUpdate(victim);
        self->resetAttackTimer(WeaponDamageType::MELEE);
    }

    if (self->canDualWield() && self->isAttackReady(WeaponDamageType::OFFHAND))
    {
        self->AttackerStateUpdate(victim, WeaponDamageType::OFFHAND);
        self->resetAttackTimer(WeaponDamageType::OFFHAND);
    }

    return true;
}

bool AI_Base::doSpellAttackWhenReady(uint32_t spellId)
{
    if (self->hasUnitStateFlag(UNIT_STATE_CASTING) || !self->isAttackReady(WeaponDamageType::MELEE))
        return true;

    if (SpellInfo const* spellInfo = sSpellMgr.getSpellInfo(spellId))
    {
        if (self->isWithinCombatRange(self->getVictim(), spellInfo->getMaxRange(false)))
        {
            self->castSpell(self->getVictim(), spellId, false);
            self->resetAttackTimer(WeaponDamageType::MELEE);
            return true;
        }
    }

    return false;
}

void AI_Base::castSpell(Unit* target, uint32_t spellId, bool triggered)
{
    self->castSpell(target, spellId, triggered);
}

void AI_Base::castSpellOnVictim(uint32_t spellId, bool triggered)
{
    if (Unit* victim = self->getAIInterface()->getCurrentTarget())
        castSpell(victim, spellId, triggered);
}

float AI_Base::getSpellMinRange(uint32_t spellId, bool friendly)
{
    SpellInfo const* spellInfo = sSpellMgr.getSpellInfo(spellId);
    return spellInfo ? spellInfo->getMinRange(friendly) : 0;
}

float AI_Base::getSpellMaxRange(uint32_t spellId, bool friendly)
{
    SpellInfo const* spellInfo = sSpellMgr.getSpellInfo(spellId);
    return spellInfo ? spellInfo->getMaxRange(friendly) : 0;
}
