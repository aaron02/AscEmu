/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <functional>
#include "LocationVector.h"
#include "WoWGuid.hpp"

class GuidAllocator;
class WorldMap;
class Object;
class Creature;
class GameObject;
class Transporter;
class DynamicObject;
class Player;
class Pet;
class Corpse;

struct QuaternionData;

namespace MySQLStructure
{
    struct CreatureSpawn;
    struct GameobjectSpawn;
}

namespace world { class WorldObjectRegistry; }
namespace visibility { class VisibilitySystem; struct ObjectMeta; enum class Container : uint8_t; enum class ObjectKind : uint8_t; }

class ObjectFactory 
{
public:
    ObjectFactory(WorldMap& map, visibility::VisibilitySystem& visibilitySystem, world::WorldObjectRegistry& reg, GuidAllocator& guids);

    void attachToWorld(Object* obj);
    void detachFromWorld(Object* obj, bool keepRegistry = false, bool soft = false);
    void transferWorld(Object* obj, WorldMap* newMap);

    void recycleAndDestroy(Object* obj, bool recycleGuid, bool destroyObject);
    void recycleAndDestroy(const WoWGuid& guid, bool recycleGuid, bool destroyObject);

    // ---- Creatures ----
    Creature* createCreature(uint32_t entry);
    Creature* createAndSpawnCreature(uint32_t entry, const LocationVector& pos);
    Creature* createAndSpawnCreatureFromSpawns(const MySQLStructure::CreatureSpawn& row);

    // ---- GameObjects ----
    GameObject* createGameObject(uint32_t entry);
    GameObject* createAndSpawnGameObject(uint32_t entry, const LocationVector& pos, const QuaternionData& rotation = QuaternionData {});
    GameObject* createAndSpawnGameObjectFromSpawns(const MySQLStructure::GameobjectSpawn& row);

    // ---- Transporters ----
    Transporter* createTransporter(uint32_t entry, uint32_t mapId, const LocationVector& pos);
    Transporter* createAndSpawnTransporter(uint32_t entry, uint32_t mapId, const LocationVector& pos);

    // ---- DynamicObject ----
    DynamicObject* createDynamic();

    // ---- Corpse ----
    Corpse* createCorpse(uint64_t rawGuid = 0);

    // ---- GUID ----
    uint64_t generateCreatureGuid(uint32_t entry, bool reuse = true) const;
    uint64_t generateGameObjectGuid(uint32_t entry, bool reuse = true) const;
    uint64_t generateDynamicGuid(bool reuse = true) const;
    uint64_t generateCorpseGuid(bool reuse = true) const;

private:
    visibility::ObjectMeta makeMeta(Object* obj, const WoWGuid& g) const;

private:
    WorldMap& m_worldMap;
    visibility::VisibilitySystem& m_visibilitySystem;
    world::WorldObjectRegistry& m_objectRegistry;
    GuidAllocator& m_guidAllocator;
};

