/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "Map/RecastIncludes.hpp"
#include "Objects/Units/Creatures/AIEvents.h"
#include "Macros/AIInterfaceMacros.hpp"
#include "Objects/Units/Creatures/CreatureDefines.hpp"
#include "Server/Script/ScriptEvent.hpp"
#include "Chat/ChatDefines.hpp"
#include "Storage/MySQLStructures.h"
#include <functional>
#include "Utilities/TimeTracker.hpp"

#include "AI/AI_Defines.h"

inline bool inRangeYZX(const float* v1, const float* v2, const float r, const float h)
{
    const float dx = v2[0] - v1[0];
    const float dy = v2[1] - v1[1]; // elevation
    const float dz = v2[2] - v1[2];
    return (dx * dx + dz * dz) < r * r && fabsf(dy) < h;
}

struct AbstractFollower;
class AreaBoundary;
class MapMgr;
class Object;
class Creature;
class Unit;
class Player;
class WorldSession;
class SpellCastTargets;
class CreatureAIScript;
class CreatureGroup;
class SpellInfo;
class CreatureAISpells;

enum MovementGeneratorType : uint8_t;

class SpellInfo;

const uint32_t AISPELL_ANY_DIFFICULTY = 4;
typedef std::set<Unit*> AssistTargetSet;
typedef std::vector<Unit*> UnitArray;

struct AI_Spell
{
    ~AI_Spell() { autocast_type = (uint32_t)-1; }
    uint32_t entryId;
    uint8_t instance_mode;
    uint16_t agent;
    uint32_t procChance;
    SpellInfo const* spell;
    uint8_t spellType;
    uint8_t spelltargetType;
    uint32_t cooldown;
    uint32_t cooldowntime;
    uint32_t procCount;
    uint32_t procCounter;
    float floatMisc1;
    uint32_t Misc2;
    float minrange;
    float maxrange;
    uint32_t autocast_type;
};

class SERVER_DECL AIInterface2
{
protected:
    Creature* self;
public:
    AIInterface2(Creature* owner);
    virtual ~AIInterface2();

    //////////////////////////////////////////////////////////////////////////////////////////
    // Combat
public:
    bool canStartAttack(Unit* target, bool force);
    bool canOwnerAttackUnit(Unit* pUnit, bool force);

    bool isTargetAcceptable(Unit* target);
    bool isTargetableForAttack(bool checkFakeDeath);
};

class SERVER_DECL AIInterface
{
public:
    AIInterface();
    virtual ~AIInterface();

    //////////////////////////////////////////////////////////////////////////////////////////
    // AI Agent functions
    bool m_canRangedAttack;
    void selectCurrentAgent(Unit* target, uint32_t spellid);
    void initializeSpells();

    void addSpellFromDatabase(std::vector<MySQLStructure::CreatureAIScripts> scripts);
    void addEmoteFromDatabase(std::vector<MySQLStructure::CreatureAIScripts> scripts, definedEmoteVector& emoteVector);

    void setCannotReachTarget(bool cannotReach);
    bool canNotReachTarget() const { return m_cannotReachTarget; }

    void callForHelp(float fRadius);
    void doFleeToGetAssistance();
    void callAssistance();
    void findAssistance();
    bool alreadyCalledForHelp() { return m_AlreadyCallAssistance; }
    void setNoCallAssistance(bool val) { m_AlreadyCallAssistance = val; }
    void setNoSearchAssistance(bool val) { m_AlreadySearchedAssistance = val; }
    bool gasSearchedAssistance() const { return m_AlreadySearchedAssistance; }
    bool canAssistTo(Unit* u, Unit* enemy, bool checkfaction = true);

    inline uint8_t getCurrentAgent() { return static_cast<uint8_t>(m_AiCurrentAgent); }
    void setCurrentAgent(AI_Agent agent) { m_AiCurrentAgent = agent; }

    bool canCallForHelp() { return m_canCallForHelp; }
    void setCanCallForHelp(bool value) { m_canCallForHelp = value; }
    bool canFlee() { return m_canFlee; }
    void setCanFlee(bool value ) { m_canFlee = value; }

    bool m_canFlee;
    float m_FleeHealth;
    uint32_t m_FleeDuration;
    bool m_canCallForHelp;
    float m_CallForHelpHealth;

private:
    AI_Agent m_AiCurrentAgent;
    bool m_hasFleed;
    std::unique_ptr<Util::SmallTimeTracker> m_fleeTimer;

protected:
    bool m_AlreadyCallAssistance;
    bool m_AlreadySearchedAssistance;

    //////////////////////////////////////////////////////////////////////////////////////////
    // Combat functions
public:
    void justEnteredCombat(Unit* pUnit);
    void engagementStart(Unit* target);
    void atEngagementStart(Unit* target);

    void engagementOver();
    void atEngagementOver();

    bool isEngaged();
    bool isEngagedBy(Unit* who) const;

    bool isImmuneToNPC();
    bool isImmuneToPC();
    void setImmuneToNPC(bool apply);
    void setImmuneToPC(bool apply);

    // Called when unit takes damage or get hits by spell
    void onHostileAction(Unit* pUnit, SpellInfo const* spellInfo = nullptr, bool ignoreThreatRedirects = false);

    void setAllowedToEnterCombat(bool val) 
    {
        setImmuneToNPC(!val);
        setImmuneToPC(!val);
        canEnterCombat = val;
    }
    inline bool getAllowedToEnterCombat(void) { return canEnterCombat; }

    void setReactState(ReactStates st) { m_reactState = st; }
    ReactStates getReactState() const { return m_reactState; }
    bool hasReactState(ReactStates state) const { return (m_reactState == state); }
    void initializeReactState();

private:
    bool m_isEngaged;

protected:
    ReactStates m_reactState;

    //////////////////////////////////////////////////////////////////////////////////////////
    // Combat behavior
private:
    bool mIsCombatDisabled;
    bool mIsMeleeDisabled;
    bool mIsRangedDisabled;
    bool mIsCastDisabled;
    bool mIsTargetingDisabled;

public:
    void setCombatDisabled(bool disable) { mIsCombatDisabled = disable; }
    bool isCombatDisabled() { return mIsCombatDisabled; }

    void setMeleeDisabled(bool disable) { mIsMeleeDisabled = disable; }
    bool isMeleeDisabled() { return mIsMeleeDisabled; }

    void setRangedDisabled(bool disable) { mIsRangedDisabled = disable; }
    bool isRangedDisabled() { return mIsRangedDisabled; }

    void setCastDisabled(bool disable) { mIsCastDisabled = disable; }
    bool isCastDisabled() { return mIsCastDisabled; }

    void setTargetingDisabled(bool disable) { mIsTargetingDisabled = disable; }
    bool isTargetingDisabled() { return mIsTargetingDisabled; }

    //////////////////////////////////////////////////////////////////////////////////////////
    // Misc functions
    void Init(Unit* un, Unit* owner = nullptr);   // used for pets
    Unit* getUnit() const;
    Unit* getPetOwner() const;
    Unit* getCurrentTarget() const;
    LocationVector m_lasttargetPosition;

    bool isGuard() { return m_isNeutralGuard; }
    void setGuard(bool value) { m_isNeutralGuard = value; }
    void setCurrentTarget(Unit* pUnit) { m_target = pUnit; }
    float calcCombatRange(Unit* target, bool ranged);

    void updateEmotes(unsigned long time_passed);
    void eventAiInterfaceParamsetFinish();
    TimedEmoteList* timed_emotes;

    bool moveTo(float x, float y, float z, float o = 0.0f, bool running = false);
    void calcDestinationAndMove(Unit* target, float dist);

    // boundary system methods
    bool checkBoundary();
    CreatureBoundary const& getBoundary() const { return _boundary; }
    // todo: does it really have to be a pointer? -Appled
    void addBoundary(std::unique_ptr<AreaBoundary const> boundary, bool overrideDefault = false, bool negativeBoundaries = false);
    void setDefaultBoundary();
    void clearBoundary();
    static bool isInBounds(CreatureBoundary const* boundary, LocationVector who);
    bool isInBoundary(LocationVector who) const;
    bool isInBoundary() const;
    void doImmediateBoundaryCheck();

    bool canUnitEvade(unsigned long time_passed);
    void enterEvadeMode();
    bool _enterEvadeMode();

    void initGroupThreat(Unit* target);
    void instanceCombatProgress(bool activate);

    // Event Handler
    void handleEvent(uint32_t event, Unit* pUnit, uint32_t misc1);

    void eventForceRedirected(Unit* pUnit, uint32_t misc1);
    void eventHostileAction(Unit* pUnit, uint32_t misc1);
    void eventUnitDied(Unit* pUnit, uint32_t misc1);
    void eventUnwander(Unit* pUnit, uint32_t misc1);
    void eventWander(Unit* pUnit, uint32_t misc1);
    void eventUnfear(Unit* pUnit, uint32_t misc1);
    void eventFear(Unit* pUnit, uint32_t misc1);
    void eventFollowOwner(Unit* pUnit, uint32_t misc1);
    void eventDamageTaken(Unit* pUnit, uint32_t misc1);
    void eventLeaveCombat(Unit* pUnit, uint32_t misc1);
    void eventEnterCombat(Unit* pUnit, uint32_t misc1);
    void eventOnTaunt(Unit* pUnit);
    void eventOnLoad();
    void eventChangeFaction(Unit* ForceAttackersToHateThisInstead = NULL);    /// we have to tell our current enemies to stop attacking us, we should also forget about our targets
    void eventOnTargetDied(Object* pKiller);

    // Update
    void Update(unsigned long time_passed);
    void updateAIScript(unsigned long time_passed);
    void updateTargets(unsigned long time_passed);

    // Attacking
    void attackStart(Unit* target);
    void attackStop();
    bool doInitialAttack(Unit* target, bool isMelee);

    // Called Eacht AIUpdate Tick to select a new Target
    bool updateTarget();
    Unit* selectTarget();
    bool isTargetAcceptable(Unit* target);
    bool isTargetableForAttack(bool checkFakeDeath);
    Unit* getTargetForPet();

    float calcAggroRange(Unit* target);

    bool canStartAttack(Unit* target, bool force);
    bool canOwnerAttackUnit(Unit* pUnit);
    
    Unit* findTarget();
    void updateAgent(uint32_t p_time);
    void updateTotem(uint32_t p_time);

    void handleAgentMelee();
    void handleAgentRanged();
    void handleAgentSpell(uint32_t spellId);
    void handleAgentFlee(uint32_t p_time);
    void handleAgentCallForHelp();

    Unit* m_Unit;
    Unit* m_PetOwner;
    Unit* m_target;
    bool m_isNeutralGuard;
    uint32_t faction_visibility;

    // Difficulty
    void setCreatureProtoDifficulty(uint32_t entry);
    uint8_t getDifficultyType();
    bool m_is_in_instance;

    inline AssistTargetSet GetAssistTargets() { return m_assistTargets; }
    bool isAlreadyAssisting(Creature* helper);

    uint8_t internalPhase;
    void initialiseScripts(uint32_t entry);
    std::vector<MySQLStructure::CreatureAIScripts> onLoadScripts;
    std::vector<MySQLStructure::CreatureAIScripts> onCombatStartScripts;
    std::vector<MySQLStructure::CreatureAIScripts> onAIUpdateScripts;
    std::vector<MySQLStructure::CreatureAIScripts> onLeaveCombatScripts;
    std::vector<MySQLStructure::CreatureAIScripts> onDiedScripts;
    std::vector<MySQLStructure::CreatureAIScripts> onKilledScripts;
    std::vector<MySQLStructure::CreatureAIScripts> onCallForHelpScripts;
    std::vector<MySQLStructure::CreatureAIScripts> onRandomWaypointScripts;
    std::vector<MySQLStructure::CreatureAIScripts> onDamageTakenScripts;
    std::vector<MySQLStructure::CreatureAIScripts> onFleeScripts;
    std::vector<MySQLStructure::CreatureAIScripts> onTauntScripts;

private:
    definedEmoteVector mEmotesOnLoad;
    definedEmoteVector mEmotesOnCombatStart;
    definedEmoteVector mEmotesOnLeaveCombat;
    definedEmoteVector mEmotesOnTargetDied;
    definedEmoteVector mEmotesOnAIUpdate;
    definedEmoteVector mEmotesOnDied;
    definedEmoteVector mEmotesOnDamageTaken;
    definedEmoteVector mEmotesOnCallForHelp;
    definedEmoteVector mEmotesOnFlee;
    definedEmoteVector mEmotesOnTaunt;
    definedEmoteVector mEmotesOnRandomWaypoint;

public:
    void sendStoredText(definedEmoteVector& store, Unit* target);

    Unit* mCurrentSpellTarget;

    //////////////////////////////////////////////////////////////////////////////////////////
    // target
    Unit* getBestPlayerTarget(TargetFilter pFilter = TargetFilter_None, float pMinRange = 0.0f, float pMaxRange = 0.0f);
    Unit* getBestUnitTarget(TargetFilter pFilter = TargetFilter_None, float pMinRange = 0.0f, float pMaxRange = 0.0f);
    Unit* getBestTargetInArray(UnitArray& pTargetArray, TargetFilter pFilter);
    Unit* getNearestTargetInArray(UnitArray& pTargetArray);
    Unit* getSecondMostHatedTargetInArray(UnitArray& pTargetArray);
    bool isValidUnitTarget(Object* pObject, TargetFilter pFilter, float pMinRange = 0.0f, float pMaxRange = 0.0f);

protected:
    std::unique_ptr<Util::SmallTimeTracker> m_boundaryCheckTime;
    CreatureBoundary _boundary;
    bool _negateBoundary;

    std::unique_ptr<Util::SmallTimeTracker> m_updateAssistTimer;
    AssistTargetSet m_assistTargets;

private:
    bool m_disableDynamicBoundary = false;

    //////////////////////////////////////////////////////////////////////////////////////////
    // Movement functions
public:
    virtual void onMovementGeneratorFinalized(MovementGeneratorType /*type*/) { }

    // Called at waypoint reached or point movement finished
    virtual void movementInform(uint32_t /*type*/, uint32_t /*id*/);

    bool canCreatePath(float x, float y, float z);
    dtStatus findSmoothPath(const float* startPos, const float* endPos, const dtPolyRef* polyPath, const uint32_t polyPathSize, float* smoothPath, uint32_t* smoothPathSize, bool & usedOffmesh, const uint32_t maxSmoothPathSize, dtNavMesh* mesh, dtNavMeshQuery* query, dtQueryFilter & filter);
    bool getSteerTarget(const float* startPos, const float* endPos, const float minTargetDist, const dtPolyRef* path, const uint32_t pathSize, float* steerPos, unsigned char & steerPosFlag, dtPolyRef & steerPosRef, dtNavMeshQuery* query);
    uint32_t fixupCorridor(dtPolyRef* path, const uint32_t npath, const uint32_t maxPath, const dtPolyRef* visited, const uint32_t nvisited);

    //////////////////////////////////////////////////////////////////////////////////////////
    // Waypoint functions
private:
    bool mShowWayPoints;

public:
    bool hasWayPoints();
    uint32_t getCurrentWayPointId();
    uint32_t getWayPointsCount();

    void setWayPointToMove(uint32_t waypointId);

    bool activateShowWayPoints(Player* player, bool showBackwards);
    bool isShowWayPointsActive();
    bool hideWayPoints(Player* player);

    //////////////////////////////////////////////////////////////////////////////////////////
    // Pet functions
    inline void setPetOwner(Unit* owner) { m_PetOwner = owner; }
    void setUnitToFollow(Unit* pUnit) { m_UnitToFollow = pUnit; }
    Unit* getUnitToFollow() { return m_UnitToFollow; }

protected:
    Unit* m_UnitToFollow;   // used in scripts

    //////////////////////////////////////////////////////////////////////////////////////////
    // Totem functions
public:
    uint32_t m_totemspelltimer;
    uint32_t m_totemspelltime;
    SpellInfo const* totemspell;

    //////////////////////////////////////////////////////////////////////////////////////////
    // Waypoint functions
public:
    virtual void waypointStarted(uint32_t /*nodeId*/, uint32_t /*pathId*/);
    virtual void waypointReached(uint32_t /*nodeId*/, uint32_t /*pathId*/);
    virtual void waypointPathEnded(uint32_t /*nodeId*/, uint32_t /*pathId*/);

    //////////////////////////////////////////////////////////////////////////////////////////
    // Spell functions
public:
    void castSpell(Unit* caster, SpellInfo const* spellInfo, SpellCastTargets targets);
    SpellInfo const* getSpellEntry(uint32_t spellId);
    SpellCastTargets setSpellTargets(SpellInfo const* spellInfo, Unit* target, uint8_t targettype) const;

    //addAISpell(spellID, Chance, TargetType, Duration (s), waitBeforeNextCast (s))
    CreatureAISpells* addAISpell(uint32_t spellId, float castChance, uint32_t targetType, uint32_t duration = 0, uint32_t cooldown = 0, bool forceRemove = false, bool isTriggered = false);

    std::list<std::unique_ptr<AI_Spell>> m_spells;
    void addSpellToList(std::unique_ptr<AI_Spell> sp);
    AI_Spell* getSpell(uint32_t entry);
    void setNextSpell(uint32_t spellId);
    void removeNextSpell(uint32_t spellId);

    //////////////////////////////////////////////////////////////////////////////////////////
    // spell

    void castAISpell(CreatureAISpells* aiSpell);
    void castAISpell(uint32_t aiSpellId);
    bool hasAISpell(CreatureAISpells* aiSpell);
    bool hasAISpell(uint32_t SpellId);
    void castSpellOnRandomTarget(CreatureAISpells* AiSpell);
    void UpdateAISpells();

    CreatureAISpells* mLastCastedSpell;

    typedef std::vector<std::unique_ptr<CreatureAISpells>> CreatureAISpellsArray;
    CreatureAISpellsArray mCreatureAISpells;

    std::unique_ptr<Util::SmallTimeTracker> mSpellWaitTimer;

    //////////////////////////////////////////////////////////////////////////////////////////
    // script events
    // \brief: 
protected:
    scriptEventMap spellEvents;

protected:
    bool canEnterCombat;

    TimedEmoteList::iterator next_timed_emote;
    uint32_t timed_emote_expire;

    bool m_cannotReachTarget;
    std::unique_ptr<Util::SmallTimeTracker> m_noTargetTimer;
    std::unique_ptr<Util::SmallTimeTracker> m_cannotReachTimer;
    std::unique_ptr<Util::SmallTimeTracker> m_updateTargetTimer;
};
