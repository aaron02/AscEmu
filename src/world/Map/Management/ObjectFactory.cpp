/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "ObjectFactory.hpp"
#include "GuidAllocator.hpp"
#include "WorldObjectRegistry.hpp"
#include "Map/Visibility/VisibilitySystem.hpp"
#include "Map/Maps/WorldMap.hpp"
#include "Server/Script/InstanceScript.hpp"
#include "Server/World.h"

// Engine
#include "Objects/Object.hpp"
#include "Objects/Units/Creatures/Creature.h"
#include "Objects/GameObject.h"
#include "Objects/Transporter.hpp"
#include "Objects/DynamicObject.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Objects/Units/Creatures/Pet.h"
#include "Objects/Units/Creatures/Corpse.hpp"

using visibility::ObjectKind;
using Maker = GameObject* (*)(uint64_t);

ObjectFactory::ObjectFactory(WorldMap& map, visibility::VisibilitySystem& visibilitySystem, world::WorldObjectRegistry& reg, GuidAllocator& guids) 
    : m_worldMap(map), m_visibilitySystem(visibilitySystem), m_objectRegistry(reg), m_guidAllocator(guids)
{
}

template <class T> static GameObject* make(uint64_t g) { return new T(g); }
static const std::unordered_map<int, Maker> registry =
{
    { GAMEOBJECT_TYPE_DOOR,                  &make<GameObject_Door> },
    { GAMEOBJECT_TYPE_BUTTON,                &make<GameObject_Button> },
    { GAMEOBJECT_TYPE_QUESTGIVER,            &make<GameObject_QuestGiver> },
    { GAMEOBJECT_TYPE_CHEST,                 &make<GameObject_Chest> },
    { GAMEOBJECT_TYPE_TRAP,                  &make<GameObject_Trap> },
    { GAMEOBJECT_TYPE_CHAIR,                 &make<GameObject_Chair> },
    { GAMEOBJECT_TYPE_SPELL_FOCUS,           &make<GameObject_SpellFocus> },
    { GAMEOBJECT_TYPE_GOOBER,                &make<GameObject_Goober> },
    { GAMEOBJECT_TYPE_TRANSPORT,             &make<GameObject_Transport> },
    { GAMEOBJECT_TYPE_CAMERA,                &make<GameObject_Camera> },
    { GAMEOBJECT_TYPE_FISHINGNODE,           &make<GameObject_FishingNode> },
    { GAMEOBJECT_TYPE_RITUAL,                &make<GameObject_Ritual> },
    { GAMEOBJECT_TYPE_SPELLCASTER,           &make<GameObject_SpellCaster> },
    { GAMEOBJECT_TYPE_MEETINGSTONE,          &make<GameObject_Meetingstone> },
    { GAMEOBJECT_TYPE_FLAGSTAND,             &make<GameObject_FlagStand> },
    { GAMEOBJECT_TYPE_FISHINGHOLE,           &make<GameObject_FishingHole> },
    { GAMEOBJECT_TYPE_FLAGDROP,              &make<GameObject_FlagDrop> },
    { GAMEOBJECT_TYPE_BARBER_CHAIR,          &make<GameObject_BarberChair> },
    { GAMEOBJECT_TYPE_DESTRUCTIBLE_BUILDING, &make<GameObject_Destructible> },
};

visibility::ObjectMeta ObjectFactory::makeMeta(Object* obj, const WoWGuid& g) const
{
    visibility::ObjectMeta m;
    m.guid = g;
    
    if (dynamic_cast<Player*>(obj))
    {
        m.container = visibility::Container::Players;
        m.kind = ObjectKind::Player;
        m.flags = visibility::UpdateFlags::CanActivate;
    }
    else if (dynamic_cast<Pet*>(obj))
    {
        m.container = visibility::Container::Pets;
        m.kind = ObjectKind::Pet;
        m.flags = visibility::UpdateFlags::None;
    }
    else if (dynamic_cast<Creature*>(obj))
    {
        m.container = visibility::Container::Creatures;
        m.kind = ObjectKind::Creature;
        m.flags = visibility::UpdateFlags::None;
    }
    else if (dynamic_cast<Transporter*>(obj))
    {
        m.container = visibility::Container::Transporter;
        m.kind = ObjectKind::Transporter;
        m.flags = visibility::UpdateFlags::CanActivate;
    }
    else if (dynamic_cast<GameObject*>(obj))
    {
        m.container = visibility::Container::GameObjects;
        m.kind = ObjectKind::GameObject;
        m.flags = visibility::UpdateFlags::None;
    }
    else if (dynamic_cast<DynamicObject*>(obj))
    {
        m.container = visibility::Container::DynamicObjects;
        m.kind = ObjectKind::Dynamic;
        m.flags = visibility::UpdateFlags::None;
    }
    else if (dynamic_cast<Corpse*>(obj))
    {
        m.container = visibility::Container::Corpses;
        m.kind = ObjectKind::Corpse;
        m.flags = visibility::UpdateFlags::None;
    }
    else
    {
        m.container = visibility::Container::Unk;
        m.kind = ObjectKind::Unk;
        m.flags = visibility::UpdateFlags::None;
    }
    return m;
}

void ObjectFactory::recycleAndDestroy(Object* obj, bool recycleGuid, bool destroyObject)
{
    if (!obj)
        return;

    const WoWGuid guid = obj->GetNewGUID();

    if (recycleGuid)
        m_guidAllocator.release(guid.getRawGuid());

    if (destroyObject)
        m_worldMap.deferDestroy(obj);
}

void ObjectFactory::recycleAndDestroy(const WoWGuid& guid, bool recycleGuid, bool destroyObject)
{
    if (Object* object = m_objectRegistry.getAny(guid))
    {
        recycleAndDestroy(object, recycleGuid, destroyObject);
    }
}

void ObjectFactory::attachToWorld(Object* obj, const LocationVector& pos, bool asActivator, bool asViewer)
{
    if (!obj)
        return;

    // Registry-Index
    if (!m_objectRegistry.contains(obj->GetNewGUID()))
    {
        m_objectRegistry.insert(obj);
    }

    // Register the Object to this World
    obj->registerToWorld(m_worldMap);

    // Object Created and Ready. Call Events before Attached to Visibility
    obj->onPreAttachToWorld();

    auto meta = makeMeta(obj, obj->GetNewGUID());
    auto h = m_worldMap.getSpatialIndex().addObject(meta, pos, obj);
    m_visibilitySystem.onObjectAdded(h);

    auto interest = m_visibilitySystem.buildInterestProfile(obj);

    if (asViewer)
        interest.viewer = true;

    if (asActivator)
        interest.activator = true;

    if (interest.viewer && interest.viewerSubscribeCells <= 0)
        interest.viewerSubscribeCells = worldConfig.server.mapCellNumber;

    if (interest.activator && interest.activatorSubscribeCells <= 0)
        interest.activatorSubscribeCells = worldConfig.server.mapCellNumber;

    m_visibilitySystem.applyInterestProfile(h, interest);

    m_visibilitySystem.drain();

    // Player::onAttachToWorld() immediately calls processPendingUpdates() while the
    // client is still loading. Flush this player's initial visibility batch now so
    // login/worldport keeps the old synchronous behavior for the entering player.
    if (obj->isPlayer())
        m_worldMap.processPendingVisibilityChangesForViewer(obj->GetNewGUID(), 256, 256);

    // Call Events for Object after Attaching to Visibility
    obj->onAttachToWorld();

    // Script-Hook
    if (auto* script = m_worldMap.getScript())
    {
        if (obj->isGameObject())
            script->OnGameObjectPushToWorld(obj->ToGameObject());
        else if (obj->isCreature())
            script->OnCreaturePushToWorld(obj->ToCreature());

        script->addObject(obj);
    }
}

void ObjectFactory::detachFromWorld(Object* obj, bool keepRegistry /*= false*/, bool soft /*= false*/)
{
    if (!obj)
        return;

    if (auto* script = m_worldMap.getScript())
        script->removeObject(obj);

    // Call Event before we Invalidate all Info
    obj->onPreDetachFromWorld();

    const WoWGuid guid = obj->GetNewGUID();

    if (auto h = m_worldMap.getSpatialIndex().handleByGuid(guid); h.id)
    {
        m_visibilitySystem.onObjectRemoving(h);
        const bool removed = m_worldMap.getSpatialIndex().removeObject(h);
        if (!removed || m_worldMap.getSpatialIndex().handleByGuid(guid).id)
            sLogger.warning("vis sanity: spatial detach incomplete guid={} removed={} handleStillPresent={}", guid.getRawGuid(), removed, m_worldMap.getSpatialIndex().handleByGuid(guid).id != 0);

        m_visibilitySystem.drain();
        m_worldMap.purgePendingVisibilityForGuid(guid);
    }

    // Call Events for Object after Detaching from Visibility
    if (!soft)
        obj->onDetachFromWorld();

    // Unregister us from Worldmap
    obj->unregisterFromWorld();

    if (!keepRegistry)
        m_objectRegistry.erase(guid);
}

void ObjectFactory::transferWorld(Object* obj, WorldMap* newMap)
{
    if (!obj || !newMap)
        return;

    const WoWGuid guid = obj->GetNewGUID();
    auto removeHandle = m_worldMap.getSpatialIndex().handleByGuid(guid);

    // Unregister us from Worldmap
    obj->unregisterFromWorld();

    // Remove from old Maps Storage
    m_objectRegistry.erase(guid);

    // Remove from old World
    if (auto* script = m_worldMap.getScript())
        script->removeObject(obj);

    // Registry-Index at new World
    if (!newMap->getRegistry().contains(guid))
    {
        newMap->getRegistry().insert(obj);
    }

    // Register the Object to this World
    obj->registerToWorld(*newMap);

    auto meta = makeMeta(obj, obj->GetNewGUID());
    auto h = newMap->getSpatialIndex().addObject(meta, obj->GetPosition(), obj);
    newMap->getVisibilitySystem().onObjectAdded(h);

    auto interest = newMap->getVisibilitySystem().buildInterestProfile(obj);

    if (interest.viewer && interest.viewerSubscribeCells <= 0)
        interest.viewerSubscribeCells = worldConfig.server.mapCellNumber;

    if (interest.activator && interest.activatorSubscribeCells <= 0)
        interest.activatorSubscribeCells = worldConfig.server.mapCellNumber;

    newMap->getVisibilitySystem().applyInterestProfile(h, interest);

    newMap->getVisibilitySystem().drain();

    // Script-Hook
    if (auto* script = m_worldMap.getScript())
    {
        if (obj->isGameObject())
            script->OnGameObjectPushToWorld(obj->ToGameObject());
        else if (obj->isCreature())
            script->OnCreaturePushToWorld(obj->ToCreature());

        script->addObject(obj);
    }

    // If we are a Transporter Teleport the Passengers
    if (obj->isTransporter())
    {
        LocationVector transPosition = obj->GetPosition();

        reinterpret_cast<Transporter*>(obj)->TeleportPlayers(
            transPosition.x,
            transPosition.y,
            transPosition.z,
            transPosition.o,
            newMap->getBaseMap()->getMapId(),
            m_worldMap.getBaseMap()->getMapId(),
            true);

        // Relocate Passengers
        reinterpret_cast<Transporter*>(obj)->UpdatePosition(transPosition.x, transPosition.y, transPosition.z, transPosition.o);
    }

    // Tell the Old Maps Visibility System to Removce us
    if (removeHandle.id)
    {
        m_visibilitySystem.onObjectRemoving(removeHandle);
        const bool removed = m_worldMap.getSpatialIndex().removeObject(removeHandle);
        if (!removed || m_worldMap.getSpatialIndex().handleByGuid(guid).id)
            sLogger.warning("vis sanity: transfer detach incomplete guid={} removed={} handleStillPresent={}", guid.getRawGuid(), removed, m_worldMap.getSpatialIndex().handleByGuid(guid).id != 0);

        m_visibilitySystem.drain();
        m_worldMap.purgePendingVisibilityForGuid(guid);
    }
}

// ===== Creatures =====
Creature* ObjectFactory::createCreature(uint32_t entry)
{
    bool isVehicle = false;

    if (auto props = sMySQLStore.getCreatureProperties(entry)) 
    {
        isVehicle = props->vehicleid != 0;
    }

    uint64_t guid = m_guidAllocator.allocCreature(entry, true, isVehicle);

    Creature* creature = new Creature(guid);
    if (!creature)
        return nullptr;

    return creature;
}

Creature* ObjectFactory::createAndSpawnCreature(uint32_t entry, const LocationVector& pos)
{
    Creature* creature = createCreature(entry);
    if (!creature)
        return nullptr;

    const auto* creature_info = sMySQLStore.getCreatureProperties(entry);
    if (creature_info == nullptr)
    {
        sLogger.debug("Error looking up entry in createAndSpawnCreature");
        delete creature;
        return nullptr;
    }

    sLogger.debug("createAndSpawnCreature: By Entry '{}'", entry);

    creature->Load(creature_info, pos.x, pos.y, pos.z, pos.o);
    
    attachToWorld(creature, pos, false, false);
    return creature;
}

Creature* ObjectFactory::createAndSpawnCreatureFromSpawns(const MySQLStructure::CreatureSpawn& rowIn)
{
    Creature* creature = createCreature(rowIn.entry);
    if (!creature)
        return nullptr;

    creature->m_loadedFromDB = true;
    if (!creature->LoadFromDB(const_cast<MySQLStructure::CreatureSpawn*>(&rowIn), &m_worldMap, /*addToWorldLater=*/false) || !creature->CanAddToWorld())
    {
        delete creature;
        return nullptr;
    }

    attachToWorld(creature, creature->GetPosition(), /*asActivator=*/false, /*asViewer=*/false);
    return creature;
}

// ===== GameObjects =====
GameObject* ObjectFactory::createGameObject(uint32_t entry)
{
    GameObjectProperties const* gameobjectProperties = sMySQLStore.getGameObjectProperties(entry);
    if (gameobjectProperties == nullptr)
        return nullptr;

    uint64_t guid = m_guidAllocator.allocGameObject(entry, true);

    GameObject* gameObject = nullptr;
    if (const auto it = registry.find(gameobjectProperties->type); it != registry.end())
        gameObject = it->second(guid);
    else
        gameObject = new GameObject(guid); // Default

    gameObject->SetGameObjectProperties(gameobjectProperties);

    return gameObject;
}

GameObject* ObjectFactory::createAndSpawnGameObject(uint32_t entry, const LocationVector& pos, const QuaternionData& rotation)
{
    GameObject* gameObject = createGameObject(entry);
    if (!gameObject)
        return nullptr;

    auto gameobject_info = sMySQLStore.getGameObjectProperties(entry);
    if (gameobject_info == nullptr)
    {
        sLogger.debug("Error looking up entry in createAndSpawnGameObject");
        delete gameObject;
        return nullptr;
    }

    sLogger.debug("CreateAndSpawnGameObject: By Entry '{}'", entry);

    gameObject->create(entry, &m_worldMap, gameObject->GetPhase(), pos, rotation, GO_STATE_CLOSED, sObjectMgr.generateGameObjectSpawnId());

    attachToWorld(gameObject, pos, false, false);
    return gameObject;
}

GameObject* ObjectFactory::createAndSpawnGameObjectFromSpawns(const MySQLStructure::GameobjectSpawn& rowIn)
{
    GameObject* go = createGameObject(rowIn.entry);
    if (!go) 
        return nullptr;

    go->m_loadedFromDB = true;
    if (!go->loadFromDB(const_cast<MySQLStructure::GameobjectSpawn*>(&rowIn), &m_worldMap, /*addToWorldLater=*/false))
    {
        delete go;
        return nullptr;
    }

    attachToWorld(go, rowIn.spawnPoint, /*asActivator=*/false, /*asViewer=*/false);
    return go;
}

// ===== Transporter =====
Transporter* ObjectFactory::createTransporter(uint32_t entry, uint32_t mapId, const LocationVector& pos)
{
    uint64_t guid = m_guidAllocator.allocTransporter(entry, true);

    Transporter* trans = new Transporter(guid);
    if (!trans->Create(entry, mapId, pos.x, pos.y, pos.z, pos.o, 255))
    {
        delete trans;
        return nullptr;
    }

    return trans;
}

Transporter* ObjectFactory::createAndSpawnTransporter(uint32_t entry, uint32_t mapId, const LocationVector& pos)
{
    Transporter* trans = createTransporter(entry, mapId, pos);
    if (!trans)
        return nullptr;

    attachToWorld(trans, pos, /*asActivator=*/true, /*asViewer=*/false);
    return trans;
}

// ===== DynamicObjects =====
DynamicObject* ObjectFactory::createDynamic()
{
    uint64_t guid = m_guidAllocator.allocDynamicObject(0, true);

    DynamicObject* dynamicObject = new DynamicObject(guid);

    if (!dynamicObject)
        return nullptr;

    return dynamicObject;
}

// ===== Corpse =====
Corpse* ObjectFactory::createCorpse()
{
    uint64_t guid = m_guidAllocator.allocCorpse(true);

    Corpse* corpse = new Corpse(guid);
    if (!corpse)
        return nullptr;

    return corpse;
}

// ---- GUID Passthroughs ----
uint64_t ObjectFactory::generateCreatureGuid(uint32_t entry, bool reuse) const { return const_cast<GuidAllocator&>(m_guidAllocator).allocCreature(entry, reuse, sMySQLStore.getCreatureProperties(entry) && sMySQLStore.getCreatureProperties(entry)->vehicleid != 0); }
uint64_t ObjectFactory::generateGameObjectGuid(uint32_t entry, bool reuse) const { return const_cast<GuidAllocator&>(m_guidAllocator).allocGameObject(entry, reuse); }
uint64_t ObjectFactory::generateDynamicGuid(bool reuse) const { return const_cast<GuidAllocator&>(m_guidAllocator).allocDynamicObject(0, reuse); }
uint64_t ObjectFactory::generateCorpseGuid(bool reuse) const { return const_cast<GuidAllocator&>(m_guidAllocator).allocCorpse(reuse); }
