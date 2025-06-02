/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "RangedAI.h"
#include "Movement/MovementManager.h"

RangedAI::RangedAI(Creature* creature) : CreatureAI(creature), m_minRange(0.0f)
{
    if (!self->GetCreatureProperties() && !self->GetCreatureProperties()->AISpells[0])
        sLogger.failure("RangedAI, creature with entry %u has no spell in slot 0 in creatureproperties, AI will do nothing", self->getEntry());

    SpellInfo const* spellInfo = sSpellMgr.getSpellInfo(self->GetCreatureProperties()->AISpells[0]);
    m_minRange = spellInfo ? spellInfo->getMinRange(false) : 0;

    if (!m_minRange)
        m_minRange = MELEE_RANGE;

    self->setCombatReach(spellInfo ? spellInfo->getMaxRange(false) : self->GetCreatureProperties()->CombatReach);
}

void RangedAI::attackStart(Unit* victim)
{
    if (!victim)
        return;

    if (self->isWithinCombatRange(victim, m_minRange))
    {
        if (self->attackStart(victim, true) && !victim->IsFlying())
            self->getMovementManager()->moveChase(victim);
    }
    else
    {
        if (self->attackStart(victim, false) && !victim->IsFlying())
            self->getMovementManager()->moveChase(victim, self->getCombatReach());
    }

    if (victim->IsFlying())
        self->getMovementManager()->moveIdle();
}

void RangedAI::updateAI(uint32_t diff)
{
    if (!updateVictim())
        return;

    if (!self->isWithinCombatRange(self->getVictim(), m_minRange))
        doSpellAttackWhenReady(self->GetCreatureProperties()->AISpells[0]);
    else
        doMeleeAttackWhenReady();
}

int32_t RangedAI::permissible(Creature const* /*creature*/)
{
    return -1;
}
