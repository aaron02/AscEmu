/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "Objects/Units/Creatures/Creature.h"
#include "CommonTypes.hpp"
#include "CreatureAIFactory.h"
#include "Errors.h"

class SERVER_DECL AI_Manager
{
public:
    static AI_Manager* getInstance();

    CreatureAI* selectAI(Creature* creature);

private:
    // Private Konstruktoren für Singleton
    AI_Manager() = default;
    AI_Manager(const AI_Manager&) = delete;
    AI_Manager& operator=(const AI_Manager&) = delete;
};

#define sAIManager AI_Manager::getInstance()
