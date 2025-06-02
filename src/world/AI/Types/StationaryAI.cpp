/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "StationaryAI.h"



StationaryAI::StationaryAI(Creature* creature) : CreatureAI(creature), m_minRange(0.0f)
{
    if (!self->GetCreatureProperties() && !self->GetCreatureProperties()->AISpells[0])
        sLogger.failure("StationaryAI, creature with entry %u has no spell in slot 0 in creatureproperties, AI will do nothing", self->getEntry());

    SpellInfo const* spellInfo = sSpellMgr.getSpellInfo(self->GetCreatureProperties()->AISpells[0]);
    m_minRange = spellInfo ? spellInfo->getMinRange(false) : 0;

    self->setCombatReach(spellInfo ? spellInfo->getMaxRange(false) : self->GetCreatureProperties()->CombatReach);
}

bool StationaryAI::canAIAttack(Unit const* who) const
{
    if (!self->isWithinCombatRange(who, self->getCombatReach()) || (m_minRange && self->isWithinCombatRange(who, m_minRange)))
        return false;
    return true;
}

void StationaryAI::attackStart(Unit* victim)
{
    if (victim)
        self->attackStart(victim, false);
}

void StationaryAI::updateAI(uint32_t diff)
{
    if (!updateVictim())
        return;

    doSpellAttackWhenReady(self->GetCreatureProperties()->AISpells[0]);
}

int32_t StationaryAI::permissible(Creature const* /*creature*/)
{
    return -1;
}
