/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "Management/WorldStatesHandler.hpp"
#include "DynamicTree.h"
#include "Server/EventableObject.h"

#include <queue>
#include <algorithm>
#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "InstanceDefines.hpp"
#include "Debugging/Errors.h"
#include "Map/SpawnGroups.hpp"

#include "Map/Visibility/VisibilitySystem.hpp"
#include "Map/Management/ObjectFactory.hpp"
#include "Map/Management/WorldObjectRegistry.hpp"
#include "Map/Management/GuidAllocator.hpp"
#include "Map/Management/SpawnManager.hpp"

namespace AscEmu::Threading
{
    class AEThread;
}

namespace WDB::Structures
{
    struct SummonPropertiesEntry;
    struct MapDifficulty;
}

namespace visibility { class SpatialIndex; class VisibilitySystem; }
namespace world { class WorldObjectRegistry; }
class GuidAllocator;
class ObjectFactory;

class WorldSession;
class ByteBuffer;
class WorldPacket;
class DynamicObject;

class InstanceScript;
class MapScriptInterface;
class Object;
class GameObject;
class Unit;
class Creature;
class Player;
class Pet;
class Transporter;
class Corpse;
class Battleground;
class InstanceScript;
class Summon;
class InstanceMap;
class CreatureGroup;
enum LineOfSightChecks : uint8_t;
enum EnterState;

typedef std::set<Object*> UpdateQueue;
typedef std::set<Player*> PUpdateQueue;

typedef std::set<uint64_t> CombatProgressMap;

class SERVER_DECL WorldMap : public EventableObject, public WorldStatesHandler::WorldStatesObserver
{
    friend class MapScriptInterface;

public:
    WorldMap(BaseMap* baseMap, uint32_t id, uint32_t expiryTime, uint32_t InstanceId, uint8_t SpawnMode);
    virtual ~WorldMap();

    virtual void initialize();
    virtual void update(uint32_t);
    virtual void delayedUpdate(std::chrono::milliseconds diff);
    virtual void unloadAll(bool onShutdown = false);

    void startMapThread();
    void runThread();
    void shutdownMapThread();
    void unsafeKillMapThread();
    bool isMapReadyForDelete() const;

    void Do();

    void setUnloadPending(bool value) { m_unloadPending = value; }
    bool isUnloadPending() { return m_unloadPending; }

    float getVisibilityRange() const { return m_VisibleDistance; }
    virtual void initVisibilityDistance();

    void outOfMapBoundariesTeleport(Object* object);

    //////////////////////////////////////////////////////////////////////////////////////////////////////
    // Visibility System
    //////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    void updateAll(std::chrono::milliseconds diff);

    void deferDestroy(Object* object);
    void drainDeferredDestroy();

    void hookVisibilityEvents();
    void onPlayerEnter(Player* plr);
    void onPlayerLeave(Player* plr);
    void onObjectMoved(Object* obj);

    void onObjectBecameVisible(const WoWGuid& viewer, const WoWGuid& obj);
    void onObjectBecameHidden(const WoWGuid& viewer, const WoWGuid& obj);

    void processPendingVisibilityChanges(std::size_t maxEvents = 2048, std::size_t maxCreatesPerPlayer = 32, std::size_t maxDestroysPerPlayer = 96);
    void processPendingVisibilityChangesForViewer(const WoWGuid& viewer, std::size_t maxCreates = 256, std::size_t maxDestroys = 256);
    void purgePendingVisibilityForGuid(const WoWGuid& guid);
    void logVisibilityMemoryDiagnostics(const char* reason);
    Player* getPlayerByGuid(const WoWGuid& g);
    Object* getObjectByGuid(const WoWGuid& g);

    visibility::SpatialIndex& getSpatialIndex() const { assert(spatialIndex_); return *spatialIndex_; }
    visibility::VisibilitySystem& getVisibilitySystem() const { assert(visibilitySystem_); return *visibilitySystem_; }
    world::WorldObjectRegistry& getRegistry() const { assert(registry_); return *registry_; }
    ObjectFactory& getObjectFactory() const { assert(factory_);  return *factory_; }
    SpawnManager& getSpawnManager() const { assert(spawnMgr_); return *spawnMgr_; }

private:
    enum class PendingVisibilityAction : uint8_t
    {
        Visible,
        Hidden
    };

    struct PendingVisibilityEvent
    {
        WoWGuid viewer;
        WoWGuid object;
        PendingVisibilityAction action = PendingVisibilityAction::Visible;
        uint64_t sequence = 0;
    };

    void queueVisibilityChange(const WoWGuid& viewer, const WoWGuid& object, PendingVisibilityAction action);
    bool applyQueuedVisibilityVisible(const WoWGuid& viewer, const WoWGuid& object);
    bool applyQueuedVisibilityHidden(const WoWGuid& viewer, const WoWGuid& object);
    bool shouldSuppressInitialValueUpdateForViewer(uint64_t viewerRaw, uint64_t objectRaw) const;

    std::vector<Object*> deferred_destroy_;

    std::deque<PendingVisibilityEvent> pendingVisibilityEvents_;
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint64_t>> pendingVisibilityLatestSeq_;
    uint64_t pendingVisibilitySequence_ = 0;

    // Objects created for a viewer in the current visibility flush already include
    // their complete initial value state in the create block. If the object also
    // has a pending value update from before the viewer saw it, suppress that
    // one values update for this new viewer to avoid replaying GO animations.
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> initialCreateValueUpdateSuppress_;

protected:
    // Spatial/Visibility Systems
    std::unique_ptr<visibility::SpatialIndex> spatialIndex_;
    std::unique_ptr<visibility::VisibilitySystem> visibilitySystem_;
    std::unique_ptr<ObjectFactory>             factory_;
    std::unique_ptr<GuidAllocator>             guids_;
    std::unique_ptr<world::WorldObjectRegistry> registry_;
    std::unique_ptr<SpawnManager>               spawnMgr_;

    //////////////////////////////////////////////////////////////////////////////////////////////////////
    // Object Registry
    //////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    Object* getObject2(const WoWGuid& g) { return registry_ ? registry_->getAny(g) : nullptr; }
    Creature* getCreature2(const WoWGuid& g) { return registry_ ? registry_->getCreature(g) : nullptr; }
    GameObject* getGameObject2(const WoWGuid& g) { return registry_ ? registry_->getGameObject(g) : nullptr; }
    DynamicObject* getDynamicObject2(const WoWGuid& g) { return registry_ ? registry_->getDynamicObject(g) : nullptr; }
    Player* getPlayer2(const WoWGuid& g) { return registry_ ? registry_->getPlayer(g) : nullptr; }
    Player* getPlayer2(const uint32_t g) { return registry_ ? registry_->getPlayer(g) : nullptr; }
    Pet* getPet2(const WoWGuid& g) { return registry_ ? registry_->getPet(g) : nullptr; }
    Corpse* getCorpse2(const WoWGuid& g) { return registry_ ? registry_->getCorpse(g) : nullptr; }
    Unit* getUnit2(const WoWGuid& g);

    //////////////////////////////////////////////////////////////////////////////////////////////////////
    // Navigation System
    //////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    void navAcquireGrid(int gid);
    void navReleaseGrid(int gid);

private:
    std::mutex nav_mtx_;
    std::unordered_map<int, uint32_t> nav_gridRefs_;

    //////////////////////////////////////////////////////////////////////////////////////////////////////

public:
    bool canUnload(uint32_t diff);

    virtual bool addPlayerToMap(Player*) { return true; }
    virtual void removePlayerFromMap(Player*) {};

    virtual EnterState cannotEnter(Player* /*player*/) { return EnterState::CAN_ENTER; }

    // Difficulty
    InstanceDifficulty::Difficulties getDifficulty() const { return InstanceDifficulty::Difficulties(getSpawnMode()); }
    bool isRegularDifficulty();
    WDB::Structures::MapDifficulty const* getMapDifficulty();

    // Area and Zone Management
    bool getAreaInfo(uint32_t phaseMask, LocationVector pos, uint32_t& mogpflags, int32_t& adtId, int32_t& rootId, int32_t& groupId);
    uint32_t getAreaId(uint32_t phaseMask, LocationVector const& pos);
    uint32_t getZoneId(uint32_t phaseMask, LocationVector const& pos);
    void getZoneAndAreaId(uint32_t phaseMask, uint32_t& zoneid, uint32_t& areaid, LocationVector const& pos);

    void getFullTerrainStatusForPosition(uint32_t phaseId, float x, float y, float z, PositionFullTerrainStatus& data, uint8_t reqLiquidType, float collisionHeight) const;

    // Water
    ZLiquidStatus getLiquidStatus(uint32_t phaseMask, LocationVector pos, uint8_t ReqLiquidType, LiquidData* data = nullptr, float collisionHeight = 2.03128f);
    float getWaterLevel(float x, float y);
    bool isInWater(uint32_t phaseMask, LocationVector pos, LiquidData* data = nullptr);
    bool isUnderWater(uint32_t phaseMask, LocationVector pos);

    // Line of Sight
    bool isInLineOfSight(LocationVector pos1, LocationVector pos2, uint32_t phasemask, LineOfSightChecks checks);
    bool getObjectHitPos(uint32_t phasemask, LocationVector pos1, LocationVector pos2, float& rx, float& ry, float& rz, float modifyDist);

    // Dynamic Map
    DynamicMapTree const& getDynamicTree() const { return _dynamicTree; }
    void balance() { _dynamicTree.balance(); }
    void removeGameObjectModel(GameObjectModel const& model) { _dynamicTree.remove(model); }
    void insertGameObjectModel(GameObjectModel const& model) { _dynamicTree.insert(model); }
    bool containsGameObjectModel(GameObjectModel const& model) const { return _dynamicTree.contains(model); }
    float getGameObjectFloor(uint32_t phasemask, LocationVector pos, float maxSearchDist = 50.0f) const;

    // Terrain
    TerrainHolder* getTerrain() const { return _terrain.get(); }
    float getWaterOrGroundLevel(uint32_t phasemask, LocationVector const& pos, float* ground = nullptr, bool swim = false, float collisionHeight = 2.03128f);
    float getGridHeight(float x, float y) const;
    float getHeight(LocationVector const& pos, bool vmap = true, float maxSearchDist = 50.0f) const;
    // phasemask seems to be invalid when loading into a map                                                                                                                                                // phase
    float getHeight(uint32_t phasemask, LocationVector const& pos, bool vmap = true, float maxSearchDist = 50.0f) const;

    // Instance
    uint32_t getInstanceId() const { return _instanceId; }
    void setInstanceId(uint32_t instanceId) { _instanceId = instanceId; }
    uint8_t getSpawnMode() const { return (_instanceSpawnMode); }
    void setSpawnMode(uint8_t mode) { _instanceSpawnMode = mode; }
    bool isRaidOrHeroicDungeon();
    bool isHeroic();
    bool is25ManRaid();

    // Player
    virtual void removeAllPlayers();

    // Creatures
    std::unordered_map<uint32_t /*leaderSpawnId*/, std::unique_ptr<CreatureGroup>> CreatureGroupHolder;

    // Summons
    Summon* summonCreature(uint32_t entry, LocationVector pos, WDB::Structures::SummonPropertiesEntry const* = nullptr, uint32_t duration = 0, Object* summoner = nullptr, uint32_t spellId = 0);
    GameObject* summonGameObject(uint32_t entryID, LocationVector pos, QuaternionData const& rot, uint32_t duration = 0, Object* summoner = nullptr);

    // Base Template
    BaseMap* getBaseMap() const { return m_baseMap; }

    bool cellHasAreaID(uint32_t x, uint32_t y, uint16_t& AreaID);
   
    void changeFarsightLocation(Player* plr, DynamicObject* farsight);

    // Packts
    void sendChatMessageToCellPlayers(Object* obj, WorldPacket* packet, uint32_t cell_radius, uint32_t langpos, int32_t lang, WorldSession* originator);
    void sendPvPCaptureMessage(int32_t ZoneMask, uint32_t ZoneId, const char* Message, ...);
    void sendPacketToAllPlayers(WorldPacket* packet) const;
    void sendPacketToPlayersInZone(uint32_t zone, WorldPacket* packet) const;

    EventableObjectHolder eventHolder;

    void respawnBossLinkedGroups(uint32_t bossId);

    // Update Timers
    std::chrono::milliseconds m_DebugAccum{ 0 };
    std::chrono::milliseconds m_visibilityAccum{ 0 };
    std::chrono::milliseconds m_respawnAccum{ 0 };
    std::chrono::milliseconds m_sessionAccum{ 0 };
    std::chrono::milliseconds m_dynamicObjectAccum{ 0 };
    std::chrono::milliseconds m_transporterAccum{ 0 };
    std::chrono::milliseconds m_gameObjectAccum{ 0 };
    std::chrono::milliseconds m_playersAccum{ 0 };
    std::chrono::milliseconds m_petsAccum{ 0 };
    std::chrono::milliseconds m_creaturesAccum{ 0 };
    uint32_t m_lastUpdateTime = 0;

    // Worldstates
    WorldStatesHandler& getWorldStatesHandler();
    void onWorldStateUpdate(uint32_t zone, uint32_t field, uint32_t value) override;

    // Update System
    std::mutex m_updateMutex;
    UpdateQueue _updates;
    PUpdateQueue _processQueue;
    void updateObjects();
    void pushToProcessed(Player* plr);
    // Mark object as updated
    void objectUpdated(Object* obj);

    // Combat Progress
    CombatProgressMap _combatProgress;
    bool isCombatInProgress();
    void addCombatInProgress(uint64_t guid);
    void removeCombatInProgress(uint64_t guid);

    // Script related
    InstanceScript* getScript();
    void loadInstanceScript();
    void callScriptUpdate();

    MapScriptInterface* getInterface();
    InstanceMap* getInstance() { return pInstance; }

private:
    std::unique_ptr<AscEmu::Threading::AEThread> m_thread;
    bool m_threadRunning = false;
    bool m_terminateThread = false;

    WorldStatesHandler worldstateshandler;
    std::unique_ptr<MapScriptInterface> ScriptInterface;
    bool m_unloadPending = false;

    std::unique_ptr<TerrainHolder> _terrain;
    uint32_t _instanceId;
    uint8_t _instanceSpawnMode = InstanceDifficulty::Difficulties::DUNGEON_NORMAL;

    // Sessions
    std::set<WorldSession*> Sessions;

protected:
    InstanceScript* mInstanceScript = nullptr;
    DynamicMapTree _dynamicTree;
    uint32_t m_unloadTimer = 0;
    float m_VisibleDistance;
    BaseMap* m_baseMap = nullptr;
    InstanceMap* pInstance = nullptr;
};
