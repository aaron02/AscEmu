/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "AI/CreatureAI.h"

class Creature;

class SERVER_DECL PetAI : public CreatureAI
{
public:
    explicit PetAI(Creature* creature);

    void updateAI(uint32_t diff) override;
    static int32_t permissible(Creature const* creature);
};
