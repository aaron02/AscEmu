/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "WorldMap.hpp"
#include "Objects/DynamicObject.hpp"
#include "Objects/Units/Creatures/CreatureGroups.h"
#include "Objects/Units/Creatures/Pet.h"
#include "Objects/Units/Creatures/Summons/Summon.hpp"
#include "Objects/Units/Unit.hpp"
#include "VMapFactory.h"
#include "MMapFactory.h"
#include "Macros/MapsMacros.hpp"
#include "shared/WoWGuid.hpp"
#include "MapScriptInterface.h"
#include "Server/Script/ScriptMgr.hpp"
#include "Map/Management/MapMgr.hpp"
#include "InstanceMap.hpp"
#include "Server/Packets/SmsgUpdateWorldState.h"
#include "Server/Packets/SmsgDefenseMessage.h"
#include "Map/Area/AreaManagementGlobals.hpp"
#include "Map/Area/AreaStorage.hpp"
#include "Debugging/CrashHandler.h"
#include "Objects/Transporter.hpp"
#include "Objects/Units/Creatures/Summons/SummonDefines.hpp"
#include "Server/DatabaseDefinition.hpp"
#include "Server/World.h"
#include "Server/WorldSession.h"
#include "Spell/Definitions/SummonControlTypes.hpp"
#include "Storage/MySQLDataStore.hpp"
#include "Storage/WDB/WDBStores.hpp"

#include <ctime>
#include <cstdarg>
#include <set>

#include "Logging/Logger.hpp"
#include "Management/ObjectMgr.hpp"
#include "Objects/Units/Creatures/Corpse.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Server/Script/InstanceScript.hpp"
#include "Objects/Item.hpp"
#include "Server/EventMgr.h"
#include "Storage/WDB/WDBStructures.hpp"

using namespace AscEmu::Packets;
using namespace AscEmu::Threading;
using namespace visibility;

extern bool bServerShutdown;

WorldMap::WorldMap(BaseMap* baseMap, uint32_t id, uint32_t expiry, uint32_t InstanceId, uint8_t SpawnMode) : eventHolder(InstanceId), worldstateshandler(id),
    _terrain(std::make_unique<TerrainHolder>(id)), m_unloadTimer(expiry), m_baseMap(baseMap)
{
    // Map
    setSpawnMode(SpawnMode);
    setInstanceId(InstanceId);

    m_holder = &eventHolder;
    m_event_Instanceid = eventHolder.GetInstanceID();

    // Thread
    const std::string threadName("WorldMap - M" + std::to_string(getBaseMap()->getMapId()) + "|I" + std::to_string(getInstanceId()));
    m_thread = std::make_unique<AEThread>(threadName, [this](AEThread& /*thread*/) { this->runThread(); }, std::chrono::milliseconds(20), false);

    //lets initialize visibility distance for Continent
    WorldMap::initVisibilityDistance();

    // VisibilitySystem
    Config cfg;
    cfg.defaultViewerRadius = worldConfig.server.mapCellNumber;
    cfg.defaultActivatorRadius = worldConfig.server.mapCellNumber;
    cfg.maxCmdsPerTick = 8192;
    cfg.cellUnloadDelay = std::chrono::minutes(worldConfig.server.mapUnloadTime);

    spatialIndex_ = std::make_unique<visibility::SpatialIndex>();
    visibilitySystem_ = std::make_unique<visibility::VisibilitySystem>(*spatialIndex_, cfg, 65536);
    registry_ = std::make_unique<world::WorldObjectRegistry>();
    guids_ = std::make_unique<GuidAllocator>();

    factory_ = std::make_unique<ObjectFactory>(*this, *visibilitySystem_, *registry_, *guids_);
    spawnMgr_ = std::make_unique<SpawnManager>(*this, *visibilitySystem_, *factory_);

    hookVisibilityEvents();

    // Create script interface
    ScriptInterface = std::make_unique<MapScriptInterface>(*this, *spatialIndex_, *registry_, *factory_, *spawnMgr_);
}

void WorldMap::initialize()
{
    // Create Instance script
    loadInstanceScript();

    // Call script OnLoad virtual procedure
    if (getScript())
        getScript()->OnLoad();

    // load corpses
    sObjectMgr.loadCorpsesForInstance(this);
    worldstateshandler.InitWorldStates(sObjectMgr.getWorldStatesForMap(getBaseMap()->getMapId()));
    worldstateshandler.setObserver(this);
}

WorldMap::~WorldMap()
{
    m_thread->killAndJoin();
    sEventMgr.RemoveEvents(this);

    // Prevents a crash on map shutdown -Appled
    ScriptInterface = nullptr;

    if (mInstanceScript != nullptr)
        mInstanceScript->Destroy();

    _updates.clear();
    _processQueue.clear();
    Sessions.clear();

    MMAP::MMapFactory::createOrGetMMapManager()->unloadMapInstance(getBaseMap()->getMapId(), getInstanceId());

    sLogger.debug("WorldMap : Instance {} shut down. ({})", getInstanceId(), getBaseMap()->getMapName());
}

void WorldMap::startMapThread()
{
    m_terminateThread = false;
    m_lastUpdateTime = Util::getMSTime();
    m_thread->reboot();
}

void WorldMap::runThread()
{
    try
    {
        Do();
    }
    catch (const std::exception& e)
    {
        // Log the standard C++ error so we know why the thread stopped
        sLogger.failure("WorldMap thread stopped due to C++ exception: {}", e.what());
    }
    catch (...)
    {
        // Catch everything else
        sLogger.failure("WorldMap thread stopped due to an unknown C++ exception.");
    }

    return;
}

void WorldMap::shutdownMapThread()
{
    pInstance = nullptr;
    m_terminateThread = true;
}

void WorldMap::unsafeKillMapThread()
{
    m_thread->killAndJoin();
}

bool WorldMap::isMapReadyForDelete() const
{
    return m_thread->isKilled() && m_thread->isDone();
}

void WorldMap::Do()
{
    using clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    m_threadRunning = true;

    auto last = clock::now();

    while (!m_terminateThread)
    {
        auto now = clock::now();

        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last);
        last = now;

        update(static_cast<uint32_t>(diff.count()));
        delayedUpdate(diff);

        std::this_thread::sleep_for(10ms);
    }

    m_threadRunning = false;
    m_thread->requestKill();
}

void WorldMap::update(uint32_t t_diff)
{
    using namespace std::chrono_literals;    

    const auto dt = std::chrono::milliseconds{ t_diff };
    const auto instanceId = getInstanceId();

    // Events
    eventHolder.Update(t_diff);

    // Dynamic
    _dynamicTree.update(t_diff);

    // Visibility
    m_visibilityAccum += dt;
    if (m_visibilityAccum >= 100ms)
    {
        auto step = m_visibilityAccum;
        visibilitySystem_->tick(step);
        m_visibilityAccum = 0ms;
    }

    // Respawns
    m_respawnAccum += dt;
    if (m_respawnAccum >= 1000ms)
    {
        auto step = m_respawnAccum;
        spawnMgr_->processRespawns();
        m_respawnAccum = 0ms;
    }

    // Debug
    m_DebugAccum += dt;
    if (m_DebugAccum >= 10000ms)
    {
        //spawnMgr_->dumpInconsistencies();
        m_DebugAccum = 0ms;
    }

    // Objects
    updateAll(dt);

    // Sessions
    m_sessionAccum += dt;
    if (m_sessionAccum >= 100ms)
    {
        auto step = m_sessionAccum;

        for (auto it = Sessions.begin(); it != Sessions.end(); )
        {
            WorldSession* session = *it;

            if (session->GetInstance() != instanceId)
            {
                it = Sessions.erase(it);
                continue;
            }

            if (Player* p = session->GetPlayer())
            {
                if (auto* wm = p->getWorldMap(); wm && wm != this)
                {
                    ++it;
                    continue;
                }
            }

            const uint8_t result = session->Update(instanceId);
            if (result != 0)
            {
                if (result == 1)
                    sWorld.deleteSession(session);
                it = Sessions.erase(it);
            }
            else
            {
                ++it;
            }
        }

        m_sessionAccum = 0ms;
    }

    // Apply queued visibility create/out-of-range changes before flushing A9 update packets.
    processPendingVisibilityChanges();


    // A9
    updateObjects();

    // Delete Objects
    drainDeferredDestroy();
}

void WorldMap::delayedUpdate(std::chrono::milliseconds diff)
{
    thread_local std::vector<Transporter*> s_trans;

    // Transporters
    registry_->snapshotTransporters(s_trans);
    registry_->forEachPinned(s_trans, [&](Transporter& t)
        {
        t.delayedUpdate(diff.count());
        });
}

void WorldMap::updateAll(std::chrono::milliseconds diff)
{
    using namespace std::chrono_literals;
    using clock = std::chrono::steady_clock;

    thread_local std::vector<Player*>        s_players;
    thread_local std::vector<Creature*>      s_creatures;
    thread_local std::vector<GameObject*>    s_gos;
    thread_local std::vector<Transporter*>   s_trans;
    thread_local std::vector<DynamicObject*> s_dyns;
    thread_local std::vector<Pet*>           s_pets;

    // Transporters:
    registry_->snapshotTransporters(s_trans);
    registry_->forEachPinned(s_trans, [&](Transporter& t)
        {
            const auto last = t.getLastUpdate();
            const auto passed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - last);

            if (passed >= 100ms)
            {
                t.Update(static_cast<uint32_t>(passed.count()));
                t.setLastUpdate(clock::now());
            }
        });

    // DynamicObjects
    m_dynamicObjectAccum += diff;
    if (m_dynamicObjectAccum >= 200ms)
    {
        registry_->snapshotDynamicObjects(s_dyns);
        registry_->forEachPinned(s_dyns, [&](DynamicObject& d)
            {
                d.updateTargets();
            });
        m_dynamicObjectAccum = 0ms;
    }

    // Creatures
    m_creaturesAccum += diff;
    if (m_creaturesAccum >= 100ms)
    {
        registry_->snapshotCreatures(s_creatures);
        registry_->forEachPinned(s_creatures, [&](Creature& c)
            {
                c.Update(static_cast<uint32_t>(m_creaturesAccum.count()));
            });
        m_creaturesAccum = 0ms;
    }

    // GameObjects
    m_gameObjectAccum += diff;
    if (m_gameObjectAccum >= 100ms)
    {
        registry_->snapshotGameObjects(s_gos);
        registry_->forEachPinned(s_gos, [&](GameObject& g)
            {
                g.Update(static_cast<uint32_t>(m_gameObjectAccum.count()));
            });
        m_gameObjectAccum = 0ms;
    }

    // Players
    m_playersAccum += diff;
    if (m_playersAccum >= 50ms)
    {
        registry_->snapshotPlayers(s_players);
        registry_->forEachPinned(s_players, [&](Player& p)
            {
                p.Update(static_cast<uint32_t>(m_playersAccum.count()));
            });
        m_playersAccum = 0ms;
    }

    // Pets
    m_petsAccum += diff;
    if (m_petsAccum >= 100ms)
    {
        registry_->snapshotPets(s_pets);
        registry_->forEachPinned(s_pets, [&](Pet& pet)
            {
                pet.Update(static_cast<uint32_t>(m_petsAccum.count()));
            });
        m_petsAccum = 0ms;
    }
}

void WorldMap::deferDestroy(Object* object)
{
    if (object)
        deferred_destroy_.push_back(object);
}

void WorldMap::drainDeferredDestroy()
{
    for (Object* object : deferred_destroy_)
        delete object;
    deferred_destroy_.clear();
}

std::map<uint32_t, Player*> WorldMap::getPlayers() const
{
    std::vector<Player*> players;
    if (registry_)
        registry_->snapshotPlayers(players);

    std::map<uint32_t, Player*> out;
    for (Player* player : players)
    {
        if (player)
            out.emplace(player->getGuidLow(), player);
    }
    return out;
}

std::vector<Creature*> WorldMap::getCreatures() const
{
    std::vector<Creature*> out;
    if (registry_)
        registry_->snapshotCreatures(out);
    return out;
}

std::vector<GameObject*> WorldMap::getGameObjects() const
{
    std::vector<GameObject*> out;
    if (registry_)
        registry_->snapshotGameObjects(out);
    return out;
}

uint32_t WorldMap::getPlayerCount() const
{
    return registry_ ? static_cast<uint32_t>(registry_->countPlayers()) : 0;
}

Unit* WorldMap::getUnit2(const uint32_t lowGuid) const
{
    if (Player* player = registry_ ? registry_->getPlayer(lowGuid) : nullptr)
        return reinterpret_cast<Unit*>(player);

    if (Creature* creature = registry_->getCreature(lowGuid))
        return reinterpret_cast<Unit*>(creature);

    if (Pet* pet = registry_ ? registry_->getPet(lowGuid) : nullptr)
        return reinterpret_cast<Unit*>(pet);

    return nullptr;
}

Creature* WorldMap::getSqlIdCreature(uint32_t spawnId) const
{
    std::vector<Creature*> creatures;
    if (registry_)
        registry_->snapshotCreatures(creatures);

    for (Creature* creature : creatures)
        if (creature && creature->getSpawnId() == spawnId)
            return creature;
    return nullptr;
}

GameObject* WorldMap::getSqlIdGameObject(uint32_t spawnId) const
{
    std::vector<GameObject*> gameObjects;
    if (registry_)
        registry_->snapshotGameObjects(gameObjects);

    for (GameObject* go : gameObjects)
        if (go && go->getSpawnId() == spawnId)
            return go;
    return nullptr;
}

Unit* WorldMap::getUnit2(const WoWGuid& g)
{
    switch (g.getHigh())
    {
        case HighGuid::Unit:
        case HighGuid::Vehicle:
            return getCreature2(g);
        case HighGuid::Player:
            return getPlayer2(g);
        case HighGuid::Pet:
            return getPet2(g);
    }

    return nullptr;
}

inline int MinGridToNavIdx(int g_min)
{
    return Terrain::TilesCount - 1 - g_min;
}

void WorldMap::navAcquireGrid(int gid)
{
    auto [gx, gy] = unpackGridId(gid);

    if (!worldConfig.terrainCollision.isCollisionEnabled) return;

    std::lock_guard<std::mutex> g(nav_mtx_);
    auto& ref = nav_gridRefs_[gid];
    if (ref++ != 0) return;

    auto* vmap = VMAP::VMapFactory::createOrGetVMapManager();
    auto* mmap = MMAP::MMapFactory::createOrGetMMapManager();

    const int nx = MinGridToNavIdx(gx);
    const int ny = MinGridToNavIdx(gy);

    getTerrain()->loadTile(nx, ny);

    const std::string vpath = worldConfig.server.dataDir + "vmaps";
    const std::string mpath = worldConfig.server.dataDir + "mmaps";
    const int mapId = getBaseMap()->getMapId();

    vmap->loadMap(vpath.c_str(), mapId, nx, ny);
    mmap->loadMap(mpath, mapId, nx, ny);
}

void WorldMap::navReleaseGrid(int gid)
{
    auto [gx, gy] = unpackGridId(gid);

    if (!worldConfig.terrainCollision.isCollisionEnabled) return;

    std::lock_guard<std::mutex> g(nav_mtx_);
    auto it = nav_gridRefs_.find(gid);
    if (it == nav_gridRefs_.end()) return;
    if (--(it->second) != 0) return;

    const int nx = MinGridToNavIdx(gx);
    const int ny = MinGridToNavIdx(gy);

    getTerrain()->unloadTile(nx, ny);

    auto* vmap = VMAP::VMapFactory::createOrGetVMapManager();
    auto* mmap = MMAP::MMapFactory::createOrGetMMapManager();
    const int mapId = getBaseMap()->getMapId();

    vmap->unloadMap(mapId, nx, ny);
    mmap->unloadMap(mapId, nx, ny);

    nav_gridRefs_.erase(it);
}

bool WorldMap::canUnload(uint32_t diff)
{
    if (!m_unloadTimer)
        return false;

    if (m_unloadTimer <= diff)
        return true;

    m_unloadTimer -= diff;
    return false;
}

void WorldMap::unloadAll(bool onShutdown/* = false*/)
{
    if (registry_->countPlayers())
        return;

    if (onShutdown)
        return;

    if (getInstanceId() == 0)
        sMapMgr.addMapToRemovePool(this);
    else
        sMapMgr.removeInstance(getInstanceId());
}

void WorldMap::initVisibilityDistance()
{
    //init visibility for continents
    m_VisibleDistance = (visibility::Cell::Size * worldConfig.server.mapCellNumber) * (visibility::Cell::Size * worldConfig.server.mapCellNumber);
}

void WorldMap::outOfMapBoundariesTeleport(Object* object)
{
    if (object->isPlayer())
    {
        Player* player = static_cast<Player*>(object);

        if (player->getBindMapId() != getBaseMap()->getMapId())
        {
            player->safeTeleport(player->getBindMapId(), 0, player->getBindPosition());
            player->getSession()->SystemMessage("Teleported you to your hearthstone location as you were out of the map boundaries.");
        }
        else
        {
            object->GetPositionV()->ChangeCoords(player->getBindPosition());
            player->getSession()->SystemMessage("Teleported you to your hearthstone location as you were out of the map boundaries.");
            player->sendTeleportAckPacket(player->getBindPosition());
        }
    }
    else
    {
        object->GetPositionV()->ChangeCoords({ 0, 0, 0, 0 });
    }
}

void WorldMap::hookVisibilityEvents()
{
    visibilitySystem_->onGridActivated([this](int gid)
        {
            sLogger.debugFlag(AscEmu::Logging::LF_MAP_CELL, "Grid Activated {} ", gid);

            // Navigation
            navAcquireGrid(gid);
            
            // Spawns
            spawnMgr_->onGridActivated(gid);
        });

    visibilitySystem_->onGridDeactivated([this](int gid)
        {
            sLogger.debugFlag(AscEmu::Logging::LF_MAP_CELL, "Grid Deactivated {}", gid);
        });

    visibilitySystem_->onGridUnload([this](int gid)
        {
            sLogger.debugFlag(AscEmu::Logging::LF_MAP_CELL, "Grid Unloaded {} ", gid);

            // Spawns
            spawnMgr_->onGridUnload(gid);

            // Navigation
            navReleaseGrid(gid);
        });

    visibilitySystem_->onGridChanged([this](const WoWGuid& g, int oldGid, int newGid)
        {
            spawnMgr_->onGridChanged(g, oldGid, newGid);
        });

    // A9 Packets...
    visibilitySystem_->onBecameVisible([this](WoWGuid viewer, WoWGuid object)
        {
            onObjectBecameVisible(viewer, object);
        });

    visibilitySystem_->onBecameHidden([this](WoWGuid viewer, WoWGuid object)
        {
            onObjectBecameHidden(viewer, object);
        });
}

void WorldMap::onPlayerEnter(Player* plr)
{
    if (!plr) 
        return;

    // Add the session to our set
    Sessions.insert(plr->getSession());

    // Change the instance ID, this will cause it to be removed from the world thread (return value 1)
    plr->getSession()->SetInstance(getInstanceId());

    factory_->attachToWorld(plr);

    auto p = plr->GetPosition();
    auto [gx, gy] = visibility::worldToGrid(p);
    auto [cx, cy] = visibility::worldToLocal(p);
    sLogger.info("Player entered World at pos=({:.1f},{:.1f}) grid=({},{}) cell=({},{})", p.x, p.y, gx, gy, cx, cy);
}

void WorldMap::onPlayerLeave(Player* plr)
{
    if (!plr)
        return;

    factory_->detachFromWorld(plr);
    factory_->recycleAndDestroy(plr, true, true);

    sLogger.info("Player left World");
}

void WorldMap::onObjectMoved(Object* obj)
{
    // Ignore if no object or visibility system is missing
    if (!obj || !visibilitySystem_) 
        return;

    // Skip if the object belongs to a different WorldMap instance
    if (obj->getWorldMap() != this)
        return;
    
    // Out of Bounds Correct Position
    if (!std::isfinite(obj->GetPositionX()) || !std::isfinite(obj->GetPositionY()) ||
        obj->GetPositionX() < Terrain::MinX || obj->GetPositionX() > Terrain::MaxX ||
        obj->GetPositionY() < Terrain::MinY || obj->GetPositionY() > Terrain::MaxY)
    {
        outOfMapBoundariesTeleport(obj);
    }

    // Update Gameobject Collision
    if (obj->isGameObject())
        obj->ToGameObject()->updateModelPosition();

    // Retrieve the visibility-system handle for this object
    auto h = spatialIndex_->handleByGuid(obj->GetNewGUID());
    if (!h.id)
        return;

    const LocationVector now = obj->GetPosition();

    // Always update the spatial index. This keeps slot position, cell membership
    // and the object's own near-cache correct for scripts, spells, AI and area
    // helpers. Expensive visibility/interest refreshes are throttled inside
    // VisibilitySystem::onObjectMoved().
    auto move = spatialIndex_->moveObject(h, now);
    visibilitySystem_->onObjectMoved(h, move);
}

void WorldMap::refreshVisibilityForObject(Object* obj)
{
    if (!obj || !visibilitySystem_)
        return;

    if (obj->getWorldMap() != this)
        return;

    auto h = spatialIndex_->handleByGuid(obj->GetNewGUID());
    if (!h.id)
        return;

    // Apply pending role/subscription changes before refreshing state.
    visibilitySystem_->drain();

    // Keep default interest roles in sync for objects whose role can change while
    // already attached. applyInterestProfile is idempotent for already enabled
    // roles and is the single place that knows object-type defaults.
    auto profile = visibilitySystem_->buildInterestProfile(obj);
    visibilitySystem_->applyInterestProfile(h, profile);
    visibilitySystem_->drain();

    visibilitySystem_->refreshObjectVisibility(h);
    visibilitySystem_->drain();

    if (profile.viewer)
        processPendingVisibilityChangesForViewer(obj->GetNewGUID(), 512, 512);
    else
        processPendingVisibilityChanges(2048, 64, 256);
}


void WorldMap::resetVisibilityForPlayerRelocation(Player* player)
{
    if (!player)
        return;

    const WoWGuid viewerGuid = player->GetNewGUID();
    const uint64_t playerRaw = viewerGuid.getRawGuid();

    // Same-map relocation/repop must be a hard client-visibility boundary.
    // Do not only clear Player::visible_: the VisibilitySystem can still have
    // the old player viewer subscribed to the death-location cells. If that
    // stale subscription remains, the old creatures are recreated or stay
    // visible until the player moves enough to trigger normal reconciliation.
    //
    // Also drop queued create/update data from the old location before queuing
    // the destroy list. Player::die()/updateVisibility() can enqueue create
    // blocks for corpse-area creatures immediately before Release Spirit. If
    // those queued creates survive this relocation reset, the outgoing packet
    // can contain: OutOfRange(old NPCs) followed by Create(old NPCs), which
    // makes the client keep the death-location creatures visible at the graveyard.
    player->getUpdateMgr().clearPendingUpdates();

    getVisibilitySystem().drain();

    std::vector<WoWGuid> oldVisible;
    player->collectVisibleObjectGuidsForRelocation(oldVisible);

    if (auto h = getSpatialIndex().handleByGuid(viewerGuid); h.id)
        getVisibilitySystem().resetViewerForRelocation(h, oldVisible);
    else
        getVisibilitySystem().clearVisibleObjectsForViewer(viewerGuid, oldVisible);

    std::set<uint64_t> unique;
    for (const WoWGuid& objectGuid : oldVisible)
    {
        const uint64_t raw = objectGuid.getRawGuid();
        if (!raw || raw == playerRaw)
            continue;

        if (unique.insert(raw).second)
            player->getUpdateMgr().pushOutOfRangeGuid(objectGuid);
    }

    player->clearVisibleObjectCachesForRelocation();

    initialCreateValueUpdateSuppress_.erase(playerRaw);
    purgePendingVisibilityForGuid(viewerGuid);

    // Send the destroy list before new creates for the destination are queued.
    player->processPendingUpdates();
}

void WorldMap::logVisibilityMemoryDiagnostics(const char* reason)
{
    if (!visibilitySystem_ || !registry_)
        return;

    const auto vis = visibilitySystem_->snapshot();
    const auto reg = registry_->counts();

    std::size_t pendingPairs = 0;
    for (const auto& [viewerRaw, objects] : pendingVisibilityLatestSeq_)
        pendingPairs += objects.size();

    std::size_t suppressPairs = 0;
    for (const auto& [viewerRaw, objects] : initialCreateValueUpdateSuppress_)
        suppressPairs += objects.size();

    sLogger.warning(
        "mapmem: reason={} map={} inst={} registry[any={},total={},players={},creatures={},gos={},dyn={},pets={},corpses={}] "
        "pending[events={},viewers={},pairs={},seq={}] suppress[viewers={},pairs={}] deferredDestroy={} "
        "vis[grids={},activeGrids={},cells={},activeCells={},poolLive={},guidIndex={},visiblePairs={},seenPairs={},gridWidePub={},cellRadiusPub={},nearCached={},nearGuidCap={},nearStampCap={}]",
        reason ? reason : "",
        getBaseMap() ? getBaseMap()->getMapId() : 0,
        getInstanceId(),
        reg.any,
        reg.total,
        reg.players,
        reg.creatures,
        reg.gameobjects,
        reg.dynamics,
        reg.pets,
        reg.corpses,
        pendingVisibilityEvents_.size(),
        pendingVisibilityLatestSeq_.size(),
        pendingPairs,
        pendingVisibilitySequence_,
        initialCreateValueUpdateSuppress_.size(),
        suppressPairs,
        deferred_destroy_.size(),
        vis.grids,
        vis.activeGrids,
        vis.cells,
        vis.activeCells,
        vis.poolLive,
        vis.guidIndex,
        vis.visiblePairs,
        vis.seenPairs,
        vis.gridWidePublishers,
        vis.cellRadiusPublishers,
        vis.nearCacheCachedGuids,
        vis.nearCacheGuidsCapacity,
        vis.nearCacheStampsCapacity);

    visibilitySystem_->logMemoryDiagnostics(reason);
}

void WorldMap::queueVisibilityChange(const WoWGuid& viewer, const WoWGuid& object, PendingVisibilityAction action)
{
    const uint64_t viewerRaw = viewer.getRawGuid();
    const uint64_t objectRaw = object.getRawGuid();

    if (!viewerRaw || !objectRaw)
        return;

    const uint64_t seq = ++pendingVisibilitySequence_;
    pendingVisibilityLatestSeq_[viewerRaw][objectRaw] = seq;
    pendingVisibilityEvents_.push_back({ viewer, object, action, seq });
}

void WorldMap::purgePendingVisibilityForGuid(const WoWGuid& guid)
{
    const uint64_t raw = guid.getRawGuid();
    if (!raw)
        return;

    // This function is called during detach. It must only purge state where the
    // removed guid acts as a viewer. Do not erase entries where the guid is the
    // object, because Hidden events queued by VisibilitySystem::onObjectRemoving()
    // still need their latest-state entries to reach the client.
    pendingVisibilityLatestSeq_.erase(raw);
    initialCreateValueUpdateSuppress_.erase(raw);
}

Player* WorldMap::getVisibilityRecipientPlayer(const WoWGuid& viewerGuid)
{
    if (Player* player = getPlayerByGuid(viewerGuid))
        return player;

    Object* viewerObject = getObjectByGuid(viewerGuid);
    if (!viewerObject)
        return nullptr;

    Unit* viewerUnit = viewerObject->ToUnit();
    if (!viewerUnit)
        return nullptr;

    Player* controller = viewerUnit->m_playerControler;
    if (!controller || !controller->IsInWorld() || controller->getWorldMap() != this)
        return nullptr;

    return controller;
}

bool WorldMap::hasOtherVisibilitySourceForRecipient(const WoWGuid& losingViewerGuid, const WoWGuid& objectGuid, Player* recipient)
{
    if (!recipient)
        return false;

    thread_local std::vector<WoWGuid> s_viewers;
    s_viewers.clear();

    getVisibilitySystem().collectViewersOf(objectGuid, s_viewers);

    const uint64_t losingRaw = losingViewerGuid.getRawGuid();
    for (const WoWGuid& otherViewerGuid : s_viewers)
    {
        if (otherViewerGuid.getRawGuid() == losingRaw)
            continue;

        if (getVisibilityRecipientPlayer(otherViewerGuid) == recipient)
            return true;
    }

    return false;
}

void WorldMap::clearVisibilitySourceForRecipient(const WoWGuid& viewerGuid, Player* recipient)
{
    if (!recipient)
        return;

    thread_local std::vector<WoWGuid> s_objects;
    s_objects.clear();

    getVisibilitySystem().collectVisibleObjectsForViewer(viewerGuid, s_objects);

    const uint64_t recipientRaw = recipient->GetNewGUID().getRawGuid();

    for (const WoWGuid& objectGuid : s_objects)
    {
        if (!recipient->seesGuid(objectGuid))
            continue;

        // Keep objects that the same client still sees through another viewer source,
        // for example the player's own body after possession ends near the body.
        if (hasOtherVisibilitySourceForRecipient(viewerGuid, objectGuid, recipient))
            continue;

        // Important: this helper is used by Unit::unPossess() while the
        // possessor still has charm/farsight state pointing at the controlled unit.
        // Do not run legacy onRemoveInRangeObject side effects here: if objectGuid
        // is the possessed unit, Player/Unit cleanup can interrupt the possess aura
        // and re-enter unPossess while it is already executing. Normal visibility
        // hidden events still run these callbacks through applyQueuedVisibilityHidden().
        // Here we only remove the client-side visibility introduced by the temporary
        // viewer source and queue the matching OutOfRange update.
        recipient->_visRemove(objectGuid);

        if (auto it = initialCreateValueUpdateSuppress_.find(recipientRaw); it != initialCreateValueUpdateSuppress_.end())
        {
            it->second.erase(objectGuid.getRawGuid());
            if (it->second.empty())
                initialCreateValueUpdateSuppress_.erase(it);
        }

        recipient->getUpdateMgr().pushOutOfRangeGuid(objectGuid);
    }
}


bool WorldMap::applyQueuedVisibilityVisible(const WoWGuid& viewerGuid, const WoWGuid& objectGuid)
{
    Player* player = getVisibilityRecipientPlayer(viewerGuid);
    if (!player)
        return false;

    Object* object = getObjectByGuid(objectGuid);
    if (!object)
        return false;

    // If the object already left the recipient player's visibility again while queued, do not create it.
    // This also deduplicates cases where the player sees the same object through both their body and
    // a possessed/farsight-controlled unit.
    if (player->seesGuid(objectGuid))
        return true;

    player->_visAdd(objectGuid);

    object->prepareInitialCreateForPlayer(player);

    ByteBuffer buf(2500);
    const uint32_t cnt = object->buildCreateUpdateBlockForPlayer(&buf, player);
    if (cnt)
        player->getUpdateMgr().pushCreationData(&buf, cnt);

    object->queueInitialVisiblePacketsForPlayer(player);

    initialCreateValueUpdateSuppress_[player->GetNewGUID().getRawGuid()].insert(objectGuid.getRawGuid());

    // The create block already contains the current values for this recipient.
    // Scheduling a normal value update here is redundant and can replay
    // GameObject state/animation updates immediately after spawn.
    return true;
}

bool WorldMap::applyQueuedVisibilityHidden(const WoWGuid& viewerGuid, const WoWGuid& objectGuid)
{
    Player* player = getVisibilityRecipientPlayer(viewerGuid);
    if (!player)
        return false;

    if (!player->seesGuid(objectGuid))
        return true;

    // Do not destroy the object on the client if the same player still sees it
    // through another visibility source, e.g. both their own body and a possessed NPC.
    if (hasOtherVisibilitySourceForRecipient(viewerGuid, objectGuid, player))
        return true;

    // Bridge old in-range removal side effects into the new visibility system.
    // Important: only run these callbacks for the player's own normal viewer.
    // A possessed NPC is a temporary viewer whose packet recipient is the
    // possessor player. When that temporary viewer is removed during unPossess(),
    // running Player::onRemoveInRangeObject(possessedNpc) can interrupt the
    // possess aura and re-enter unPossess(), causing recursive cleanup/crashes.
    // The player's real viewer source will still run normal gameplay side effects.
    const bool normalPlayerViewer = viewerGuid.getRawGuid() == player->GetNewGUID().getRawGuid();
    if (normalPlayerViewer)
    {
        if (Object* object = getObjectByGuid(objectGuid))
        {
            player->onRemoveInRangeObject(object);
            object->onRemoveInRangeObject(player);
        }
    }

    player->_visRemove(objectGuid);
    if (auto it = initialCreateValueUpdateSuppress_.find(player->GetNewGUID().getRawGuid()); it != initialCreateValueUpdateSuppress_.end())
    {
        it->second.erase(objectGuid.getRawGuid());
        if (it->second.empty())
            initialCreateValueUpdateSuppress_.erase(it);
    }

    // Normal visibility leave should be batched through SMSG_UPDATE_OBJECT
    // out-of-range GUIDs, not sent as many individual destroy packets.
    player->getUpdateMgr().pushOutOfRangeGuid(objectGuid);

    return true;
}


bool WorldMap::shouldSuppressInitialValueUpdateForViewer(uint64_t viewerRaw, uint64_t objectRaw) const
{
    auto viewerIt = initialCreateValueUpdateSuppress_.find(viewerRaw);
    if (viewerIt == initialCreateValueUpdateSuppress_.end())
        return false;

    return viewerIt->second.find(objectRaw) != viewerIt->second.end();
}

void WorldMap::processPendingVisibilityChanges(std::size_t maxEvents, std::size_t maxCreatesPerPlayer, std::size_t maxDestroysPerPlayer)
{
    if (pendingVisibilityEvents_.empty())
        return;

    std::unordered_map<uint64_t, std::size_t> createsByViewer;
    std::unordered_map<uint64_t, std::size_t> destroysByViewer;

    std::size_t processed = 0;
    std::size_t deferred = 0;
    std::size_t scanned = 0;
    const std::size_t initialCount = pendingVisibilityEvents_.size();

    while (!pendingVisibilityEvents_.empty() && processed < maxEvents && scanned < initialCount)
    {
        PendingVisibilityEvent event = pendingVisibilityEvents_.front();
        pendingVisibilityEvents_.pop_front();
        ++scanned;

        const uint64_t viewerRaw = event.viewer.getRawGuid();
        const uint64_t objectRaw = event.object.getRawGuid();

        auto viewerIt = pendingVisibilityLatestSeq_.find(viewerRaw);
        if (viewerIt == pendingVisibilityLatestSeq_.end())
            continue;

        auto objIt = viewerIt->second.find(objectRaw);
        if (objIt == viewerIt->second.end() || objIt->second != event.sequence)
            continue;

        const bool isCreate = event.action == PendingVisibilityAction::Visible;
        auto& budgetMap = isCreate ? createsByViewer : destroysByViewer;
        const std::size_t budget = isCreate ? maxCreatesPerPlayer : maxDestroysPerPlayer;

        if (budgetMap[viewerRaw] >= budget)
        {
            // Keep the latest final state for the next tick.
            const uint64_t seq = ++pendingVisibilitySequence_;
            objIt->second = seq;
            event.sequence = seq;
            pendingVisibilityEvents_.push_back(event);
            ++deferred;

            // Avoid spinning on the same over-budget viewer forever in this call.
            if (deferred >= initialCount)
                break;
            continue;
        }

        const bool applied = isCreate
            ? applyQueuedVisibilityVisible(event.viewer, event.object)
            : applyQueuedVisibilityHidden(event.viewer, event.object);

        if (applied)
        {
            ++budgetMap[viewerRaw];
            ++processed;
        }

        viewerIt = pendingVisibilityLatestSeq_.find(viewerRaw);
        if (viewerIt != pendingVisibilityLatestSeq_.end())
        {
            objIt = viewerIt->second.find(objectRaw);
            if (objIt != viewerIt->second.end() && objIt->second == event.sequence)
                viewerIt->second.erase(objIt);

            if (viewerIt->second.empty())
                pendingVisibilityLatestSeq_.erase(viewerIt);
        }
    }
}

void WorldMap::processPendingVisibilityChangesForViewer(const WoWGuid& viewer, std::size_t maxCreates, std::size_t maxDestroys)
{
    if (pendingVisibilityEvents_.empty())
        return;

    const uint64_t wantedViewerRaw = viewer.getRawGuid();
    std::size_t creates = 0;
    std::size_t destroys = 0;
    const std::size_t initialCount = pendingVisibilityEvents_.size();

    for (std::size_t i = 0; i < initialCount && !pendingVisibilityEvents_.empty(); ++i)
    {
        PendingVisibilityEvent event = pendingVisibilityEvents_.front();
        pendingVisibilityEvents_.pop_front();

        const uint64_t viewerRaw = event.viewer.getRawGuid();
        const uint64_t objectRaw = event.object.getRawGuid();

        if (viewerRaw != wantedViewerRaw)
        {
            pendingVisibilityEvents_.push_back(event);
            continue;
        }

        auto viewerIt = pendingVisibilityLatestSeq_.find(viewerRaw);
        if (viewerIt == pendingVisibilityLatestSeq_.end())
            continue;

        auto objIt = viewerIt->second.find(objectRaw);
        if (objIt == viewerIt->second.end() || objIt->second != event.sequence)
            continue;

        const bool isCreate = event.action == PendingVisibilityAction::Visible;
        if ((isCreate && creates >= maxCreates) || (!isCreate && destroys >= maxDestroys))
        {
            pendingVisibilityEvents_.push_back(event);
            continue;
        }

        const bool applied = isCreate
            ? applyQueuedVisibilityVisible(event.viewer, event.object)
            : applyQueuedVisibilityHidden(event.viewer, event.object);

        if (applied)
        {
            if (isCreate)
                ++creates;
            else
                ++destroys;
        }

        // Consume latest-state entry even if apply failed because the object/viewer
        // was already gone. Otherwise stale pending state can accumulate.
        viewerIt = pendingVisibilityLatestSeq_.find(viewerRaw);
        if (viewerIt != pendingVisibilityLatestSeq_.end())
        {
            objIt = viewerIt->second.find(objectRaw);
            if (objIt != viewerIt->second.end() && objIt->second == event.sequence)
                viewerIt->second.erase(objIt);

            if (viewerIt->second.empty())
                pendingVisibilityLatestSeq_.erase(viewerIt);
        }
    }
}

void WorldMap::onObjectBecameVisible(const WoWGuid& viewer, const WoWGuid& obj)
{
    queueVisibilityChange(viewer, obj, PendingVisibilityAction::Visible);
}

void WorldMap::onObjectBecameHidden(const WoWGuid& viewer, const WoWGuid& obj)
{
    queueVisibilityChange(viewer, obj, PendingVisibilityAction::Hidden);
}

Player* WorldMap::getPlayerByGuid(const WoWGuid& g)
{
    Object* o = getObjectByGuid(g);
    return (o && o->isPlayer()) ? static_cast<Player*>(o) : nullptr;
}

Object* WorldMap::getObjectByGuid(const WoWGuid& g)
{
    return registry_->getAny(g);
}

inline int homeGridFromWorld(float x, float y) 
{
    LocationVector p{ x, y, 0.f, 0.f };
    auto [gx, gy] = visibility::worldToGrid(p);
    return visibility::packGridId(gx, gy);
}

void WorldMap::removeAllPlayers()
{
    thread_local std::vector<Player*> s_players;
    registry_->snapshotPlayers(s_players);

    thread_local std::vector<WoWGuid> s_playerGuids;
    s_playerGuids.clear();
    s_playerGuids.reserve(s_players.size());
    for (Player* p : s_players)
    {
        if (p && p->getWorldMap() == this)
            s_playerGuids.push_back(p->GetNewGUID());
    }

    for (const WoWGuid& g : s_playerGuids)
    {
        Player* player = registry_->getPlayer(g);
        if (!player || player->getWorldMap() != this)
            continue;

        if (WorldSession* session = player->getSession())
        {
            session->LogoutPlayer(false);
        }
        else
        {
            factory_->detachFromWorld(player);
            factory_->recycleAndDestroy(player, true, true);
        }
    }

    visibilitySystem_->drain();
    drainDeferredDestroy();
}

bool WorldMap::cellHasAreaID(uint32_t CellX, uint32_t CellY, uint16_t& AreaID)
{
    int32_t TileX = CellX / 8;
    int32_t TileY = CellY / 8;

    if (!getTerrain()->areTilesValid(TileX, TileY))
        return false;

    int32_t OffsetTileX = TileX - getTerrain()->TileStartX;
    int32_t OffsetTileY = TileY - getTerrain()->TileStartY;

    bool Required = false;
    bool Result = false;

    if (!getTerrain()->tileLoaded(OffsetTileX, OffsetTileY))
        Required = true;

    if (Required)
    {
        getTerrain()->loadTile(TileX, TileY);
        getTerrain()->loadTile(TileX, TileY);
        return Result;
    }

    for (uint32_t xc = (CellX % Cell::CellsPerTile) * 16 / Cell::CellsPerTile; xc < (CellX % Cell::CellsPerTile) * 16 / Cell::CellsPerTile + 16 / Cell::CellsPerTile; xc++)
    {
        for (uint32_t yc = (CellY % Cell::CellsPerTile) * 16 / Cell::CellsPerTile; yc < (CellY % Cell::CellsPerTile) * 16 / Cell::CellsPerTile + 16 / Cell::CellsPerTile; yc++)
        {
            const auto areaid = getTerrain()->getTile(OffsetTileX, OffsetTileY)->m_map.m_areaMap[yc * 16 + xc];
            if (areaid)
            {
                AreaID = areaid;
                Result = true;
                break;
            }
        }
    }

    if (Required)
        getTerrain()->unloadTile(TileX, TileY);

    return Result;
}

void WorldMap::changeFarsightLocation(Player* plr, DynamicObject* farsight)
{
    if (!plr)
        return;

    const float visRange = getVisibilityRange();
    auto& farSet = plr->m_visibleFarsightObjects;

    // remove farsight
    if (farsight == nullptr)
    {
        for (auto it = farSet.begin(); it != farSet.end(); ++it)
        {
            Object* obj = *it;
            if (!obj)
                continue;

            if (plr->seesGuid(obj->GetNewGUID()) && !plr->canSee(obj))
                plr->getUpdateMgr().pushOutOfRangeGuid(obj->GetNewGUID());
        }
        farSet.clear();
        return;
    }

    // snapshot containers
    thread_local std::vector<Player*>        s_players;
    thread_local std::vector<Creature*>      s_creatures;
    thread_local std::vector<GameObject*>    s_gos;
    thread_local std::vector<DynamicObject*> s_dyns;
    thread_local std::vector<Corpse*>        s_corpses;
    thread_local std::vector<Pet*>           s_pets;

    registry_->snapshotPlayers(s_players);
    registry_->snapshotCreatures(s_creatures);
    registry_->snapshotGameObjects(s_gos);
    registry_->snapshotDynamicObjects(s_dyns);
    registry_->snapshotCorpses(s_corpses);
    registry_->snapshotPets(s_pets);

    std::unordered_set<Object*> now;
    now.reserve(farSet.size() + 128);

    auto consider = [&](Object* obj)
        {
            if (!obj || obj == plr)
                return;

            if (obj->getWorldMap() != this)
                return;

            // only stuff we are allowed to see
            if (!plr->canSee(obj))
                return;
            
            // distance relative to our farsight location
            if (farsight->GetDistance2dSq(obj) > visRange)
                return;

            // add to container
            now.insert(obj);

            // build a9 packet
            if (!plr->seesGuid(obj->GetNewGUID()))
            {
                ByteBuffer buf;
                const uint32_t count = obj->buildCreateUpdateBlockForPlayer(&buf, plr);
                plr->getUpdateMgr().pushCreationData(&buf, count);
            }
        };

    // get objects from containers
    for (Player* p : s_players)   consider(p);
    for (Creature* c : s_creatures) consider(c);
    for (GameObject* g : s_gos)       consider(g);
    for (DynamicObject* d : s_dyns)      consider(d);
    for (Corpse* co : s_corpses)   consider(co);
    for (Pet* pt : s_pets)      consider(pt);

    for (auto it = farSet.begin(); it != farSet.end(); )
    {
        Object* obj = *it;
        const bool stillInside = (now.find(obj) != now.end());

        if (!stillInside)
        {
            if (obj && plr->seesGuid(obj->GetNewGUID()) && !plr->canSee(obj))
                plr->getUpdateMgr().pushOutOfRangeGuid(obj->GetNewGUID());

            it = farSet.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // update farsight set
    for (Object* obj : now)
        farSet.insert(obj);
}

Summon* WorldMap::summonCreature(uint32_t entry, LocationVector pos, WDB::Structures::SummonPropertiesEntry const* properties /*= nullptr*/, uint32_t duration /*= 0*/, Object* summoner /*= nullptr*/, uint32_t spellId /*= 0*/)
{
    // Generate always a new guid for totems, otherwise the totem bar will get messed up
    const auto isTotemSummon = properties != nullptr &&
        (properties->ControlType == SUMMON_CONTROL_TYPE_WILD ||
            properties->ControlType == SUMMON_CONTROL_TYPE_GUARDIAN ||
            properties->ControlType == SUMMON_CATEGORY_UNK) &&
        properties->Type == SUMMONTYPE_TOTEM;

    uint64_t guid = factory_->generateCreatureGuid(entry, !isTotemSummon);

    // Phase
    uint32_t phase = 1;
    if (summoner)
        phase = summoner->GetPhase();

    Unit* summonerUnit = summoner ? summoner->ToUnit() : nullptr;

    Summon* summon = nullptr;
    if (properties)
    {
        switch (properties->ControlType)
        {
            case SUMMON_CONTROL_TYPE_PET: // Guardians
            {
                summon = new WildSummon(guid, properties);
            } break;
            case SUMMON_CONTROL_TYPE_POSSESSED:
            {
                summon = new PossessedSummon(guid, properties);
            } break;
            case SUMMON_CONTROL_TYPE_VEHICLE:
            {
                summon = new CompanionSummon(guid, properties);
            } break;
            case SUMMON_CONTROL_TYPE_WILD:
            case SUMMON_CONTROL_TYPE_GUARDIAN:
            case SUMMON_CATEGORY_UNK:
            {
                switch (properties->Type)
                {
                    case SUMMONTYPE_MINION:
                    case SUMMONTYPE_GUARDIAN:
                    case SUMMONTYPE_GUARDIAN2:
                    {
                        summon = new WildSummon(guid, properties);
                    } break;
                    case SUMMONTYPE_TOTEM:
                    case SUMMONTYPE_LIGHTWELL:
                    {
                        summon = new TotemSummon(guid, properties);
                    } break;
                    case SUMMONTYPE_VEHICLE:
                    case SUMMONTYPE_VEHICLE2:
                    {
                        summon = new Summon(guid, properties);
                    } break;
                    case SUMMONTYPE_MINIPET:
                    {
                        summon = new CompanionSummon(guid, properties);
                    } break;
                    default:
                    {
                        if (properties->Flags & 512) // Mirror Image, Summon Gargoyle
                            summon = new WildSummon(guid, properties);
                    } break;
                }
            } break;
            default:
                summon = new Summon(guid, properties);
                break;
        }
    }
    else
    {
        summon = new Summon(guid, properties);
    }

    const auto* cp = sMySQLStore.getCreatureProperties(entry);
    if (cp == nullptr)
    {
        delete summon;
        return nullptr;
    }

    summon->load(cp, summonerUnit, pos, duration, spellId);
    summon->setPhase(PHASE_SET, phase);
    factory_->attachToWorld(summon);

    // This is needed to CastSpells or Move Right at Spawn
    updateObjects();

    // Delay this a bit to make sure its Spawned
    sEventMgr.AddEvent(static_cast<Creature*>(summon), &Creature::InitSummon, static_cast<Object*>(summonerUnit), EVENT_UNK, 100, 1, EVENT_FLAG_DO_NOT_EXECUTE_IN_WORLD_CONTEXT);

    return summon;
}

GameObject* WorldMap::summonGameObject(uint32_t entryID, LocationVector pos, QuaternionData const& rot, uint32_t duration, Object* summoner)
{
    auto gameobject_info = sMySQLStore.getGameObjectProperties(entryID);
    if (gameobject_info == nullptr)
    {
        sLogger.debug("Error looking up entry in CreateAndSpawnGameObject");
        return nullptr;
    }

    sLogger.debug("CreateAndSpawnGameObject: By Entry '{}'", entryID);

    GameObject* go = getSpawnManager().summonGameObject(entryID, pos, rot);
    if (summoner)
    {
        go->m_phase = summoner->GetPhase();
    }

    go->setSpawnedByDefault(false);

    return go;
}

void WorldMap::sendChatMessageToCellPlayers(Object* obj, WorldPacket* packet, uint32_t cell_radius, uint32_t langpos, int32_t lang, WorldSession* originator)
{
    if (!obj || !packet)
        return;

    thread_local std::vector<WoWGuid> s_guids;
    s_guids.clear();
    s_guids.reserve(128);

    spatialIndex_->collectGuidsInCellsAroundPos<Player>(obj->GetPosition(), static_cast<int>(cell_radius), 0, s_guids);

    const uint32_t senderPhase = obj->GetPhase();

    for (const WoWGuid& guid : s_guids)
    {
        Player* player = registry_->getPlayer(guid);
        if (!player)
            continue;

        if (player->getWorldMap() != this)
            continue;

        if ((player->GetPhase() & senderPhase) == 0)
            continue;

        Object::UpdatePin pin(player);

        if (WorldSession* session = player->getSession())
        {
            session->SendChatPacket(packet, langpos, lang, originator);
        }
        else
        {
            sLogger.failure("sendChatMessageToCellPlayers: invalid session for player {}", guid.getGuidLowPart());
        }
    }
}

void WorldMap::sendPvPCaptureMessage(int32_t ZoneMask, uint32_t ZoneId, const char* Message, ...)
{
    va_list ap;
    va_start(ap, Message);

    char msgbuf[200];
    vsnprintf(msgbuf, 200, Message, ap);
    va_end(ap);

    thread_local std::vector<Player*> s_players;
    registry_->snapshotPlayers(s_players);
    registry_->forEachPinned(s_players, [&](Player& player)
        {
            if (player.getWorldMap() != this)
                return;

            if ((ZoneMask != ZONE_MASK_ALL && player.getZoneId() != static_cast<uint32_t>(ZoneMask)))
                return;

            if (WorldSession* session = player.getSession())
            {
                session->SendPacket(SmsgDefenseMessage(ZoneId, msgbuf).serialise().get());
            }
            else
            {
                sLogger.failure("sendPvPCaptureMessage: failed invalid session for player {}", player.getGuidLow());
            }
        });
}

void WorldMap::sendPacketToAllPlayers(WorldPacket* packet) const
{
    if (!packet)
        return;

    thread_local std::vector<Player*> s_players;
    registry_->snapshotPlayers(s_players);

    registry_->forEachPinned(s_players, [&](Player& player)
        {
            if (player.getWorldMap() != this) return;

            if (WorldSession* session = player.getSession())
                session->SendPacket(packet);
            else
                sLogger.failure("sendPacketToAllPlayers: invalid session for player {}", player.getGuidLow());
        });
}

void WorldMap::sendPacketToPlayersInZone(uint32_t zone, WorldPacket* packet) const
{
    if (!packet)
        return;

    thread_local std::vector<Player*> s_players;
    registry_->snapshotPlayers(s_players);

    registry_->forEachPinned(s_players, [&](Player& player)
        {
            if (player.getWorldMap() != this)
                return;

            if (player.getZoneId() != zone)
                return;

            if (WorldSession* session = player.getSession())
            {
                session->SendPacket(packet);
            }
            else
            {
                sLogger.failure("sendPacketToPlayersInZone: invalid session for player {}", player.getGuidLow());
            }
        });
}

InstanceScript* WorldMap::getScript()
{
    return mInstanceScript;
}

void WorldMap::loadInstanceScript()
{
    mInstanceScript = sScriptMgr.CreateScriptClassForInstance(getBaseMap()->getMapId(), this);
};

void WorldMap::callScriptUpdate()
{
    if (mInstanceScript != nullptr)
    {
        mInstanceScript->UpdateEvent();
        mInstanceScript->updateTimers();
    }
    else
    {
        sLogger.failure("WorldMap::callScriptUpdate tries to call without valid instance script (nullptr)");
    }
};

void WorldMap::updateObjects()
{
    std::scoped_lock<std::mutex> lock(m_updateMutex);

    if (!_updates.size() && !_processQueue.size())
    {
        initialCreateValueUpdateSuppress_.clear();
        return;
    }

    ByteBuffer update(2500);
    uint32_t count = 0;

    for (auto pObj : _updates)
    {
        if (pObj == nullptr)
            continue;

        if (pObj->isItem() || pObj->isContainer())
        {
            // our update is only sent to the owner here.
            Player* pOwner = static_cast<Item*>(pObj)->getOwner();
            if (pOwner != nullptr)
            {
                count = pObj->BuildValuesUpdateBlockForPlayer(&update, pOwner);
                // send update to owner
                if (count)
                {
                    pOwner->getUpdateMgr().pushUpdateData(&update, count);
                    update.clear();
                }
            }
        }
        else
        {
            if (pObj->IsInWorld())
            {
                // players have to receive their own updates ;)
                if (pObj->isPlayer())
                {
                    // need to be different! ;)
                    count = pObj->BuildValuesUpdateBlockForPlayer(&update, static_cast<Player*>(pObj));
                    if (count)
                    {
                        static_cast<Player*>(pObj)->getUpdateMgr().pushUpdateData(&update, count);
                        update.clear();
                    }
                }
                else if (pObj->isCreatureOrPlayer() && static_cast<Unit*>(pObj)->m_playerControler != nullptr)
                {
                    Player* controller = static_cast<Unit*>(pObj)->m_playerControler;
                    const uint64_t viewerRaw = controller->GetNewGUID().getRawGuid();
                    const uint64_t objectRaw = pObj->GetNewGUID().getRawGuid();

                    if (!shouldSuppressInitialValueUpdateForViewer(viewerRaw, objectRaw))
                    {
                        count = pObj->BuildValuesUpdateBlockForPlayer(&update, controller);
                        if (count)
                        {
                            controller->getUpdateMgr().pushUpdateData(&update, count);
                            update.clear();
                        }
                    }
                }

                // build the update
                count = pObj->BuildValuesUpdateBlockForPlayer(&update, static_cast<Player*>(nullptr));
                update.clear();

                if (count)
                {
                    for (const auto& itr : pObj->getInRangePlayersSet())
                    {
                        Player* lplr = static_cast<Player*>(itr);

                        // Make sure that the target player can see us.
                        if (lplr && lplr->seesGuid(pObj->GetNewGUID()))
                        {
                            const uint64_t viewerRaw = lplr->GetNewGUID().getRawGuid();
                            const uint64_t objectRaw = pObj->GetNewGUID().getRawGuid();

                            // The queued create packet already contains the initial values for
                            // this recipient. Suppress exactly one normal value-update pass so
                            // delayed visibility creates do not immediately replay stale/duplicate
                            // GameObject, animation, dynamic flag, or combat-state values.
                            if (shouldSuppressInitialValueUpdateForViewer(viewerRaw, objectRaw))
                                continue;

                            // Build correct update to each player
                            // Data may differ from player to player
                            const uint32_t recipientCount = pObj->BuildValuesUpdateBlockForPlayer(&update, lplr);
                            if (recipientCount)
                                lplr->getUpdateMgr().pushUpdateData(&update, recipientCount);
                            update.clear();
                        }
                    }
                }
            }
        }
        pObj->ClearUpdateMask();
    }
    _updates.clear();
    initialCreateValueUpdateSuppress_.clear();

    // generate pending a9packets and send to clients.
    for (auto it = _processQueue.begin(); it != _processQueue.end();)
    {
        Player* player = *it;

        auto it2 = it;
        ++it;

        _processQueue.erase(it2);
        if (player->getWorldMap() == this)
            player->processPendingUpdates();
    }
}

void WorldMap::pushToProcessed(Player* plr)
{
    _processQueue.insert(plr);
}

MapScriptInterface* WorldMap::getInterface()
{
    return ScriptInterface.get();
}

WorldStatesHandler& WorldMap::getWorldStatesHandler()
{
    return worldstateshandler;
}

void WorldMap::onWorldStateUpdate(uint32_t zone, uint32_t field, uint32_t value)
{
    sendPacketToPlayersInZone(zone, SmsgUpdateWorldState(field, value).serialise().get());
}

bool WorldMap::isCombatInProgress()
{
    return (_combatProgress.size() > 0);
}

void WorldMap::addCombatInProgress(uint64_t guid)
{
    _combatProgress.insert(guid);
}

void WorldMap::removeCombatInProgress(uint64_t guid)
{
    _combatProgress.erase(guid);
}

void WorldMap::objectUpdated(Object* obj)
{
    // set our fields to dirty stupid fucked up code in places.. i hate doing this but i've got to :<- burlex
    std::scoped_lock<std::mutex> lock(m_updateMutex);
    _updates.insert(obj);
}

bool WorldMap::isRegularDifficulty()
{
    return getDifficulty() == InstanceDifficulty::Difficulties::DUNGEON_NORMAL;
}

WDB::Structures::MapDifficulty const* WorldMap::getMapDifficulty()
{
    return getMapDifficultyData(getBaseMap()->getMapId(), getDifficulty());
}

bool WorldMap::isRaidOrHeroicDungeon()
{
    return getBaseMap()->isRaid() || getSpawnMode() > InstanceDifficulty::Difficulties::DUNGEON_NORMAL;
}

bool WorldMap::isHeroic()
{
    return getBaseMap()->isRaid() ? getSpawnMode() >= InstanceDifficulty::Difficulties::RAID_10MAN_HEROIC : getSpawnMode() >= InstanceDifficulty::Difficulties::DUNGEON_HEROIC;
}

bool WorldMap::is25ManRaid()
{
    return getBaseMap()->isRaid() && getSpawnMode() & 1;
}

bool WorldMap::getAreaInfo(uint32_t /*phaseMask*/, LocationVector pos, uint32_t& flags, int32_t& adtId, int32_t& rootId, int32_t& groupId)
{
    float vmap_z = pos.z;
    float dynamic_z = pos.z;
    float check_z = pos.z;
    const auto vmgr = VMAP::VMapFactory::createOrGetVMapManager();
    uint32_t vflags;
    int32_t vadtId;
    int32_t vrootId;
    int32_t vgroupId;
    uint32_t dflags;
    int32_t dadtId;
    int32_t drootId;
    int32_t dgroupId;

    bool hasVmapAreaInfo = vmgr->getAreaInfo(getBaseMap()->getMapId(), pos.x, pos.y, vmap_z, vflags, vadtId, vrootId, vgroupId);
    bool hasDynamicAreaInfo = false;/*_dynamicTree.getAreaInfo(x, y, dynamic_z, phaseMask, dflags, dadtId, drootId, dgroupId);*/
    auto useVmap = [&]() { check_z = vmap_z; flags = vflags; adtId = vadtId; rootId = vrootId; groupId = vgroupId; };
    auto useDyn = [&]() { check_z = dynamic_z; flags = dflags; adtId = dadtId; rootId = drootId; groupId = dgroupId; };

    if (hasVmapAreaInfo)
    {
        if (hasDynamicAreaInfo && dynamic_z > vmap_z)
            useDyn();
        else
            useVmap();
    }
    else if (hasDynamicAreaInfo)
    {
        useDyn();
    }

    if (hasVmapAreaInfo || hasDynamicAreaInfo)
    {
        // check if there's terrain between player height and object height
        if (TerrainTile* gmap = getTerrain()->getTile(pos.x, pos.y))
        {
            float mapHeight = gmap->m_map.getHeight(pos.x, pos.y);
            // z + 2.0f condition taken from getHeight(), not sure if it's such a great choice...
            if (pos.z + 2.0f > mapHeight && mapHeight > check_z)
                return false;
        }
        return true;
    }
    return false;
}

uint32_t WorldMap::getAreaId(uint32_t phaseMask, LocationVector const& pos)
{
    uint32_t mogpFlags;
    int32_t adtId, rootId, groupId;
    float vmapZ = pos.z;
    bool hasVmapArea = getAreaInfo(phaseMask, LocationVector(pos.x, pos.y, vmapZ), mogpFlags, adtId, rootId, groupId);

    uint32_t gridAreaId = 0;
    float gridMapHeight = INVALID_HEIGHT;
    if (TerrainTile* gmap = getTerrain()->getTile(pos.x, pos.y))
    {
        gridAreaId = gmap->m_map.getArea(pos.x, pos.y);
        gridMapHeight = gmap->m_map.getHeight(pos.x, pos.y);
    }

    uint32_t areaId = 0;

    // floor is the height we are closer to (but only if above)
    if (hasVmapArea && G3D::fuzzyGe(pos.z, vmapZ - GROUND_HEIGHT_TOLERANCE) && (G3D::fuzzyLt(pos.z, gridMapHeight - GROUND_HEIGHT_TOLERANCE) || vmapZ > gridMapHeight))
    {
        // wmo found
        if (WDB::Structures::WMOAreaTableEntry const* wmoEntry = GetWMOAreaTableEntryByTriple(rootId, adtId, groupId))
            areaId = wmoEntry->areaId;

        if (!areaId)
            areaId = gridAreaId;
    }
    else
    {
        areaId = gridAreaId;
    }

    if (!areaId)
        areaId = getBaseMap()->getMapEntry()->linked_zone;

    return areaId;
}

uint32_t WorldMap::getZoneId(uint32_t phaseMask, LocationVector const& pos)
{
    uint32_t areaId = 0;
    if (const auto* area = MapManagement::AreaManagement::AreaStorage::getExactArea(this, pos, phaseMask))
    {
        areaId = area->id;
        if (area->zone)
            return area->zone;
    }

    return areaId;
}

void WorldMap::getZoneAndAreaId(uint32_t phaseMask, uint32_t& zoneid, uint32_t& areaid, LocationVector const& pos)
{
    if (const auto* area = MapManagement::AreaManagement::AreaStorage::getExactArea(this, pos, phaseMask))
    {
        areaid = area->id;
        if (area->zone)
            zoneid = area->zone;
    }
}

static inline bool isInWMOInterior(uint32_t mogpFlags)
{
    return (mogpFlags & 0x2000) != 0;
}

ZLiquidStatus WorldMap::getLiquidStatus(uint32_t phaseMask, LocationVector pos, uint8_t ReqLiquidType, LiquidData* data, float collisionHeight)
{
    ZLiquidStatus result = LIQUID_MAP_NO_WATER;
    const auto vmgr = VMAP::VMapFactory::createOrGetVMapManager();
    float liquid_level = INVALID_HEIGHT;
    float ground_level = INVALID_HEIGHT;
    uint32_t liquid_type = 0;
    uint32_t mogpFlags = 0;
    bool useGridLiquid = true;
    if (getBaseMap() && vmgr->getLiquidLevel(getBaseMap()->getMapId(), pos.x, pos.y, pos.z, ReqLiquidType, liquid_level, ground_level, liquid_type, mogpFlags))
    {
        useGridLiquid = !isInWMOInterior(mogpFlags);
        // Check water level and ground level
        if (liquid_level > ground_level && G3D::fuzzyGe(pos.z, ground_level - GROUND_HEIGHT_TOLERANCE))
        {
            // All ok in water -> store data
            if (data)
            {
                // hardcoded in client like this
                if (getBaseMap()->getMapId() == 530 && liquid_type == 2)
                    liquid_type = 15;

                uint32_t liquidFlagType = 0;
                if (WDB::Structures::LiquidTypeEntry const* liq = sLiquidTypeStore.lookupEntry(liquid_type))
                    liquidFlagType = liq->Type;

                if (liquid_type && liquid_type < 21)
                {
                    if (const auto* area = MapManagement::AreaManagement::AreaStorage::getExactArea(this, pos, phaseMask))
                    {
#if VERSION_STRING > Classic
                        uint32_t overrideLiquid = area->liquid_type_override[liquidFlagType];
                        if (!overrideLiquid && area->zone)
                        {
                            area = MapManagement::AreaManagement::AreaStorage::GetAreaById(area->zone);
                            if (area)
                                overrideLiquid = area->liquid_type_override[liquidFlagType];
                        }
#else
                        uint32_t overrideLiquid = area->liquid_type_override;
                        if (!overrideLiquid && area->zone)
                        {
                            area = MapManagement::AreaManagement::AreaStorage::GetAreaById(area->zone);
                            if (area)
                                overrideLiquid = area->liquid_type_override;
                        }
#endif

                        if (WDB::Structures::LiquidTypeEntry const* liq = sLiquidTypeStore.lookupEntry(overrideLiquid))
                        {
                            liquid_type = overrideLiquid;
                            liquidFlagType = liq->Type;
                        }
                    }
                }

                data->level = liquid_level;
                data->depth_level = ground_level;

                data->entry = liquid_type;
                data->type_flags = 1U << liquidFlagType;
            }

            float delta = liquid_level - pos.z;

            // Get position delta
            if (delta > collisionHeight)        // Under water
                return LIQUID_MAP_UNDER_WATER;
            if (delta > 0.0f)                   // In water
                return LIQUID_MAP_IN_WATER;
            if (delta > -0.1f)                  // Walk on water
                return LIQUID_MAP_WATER_WALK;
            result = LIQUID_MAP_ABOVE_WATER;
        }
    }

    if (useGridLiquid)
    {
        if (TerrainTile* gmap = getTerrain()->getTile(pos.x, pos.y))
        {
            LiquidData map_data;
            ZLiquidStatus map_result = gmap->m_map.getLiquidStatus(pos, ReqLiquidType, &map_data, collisionHeight);
            // Not override LIQUID_MAP_ABOVE_WATER with LIQUID_MAP_NO_WATER:
            if (map_result != LIQUID_MAP_NO_WATER && (map_data.level > ground_level))
            {
                if (data)
                {
                    // hardcoded in client like this
                    if (getBaseMap()->getMapId() == 530 && map_data.entry == 2)
                        map_data.entry = 15;

                    *data = map_data;
                }
                return map_result;
            }
        }
    }
    return result;
}

void WorldMap::getFullTerrainStatusForPosition(uint32_t phaseMask, float x, float y, float z, PositionFullTerrainStatus& data, uint8_t reqLiquidType, float collisionHeight) const
{
    if (!getTerrain())
        return;

    VMAP::IVMapManager* vmgr = VMAP::VMapFactory::createOrGetVMapManager();
    VMAP::AreaAndLiquidData vmapData;
    VMAP::AreaAndLiquidData dynData;
    VMAP::AreaAndLiquidData* wmoData = nullptr;

    TerrainTile* gmap = getTerrain()->getTile(x, y);
    if (!gmap)
        return;

    vmgr->getAreaAndLiquidData(getBaseMap()->getMapId(), x, y, z, reqLiquidType, vmapData);
    _dynamicTree.getAreaAndLiquidData(x, y, z, phaseMask, reqLiquidType, dynData);

    uint32_t gridAreaId = 0;
    float gridMapHeight = INVALID_HEIGHT;
    if (gmap)
    {
        gridAreaId = gmap->m_map.getArea(x, y);
        gridMapHeight = gmap->m_map.getHeight(x, y);
    }

    bool useGridLiquid = true;

    // floorZ is the height we are closer to example we stand on an wmo
    data.floorZ = VMAP_INVALID_HEIGHT;
    if (gridMapHeight > INVALID_HEIGHT && G3D::fuzzyGe(z, gridMapHeight - GROUND_HEIGHT_TOLERANCE))
        data.floorZ = gridMapHeight;
    if (vmapData.floorZ > VMAP_INVALID_HEIGHT &&
        G3D::fuzzyGe(z, vmapData.floorZ - GROUND_HEIGHT_TOLERANCE) &&
        (G3D::fuzzyLt(z, gridMapHeight - GROUND_HEIGHT_TOLERANCE) || vmapData.floorZ > gridMapHeight))
    {
        data.floorZ = vmapData.floorZ;
        wmoData = &vmapData;
    }

    // When spawning and despawning Wmos the provided area/liquid from beneath them wont get detected properly
    // example: Lich King platform
    if (dynData.floorZ > VMAP_INVALID_HEIGHT &&
        G3D::fuzzyGe(z, dynData.floorZ - GROUND_HEIGHT_TOLERANCE) &&
        (G3D::fuzzyLt(z, gridMapHeight - GROUND_HEIGHT_TOLERANCE) || dynData.floorZ > gridMapHeight) &&
        (G3D::fuzzyLt(z, vmapData.floorZ - GROUND_HEIGHT_TOLERANCE) || dynData.floorZ > vmapData.floorZ))
    {
        data.floorZ = dynData.floorZ;
        wmoData = &dynData;
    }

    if (wmoData)
    {
        if (wmoData->areaInfo)
        {
            data.areaInfo.emplace(wmoData->areaInfo->adtId, wmoData->areaInfo->rootId, wmoData->areaInfo->groupId, wmoData->areaInfo->mogpFlags);
            // wmo found
            WDB::Structures::WMOAreaTableEntry const* wmoEntry = GetWMOAreaTableEntryByTriple(wmoData->areaInfo->rootId, wmoData->areaInfo->adtId, wmoData->areaInfo->groupId);
            data.outdoors = (wmoData->areaInfo->mogpFlags & 0x8) != 0;
            if (wmoEntry)
            {
                data.areaId = wmoEntry->areaId;
                if (wmoEntry->flags & 4)
                    data.outdoors = true;
                else if (wmoEntry->flags & 2)
                    data.outdoors = false;
            }

            if (!data.areaId)
                data.areaId = gridAreaId;

            useGridLiquid = !isInWMOInterior(wmoData->areaInfo->mogpFlags);
        }
    }
    else
    {
        data.outdoors = true;
        data.areaId = gridAreaId;
        if (WDB::Structures::AreaTableEntry const* areaEntry = sAreaStore.lookupEntry(data.areaId))
            data.outdoors = (areaEntry->flags & (MapManagement::AreaManagement::AreaFlags::AREA_FLAG_INSIDE | MapManagement::AreaManagement::AreaFlags::AREA_FLAG_OUTSIDE)) != MapManagement::AreaManagement::AreaFlags::AREA_FLAG_INSIDE;
    }

    if (!data.areaId)
        data.areaId = getBaseMap()->getMapEntry()->linked_zone;

    WDB::Structures::AreaTableEntry const* areaEntry = sAreaStore.lookupEntry(data.areaId);

    // liquid processing
    data.liquidStatus = LIQUID_MAP_NO_WATER;
    if (wmoData && wmoData->liquidInfo && wmoData->liquidInfo->level > wmoData->floorZ)
    {
        uint32_t liquidType = wmoData->liquidInfo->type;
        if (getBaseMap()->getMapId() == 530 && liquidType == 2) // gotta love blizzard hacks
            liquidType = 15;

        uint32_t liquidFlagType = 0;
        if (WDB::Structures::LiquidTypeEntry const* liquidData = sLiquidTypeStore.lookupEntry(liquidType))
            liquidFlagType = liquidData->Type;

        if (liquidType && liquidType < 21 && areaEntry)
        {
#if VERSION_STRING > Classic
            uint32_t overrideLiquid = areaEntry->liquid_type_override[liquidFlagType];
            if (!overrideLiquid && areaEntry->zone)
            {
                WDB::Structures::AreaTableEntry const* zoneEntry = sAreaStore.lookupEntry(areaEntry->zone);
                if (zoneEntry)
                    overrideLiquid = zoneEntry->liquid_type_override[liquidFlagType];
            }
#else
            uint32_t overrideLiquid = areaEntry->liquid_type_override;
            if(!overrideLiquid && areaEntry->zone)
            {
                WDB::Structures::AreaTableEntry const* zoneEntry = sAreaStore.lookupEntry(areaEntry->zone);
                if (zoneEntry)
                    overrideLiquid = zoneEntry->liquid_type_override;
            }
#endif
            if (WDB::Structures::LiquidTypeEntry const* overrideData = sLiquidTypeStore.lookupEntry(overrideLiquid))
            {
                liquidType = overrideLiquid;
                liquidFlagType = overrideData->Type;
            }
        }

        data.liquidInfo.emplace();
        data.liquidInfo->level = wmoData->liquidInfo->level;
        data.liquidInfo->depth_level = wmoData->floorZ;
        data.liquidInfo->entry = liquidType;
        data.liquidInfo->type_flags = 1 << liquidFlagType;

        float delta = wmoData->liquidInfo->level - z;
        if (delta > collisionHeight)
            data.liquidStatus = LIQUID_MAP_UNDER_WATER;
        else if (delta > 0.0f)
            data.liquidStatus = LIQUID_MAP_IN_WATER;
        else if (delta > -0.1f)
            data.liquidStatus = LIQUID_MAP_WATER_WALK;
        else
            data.liquidStatus = LIQUID_MAP_ABOVE_WATER;
    }
    // look up liquid data from grid map
    if (gmap && useGridLiquid)
    {
        LiquidData gridMapLiquid;
        ZLiquidStatus gridMapStatus = gmap->m_map.getLiquidStatus(LocationVector(x, y, z), reqLiquidType, &gridMapLiquid, collisionHeight);
        if (gridMapStatus != LIQUID_MAP_NO_WATER && (!wmoData || gridMapLiquid.level > wmoData->floorZ))
        {
            if (getBaseMap()->getMapId() == 530 && gridMapLiquid.entry == 2)
                gridMapLiquid.entry = 15;
            data.liquidInfo = gridMapLiquid;
            data.liquidStatus = gridMapStatus;
        }
    }
}

float WorldMap::getWaterLevel(float x, float y)
{
    if (TerrainTile* gmap = getTerrain()->getTile(x, y))
        return gmap->m_map.getLiquidLevel(x, y);
    else
        return 0;
}

bool WorldMap::isInWater(uint32_t phaseMask, LocationVector pos, LiquidData* data)
{
    LiquidData liquid_status{};
    LiquidData* liquid_ptr = data ? data : &liquid_status;
    return (getLiquidStatus(phaseMask, pos, MAP_ALL_LIQUIDS, liquid_ptr) & (LIQUID_MAP_IN_WATER | LIQUID_MAP_UNDER_WATER)) != 0;
}

bool WorldMap::isUnderWater(uint32_t phaseMask, LocationVector pos)
{
    return (getLiquidStatus(phaseMask, pos, MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_OCEAN) & LIQUID_MAP_UNDER_WATER) != 0;
}

bool WorldMap::isInLineOfSight(LocationVector pos1, LocationVector pos2, uint32_t phasemask, LineOfSightChecks checks)
{
    if ((checks & LINEOFSIGHT_CHECK_VMAP)
        && !VMAP::VMapFactory::createOrGetVMapManager()->isInLineOfSight(getBaseMap()->getMapId(), pos1.x, pos1.y, pos1.z, pos2.x, pos2.y, pos2.z))
        return false;

    if (checks & LINEOFSIGHT_CHECK_GOBJECT && !_dynamicTree.isInLineOfSight(pos1.x, pos1.y, pos1.z, pos2.x, pos2.y, pos2.z, phasemask))
        return false;

    return true;
}

bool WorldMap::getObjectHitPos(uint32_t phasemask, LocationVector pos1, LocationVector pos2, float& rx, float& ry, float& rz, float modifyDist)
{
    G3D::Vector3 startPos(pos1.x, pos1.y, pos1.z);
    G3D::Vector3 dstPos(pos2.x, pos2.y, pos2.z);

    G3D::Vector3 resultPos;
    bool result = _dynamicTree.getObjectHitPos(phasemask, startPos, dstPos, resultPos, modifyDist);

    rx = resultPos.x;
    ry = resultPos.y;
    rz = resultPos.z;
    return result;
}

float WorldMap::getGameObjectFloor(uint32_t phasemask, LocationVector pos, float maxSearchDist /*= 50.0f*/) const
{
    return _dynamicTree.getHeight(pos.x, pos.y, pos.z, maxSearchDist, phasemask);
}

float WorldMap::getHeight(uint32_t phasemask, LocationVector const& pos, bool vmap /*= true*/, float maxSearchDist /*= 50.0f*/) const
{
    return std::max<float>(getHeight(pos, vmap, maxSearchDist), getGameObjectFloor(phasemask, pos, maxSearchDist));
}


float WorldMap::getWaterOrGroundLevel(uint32_t phasemask, LocationVector const& pos, float* ground /*= nullptr*/, bool /*swim = false*/, float collisionHeight /*= 2.03128f*/)
{
    if (getTerrain()->getTile(pos.x, pos.y))
    {
        // we need ground level (including grid height version) for proper return water level in point
        float ground_z = getHeight(phasemask, LocationVector(pos.x, pos.y, pos.z + collisionHeight), true, 50.0f);
        if (ground)
            *ground = ground_z;

        LiquidData liquid_status;

        ZLiquidStatus res = getLiquidStatus(phasemask, LocationVector(pos.x, pos.y, ground_z), MAP_ALL_LIQUIDS, &liquid_status, collisionHeight);
        switch (res)
        {
            case LIQUID_MAP_ABOVE_WATER:
                return std::max<float>(liquid_status.level, ground_z);
            case LIQUID_MAP_NO_WATER:
                return ground_z;
            default:
                return liquid_status.level;
        }
    }

    return VMAP_INVALID_HEIGHT_VALUE;
}

float WorldMap::getHeight(LocationVector const& pos, bool checkVMap /*= true*/, float maxSearchDist /*= 50.0f*/) const
{
    // find raw .map surface under Z coordinates
    float mapHeight = VMAP_INVALID_HEIGHT_VALUE;
    float gridHeight = getGridHeight(pos.x, pos.y);
    if (G3D::fuzzyGe(pos.z, gridHeight - GROUND_HEIGHT_TOLERANCE))
        mapHeight = gridHeight;

    float vmapHeight = VMAP_INVALID_HEIGHT_VALUE;
    if (checkVMap)
    {
        const auto vmgr = VMAP::VMapFactory::createOrGetVMapManager();
        if (vmgr->isHeightCalcEnabled())
            vmapHeight = vmgr->getHeight(getBaseMap()->getMapId(), pos.x, pos.y, pos.z, maxSearchDist);
    }

    // mapHeight set for any above raw ground Z or <= INVALID_HEIGHT
    // vmapheight set for any under Z value or <= INVALID_HEIGHT
    if (vmapHeight > INVALID_HEIGHT)
    {
        if (mapHeight > INVALID_HEIGHT)
        {
            // we have mapheight and vmapheight and must select more appropriate

            // vmap height above map height
            // or if the distance of the vmap height is less the land height distance
            if (vmapHeight > mapHeight || std::fabs(mapHeight - pos.z) > std::fabs(vmapHeight - pos.z))
                return vmapHeight;

            return mapHeight;                           // better use .map surface height
        }

        return vmapHeight;                              // we have only vmapHeight (if have)
    }

    return mapHeight;                               // explicitly use map data
}

float WorldMap::getGridHeight(float x, float y) const
{
    if (TerrainTile* gmap = getTerrain()->getTile(x, y))
        return gmap->m_map.getHeight(x, y);

    return VMAP_INVALID_HEIGHT_VALUE;
}

void WorldMap::respawnBossLinkedGroups(uint32_t bossId)
{
    // todo aaron02
    /*
    // Get all Killed npcs out of Killed npc Store
    for (Creature* spawn : sMySQLStore.getSpawnGroupDataByBoss(bossId))
    {
        if (spawn && spawn->m_spawn && spawn->getSpawnId())
        {
            // Get the Group Data and see if we have to Respawn the npcs
            auto data = sMySQLStore.getSpawnGroupDataBySpawn(spawn->getSpawnId());

            if (data && data->spawnFlags & SPAWFLAG_FLAG_BOUNDTOBOSS)
            {
                // Respawn the Npc
                if (spawn->IsInWorld() && !spawn->isAlive())
                {
                    spawn->Despawn(0, 1000);
                }
                else if (!spawn->isAlive())
                {
                    // get the cell with our SPAWN location. if we've moved cell this might break :P
                    MapCell* pCell = getCellByCoords(spawn->GetSpawnX(), spawn->GetSpawnY());
                    if (pCell == nullptr)
                        pCell = spawn->GetMapCell();

                    if (pCell != nullptr)
                    {
                        pCell->_respawnObjects.insert(spawn);

                        sEventMgr.RemoveEvents(spawn);
                        doRespawn(SPAWN_TYPE_CREATURE, spawn, spawn->getSpawnId(), pCell->getPositionX(), pCell->getPositionY());

                        spawn->SetPosition(spawn->GetSpawnPosition(), true);
                        spawn->m_respawnCell = pCell;
                    }
                }
            }
        }
    }*/
}
