/*
Copyright (c) 2014-2024 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "CommonTypes.hpp"
#include "AI_Targetgenerator.h"


enum Variables
{
    UPDATE_INTERVAL     = 5000,
    VISIBILITY_RANGE    = 10000
};

enum AIReaction
{
    AI_REACTION_ALERT       = 0,    // pre-aggro (used in client packet handler)
    AI_REACTION_FRIENDLY    = 1,    // (NOT used in client packet handler)
    AI_REACTION_HOSTILE     = 2,    // sent on every attack, triggers aggro sound (used in client packet handler)
    AI_REACTION_AFRAID      = 3,    // seen for polymorph (when AI not in control of self?) (NOT used in client packet handler)
    AI_REACTION_DESTROY     = 4     // used on object destroy (NOT used in client packet handler)
};

enum AI_SCRIPT_EVENT_TYPES
{
    onLoad              = 0,
    onEnterCombat       = 1,
    onLeaveCombat       = 2,
    onDied              = 3,
    onTargetDied        = 4,
    onAIUpdate          = 5,
    onCallForHelp       = 6,
    onRandomWaypoint    = 7,
    onDamageTaken       = 8,
    onFlee              = 9,
    onTaunt             = 10
};

enum AI_SCRIPT_ACTION_TYPES
{
    actionNone          = 0,
    actionSpell         = 1,
    actionSendMessage   = 2,
    actionPhaseChange   = 3
};

struct AI_SCRIPT_SENDMESSAGES
{
    uint32_t textId;
    float canche;
    uint32_t phase;
    float healthPrecent;
    uint32_t count;
    uint32_t maxCount;
};

typedef std::vector<std::shared_ptr<AI_SCRIPT_SENDMESSAGES>> definedEmoteVector;

enum ReactStates : uint8_t
{
    REACT_PASSIVE = 0,
    REACT_DEFENSIVE = 1,
    REACT_AGGRESSIVE = 2
};

enum AI_Agent : uint8_t
{
    AGENT_NULL,
    AGENT_MELEE,
    AGENT_RANGED,
    AGENT_FLEE,
    AGENT_SPELL,
    AGENT_CALLFORHELP
};

enum AI_SpellType
{
    STYPE_NULL,
    STYPE_ROOT,
    STYPE_HEAL,
    STYPE_STUN,
    STYPE_FEAR,
    STYPE_SILENCE,
    STYPE_CURSE,
    STYPE_AOEDAMAGE,
    STYPE_DAMAGE,
    STYPE_SUMMON,
    STYPE_BUFF,
    STYPE_DEBUFF
};

enum AI_SpellTargetType
{
    TTYPE_NULL,
    TTYPE_SINGLETARGET,
    TTYPE_DESTINATION,
    TTYPE_SOURCE,
    TTYPE_CASTER,
    TTYPE_OWNER
};

enum AISpellTargetType
{
    TARGET_SELF,
    TARGET_VARIOUS,
    TARGET_ATTACKING,
    TARGET_DESTINATION,
    TARGET_SOURCE,
    TARGET_RANDOM_FRIEND,
    TARGET_RANDOM_SINGLE,
    TARGET_RANDOM_DESTINATION,
    TARGET_CLOSEST,
    TARGET_FURTHEST,
    TARGET_CUSTOM,
    TARGET_FUNCTION
};

struct AISpellInfo
{
    AISpellInfo() : target(TARGET_SELF), condition(onAIUpdate), cooldown(5000), realCooldown(0), maxRange(0.0f) { }

    AISpellTargetType target;
    AI_SCRIPT_EVENT_TYPES condition;
    uint32_t cooldown;
    uint32_t realCooldown;
    float maxRange;
};
