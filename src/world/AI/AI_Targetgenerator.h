/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "CommonTypes.hpp"
#include "Objects/Object.hpp"
#include "Objects/Units/Unit.hpp"

#include <memory>
#include <list>
#include <utility>

// Forward declaration
class Object;
class Unit;

//////////////////////////////////////////////////////////////////////////////////////////
/// Abstract Class for TargetSelectors
struct TargetSelector
{
    Unit const* self;

    explicit TargetSelector(Unit const* self = nullptr): self(self) {}

    virtual bool operator()(const Unit* unit) const = 0;
    virtual float evaluate(const Unit* unit) const = 0;

    virtual ~TargetSelector() = default;
};

//////////////////////////////////////////////////////////////////////////////////////////
/// Default Target Selector
struct DefaultTargetSelector : public TargetSelector
{
    float _distance;
    bool _onlyPlayers;
    int32_t _activeAura;

    DefaultTargetSelector(Unit const* self, float distance = 0.0f, bool onlyPlayers = false, int32_t aura = 0);
    
    bool operator()(const Unit* unit) const override;
    float evaluate(const Unit* unit) const override;
};

//////////////////////////////////////////////////////////////////////////////////////////
/// Lowest Health Selector
struct LowestHealthSelector : public TargetSelector
{
    explicit LowestHealthSelector(Unit const* self);

    bool operator()(const Unit* unit) const override;
    float evaluate(const Unit* unit) const override;
};

//////////////////////////////////////////////////////////////////////////////////////////
/// RandomTarget Selector
struct RandomTargetSelector : public TargetSelector 
{
    explicit RandomTargetSelector();

    bool operator()(const Unit* unit) const override;
    float evaluate(const Unit* unit) const override;
};

//////////////////////////////////////////////////////////////////////////////////////////
/// Composite Selector
struct CompositeSelector : public TargetSelector 
{
    std::list<std::unique_ptr<TargetSelector>> selectors;

    CompositeSelector(Unit const* self) : TargetSelector(self) {}

    CompositeSelector(Unit const* self, std::vector<std::unique_ptr<DefaultTargetSelector>>&& list)
        : TargetSelector(self)
    {
        for (auto& selector : list) 
        {
            selectors.push_back(std::move(selector)); // Verschiebung des Besitzes
        }
    }

    // Hinzufügen eines neuen Selectors mit std::move
    void addSelector(std::unique_ptr<TargetSelector> selector) 
    {
        selectors.push_back(std::move(selector));
    }

    // Operator für die Filterung
    bool operator()(const Unit* unit) const override 
    {
        for (const auto& selector : selectors) 
        {
            if (!(*selector)(unit)) 
            {
                return false;
            }
        }
        return true;
    }

    // Bewertungsfunktion, summiert die Scores der enthaltenen Selektoren
    float evaluate(const Unit* unit) const override 
    {
        float score = 0.0f;
        for (const auto& selector : selectors) 
        {
            score += selector->evaluate(unit);
        }
        return score;
    }
};

enum TargetFilter : uint32_t
{
    // Standard filters
    TargetFilter_None = 0,                              // 0
    TargetFilter_Closest = 1 << 0,                      // 1
    TargetFilter_Friendly = 1 << 1,                     // 2
    TargetFilter_NotCurrent = 1 << 2,                   // 4
    TargetFilter_Wounded = 1 << 3,                      // 8
    TargetFilter_SecondMostHated = 1 << 4,              // 16
    TargetFilter_Aggroed = 1 << 5,                      // 32
    TargetFilter_Corpse = 1 << 6,                       // 64
    TargetFilter_InMeleeRange = 1 << 7,                 // 128
    TargetFilter_InRangeOnly = 1 << 8,                  // 256
    TargetFilter_IgnoreSpecialStates = 1 << 9,          // 512 - not really a TargetFilter, more like requirement for spell
    TargetFilter_IgnoreLineOfSight = 1 << 10,           // 1024
    TargetFilter_Current = 1 << 11,                     // 2048
    TargetFilter_LowestHealth = 1 << 12,                // 4096
    TargetFilter_Health = 1 << 13,                      // 8192
    TargetFilter_AOE = 1 << 14,                         // 16348 - not really a Filter just no Target for AOE spells
    TargetFilter_Self = 1 << 15,                        // 32768 - mostlikely return ourself unless we set aura filtering and we dont have it
    TargetFilter_Caster = 1 << 16,                      // 65536 - Mana Based Class
    TargetFilter_Casting = 1 << 17,                     // 131072 - Target is Casting currently
    TargetFilter_Player = 1 << 18,                      // 262144 - Players Only

    // Predefined filters
    TargetFilter_ClosestFriendly = TargetFilter_Closest | TargetFilter_Friendly,                // 3
    TargetFilter_ClosestNotCurrent = TargetFilter_Closest | TargetFilter_NotCurrent,            // 5
    TargetFilter_WoundedFriendly = TargetFilter_Wounded | TargetFilter_Friendly,                // 10
    TargetFilter_FriendlyCorpse = TargetFilter_Corpse | TargetFilter_Friendly,                  // 66
    TargetFilter_ClosestFriendlyCorpse = TargetFilter_Closest | TargetFilter_FriendlyCorpse,    // 67
    TargetFilter_CurrentInRangeOnly = TargetFilter_Current | TargetFilter_InRangeOnly,          // 2304
    TargetFilter_WoundedFriendlyLowestHealth = TargetFilter_Wounded | TargetFilter_Friendly | TargetFilter_LowestHealth, // 4106
    TargetFilter_WoundedFriendlyLowestHealthInRange = TargetFilter_Wounded | TargetFilter_Friendly | TargetFilter_LowestHealth | TargetFilter_InRangeOnly, // 4362
    TargetFilter_CasterWhileCasting = TargetFilter_Casting | TargetFilter_Caster, // 196608
    TargetFilter_SelfBelowHealth = TargetFilter_Self | TargetFilter_Health  // 40960
};