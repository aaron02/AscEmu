/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "CombatAI.h"

CombatAI::CombatAI(Creature* creature) : CreatureAI(creature) { }

void CombatAI::initializeAI()
{
    for (uint8_t i = 0; i < 8; ++i)
        if (self->GetCreatureProperties() && self->GetCreatureProperties()->AISpells[i] && sSpellMgr.getSpellInfo(self->GetCreatureProperties()->AISpells[i]))
            spells.push_back(self->GetCreatureProperties()->AISpells[i]);

    CreatureAI::initializeAI();
}

void CombatAI::reset()
{
    scriptEvents.resetEvents();
}

void CombatAI::onEnterCombat(Unit* causer)
{
    for (auto i : spells)
    {
        if (mAISpellInfo[i].condition == AI_SCRIPT_EVENT_TYPES::onEnterCombat)
            self->castSpell(causer, i, false);
        else if (mAISpellInfo[i].condition == AI_SCRIPT_EVENT_TYPES::onAIUpdate)
            scriptEvents.addEvent(i, mAISpellInfo[i].cooldown + Util::getRandomUInt(UINT32_MAX) % mAISpellInfo[i].cooldown);
    }
}

void CombatAI::onDied(Unit* killer)
{
    for (auto i : spells)
        if (mAISpellInfo[i].condition == AI_SCRIPT_EVENT_TYPES::onDied)
            self->castSpell(killer, i, true);
}

void CombatAI::updateAI(uint32_t diff)
{
    if (!updateVictim())
        return;

    scriptEvents.updateEvents(diff, 0);

    if (self->hasUnitStateFlag(UNIT_STATE_CASTING))
        return;

    if (uint32_t spellId = scriptEvents.getFinishedEvent())
    {
        castAISpell(spellId);
        scriptEvents.addEvent(spellId, mAISpellInfo[spellId].cooldown + Util::getRandomUInt(UINT32_MAX) % mAISpellInfo[spellId].cooldown);
    }
    else
        doMeleeAttackWhenReady();
}

void CombatAI::onSpellInterrupted(uint32_t spellId)
{
    scriptEvents.addEvent(spellId, 1000);
}

int32_t CombatAI::permissible(Creature const* /*creature*/)
{
    return -1;
}
