/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "AI/CreatureAI.h"

class SERVER_DECL RangedAI : public CreatureAI
{
public:
    explicit RangedAI(Creature* creature);

    void attackStart(Unit* victim) override;
    void updateAI(uint32_t diff) override;

    static int32_t permissible(Creature const* /*creature*/);

protected:
    float m_minRange;
};
