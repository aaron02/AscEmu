/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "TriggerAI.h"

TriggerAI::TriggerAI(Creature* creature) : DefaultAI(creature) { }

void TriggerAI::onSummonedBy(Unit* summoner)
{
    if (self->GetCreatureProperties() && self->GetCreatureProperties()->AISpells[0])
        self->castSpell(self, self->GetCreatureProperties()->AISpells[0], false);
}

int32_t TriggerAI::permissible(Creature const* creature)
{
    if (creature->isTrigger() && creature->GetCreatureProperties() && creature->GetCreatureProperties()->AISpells[0])
        return 800;

    return -1;
}
