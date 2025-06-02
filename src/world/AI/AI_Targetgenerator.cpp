/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#ifndef UNIX
#include <cmath>
#endif

#include "AI_Targetgenerator.h"

//////////////////////////////////////////////////////////////////////////////////////////
/// Default Target Selector
DefaultTargetSelector::DefaultTargetSelector(Unit const* self, float distance, bool onlyPlayers, int32_t aura)
    : TargetSelector(self), _distance(distance), _onlyPlayers(onlyPlayers), _activeAura(aura) 
{
}

bool DefaultTargetSelector::operator()(const Unit* unit) const
{
    if (!self)
        return false;

    if (!unit)
        return false;

    if (_onlyPlayers && (unit->getObjectTypeId() != TYPEID_PLAYER))
        return false;

    if (_distance > 0.0f && !self->isWithinCombatRange(unit, _distance))
        return false;

    if (_distance < 0.0f && self->isWithinCombatRange(unit, -_distance))
        return false;

    if (_activeAura) {
        if (_activeAura > 0) {
            if (!unit->hasAurasWithId(std::abs(_activeAura)))
                return false;
        }
        else {
            if (unit->hasAurasWithId(std::abs(_activeAura)))
                return false;
        }
    }

    return true;
}

float DefaultTargetSelector::evaluate(const Unit* unit) const 
{
    return 1.0f / (unit->getDistance(self) + 1.0f); // Higher score for closer units
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Lowest Health Selector
LowestHealthSelector::LowestHealthSelector(Unit const* self) : TargetSelector(self)
{
}

bool LowestHealthSelector::operator()(const Unit* unit) const
{
    return unit && unit->isAlive(); // Ensure the unit is alive
}

float LowestHealthSelector::evaluate(const Unit* unit) const
{
    return 1.0f / (unit->getHealthPct() + 1.0f); // Higher score for lower health
}

//////////////////////////////////////////////////////////////////////////////////////////
/// RandomTarget Selector
RandomTargetSelector::RandomTargetSelector()
{
}

bool RandomTargetSelector::operator()(const Unit* unit) const
{
    return unit && unit->isAlive(); // Ensure the unit is alive
}

float RandomTargetSelector::evaluate(const Unit* unit) const
{
    return Util::getRandomFloat(0.0f, 1.0f); // Random Number from 0.0 - 1.0
}
