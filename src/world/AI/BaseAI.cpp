/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#ifndef UNIX
#include <cmath>
#endif

#include "BaseAI.h"

#include "Objects/Units/Unit.hpp"
#include "Spell/SpellInfo.hpp"
#include "Spell/Definitions/SpellEffectTarget.hpp"
#include "Spell/Definitions/AuraInterruptFlags.hpp"
#include "Movement/MovementManager.h"
#include "AI/CreatureAISpells.h"


void BaseAI::attackStart(Unit* target)
{
    if (target && self->attackStart(target, true))
    {
        // Clear distracted state on attacking
        if (self->hasUnitStateFlag(UNIT_STATE_DISTRACTED))
        {
            self->removeUnitStateFlag(UNIT_STATE_DISTRACTED);
            self->getMovementManager()->clear();
        }
        self->getMovementManager()->moveChase(target);
    }
}

void BaseAI::attackStartCaster(Unit* target, float dist)
{
    if (target && self->attackStart(target, false))
        self->getMovementManager()->moveChase(target, dist);
}

void BaseAI::attack(Unit* victim, WeaponDamageType damageType)
{
    if (self->hasUnitStateFlag(UNIT_STATE_CANNOT_AUTOATTACK) || self->hasUnitFlags(UNIT_FLAG_PACIFIED))
        return;

    if (!victim->isAlive())
        return;

    if ((damageType == MELEE || damageType == OFFHAND) && !self->IsWithinLOSInMap(victim))
        return;

    self->removeAllAurasByAuraInterruptFlag(AURA_INTERRUPT_ON_START_ATTACK);

    if (damageType != MELEE && damageType != OFFHAND)
        return;

    if (self->getObjectTypeId() == TYPEID_UNIT && !self->hasUnitFlags(UNIT_FLAG_PLAYER_CONTROLLED_CREATURE) && !self->hasUnitFlags2(UNIT_FLAG2_DISABLE_TURN))
        self->setFacingToObject(victim, false);

    if (self->getOnMeleeSpell() == 0 || damageType == OFFHAND)
        self->strike(victim, damageType, nullptr, 0, 0, 0, false, false);
    else
        self->castOnMeleeSpell();
}

void BaseAI::initializeAI()
{
    if (self->isAlive())
        reset();
}

Unit* BaseAI::selectTarget(const Unit* unit, const TargetSelector& selector)
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

Unit* BaseAI::selectTarget(const TargetSelector& selector, const std::list<Unit*>& units)
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

Unit* BaseAI::selectRandomTarget(const std::list<Unit*>& units)
{
    if (units.empty())
        return nullptr;

    size_t randomIndex = std::rand() % units.size();

    auto it = units.begin();
    std::advance(it, randomIndex);

    return *it;
}

bool BaseAI::doMeleeAttackWhenReady()
{
    if (self->hasUnitStateFlag(UNIT_STATE_CASTING))
        return false;

    Unit* victim = self->getVictim();

    if (!self->isWithinMeleeRange(victim))
        return false;

    //Make sure our attack is ready and we aren't currently casting before checking distance
    if (self->isAttackReady(WeaponDamageType::MELEE))
    {
        attack(victim, WeaponDamageType::MELEE);
        self->resetAttackTimer(WeaponDamageType::MELEE);
    }

    if (self->canDualWield() && self->isAttackReady(WeaponDamageType::OFFHAND))
    {
        attack(victim ,WeaponDamageType::OFFHAND);
        self->resetAttackTimer(WeaponDamageType::OFFHAND);
    }

    return true;
}

bool BaseAI::doSpellAttackWhenReady(uint32_t spellId)
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

void BaseAI::castAISpell(uint32_t spellId)
{
    Unit* target = nullptr;

    // todo aaron02

    if (target)
        castSpell(target, spellId, false);
}

void BaseAI::castSpell(Unit* target, uint32_t spellId, bool triggered)
{
    self->castSpell(target, spellId, triggered);
}

void BaseAI::castSpellOnVictim(uint32_t spellId, bool triggered)
{
    if (Unit* victim = self->getAIInterface()->getCurrentTarget())
        castSpell(victim, spellId, triggered);
}

float BaseAI::getSpellMinRange(uint32_t spellId, bool friendly)
{
    SpellInfo const* spellInfo = sSpellMgr.getSpellInfo(spellId);
    return spellInfo ? spellInfo->getMinRange(friendly) : 0;
}

float BaseAI::getSpellMaxRange(uint32_t spellId, bool friendly)
{
    SpellInfo const* spellInfo = sSpellMgr.getSpellInfo(spellId);
    return spellInfo ? spellInfo->getMaxRange(friendly) : 0;
}

#define UPDATE_TARGET(a) {if (AIInfo->target<a) AIInfo->target=a;}

void BaseAI::fillAISpellInfo()
{
    mAISpellInfo = new AISpellInfo[sSpellMgr.getSpellInfoMap()->size()];

    AISpellInfo* AIInfo = mAISpellInfo;
    for (auto itr = sSpellMgr.getSpellInfoMap()->begin(); itr != sSpellMgr.getSpellInfoMap()->end(); ++itr, ++AIInfo)
    {
        SpellInfo const* spellInfo = sSpellMgr.getSpellInfo(itr->first);
        if (!spellInfo)
            continue;

        if (spellInfo->hasAttribute(ATTRIBUTES_DEAD_CASTABLE))
        {
            AIInfo->condition = AI_SCRIPT_EVENT_TYPES::onDied;
        }
        else if (spellInfo->isPassive() || spellInfo->getSpellDefaultDuration(nullptr) == -1)
        {
            AIInfo->condition = AI_SCRIPT_EVENT_TYPES::onEnterCombat;
        }
        else
        {
            AIInfo->condition = AI_SCRIPT_EVENT_TYPES::onAIUpdate;
        }

        if (AIInfo->cooldown < spellInfo->getRecoveryTime())
            AIInfo->cooldown = spellInfo->getRecoveryTime();

        if (!spellInfo->getMaxRange(false))
        {
            UPDATE_TARGET(TARGET_SELF)
        }
        else
        {
            for (uint32_t j = 0; j < MAX_SPELL_EFFECTS; ++j)
            {
                uint32_t targetType = spellInfo->getEffectImplicitTargetA(j);

                if (targetType == EFF_TARGET_SINGLE_ENEMY || targetType == EFF_TARGET_CURRENT_SELECTION)
                {
                    UPDATE_TARGET(TARGET_ATTACKING)
                }
                else if (targetType == EFF_TARGET_ALL_ENEMY_IN_AREA_INSTANT)
                {
                    UPDATE_TARGET(TARGET_VARIOUS)

                        if (spellInfo->getEffect(j) == SPELL_EFFECT_APPLY_AURA)
                        {
                            if (targetType == EFF_TARGET_SINGLE_ENEMY)
                            {
                                UPDATE_TARGET(TARGET_VARIOUS)
                            }
                            else if (!(spellInfo->getAttributes() & ATTRIBUTES_NEGATIVE))
                            {
                                UPDATE_TARGET(TARGET_SELF)
                            }
                        }
                }
            }
        }
        AIInfo->realCooldown = spellInfo->getRecoveryTime() + spellInfo->getStartRecoveryTime();
        AIInfo->maxRange = spellInfo->getMaxRange(false) * 3 / 4;
    }
}
