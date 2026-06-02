/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "SpawnManager.hpp"
#include "Map/Maps/WorldMap.hpp"
#include "ObjectFactory.hpp"
#include "Objects/Units/Creatures/Creature.h"
#include "Objects/GameObject.h"
#include "Server/DatabaseDefinition.hpp"
#include <algorithm>

using visibility::worldToGrid;
using visibility::packGridId;

SpawnManager::SpawnManager(WorldMap& map, visibility::VisibilitySystem& visibilitySystem, ObjectFactory& factory)
    : map_(map), visibilitySystem_(visibilitySystem), factory_(factory) 
{
    loadSpawns(true);
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Grid Spawns
//////////////////////////////////////////////////////////////////////////////////////////
void SpawnManager::loadSpawns(bool reload)
{
    const uint32_t mapId = map_.getBaseMap()->getMapId();

    // Create Bucket
    decltype(spawns) newSpawns{};
    uint32_t newCreatureCount = 0;
    uint32_t newGOCount = 0;

    // Creatures
    if (!sMySQLStore.isTransportMap(mapId))
    {
        const auto& cstore = sMySQLStore._creatureSpawnsStore[mapId];
        for (const auto* row : cstore)
        {
            auto [gx, gy] = visibility::worldToGrid(row->spawnPoint);
            const int gid = visibility::packGridId(gx, gy);
            if (gid < 0 || gid >= kGrids) continue;

            if (!newSpawns[gid]) newSpawns[gid] = std::make_unique<GridSpawns>();
            newSpawns[gid]->CreatureSpawns.push_back(const_cast<MySQLStructure::CreatureSpawn*>(row));
            ++newCreatureCount;
        }
    }

    // GameObjects
    {
        const auto& gstore = sMySQLStore._gameobjectSpawnsStore[mapId];
        for (const auto* row : gstore)
        {
            auto [gx, gy] = visibility::worldToGrid(row->spawnPoint);
            const int gid = visibility::packGridId(gx, gy);
            if (gid < 0 || gid >= kGrids) continue;

            if (!newSpawns[gid]) newSpawns[gid] = std::make_unique<GridSpawns>();
            newSpawns[gid]->GameobjectSpawns.push_back(const_cast<MySQLStructure::GameobjectSpawn*>(row));
            ++newGOCount;
        }
    }

    // Apply containers
    {
        std::unique_lock wlk(spawnsMtx_);

        if (reload)
        {
            for (auto& p : spawns) p.reset();
        }

        spawns.swap(newSpawns);
        CreatureSpawnCount = newCreatureCount;
        GameObjectSpawnCount = newGOCount;
    }

    sLogger.info("SpawnMgr: Loaded {} creature spawns and {} gameobject spawns for map {}.", CreatureSpawnCount, GameObjectSpawnCount, map_.getBaseMap()->getMapId());
}

GridSpawns* SpawnManager::getSpawnsList(int gid)
{
    if (gid < 0 || gid >= kGrids)
        return nullptr;

    std::shared_lock rlk(spawnsMtx_);
    return spawns[gid] ? spawns[gid].get() : nullptr;
}

GridSpawns* SpawnManager::getSpawnsListAndCreate(int gid)
{
    if (gid < 0 || gid >= kGrids)
        return nullptr;

    {
        std::shared_lock rlk(spawnsMtx_);
        if (spawns[gid])
            return spawns[gid].get();
    }

    std::unique_lock wlk(spawnsMtx_);
    if (!spawns[gid])
        spawns[gid] = std::make_unique<GridSpawns>();

    return spawns[gid].get();
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Summon (non-persistent) creature. The creature is created only in memory/world and
/// not backed by a DB row. Optionally disables respawn.
//////////////////////////////////////////////////////////////////////////////////////////
Creature* SpawnManager::summonCreature(uint32_t entry, LocationVector const& pos, bool noRespawn)
{
    Creature* creature = factory_.createAndSpawnCreature(entry, pos);
    if (!creature)
        return nullptr;

    auto [gx, gy] = worldToGrid(pos);
    const int homeGid = packGridId(gx, gy);

    SpawnKey key{ SPAWN_TYPE_CREATURE2, allocEphemeralId() };
    SpawnState s{};
    s.spawnId           = key.id;
    s.homeGid           = homeGid;
    s.type              = SPAWN_TYPE_CREATURE2;
    s.entry             = entry;
    s.position          = pos;
    s.guidRaw           = creature->GetNewGUID().getRawGuid();
    s.allowRespawn      = !noRespawn;
    s.respawnAllowed    = !noRespawn;
    s.persistent        = false;

    /// Set temporary spawn id on the live object so we can track it.
    creature->setSpawnId(key.id);

    {
        std::unique_lock lk(mtx_);
        spawns_.emplace(key, s);
        byHome_[homeGid].push_back(key);
        guidIndex_[s.guidRaw] = key;

        // live index
        const int curG = homeGid;
        indexLiveSpawn_NoLock(key, s, curG);
    }

    return creature;
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Spawn (persistent) creature from DB row or by entry (creates DB row if needed).
//////////////////////////////////////////////////////////////////////////////////////////
Creature* SpawnManager::spawnCreature(uint32_t entry, LocationVector const& pos, const MySQLStructure::CreatureSpawn* row)
{
    Creature* creature = nullptr;

    if (row)
    {
        creature = factory_.createAndSpawnCreatureFromSpawns(*row);
        if (!creature)
            return nullptr;

        LocationVector spawnPos = row->spawnPoint;
        auto [gx, gy] = worldToGrid(spawnPos);
        const int homeGid = packGridId(gx, gy);

        SpawnKey key{ SPAWN_TYPE_CREATURE2, row->id };
        SpawnState s{};
        s.spawnId           = key.id;
        s.homeGid           = homeGid;
        s.type              = SPAWN_TYPE_CREATURE2;
        s.entry             = row->entry;
        s.position          = spawnPos;
        s.rowC              = const_cast<MySQLStructure::CreatureSpawn*>(row);
        s.guidRaw           = creature->GetNewGUID().getRawGuid();
        s.respawnAllowed    = true;
        s.persistent        = true;

        {
            std::unique_lock lk(mtx_);
            spawns_.emplace(key, s);
            byHome_[homeGid].push_back(key);
            guidIndex_[s.guidRaw] = key;

            // live index
            const int curG = homeGid;
            indexLiveSpawn_NoLock(key, s, curG);
        }

        return creature;
    }
    else
    {
        creature = factory_.createAndSpawnCreature(entry, pos);
        if (!creature)
            return nullptr;

        /// Create database entry and associate m_spawn
        creature->SaveToDB();

        if (!creature->m_spawn)
        {
            sLogger.failure("SpawnManager:: spawnCreature failed database spawn could not be created.");
            factory_.detachFromWorld(creature);
            factory_.recycleAndDestroy(creature, /*recycleGuid=*/true, /*destroyObject=*/true);
            return nullptr;
        }

        auto [gx, gy] = worldToGrid(pos);
        const int homeGid = packGridId(gx, gy);

        SpawnKey key{ SPAWN_TYPE_CREATURE2, creature->m_spawn->id };
        SpawnState s{};

        s.spawnId           = key.id;
        s.homeGid           = homeGid;
        s.type              = SPAWN_TYPE_CREATURE2;
        s.entry             = creature->m_spawn->entry;
        s.position          = pos;
        s.rowC              = creature->m_spawn;
        s.guidRaw           = creature->GetNewGUID().getRawGuid();
        s.respawnAllowed    = true;
        s.persistent        = true;

        {
            std::unique_lock lk(mtx_);
            spawns_.emplace(key, s);
            byHome_[homeGid].push_back(key);
            guidIndex_[s.guidRaw] = key;

            // live index
            const int curG = homeGid;
            indexLiveSpawn_NoLock(key, s, curG);
        }

        return creature;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Summon (non-persistent) gameobject. Only exists in memory/world.
//////////////////////////////////////////////////////////////////////////////////////////
GameObject* SpawnManager::summonGameObject(uint32_t entry, LocationVector const& pos, QuaternionData const& rotation, bool noRespawn)
{
    GameObject* go = factory_.createAndSpawnGameObject(entry, pos, rotation);
    if (!go)
        return nullptr;

    auto [gx, gy] = worldToGrid(pos);
    const int homeGid = packGridId(gx, gy);

    SpawnKey key{ SPAWN_TYPE_GAMEOBJECT2, allocEphemeralId() };
    SpawnState s{};

    s.spawnId           = key.id;
    s.homeGid           = homeGid;
    s.type              = SPAWN_TYPE_GAMEOBJECT2;
    s.entry             = entry;
    s.position          = pos;
    s.guidRaw           = go->GetNewGUID().getRawGuid();
    s.allowRespawn      = !noRespawn;
    s.respawnAllowed    = !noRespawn;
    s.persistent        = false;

    /// Set temporary spawn id on the live object so we can track it.
    go->setSpawnId(key.id);

    {
        std::unique_lock lk(mtx_);
        spawns_.emplace(key, s);
        byHome_[homeGid].push_back(key);
        guidIndex_[s.guidRaw] = key;

        const int curG = homeGid;
        indexLiveSpawn_NoLock(key, s, curG);
    }

    return go;
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Spawn (persistent) gameobject from DB row or by entry (creates DB row if needed).
//////////////////////////////////////////////////////////////////////////////////////////
GameObject* SpawnManager::spawnGameObject(uint32_t entry, LocationVector const& pos, QuaternionData const& rotation, const MySQLStructure::GameobjectSpawn* row)
{
    GameObject* go = nullptr;

    if (row)
    {
        go = factory_.createAndSpawnGameObjectFromSpawns(*row);
        if (!go)
            return nullptr;

        auto [gx, gy] = worldToGrid(row->spawnPoint);
        const int homeGid = packGridId(gx, gy);

        SpawnKey key{ SPAWN_TYPE_GAMEOBJECT2, row->id };
        SpawnState s{};

        s.spawnId           = key.id;
        s.homeGid           = homeGid;
        s.type              = SPAWN_TYPE_GAMEOBJECT2;
        s.entry             = row->entry;
        s.position          = row->spawnPoint;
        s.rowGO             = const_cast<MySQLStructure::GameobjectSpawn*>(row);
        s.guidRaw           = go->GetNewGUID().getRawGuid();
        s.respawnAllowed    = true;
        s.persistent        = true;

        {
            std::unique_lock lk(mtx_);
            spawns_.emplace(key, s);
            byHome_[homeGid].push_back(key);
            guidIndex_[s.guidRaw] = key;

            const int curG = homeGid;
            indexLiveSpawn_NoLock(key, s, curG);
        }

        return go;
    }
    else
    {
        go = factory_.createAndSpawnGameObject(entry, pos, rotation);
        if (!go)
            return nullptr;

        /// Create database entry and associate m_spawn
        go->saveToDB();

        if (!go->m_spawn)
        {
            sLogger.failure("SpawnManager:: spawnGameObject failed database spawn could not be created.");
            factory_.detachFromWorld(go);
            factory_.recycleAndDestroy(go, /*recycleGuid=*/true, /*destroyObject=*/true);
            return nullptr;
        }

        auto [gx, gy] = worldToGrid(pos);
        const int homeGid = packGridId(gx, gy);

        SpawnKey key{ SPAWN_TYPE_GAMEOBJECT2, go->m_spawn->id };
        SpawnState s{};

        s.spawnId           = key.id;
        s.homeGid           = homeGid;
        s.type              = SPAWN_TYPE_GAMEOBJECT2;
        s.entry             = go->m_spawn->entry;
        s.position          = pos;
        s.rowGO             = go->m_spawn;
        s.guidRaw           = go->GetNewGUID().getRawGuid();
        s.respawnAllowed    = true;
        s.persistent        = true;

        {
            std::unique_lock lk(mtx_);
            spawns_.emplace(key, s);
            byHome_[homeGid].push_back(key);
            guidIndex_[s.guidRaw] = key;

            const int curG = homeGid;
            indexLiveSpawn_NoLock(key, s, curG);
        }

        return go;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Attach templates (non-live) to a grid that just got activated.
//////////////////////////////////////////////////////////////////////////////////////////
void SpawnManager::attachTemplatesForGrid(int gid, const CreatureSpawnList& creatures, const GameobjectSpawnList& gameobjects)
{
    std::unique_lock lk(mtx_);
    auto& vec = byHome_[gid];
    vec.reserve(vec.size() + creatures.size() + gameobjects.size());

    for (const auto& creature : creatures) 
    {
        SpawnKey key{ SPAWN_TYPE_CREATURE2, creature->id };
        if (spawns_.count(key))
            continue;

        SpawnState state;
        state.spawnId           = creature->id;
        state.homeGid           = gid;
        state.type              = SPAWN_TYPE_CREATURE2;
        state.entry             = creature->entry;
        state.position          = creature->spawnPoint;
        state.respawnAllowed    = true;
        state.rowC              = creature;

        spawns_.emplace(key, std::move(state));
        vec.push_back(key);
    }

    for (const auto& gameObject : gameobjects) 
    {
        SpawnKey key{ SPAWN_TYPE_GAMEOBJECT2, gameObject->id };
        if (spawns_.count(key))
            continue;

        SpawnState state;
        state.spawnId           = gameObject->id;
        state.homeGid           = gid;
        state.type              = SPAWN_TYPE_GAMEOBJECT2;
        state.entry             = gameObject->entry;
        state.position          = gameObject->spawnPoint;
        state.respawnAllowed    = true;
        state.rowGO             = gameObject;

        spawns_.emplace(key, std::move(state));
        vec.push_back(key);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Grid activated — mark active and spawn missing live instances if needed.
//////////////////////////////////////////////////////////////////////////////////////////
void SpawnManager::onGridActivated(int gid)
{
    { // Epoch filter: protect against duplicate activation in this epoch.
        std::unique_lock lk(mtx_);
        if (!activeEpoch_.insert(gid).second)
            return;
    }

    // Attach Spawns
    if (GridSpawns* spawns = getSpawnsList(gid))
        attachTemplatesForGrid(gid, spawns->CreatureSpawns, spawns->GameobjectSpawns);

    std::vector<SpawnKey> keys;
    {
        std::shared_lock lk(mtx_);
        if (auto it = byHome_.find(gid); it != byHome_.end())
            keys = it->second;
    }

    for (auto key : keys)
    {
        std::unique_lock lk(mtx_);
        auto it = spawns_.find(key);
        if (it == spawns_.end())
            continue;

        auto& s = it->second;
        s.respawnAllowed = true;

        const bool scheduled = hasRespawnScheduled(s.type, s.spawnId);
        const bool live = hasInstance(s);

        if (!live && (s.pendingRespawn || !scheduled))
        {
            s.pendingRespawn = false;
            lk.unlock();
            spawnFromTemplate(s);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Grid unloading — hard-despawn live objects from this grid and drop templates.
//////////////////////////////////////////////////////////////////////////////////////////
void SpawnManager::onGridUnload(int gid)
{
    std::vector<SpawnKey> fromHome;
    std::vector<SpawnKey> fromLive;

    {
        std::unique_lock lk(mtx_);
        unloading_.insert(gid);

        if (auto it = byHome_.find(gid); it != byHome_.end())
            fromHome = it->second;

        if (auto jt = byGridNow_.find(gid); jt != byGridNow_.end())
        {
            /// Important: swap out the vector so later onGridChanged() can’t push into it anymore.
            std::vector<SpawnKey> tmp;
            tmp.swap(jt->second);
            byGridNow_.erase(jt);
            fromLive.swap(tmp);
        }
    }

    /// Merge and unique
    std::vector<SpawnKey> keys = fromHome;
    keys.insert(keys.end(), fromLive.begin(), fromLive.end());
    std::sort(keys.begin(), keys.end(), [](auto const& a, auto const& b)
        {
            return (a.type != b.type) ? a.type < b.type : a.id < b.id;
        });
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    /// O(1) membership test for items that were live in this grid.
    using SpawnKeyHasher = typename decltype(spawns_)::hasher;
    using SpawnKeyEq = typename decltype(spawns_)::key_equal;

    std::unordered_set<SpawnKey, SpawnKeyHasher, SpawnKeyEq>
        liveSet(fromLive.begin(), fromLive.end());

    std::vector<SpawnKey> toErase;

    for (auto key : keys)
    {
        std::unique_lock lk(mtx_);
        auto it = spawns_.find(key);
        if (it == spawns_.end())
            continue;

        auto& s = it->second;

        const bool inst = hasInstance(s);
        const bool inThisGrid = liveSet.count(key) > 0;

        lk.unlock();

        if (!inst)
        {
            /// Pure templates at this grid can be dropped now.
            if (s.homeGid == gid)
            {
                std::unique_lock lk2(mtx_);
                toErase.push_back(key);
            }
            continue;
        }

        if (inThisGrid)
        {
            /// Hard-despawn live object; keep respawn plan (if any).
            despawn(s.guidRaw, /*respawnAt=*/0, /*keepRespawnSchedule=*/true);
            std::unique_lock lk2(mtx_);
            toErase.push_back(key);
        }
    }

    if (!toErase.empty())
    {
        std::unique_lock lk(mtx_);
        for (auto key : toErase)
        {
            auto it = spawns_.find(key);
            if (it == spawns_.end())
                continue;

            const int curG = currentGidOfNoLock(it->second);

            /// Tear down indices
            unindexLiveSpawn_NoLock(key, it->second, curG);

            /// Remove from byHome_
            if (auto bh = byHome_.find(it->second.homeGid); bh != byHome_.end())
            {
                auto& v = bh->second;
                v.erase(std::remove(v.begin(), v.end(), key), v.end());
                if (v.empty())
                    byHome_.erase(bh);
            }

            /// Clean GUID indices
            if (it->second.guidRaw)
            {
                guidIndex_.erase(it->second.guidRaw);
                guidToSpawn_.erase(it->second.guidRaw);
            }

            spawns_.erase(it);
        }

        /// Rehash buckets when they sparsify to save memory.
        if (spawns_.bucket_count() && spawns_.size() * 3 < spawns_.bucket_count())
            spawns_.rehash(0);
    }

    /// Drop empty vectors and state for this grid.
    {
        std::unique_lock lk(mtx_);
        activeEpoch_.erase(gid);
        byHome_.erase(gid);
        byGridNow_.erase(gid);

        unloading_.erase(gid);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Object moved across grid boundaries — maintain byGridNow_ membership.
//////////////////////////////////////////////////////////////////////////////////////////
void SpawnManager::onGridChanged(const WoWGuid& guid, int oldGid, int newGid)
{
    if (oldGid == newGid) return;

    std::unique_lock lk(mtx_);

    /// Avoid race conditions while a grid is unloading.
    if (unloading_.count(oldGid) || unloading_.count(newGid))
        return;

    auto itKey = guidToSpawn_.find(guid.getRawGuid());
    if (itKey == guidToSpawn_.end())
        return;

    const SpawnKey key = itKey->second;

    auto itS = spawns_.find(key);
    if (itS == spawns_.end())
        return;

    const bool newActive = isGridActive(newGid);
    const bool oldActive = isGridActive(oldGid);

    /// Target in inactive grid → keep membership on old grid to ensure despawn on unload.
    if (!newActive)
    {
        auto& back = byGridNow_[oldGid];
        if (std::find(back.begin(), back.end(), key) == back.end())
            back.push_back(key);
        return;
    }

    /// Cleanly remove from old grid.
    if (oldActive)
    {
        if (auto it = byGridNow_.find(oldGid); it != byGridNow_.end())
        {
            auto& v = it->second;
            v.erase(std::remove(v.begin(), v.end(), key), v.end());
            if (v.empty())
                byGridNow_.erase(it);
        }
    }

    /// Add to new grid.
    auto& dst = byGridNow_[newGid];
    if (std::find(dst.begin(), dst.end(), key) == dst.end())
        dst.push_back(key);
}

//////////////////////////////////////////////////////////////////////////////////////////
/// A live creature died — either schedule a respawn or defer it.
//////////////////////////////////////////////////////////////////////////////////////////
void SpawnManager::addRespawnForCreature(Creature* creature)
{
    if (!creature)
        return;

    std::unique_lock lk(mtx_);
    SpawnKey key{ SPAWN_TYPE_CREATURE2, creature->getSpawnId() };
    auto it = spawns_.find(key);
    if (it == spawns_.end())
        return;

    auto& s = it->second;

    if (!s.respawnAllowed || !s.allowRespawn)
    {
        s.pendingRespawn = true;
        cancelRespawn(SPAWN_TYPE_CREATURE2, creature->getSpawnId());
        return;
    }
    lk.unlock();

    scheduleRespawn(SPAWN_TYPE_CREATURE2, creature->getSpawnId(), s.entry, creature->getRespawnTime());
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Respawn maps
//////////////////////////////////////////////////////////////////////////////////////////
RespawnMap& SpawnManager::getRespawnMapForType(SpawnObjectType2 type)
{
    if (type == SPAWN_TYPE_CREATURE2)
        return creatureRespawns_;
    return goRespawns_;
}

RespawnMap const& SpawnManager::getRespawnMapForType(SpawnObjectType2 type) const
{
    if (type == SPAWN_TYPE_CREATURE2)
        return creatureRespawns_;
    return goRespawns_;
}

time_t SpawnManager::getRespawnTime(SpawnObjectType2 type, uint32_t spawnId) const
{
    auto const& map = getRespawnMapForType(type);
    auto it = map.find(spawnId);
    return (it == map.end()) ? 0 : it->second->time;
}

uint32_t SpawnManager::spawnIdForGuid(uint64_t guidRaw) const
{
    std::shared_lock lk(mtx_);
    auto it = guidIndex_.find(guidRaw);
    if (it == guidIndex_.end()) return 0;
    auto sit = spawns_.find(it->second);
    if (sit == spawns_.end()) return 0;
    return sit->second.spawnId;
}

bool SpawnManager::hasSavedRespawn(SpawnObjectType2 type, uint32_t spawnId, time_t& when_out) const
{
    std::shared_lock lk(mtx_);
    auto const& mp = mapFor(type);
    auto it = mp.find(spawnId);
    if (it == mp.end())
        return false;
    when_out = it->second->time;
    return std::time(nullptr) < it->second->time;
}

void SpawnManager::ensureRespawnScheduled(SpawnObjectType2 type, uint32_t spawnId, uint32_t entry, time_t when)
{
    scheduleRespawn(type, spawnId, entry, when);
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Load respawn times from DB and (re)populate in-memory schedule.
//////////////////////////////////////////////////////////////////////////////////////////
void SpawnManager::loadRespawnTimes()
{
    /// 1) Purge expired rows for this map/instance.
    CharacterDatabase.Execute(
        "DELETE FROM respawn "
        "WHERE mapId=%u AND instanceId=%u AND respawnTime <= UNIX_TIMESTAMP()",
        map_.getBaseMap()->getMapId(), map_.getInstanceId());

    /// Build quick lookup for entry by spawnId (per type).
    std::unordered_map<uint32_t, uint32_t> creatureEntryBySid;
    std::unordered_map<uint32_t, uint32_t> goEntryBySid;

    {
        const uint32_t mapId = map_.getBaseMap()->getMapId();

        // creatures
        if (mapId < sMySQLStore._creatureSpawnsStore.size())
        {
            for (auto const* row : sMySQLStore._creatureSpawnsStore[mapId])
                creatureEntryBySid.emplace(row->id, row->entry);
        }

        // gameobjects
        if (mapId < sMySQLStore._gameobjectSpawnsStore.size())
        {
            for (auto const* row : sMySQLStore._gameobjectSpawnsStore[mapId])
                goEntryBySid.emplace(row->id, row->entry);
        }
    }

    /// 2) Load remaining (not yet expired) rows.
    auto result = CharacterDatabase.Query(
        "SELECT type, spawnId, respawnTime FROM respawn WHERE mapId=%u AND instanceId=%u",
        map_.getBaseMap()->getMapId(), map_.getInstanceId());

    if (!result)
        return;

    do 
    {
        Field* f = result->Fetch();
        auto type = static_cast<SpawnObjectType2>(f[0].asUint16());
        uint32_t spawnId = f[1].asUint32();
        time_t when = static_cast<time_t>(f[2].asUint64());

        uint32_t entry = 0;
        if (type == SPAWN_TYPE_CREATURE2)
        {
            if (auto it = creatureEntryBySid.find(spawnId); it != creatureEntryBySid.end())
                entry = it->second;
        }
        else // SPAWN_TYPE_GAMEOBJECT2
        {
            if (auto it = goEntryBySid.find(spawnId); it != goEntryBySid.end())
                entry = it->second;
        }

        scheduleRespawn(type, spawnId, entry, when);
    } while (result->NextRow());
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Process due respawns (triggered by a timer/tick).
//////////////////////////////////////////////////////////////////////////////////////////
void SpawnManager::processRespawns()
{
    const time_t now = std::time(nullptr);

    while (!respawnTimes_.empty())
    {
        RespawnInfo* top = respawnTimes_.top().get();
        if (now < top->time)
            break;

        const SpawnObjectType2 t = top->type;
        const uint32_t sid = top->spawnId;

        std::unique_lock lk(mtx_);

        /// 1) Remove from type map and queue; also clean DB row.
        auto& mp = (t == SPAWN_TYPE_CREATURE2) ? creatureRespawns_ : goRespawns_;
        mp.erase(sid);
        respawnTimes_.pop();
        deleteRespawnFromDB(t, sid);

        auto it = spawns_.find({ t, sid });
        if (it == spawns_.end())
            continue;

        auto& s = it->second;            

        /// 2) If home grid is inactive → don’t spawn, just drop template.
        const bool homeActive = (activeEpoch_.find(s.homeGid) != activeEpoch_.end());
        if (!homeActive)
        {
            // cleanly unindex & erase template
            const int curG = currentGidOfNoLock(s);           // likely -1 for template
            unindexLiveSpawn_NoLock({ t, sid }, s, curG);

            if (auto bh = byHome_.find(s.homeGid); bh != byHome_.end())
            {
                auto& v = bh->second;
                v.erase(std::remove(v.begin(), v.end(), SpawnKey{ t, sid }), v.end());
                if (v.empty()) byHome_.erase(bh);
            }

            if (s.guidRaw)
            {
                guidIndex_.erase(s.guidRaw);
                guidToSpawn_.erase(s.guidRaw);
            }

            spawns_.erase(it);
            // loop continues, nothing gets spawned now
            continue;
        }

        /// 3) Home grid active → spawn now (or defer if respawn disallowed).
        if (!s.respawnAllowed)
        {
            s.pendingRespawn = true;
            continue;
        }

        lk.unlock();
        spawnFromTemplate(s);
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Force respawn by spawn id or by live guid.
//////////////////////////////////////////////////////////////////////////////////////////
bool SpawnManager::respawnNow(SpawnObjectType2 type, uint32_t spawnId)
{
    std::unique_lock lk(mtx_);
    SpawnKey key{ type, spawnId };
    auto it = spawns_.find(key);
    if (it == spawns_.end())
        return false;

    auto& s = it->second;

    if (auto mpit = mapFor(type).find(spawnId); mpit != mapFor(type).end())
    {
        respawnTimes_.remove(mpit->second);
        mapFor(type).erase(mpit);
    }

    if (s.persistent)
        deleteRespawnFromDB(type, spawnId);

    if (!s.respawnAllowed)
        return false;

    s.pendingRespawn = false;
    lk.unlock();

    spawnFromTemplate(s);
    return true;
}

bool SpawnManager::respawnNow(SpawnObjectType2 type, uint64_t guidRaw)
{
    const uint32_t sid = spawnIdForGuid(guidRaw);
    if (sid == 0) return false;
    return respawnNow(type, sid);
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Clear all in-memory respawn info and DB rows for this map/instance.
//////////////////////////////////////////////////////////////////////////////////////////
void SpawnManager::deleteRespawnTimes()
{
    unloadAllRespawnInfos();
    deleteRespawnTimesInDB(map_.getBaseMap()->getMapId(), map_.getInstanceId());
}

void SpawnManager::unloadAllRespawnInfos()
{
    respawnTimes_.clear();
    creatureRespawns_.clear();
    goRespawns_.clear();
}

//////////////////////////////////////////////////////////////////////////////////////////
/// Helpers
//////////////////////////////////////////////////////////////////////////////////////////
bool SpawnManager::hasInstance(const SpawnState& s) const
{
    if (!s.guidRaw)
        return false;

    return (map_.getObjectByGuid(WoWGuid(s.guidRaw)) != nullptr);
}

int SpawnManager::currentGidOf(const SpawnState& s) const
{
    if (!s.guidRaw)
        return -1;

    if (auto* obj = map_.getObjectByGuid(WoWGuid(s.guidRaw)))
    {
        auto p = obj->GetPosition();
        auto [gx, gy] = worldToGrid(p);
        return packGridId(gx, gy);
    }
    return -1;
}

int SpawnManager::currentGidOfNoLock(const SpawnState& s) const
{
    if (!s.guidRaw)
        return -1;

    if (auto* obj = map_.getObjectByGuid(WoWGuid(s.guidRaw)))
    {
        auto p = obj->GetPosition();
        auto [gx, gy] = worldToGrid(p);
        return packGridId(gx, gy);
    }
    return -1;
}

void SpawnManager::spawnFromTemplate(SpawnState& s)
{
    time_t when = 0;
    if (hasSavedRespawn(s.type, s.spawnId, when))
    {
        ensureRespawnScheduled(s.type, s.spawnId, s.entry, when);
        return;
    }

    /// Already have a valid object? Re-attach instead of creating a new one.
    if (s.guidRaw)
    {
        if (auto* obj = map_.getObjectByGuid(WoWGuid(s.guidRaw)))
        {
            if (s.type == SPAWN_TYPE_CREATURE2)
            {
                if (auto* creature = obj->ToCreature())
                {
                    factory_.attachToWorld(creature);
                    creature->OnRespawn();

                    std::unique_lock lk(mtx_);
                    guidIndex_[s.guidRaw] = SpawnKey{ s.type, s.spawnId };
                    const int curG = currentGidOfNoLock(s);
                    indexLiveSpawn_NoLock({ s.type, s.spawnId }, s, curG);
                    return;
                }
            }
            else
            {
                if (auto* go = obj->ToGameObject())
                {
                    factory_.attachToWorld(go);

                    std::unique_lock lk(mtx_);
                    guidIndex_[s.guidRaw] = SpawnKey{ s.type, s.spawnId };
                    const int curG = currentGidOfNoLock(s);
                    indexLiveSpawn_NoLock({ s.type, s.spawnId }, s, curG);
                    return;
                }
            }
        }
    }

    /// Create new object from template or entry.
    if (s.type == SPAWN_TYPE_GAMEOBJECT2)
    {
        if (s.rowGO)
        {
            if (auto* go = factory_.createAndSpawnGameObjectFromSpawns(*s.rowGO))
                s.guidRaw = go->GetNewGUID().getRawGuid();
        }
        else
        {
            if (auto* go = factory_.createAndSpawnGameObject(s.entry, s.position))
                s.guidRaw = go->GetNewGUID().getRawGuid();
        }
    }
    else
    {
        if (s.rowC)
        {
            if (auto* c = factory_.createAndSpawnCreatureFromSpawns(*s.rowC))
                s.guidRaw = c->GetNewGUID().getRawGuid();
        }
        else
        {
            if (auto* c = factory_.createAndSpawnCreature(s.entry, s.position))
                s.guidRaw = c->GetNewGUID().getRawGuid();
        }
    }

    if (s.guidRaw)
    {
        std::unique_lock lk(mtx_);
        guidIndex_[s.guidRaw] = SpawnKey{ s.type, s.spawnId };
        const int curG = currentGidOfNoLock(s);
        indexLiveSpawn_NoLock({ s.type, s.spawnId }, s, curG);
    }
}

bool SpawnManager::despawn(Object* object, time_t respawnAt, bool keepRespawnSchedule /*= false*/)
{
    if (!object)
        return false;

    return despawn(object->getGuid(), respawnAt, keepRespawnSchedule);
}

bool SpawnManager::despawn(uint64_t guidRaw, time_t respawnAt /*=0*/, bool keepRespawnSchedule /*= false*/)
{
    WoWGuid guid(guidRaw);

    if (guid.isGameObject() || (guid.isUnit() && !guid.isPlayer()))
    {
        SpawnState* st = nullptr;
        SpawnKey key{};
        {
            std::unique_lock lk(mtx_);
            if (auto it = guidIndex_.find(guidRaw); it != guidIndex_.end())
            {
                key = it->second;
                if (auto sit = spawns_.find(key); sit != spawns_.end())
                    st = &sit->second;
            }
        }

        Object* obj = map_.getObjectByGuid(WoWGuid(guidRaw));
        if (!obj)
        {
            std::unique_lock lk(mtx_);
            guidIndex_.erase(guidRaw);
            guidToSpawn_.erase(guidRaw);
            if (st)
                st->guidRaw = 0;
            return false;
        }

        int curG = -1;
        {
            auto p = obj->GetPosition();
            auto [gx, gy] = worldToGrid(p);
            curG = packGridId(gx, gy);
        }

        const bool wantSoft = (respawnAt > 0) && st && st->allowRespawn && st->respawnAllowed;

        if (wantSoft)
        {
            /// Soft-despawn: keep object registered, schedule respawn.
            factory_.detachFromWorld(obj, /*keepRegistry=*/true);
            scheduleRespawn(st->type, st->spawnId, st->entry, respawnAt);
            return true;
        }

        /// Hard-despawn: remove completely.
        factory_.detachFromWorld(obj, /*keepRegistry=*/false);
        {
            std::unique_lock lk(mtx_);
            guidIndex_.erase(guidRaw);
            guidToSpawn_.erase(guidRaw);
            if (st)
                st->guidRaw = 0;

            if (curG != -1 && st)
                unindexLiveSpawn_NoLock(key, *st, curG);
        }

        factory_.recycleAndDestroy(obj, /*recycleGuid=*/true, /*destroyObject=*/true);

        if (st && !keepRespawnSchedule)
            cancelRespawn(st->type, st->spawnId);

    }
    else
    {
        Object* obj = map_.getObjectByGuid(WoWGuid(guidRaw));
        if (!obj)
            return false;

        factory_.detachFromWorld(obj, /*keepRegistry=*/false);
        factory_.recycleAndDestroy(obj, /*recycleGuid=*/true, /*destroyObject=*/true);
    }

    return true;
}

void SpawnManager::scheduleRespawn(SpawnObjectType2 type, uint32_t spawnId, uint32_t entry, time_t when)
{
    if (!spawnId || !when)
        return;

    std::unique_lock lk(mtx_);
    RespawnMap& mp = mapFor(type);
    if (auto it = mp.find(spawnId); it != mp.end())
    {
        RespawnInfo const* existing = it->second;
        if (when <= existing->time)
        {
            // Delete existing entry (we will insert a newer/earlier one below).
            respawnTimes_.remove(existing);
        }
        else
        {
            return;
        }
    }

    auto ri = std::make_unique<RespawnInfo>();
    ri->type = type;
    ri->spawnId = spawnId;
    ri->entry = entry;
    ri->time = when;

    RespawnInfo* ptr = ri.get();
    mp[spawnId] = ptr;
    respawnTimes_.emplace(std::move(ri));

    if (auto sit = spawns_.find({ type, spawnId }); sit != spawns_.end() && sit->second.persistent)
        saveRespawnDB(type, spawnId, when);
}

void SpawnManager::cancelRespawn(SpawnObjectType2 type, uint32_t spawnId)
{
    std::unique_lock lk(mtx_);
    RespawnMap& mp = mapFor(type);
    if (auto it = mp.find(spawnId); it != mp.end()) 
    {
        respawnTimes_.remove(it->second);
        if (auto sit = spawns_.find({ type, spawnId }); sit != spawns_.end() && sit->second.persistent)
            deleteRespawnFromDB(type, spawnId);
        mp.erase(it);
    }
}

bool SpawnManager::hasRespawnScheduled(SpawnObjectType2 type, uint32_t spawnId) const
{
    return mapFor(type).find(spawnId) != mapFor(type).end();
}

void SpawnManager::saveRespawnDB(SpawnObjectType2 type, uint32_t spawnId, time_t when)
{
    CharacterDatabase.Execute(
        "REPLACE INTO respawn (type, spawnId, respawnTime, mapId, instanceId) "
        "VALUES (%u, %u, %u, %u, %u)",
        static_cast<uint32_t>(type), spawnId, static_cast<uint64_t>(when),
        map_.getBaseMap()->getMapId(), map_.getInstanceId());
}

void SpawnManager::deleteRespawnFromDB(SpawnObjectType2 type, uint32_t spawnId)
{
    CharacterDatabase.Execute(
        "DELETE FROM respawn WHERE type=%u AND spawnId=%u AND mapId=%u AND instanceId=%u",
        static_cast<uint32_t>(type), spawnId,
        map_.getBaseMap()->getMapId(), map_.getInstanceId());
}

void SpawnManager::deleteRespawnTimesInDB(uint32_t mapId, uint32_t instanceId)
{
    CharacterDatabase.Execute("DELETE FROM respawn WHERE mapId = %u AND instanceId = %u", mapId, instanceId);
}

bool SpawnManager::eraseGameObjectSpawnBySpawnID(uint32_t spawnID)
{
    std::unique_lock lk(mtx_);
    uint32_t sid = 0;

    for (auto it = spawns_.begin(); it != spawns_.end(); ++it)
    {
        auto& s = it->second;
        if (s.type == SPAWN_TYPE_GAMEOBJECT2 && s.rowGO && s.rowGO->id == spawnID)
        {
            sid = s.spawnId;
            const uint64_t live = s.guidRaw;
            int home = s.homeGid;

            if (auto bh = byHome_.find(home); bh != byHome_.end())
            {
                auto& vec = bh->second;
                for (size_t i = 0; i < vec.size(); ++i)
                    if (vec[i].type == SPAWN_TYPE_GAMEOBJECT2 && vec[i].id == s.spawnId)
                    {
                        vec[i] = vec.back();
                        vec.pop_back();
                        break;
                    }

                if (vec.empty())
                    byHome_.erase(bh);
            }

            // Remove live index.
            const int curG = currentGidOfNoLock(s);
            unindexLiveSpawn_NoLock({ s.type, s.spawnId }, s, curG);

            spawns_.erase(it);
            lk.unlock();

            cancelRespawn(SPAWN_TYPE_GAMEOBJECT2, sid);

            if (live)
            {
                despawn(live);
                std::unique_lock lk2(mtx_);
                guidIndex_.erase(live);
                guidToSpawn_.erase(live);
            }
            return true;
        }
    }
    return false;
}

bool SpawnManager::eraseCreatureSpawnBySpawnID(uint32_t spawnID)
{
    std::unique_lock lk(mtx_);
    uint32_t sid = 0;

    for (auto it = spawns_.begin(); it != spawns_.end(); ++it)
    {
        auto& s = it->second;
        if (s.type == SPAWN_TYPE_CREATURE2 && s.rowC && s.rowC->id == spawnID)
        {
            sid = s.spawnId;
            const uint64_t live = s.guidRaw;
            int home = s.homeGid;

            if (auto bh = byHome_.find(home); bh != byHome_.end())
            {
                auto& vec = bh->second;
                for (size_t i = 0; i < vec.size(); ++i)
                    if (vec[i].type == SPAWN_TYPE_CREATURE2 && vec[i].id == s.spawnId)
                    {
                        vec[i] = vec.back();
                        vec.pop_back();
                        break;
                    }

                if (vec.empty())
                    byHome_.erase(bh);
            }

            // Remove live index.
            const int curG = currentGidOfNoLock(s);
            unindexLiveSpawn_NoLock({ s.type, s.spawnId }, s, curG);

            spawns_.erase(it);
            lk.unlock();

            cancelRespawn(SPAWN_TYPE_CREATURE2, sid);

            if (live)
            {
                despawn(live);
                std::unique_lock lk2(mtx_);
                guidIndex_.erase(live);
                guidToSpawn_.erase(live);
            }
            return true;
        }
    }
    return false;
}

void SpawnManager::indexLiveSpawn_NoLock(const SpawnKey& key, const SpawnState& s, int curGid)
{
    if (s.guidRaw)
        guidToSpawn_[s.guidRaw] = key;
    if (curGid >= 0)
        byGridNow_[curGid].push_back(key);
}

void SpawnManager::unindexLiveSpawn_NoLock(const SpawnKey& key, const SpawnState& s, int curGid)
{
    if (s.guidRaw)
        guidToSpawn_.erase(s.guidRaw);

    if (curGid >= 0)
    {
        if (auto it = byGridNow_.find(curGid); it != byGridNow_.end())
        {
            auto& v = it->second;
            v.erase(std::remove(v.begin(), v.end(), key), v.end());
            if (v.empty()) byGridNow_.erase(it);
        }
    }
}

void SpawnManager::dumpInconsistencies() const
{
    std::unordered_set<uint64_t> guidKeys;
    for (auto const& [g, k] : guidIndex_) guidKeys.insert(g);

    size_t tmpl = 0, liveNoIdx = 0, idxNoLive = 0;
    for (auto const& [key, s] : spawns_)
    {
        if (!s.guidRaw) { ++tmpl; continue; }
        if (!guidKeys.count(s.guidRaw)) ++liveNoIdx;
    }

    for (auto const& [g, k] : guidIndex_)
    {
        auto it = spawns_.find(k);
        if (it == spawns_.end() || it->second.guidRaw != g) ++idxNoLive;
    }

    sLogger.warning("SM audit: spawns={} live={} tmpl={} liveNoIdx={} idxNoLive={}",
        spawns_.size(), guidIndex_.size(), tmpl, liveNoIdx, idxNoLive);
}
