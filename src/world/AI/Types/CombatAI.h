/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "AI/CreatureAI.h"

class Creature;

class SERVER_DECL CombatAI : public CreatureAI
{
public:
    explicit CombatAI(Creature* creature);

    void initializeAI() override;
    void reset() override;
    void onEnterCombat(Unit* causer) override;
    void onDied(Unit* killer) override;
    void updateAI(uint32_t diff) override;
    void onSpellInterrupted(uint32_t spellId) override;

    static int32_t permissible(Creature const* /*creature*/);

protected:
    scriptEventMap scriptEvents;
    std::vector<uint32_t> spells;
};
