/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "AI_Defines.h"
#include "Objects/ObjectDefines.hpp"
#include "AI_Base.h"

class AreaBoundary;
class Object;
class Creature;
class Unit;
class Player;

typedef std::vector<AreaBoundary const*> CreatureBoundary;

class SERVER_DECL AI_Creature : AI_Base
{
public:
    explicit AI_Creature(Creature* creature);
    ~AI_Creature();

protected:
    // Update Current Victim
    bool updateVictim();

    // Boundary
    bool checkBoundary(LocationVector const* who = nullptr) const;

    // summon Handling
    Creature* summonCreature(uint32 entry, LocationVector const& pos, uint32 despawnTime = 30000, CreatureSummonDespawnType summonType = CORPSE_TIMED_DESPAWN);
    Creature* summonCreature(uint32 entry, Object* obj, float radius = 5.0f, uint32 despawnTime = 30000, CreatureSummonDespawnType summonType = CORPSE_TIMED_DESPAWN);

public:
    //////////////////////////////////////////////////////////////////////////////////////////
    // Internal Functions
    void setZoneWideCombat(Creature* creature = nullptr, float maxRangeToNearestTarget = 250.0f);

    bool internalEnterEvadeMode();
    void internalOnOwnerCombatInteraction(Unit* target);

    virtual void internalLineOfSightMovement(Unit* /*causer*/);


    //////////////////////////////////////////////////////////////////////////////////////////
    // Boundary
    virtual bool CheckInRoom();
    CreatureBoundary const* GetBoundary() const { return _boundary; }
    void SetBoundary(CreatureBoundary const* boundary, bool negativeBoundaries = false);
    static bool IsInBounds(CreatureBoundary const& boundary, Position const* who);

    //////////////////////////////////////////////////////////////////////////////////////////
    // Reactions

    // Called when Unit enters visibility zone of AI
    void onLineOfSightReaction(Unit* causer);

    // Called when Stealthed unit is visible
    void onAlertReaction(Unit const* causer) const;

    // Called when Unit reacts to enter Combat (only when not in Combat)
    virtual void onEngagementStart(Unit* /*victim*/) { }
    
    // Called when Unit evades at no attackers or active targets
    virtual void onEnterEvadeMode();

    // Called when creature is Killed
    virtual void onDied(Unit* /*killer*/) { }

    // Called when our corpse gets removed
    virtual void onCorpseRemoved(uint32_t& /*respawnDelay*/) {}

    // Called when creature kills a Victim
    virtual void OnUnitKilled(Unit* /*victim*/) { }

    // Called when the creature summons other creature
    virtual void JustSummoned(Creature* /*summon*/) { }

    // Called when we got Summoned by other creature
    virtual void GotSummonedBy(Unit* /*summoner*/) { }

    // Called when a summoned Creature dies
    virtual void onSummonDied(Creature* /*summon*/) {}

    // Called when a summoned Creature despawns
    virtual void onSummonDespawn(Creature* /*summon*/) {}

    // Called when creature got Hit by a Spell
    virtual void onSpellHit(Unit* /*caster*/, SpellInfo const* /*spellInfo*/) { }

    // Called when Spell hit a Target
    virtual void onSpellHitTarget(Unit* /*target*/, SpellInfo const* /*spellInfo*/) { }

    // Called when creature gets pushed to World
    virtual void onPushToWorld() { }

    // Called when waypoint or point movement reached
    virtual void onMovementInform(uint32_t /*type*/, uint32_t /*id*/) { }

    // Called when a spell cast gets interrupted
    virtual void onSpellCastInterrupt(SpellInfo const* /*spellInfo*/) { }

    // Called when a spell cast has ben succesfully
    virtual void onSpellCastSuccessful(SpellInfo const* /*spellInfo*/) { }

    // Called when creature reached its Home after evading
    virtual void onHomeReached() { }

    // Called emote received
    virtual void onEmoteReceive(Unit* /*instingator*/, uint32_t /*emoteId*/) { }

    // Called when owner takes damage
    virtual void onOwnerDamageTaken(Unit* /*attacker*/) { }

    // Called when owner attacks anything
    virtual void onOwnerAttacked(Unit* /*target*/) { }

    // Called when Passenger entered Vehicle
    virtual void onPassengerEntered(Unit* /*passenger*/, int8_t /*seatId*/, bool /*apply*/) { }

    // Called onSpellClick
    virtual void onSpellClick(Unit* /*clicker*/, bool& /*result*/) { }

private:
    bool m_MoveInLineOfSight_locked;

protected:
    Creature* const self;

    CreatureBoundary const* _boundary;
    bool _negateBoundary;
};
