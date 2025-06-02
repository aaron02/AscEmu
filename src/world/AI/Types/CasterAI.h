/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "CombatAI.h"

class SERVER_DECL CasterAI : public CombatAI
{
public:
    explicit CasterAI(Creature* creature);

    void initializeAI() override;
    void onEnterCombat(Unit* causer) override;
    void updateAI(uint32_t diff) override;

    bool hasBreakableCCAura(Unit* target, Unit* exclude);
    bool hasBreakableByDamageAuraType(Unit* target, AuraEffect type, uint32_t excludeAura);

protected:
    float m_attackDistance;
};
