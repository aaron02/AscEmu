/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <memory>
#include <shared_mutex>
#include <ctime>
#include <array>
#include <algorithm>
#include <cstdint>
#include "Map/Visibility/VisibilitySystem.hpp"

// Forward declarations to avoid heavy includes in the header.
class Object;
class Creature;
class GameObject;
class ObjectFactory;

class LocationVector;
struct QuaternionData;

namespace visibility
{
    class VisibilitySystem;
}

class WoWGuid;

class WorldMap;

namespace MySQLStructure
{
    struct CreatureSpawn;
    struct GameobjectSpawn;
}

using CreatureSpawnList = std::vector<MySQLStructure::CreatureSpawn*>;
using GameobjectSpawnList = std::vector<MySQLStructure::GameobjectSpawn*>;

struct GridSpawns
{
    CreatureSpawnList CreatureSpawns;
    GameobjectSpawnList GameobjectSpawns;
};

//////////////////////////////////////////////////////////////////////////////////////////
/// Lightweight templates we attach per grid when a grid becomes active.
//////////////////////////////////////////////////////////////////////////////////////////
struct CreatureSpawnTpl 
{
    uint32_t spawnId;
    uint32_t entry;
    int homeGrid;
    LocationVector homePosition;
    const MySQLStructure::CreatureSpawn* row = nullptr;
};

struct GameObjectSpawnTpl 
{
    uint32_t spawnId;
    uint32_t entry;
    int homeGrid;
    LocationVector homePosition;
    const MySQLStructure::GameobjectSpawn* row = nullptr;
};

//////////////////////////////////////////////////////////////////////////////////////////
/// Distinguishes between invalid, creature and gameobject spawns.
//////////////////////////////////////////////////////////////////////////////////////////
enum SpawnObjectType2 : uint16_t 
{
    SPAWN_TYPE_INVALID = 0,
    SPAWN_TYPE_CREATURE2 = 1,
    SPAWN_TYPE_GAMEOBJECT2 = 2
};

//////////////////////////////////////////////////////////////////////////////////////////
/// In-memory state tracked for a spawn (template + live instance bookkeeping).
//////////////////////////////////////////////////////////////////////////////////////////
struct SpawnState
{
    uint32_t spawnId { 0 };
    int      homeGid { 0 };
    SpawnObjectType2 type { SPAWN_TYPE_INVALID };
    uint32_t entry { 0 };
    LocationVector position{};

    const MySQLStructure::CreatureSpawn* rowC { nullptr };
    const MySQLStructure::GameobjectSpawn* rowGO { nullptr };

    uint64_t guidRaw{ 0 };         /// Non-zero when a live world object exists

    bool allowRespawn{ true };     /// Whether this spawn is allowed to ever respawn
    bool respawnAllowed{ true };   /// Whether respawning is currently allowed (grid policy)
    bool pendingRespawn{ false };  /// If a respawn should occur once the grid allows it
    bool persistent{ true };       /// True when it has a DB row (non-ephemeral)
};

//////////////////////////////////////////////////////////////////////////////////////////
/// Respawn scheduling payload.
//////////////////////////////////////////////////////////////////////////////////////////
struct RespawnInfo
{
    SpawnObjectType2 type;
    uint32_t spawnId;
    uint32_t entry;
    time_t   time;                  /// UNIX seconds when the spawn should respawn
};

struct CompareRespawnInfo
{
    bool operator()(std::unique_ptr<RespawnInfo> const& a, std::unique_ptr<RespawnInfo> const& b) const
    {
        if (a->time != b->time)
            return a->time > b->time; // min-heap via priority_queue invert
        if (a->spawnId != b->spawnId)
            return a->spawnId < b->spawnId;
        return a->type < b->type;
    }
};

//////////////////////////////////////////////////////////////////////////////////////////
/// Priority queue with removal/clear helpers used for respawn timing.
//////////////////////////////////////////////////////////////////////////////////////////
class respawnQueue : public std::priority_queue<std::unique_ptr<RespawnInfo>, std::vector<std::unique_ptr<RespawnInfo>>, CompareRespawnInfo>
{
public:
    bool remove(RespawnInfo const* value)
    {
        auto it = std::find_if(this->c.begin(), this->c.end(), [value](std::unique_ptr<RespawnInfo> const& respawn) { return respawn.get() == value; });
        if (it != this->c.end())
        {
            this->c.erase(it);
            std::make_heap(this->c.begin(), this->c.end(), this->comp);
            return true;
        }

        return false;
    }

    void clear()
    {
        this->c.clear();
    }
};

using RespawnMap = std::unordered_map<uint32_t, RespawnInfo*>;

//////////////////////////////////////////////////////////////////////////////////////////
/// Hashable key for spawns (type + id).
//////////////////////////////////////////////////////////////////////////////////////////
struct SpawnKey
{
    SpawnObjectType2 type;
    uint32_t id;

    bool operator==(const SpawnKey& o) const noexcept
    {
        return type == o.type && id == o.id;
    }

    bool operator!=(SpawnKey const& rhs) const noexcept
    {
        return !(*this == rhs);
    }
};

struct SpawnKeyHash
{
    size_t operator()(const SpawnKey& k) const noexcept
    {
        return (static_cast<size_t>(k.type) << 32) ^ static_cast<size_t>(k.id);
    }
};

//////////////////////////////////////////////////////////////////////////////////////////
/// SpawnManager
///
/// Central manager responsible for:
///  - Creating/destroying live instances of creatures and gameobjects
///  - Maintaining template state per grid and per live object
///  - Scheduling and persisting respawns
///  - Reacting to grid activation/unload and object movement across grids
//////////////////////////////////////////////////////////////////////////////////////////
class SpawnManager 
{
public:
    /// Construct a spawn manager for a given world map.
    /// \param map The owning world map (instance-aware).
    /// \param vis The visibility system for active grid checks.
    /// \param factory Factory responsible for creating/attaching/destroying objects.
    SpawnManager(WorldMap& map, visibility::VisibilitySystem& visibilitySystem, ObjectFactory& factory);


    //----------------------------------------------------------------------------------
    // Grid Spawns
    //----------------------------------------------------------------------------------
    void loadSpawns(bool reload);    // set to true to make clean up
    GridSpawns* getSpawnsList(int gid);
    GridSpawns* getSpawnsListAndCreate(int gid);

    //----------------------------------------------------------------------------------
    // Spawning API
    //----------------------------------------------------------------------------------
    /// Summon a creature not backed by DB (non-persistent).
    Creature* summonCreature(uint32_t entry, LocationVector const& pos, bool noRespawn = true);

    /// Spawn a persistent creature (row provided) or create a DB row on the fly.
    Creature* spawnCreature(uint32_t entry, LocationVector const& pos, const MySQLStructure::CreatureSpawn* row = nullptr);

    /// Summon a gameobject not backed by DB (non-persistent).
    GameObject* summonGameObject(uint32_t entry, LocationVector const& pos, QuaternionData const& rotation = QuaternionData {}, bool noRespawn = true);
    
    /// Spawn a persistent gameobject (row provided) or create a DB row on the fly.
    GameObject* spawnGameObject(uint32_t entry, LocationVector const& pos, QuaternionData const& rotation = QuaternionData {}, const MySQLStructure::GameobjectSpawn* row = nullptr);

    //----------------------------------------------------------------------------------
    // Despawn
    //----------------------------------------------------------------------------------
    bool despawn(Object* object, time_t respawnAt = 0, bool keepRespawnSchedule = false);
    bool despawn(uint64_t guidRaw, time_t respawnAt = 0, bool keepRespawnSchedule = false);

    //----------------------------------------------------------------------------------
    // Grid lifecycle
    //----------------------------------------------------------------------------------
    /// Attach spawn templates for a grid that just became active.
    void attachTemplatesForGrid(int gid, const CreatureSpawnList& creatures, const GameobjectSpawnList& gameobjects);

    /// Mark grid as active and spawn due instances.
    void onGridActivated(int gid);

    /// Grid unload handler: despawn live objects and drop templates from indices.
    void onGridUnload(int gid);

    /// Maintain live index when an object moves across grids.
    void onGridChanged(const WoWGuid& guid, int oldGid, int newGid);

    //----------------------------------------------------------------------------------
    // Respawn
    //----------------------------------------------------------------------------------
    /// Record a creature death and schedule or defer its respawn.
    void addRespawnForCreature(Creature* c);

    RespawnMap& getRespawnMapForType(SpawnObjectType2 type);
    RespawnMap const& getRespawnMapForType(SpawnObjectType2 type) const;

    time_t getRespawnTime(SpawnObjectType2 type, uint32_t spawnId) const;
    time_t getCreatureRespawnTime(uint32_t spawnId) const { return getRespawnTime(SPAWN_TYPE_CREATURE2, spawnId); }
    time_t getGORespawnTime(uint32_t spawnId) const { return getRespawnTime(SPAWN_TYPE_GAMEOBJECT2, spawnId); }

    bool hasSavedRespawn(SpawnObjectType2 type, uint32_t spawnId, time_t& when_out) const;
    void ensureRespawnScheduled(SpawnObjectType2 type, uint32_t spawnId, uint32_t entry, time_t when);

    /// Load respawn times from DB and rebuild schedule.
    void loadRespawnTimes();

    /// Process due respawns (to be called from a periodic tick).
    void processRespawns();

    /// Force respawn now by spawn id or by live guid.
    bool respawnNow(SpawnObjectType2 type, uint32_t spawnId);
    bool respawnNow(SpawnObjectType2 type, uint64_t guidRaw);
    uint32_t spawnIdForGuid(uint64_t guidRaw) const;

    /// Clear all in-memory respawn info and DB rows for current map/instance.
    void deleteRespawnTimes();
    void unloadAllRespawnInfos();

    //----------------------------------------------------------------------------------
    // Despawn / DB helpers
    //----------------------------------------------------------------------------------
    void  scheduleRespawn(SpawnObjectType2 type, uint32_t spawnId, uint32_t entry, time_t when);
    void  cancelRespawn(SpawnObjectType2 type, uint32_t spawnId);
    bool  hasRespawnScheduled(SpawnObjectType2 type, uint32_t spawnId) const;

    void  saveRespawnDB(SpawnObjectType2 type, uint32_t spawnId, time_t when);
    void  deleteRespawnFromDB(SpawnObjectType2 type, uint32_t spawnId);
    static void deleteRespawnTimesInDB(uint32_t mapId, uint32_t instanceId);

    bool eraseGameObjectSpawnBySpawnID(uint32_t spawnID);
    bool eraseCreatureSpawnBySpawnID(uint32_t spawnID);

    //----------------------------------------------------------------------------------
    // Debug / audit
    //----------------------------------------------------------------------------------
    /// Print internal consistency stats to the logger.
    void dumpInconsistencies() const;

private:
    /// True if there is a live instance for the spawn state.
    bool  hasInstance(const SpawnState& s) const;

    /// Compute current grid id for the live instance (or -1).
    int   currentGidOf(const SpawnState& s) const;

    /// Same as currentGidOf but assumes caller holds the manager's lock where needed.
    int   currentGidOfNoLock(const SpawnState& s) const;

    /// Construct (or re-attach) a live instance from the stored template.
    void  spawnFromTemplate(SpawnState& s);

    /// Convenience access to respawn map for a given type (non-const/const).
    RespawnMap& mapFor(SpawnObjectType2 t) { return t == SPAWN_TYPE_CREATURE2 ? creatureRespawns_ : goRespawns_; }
    RespawnMap const& mapFor(SpawnObjectType2 t) const { return t == SPAWN_TYPE_CREATURE2 ? creatureRespawns_ : goRespawns_; }

    /// True if grid is currently considered active.
    inline bool isGridActive(int gid) const { return activeEpoch_.find(gid) != activeEpoch_.end() && unloading_.find(gid) == unloading_.end(); }

    /// Index and unindex helpers (expect caller to hold lock as indicated).
    void indexLiveSpawn_NoLock(const SpawnKey& key, const SpawnState& s, int curGid);
    void unindexLiveSpawn_NoLock(const SpawnKey& key, const SpawnState& s, int curGid);

    /// Allocate a temporary id for non-persistent summons.
    inline uint32_t allocEphemeralId() { return EPHEMERAL_MASK | (nextEphemeralId_++); }

private:
    // Core services
    WorldMap& map_;
    visibility::VisibilitySystem& visibilitySystem_;
    ObjectFactory& factory_;

    // Grid Spawns
    uint32_t CreatureSpawnCount{ 0 };
    uint32_t GameObjectSpawnCount{ 0 };

    static constexpr int kTiles = visibility::Terrain::TilesCount;      // 64
    static constexpr int kGrids = kTiles * kTiles;                      // 4096

    std::array<std::unique_ptr<GridSpawns>, kGrids> spawns{};

    mutable std::shared_mutex spawnsMtx_;

    // Synchronization
    mutable std::shared_mutex mtx_;

    // Templates and live bookkeeping
    std::unordered_map<SpawnKey, SpawnState, SpawnKeyHash>  spawns_;        // (type,id) -> state
    std::unordered_map<int, std::vector<SpawnKey>>          byHome_;        // gid -> spawn keys
    std::unordered_map<int, std::vector<SpawnKey>>          byGridNow_;     // current gid -> spawn keys

    // GUID <-> spawn key indices
    std::unordered_map<uint64_t, SpawnKey>                  guidIndex_;     // guid -> SpawnKey
    std::unordered_map<uint64_t, SpawnKey>                  guidToSpawn_;   // guid -> SpawnKey (movetracking)

    // Active epoch and unloading guards
    std::unordered_set<int>                                 activeEpoch_;   // active grids
    std::unordered_set<int>                                 unloading_;     // grids to be removed

    // Respawn
    respawnQueue respawnTimes_;
    RespawnMap creatureRespawns_;
    RespawnMap goRespawns_;

    // Ephemeral id counter for summons
    uint32_t nextEphemeralId_ = 1;
    static constexpr uint32_t EPHEMERAL_MASK = 0x80000000;
};
