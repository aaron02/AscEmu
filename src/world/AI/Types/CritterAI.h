/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "AI/Types/PassiveAI.h"

class SERVER_DECL CritterAI : public PassiveAI
{
public:
    explicit CritterAI(Creature* creature);

    void onDamageTaken(Unit* /*attacker*/, uint32_t& /*damage*/) override;
    void onEnterEvadeMode() override;

    static int32_t permissible(Creature const* creature);
};
