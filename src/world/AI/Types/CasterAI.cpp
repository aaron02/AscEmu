/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "CasterAI.h"
#include "Spell/Definitions/AuraInterruptFlags.hpp"

CasterAI::CasterAI(Creature* creature) : CombatAI(creature), m_attackDistance(MELEE_RANGE) { }

void CasterAI::initializeAI()
{
    CombatAI::initializeAI();

    m_attackDistance = 30.0f;
    for (auto itr : spells)
    {
        if (mAISpellInfo[itr].condition == AI_SCRIPT_EVENT_TYPES::onAIUpdate && m_attackDistance > mAISpellInfo[itr].maxRange)
        {
            m_attackDistance = mAISpellInfo[itr].maxRange;
        }
    }

    if (m_attackDistance == 30.0f)
        m_attackDistance = MELEE_RANGE;
}

void CasterAI::onEnterCombat(Unit* causer)
{
    if (spells.empty())
        return;

    uint32_t spell = Util::getRandomUInt(UINT32_MAX) % spells.size();
    uint32_t count = 0;
    for (auto itr : spells)
    {
        if (mAISpellInfo[itr].condition == AI_SCRIPT_EVENT_TYPES::onEnterCombat)
        {
            self->castSpell(causer, itr, false);
        }
        else if (mAISpellInfo[itr].condition == AI_SCRIPT_EVENT_TYPES::onAIUpdate)
        {
            uint32_t cooldown = mAISpellInfo[itr].realCooldown;
            if (itr == spell)
            {
                castAISpell(spells[spell]);
                cooldown += self->getCurrentSpellById(itr)->getFullCastTime();
            }
            scriptEvents.addEvent(itr, cooldown);
        }
    }
}

void CasterAI::updateAI(uint32_t diff)
{
    if (!updateVictim())
        return;

    scriptEvents.updateEvents(diff, 0);

    if (self->getVictim() && hasBreakableCCAura(self->getVictim(), self))
    {
        self->interruptSpell(0, false);
        return;
    }

    if (self->hasUnitStateFlag(UNIT_STATE_CASTING))
        return;

    if (uint32_t spellId = scriptEvents.getFinishedEvent())
    {
        castAISpell(spellId);
        uint32_t casttime = self->getCurrentSpellById(spellId)->getFullCastTime();
        scriptEvents.addEvent(spellId, (casttime ? casttime : 500) + mAISpellInfo[spellId].realCooldown);
    }
}

bool CasterAI::hasBreakableCCAura(Unit* target, Unit* exclude)
{
    uint32_t excludeAura = 0;
    if (Spell* currentChanneledSpell = exclude ? exclude->getCurrentSpell(CURRENT_CHANNELED_SPELL) : nullptr)
        excludeAura = currentChanneledSpell->getSpellInfo()->getId();

    return (hasBreakableByDamageAuraType(target, SPELL_AURA_MOD_CONFUSE, excludeAura)
        || hasBreakableByDamageAuraType(target, SPELL_AURA_MOD_FEAR, excludeAura)
        || hasBreakableByDamageAuraType(target, SPELL_AURA_MOD_STUN, excludeAura)
        || hasBreakableByDamageAuraType(target, SPELL_AURA_MOD_ROOT, excludeAura)
        || hasBreakableByDamageAuraType(target, SPELL_AURA_TRANSFORM, excludeAura));
}

bool CasterAI::hasBreakableByDamageAuraType(Unit* target, AuraEffect type, uint32_t excludeAura)
{
    AuraEffectList const& auras = target->getAuraEffectList(type);
    for (auto itr : auras)
    {
        if ((!excludeAura || excludeAura != (itr)->getAura()->getSpellId()) && ((itr)->getAura()->getSpellInfo()->getAuraInterruptFlags() & AURA_INTERRUPT_ON_ANY_DAMAGE_TAKEN))
            return true;
    }
    return false;
}
