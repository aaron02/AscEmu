/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "AI_Defines.h"

// Forward Declarations
class Unit;

class SERVER_DECL AI_Base
{
public:
    explicit AI_Base(Unit* unit) : self(unit) { }
    virtual ~AI_Base() { }

    virtual bool CanAIAttack(Unit const* /*target*/) const { return true; }
    virtual void AttackStart(Unit* /*target*/);
    virtual void AttackStartCaster(Unit* /*victim*/, float /*dist*/);
    
    virtual void UpdateAI(uint32 diff) = 0;

    virtual void InitializeAI();

    virtual void Reset() {}

    //////////////////////////////////////////////////////////////////////////////////////////
    // Data sharing between scripts

    virtual void DoAction(int32_t /*param*/) {}

    virtual uint32_t GetData(uint32_t /*id = 0*/) const { return 0; }
    virtual void SetData(uint32_t /*id*/, uint32_t /*data*/) {}
    virtual uint64_t GetGUID(int32 /*id*/ = 0) const { return 0; }
    virtual void SetGUID(int32_t /*id*/, uint64_t /*data*/) {}
    
    //////////////////////////////////////////////////////////////////////////////////////////
    // Target Selection

    Unit* selectTarget(const Unit* unit, const TargetSelector& selector);
    Unit* selectTarget(const TargetSelector& selector, const std::list<Unit*>& units);
    Unit* selectRandomTarget(const std::list<Unit*>& units);


    //////////////////////////////////////////////////////////////////////////////////////////
    // Events
    
    // Called when Unit enters combat
    virtual void onEnterCombat(Unit* /*causer*/) {}

    // Called when Unit leaves combat
    virtual void onLeaveCombat() { }

    // Called when Unit gets removed from World
    virtual void onRemoveFromWorld() { }

    // Called when Unit is about to deal damage
    virtual void onDamageDealt(Unit* /*victim*/, uint32_t& /*damage*/) { }

    // Called when Unit is about to take damage
    virtual void onDamageTaken(Unit* /*attacker*/, uint32_t& /*damage*/) { }

    // Called when Unit is about to receive heal
    virtual void onHealReceived(Unit* /*causer*/, uint32_t& /*amount*/) {}

    // Called when Unit is healing 
    virtual void onHealDone(Unit* /*target*/, uint32_t& /*amount*/) {}

    // Called when a Spell our Unit is casting gets Interrupted
    virtual void onSpellInterrupted(uint32_t /*spellId*/) {}
    
    //////////////////////////////////////////////////////////////////////////////////////////
    // Functions

    // Combat
    bool doMeleeAttackWhenReady();
    bool doSpellAttackWhenReady(uint32_t spellId);

    // Spell helpers
    void castSpell(Unit* target, uint32_t spellId, bool triggered = false);
    void castSpellOnSelf(uint32_t spellId, bool triggered = false) { castSpell(self, spellId, triggered); }
    void castSpellOnVictim(uint32_t spellId, bool triggered = false);
    void castSpellAOE(uint32_t spellId, bool triggered = false) { castSpell(nullptr, spellId, triggered); }

    float getSpellMinRange(uint32_t spellId, bool friendly);
    float getSpellMaxRange(uint32_t spellId, bool friendly);

protected:
    Unit* const self;
};
