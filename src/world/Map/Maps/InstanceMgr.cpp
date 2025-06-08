/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include <cstdint>

#include "Storage/WDB/WDBStores.hpp"
#include "InstanceMgr.hpp"

#include "InstanceMap.hpp"
#include "Logging/Logger.hpp"
#include "Management/Group.h"
#include "Storage/MySQLDataStore.hpp"
#include "Map/Management/MapMgr.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Server/DatabaseDefinition.hpp"
#include "Server/Definitions.h"
#include "Server/World.h"
#include "Server/Script/InstanceScript.hpp"
#include "Server/Script/ScriptMgr.hpp"
#include "Storage/WDB/WDBStructures.hpp"
#include "Utilities/Narrow.hpp"

InstanceSaved::InstanceSaved(uint32_t mapId, uint32_t instanceId, InstanceDifficulty::Difficulties difficulty, time_t resetTime, bool canReset)
    : m_resetTime(resetTime),
    m_instanceid(instanceId),
    m_mapid(mapId),
    m_difficulty(difficulty),
    m_canReset(canReset)
{
}

InstanceSaved::~InstanceSaved()
{
}

void InstanceSaved::addPlayer(Player* player)
{
    std::scoped_lock<std::mutex> lock(_playerListLock);
    m_playerList.push_back(player);
}

bool InstanceSaved::removePlayer(Player* player)
{
    {
        std::scoped_lock<std::mutex> lock(_playerListLock);
        m_playerList.remove(player);
    }

    //delete here if needed, after releasing the lock
    return unloadIfEmpty();
}

void InstanceSaved::addGroup(Group* group)
{
    m_groupList.push_back(group);
}

bool InstanceSaved::removeGroup(Group* group)
{
    m_groupList.remove(group);
    return unloadIfEmpty();
}

void InstanceSaved::saveToDB()
{
    // completed encounters as String
    std::string data;

    uint32_t completedEncounters = 0;

    WorldMap* map = sMapMgr.findInstanceMap(m_instanceid);
    if (map)
    {
        if (map->getBaseMap()->isDungeon())
        {
            if (InstanceScript* instanceScript = map->getScript())
            {
                data = instanceScript->getSaveData();
                completedEncounters = instanceScript->getCompletedEncounterMask();
            }
        }
    }

    auto stmt = CharacterDatabase.CreateStatement(CHAR_INSTANCE_INSERT);
    stmt->Bind(0, m_instanceid);
    stmt->Bind(1, getMapId());
    stmt->Bind(2, static_cast<uint64_t>(getResetTimeForDB()));
    stmt->Bind(3, static_cast<uint8_t>(getDifficulty()));
    stmt->Bind(4, completedEncounters);
    stmt->Bind(5, data);

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

void InstanceSaved::deleteFromDB()
{
    InstanceMgr::deleteInstanceFromDB(getInstanceId());
}

time_t InstanceSaved::getResetTimeForDB()
{
    // only save the reset time for normal instances
    WDB::Structures::MapEntry const* entry = sMapStore.lookupEntry(getMapId());
    if (!entry || entry->map_type == MAP_RAID || getDifficulty() == InstanceDifficulty::Difficulties::DUNGEON_HEROIC)
        return 0;
    else
        return getResetTime();
}

bool InstanceSaved::unloadIfEmpty()
{
    if (m_playerList.empty() && m_groupList.empty())
    {
        // don't remove the save if there are still players inside the map
        if (WorldMap* map = sMapMgr.findWorldMap(getMapId(), getInstanceId()))
            if (map->getPlayerCount())
                return true;

        if (!sInstanceMgr.lock_instanceLists)
            sInstanceMgr.removeInstanceSave(getInstanceId());

        return false;
    }
    else
    {
        return true;
    }
}

uint16_t InstanceMgr::ResetTimeDelay[] = { 3600, 900, 300, 60 };

InstanceMgr& InstanceMgr::getInstance()
{
    static InstanceMgr mInstance;
    return mInstance;
}

void InstanceMgr::loadInstances()
{
    const auto oldTime = Util::TimeNow();

    // Delete expired instances
    {
        auto stmt = CharacterDatabase.CreateStatement(CHAR_INSTANCE_DELETE_EXPIRED);
        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }

    // Delete invalid character_instance and group_instance references
    {
        auto stmt1 = CharacterDatabase.CreateStatement(CHAR_INSTANCE_DELETE_INVALID_CHARACTERS);
        CharacterDatabase.ExecuteStatement(std::move(stmt1));

        auto stmt2 = CharacterDatabase.CreateStatement(CHAR_INSTANCE_DELETE_INVALID_GROUPS);
        CharacterDatabase.ExecuteStatement(std::move(stmt2));
    }

    // Delete orphaned instances
    {
        auto stmt = CharacterDatabase.CreateStatement(CHAR_INSTANCE_DELETE_ORPHANED_INSTANCES);
        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }

    // Delete invalid respawn references
    {
        auto stmt1 = CharacterDatabase.CreateStatement(CHAR_RESPAWN_DELETE_INVALID);
        CharacterDatabase.ExecuteStatement(std::move(stmt1));

        auto stmt2 = CharacterDatabase.CreateStatement(CHAR_CHARACTER_INSTANCE_DELETE_INVALID);
        CharacterDatabase.ExecuteStatement(std::move(stmt2));

        auto stmt3 = CharacterDatabase.CreateStatement(CHAR_GROUP_INSTANCE_DELETE_INVALID);
        CharacterDatabase.ExecuteStatement(std::move(stmt3));
    }

    // Clean corpses / characters with invalid instance refs
    {
        auto stmt1 = CharacterDatabase.CreateStatement(CHAR_CORPSE_CLEAN_INVALID_INSTANCE);
        CharacterDatabase.ExecuteStatement(std::move(stmt1));

        auto stmt2 = CharacterDatabase.CreateStatement(CHAR_CHARACTER_CLEAN_INVALID_INSTANCE);
        CharacterDatabase.ExecuteStatement(std::move(stmt2));
    }

    // Initialize & Load
    sMapMgr.initializeInstanceIds();
    sInstanceMgr.loadResetTimes();

    sLogger.info("Loaded instances in {} ms", Util::GetTimeDifferenceToNow(oldTime));
}

void InstanceMgr::loadResetTimes()
{
    const auto now = Util::getTimeNow();
    time_t today = (now / DAY) * DAY;

    // resettime = 0 in the DB for raid/heroic instances
    //                   map/difficulty, resetTime
    typedef std::pair<uint32_t, time_t> ResetTimeMapDiffType;
    typedef std::map<uint32_t, ResetTimeMapDiffType> InstResetTimeMapDiffType;
    InstResetTimeMapDiffType instResetTime;

    // index instance ids by map/difficulty pairs
    //                   map/difficulty, instanceId
    typedef std::multimap<uint32_t, uint32_t> ResetTimeMapDiffInstances;
    typedef std::pair<ResetTimeMapDiffInstances::const_iterator, ResetTimeMapDiffInstances::const_iterator> ResetTimeMapDiffInstancesBounds;
    ResetTimeMapDiffInstances mapDiffResetInstances;

    auto stmt = CharacterDatabase.CreateStatement(CHAR_INSTANCE_RESETTIME_SELECT);
    if (auto result = CharacterDatabase.QueryStatement(std::move(stmt)))
    {
        do
        {
            Field* fields = result->Fetch();

            uint32_t instanceId = fields[0].asUint32();

            // Add our Current Instance Id as Used to our Pool
            sMapMgr.instanceIdPool.addUsedValue(instanceId);

            if (time_t resettime = static_cast<time_t>(fields[3].asUint64()))
            {
                auto mapid = fields[1].asUint16();
                auto difficulty = fields[2].asUint8();

                instResetTime[instanceId] = ResetTimeMapDiffType(Util::MAKE_PAIR32(mapid, difficulty), resettime);
                mapDiffResetInstances.insert(ResetTimeMapDiffInstances::value_type(Util::MAKE_PAIR32(mapid, difficulty), instanceId));
            }
        } while (result->NextRow());

        // Add Event for our Resettime
        for (InstResetTimeMapDiffType::iterator itr = instResetTime.begin(); itr != instResetTime.end(); ++itr)
        {
            if (itr->second.second > now)
            {
                DifficultyMap map = DifficultyMap();
                map.MapPair = itr->second.first;

                addResetEvent(true, itr->second.second, InstResetEvent(0, map.MapPairParts.mapid, InstanceDifficulty::Difficulties(map.MapPairParts.difficulty), static_cast<uint16_t>(itr->first)));
            }
        }
    }

    // load the global resettimes for raid and heroic instances
    auto resetHour = static_cast<uint8_t>(worldConfig.instance.relativeDailyHeroicInstanceResetHour);
    auto stmt2 = CharacterDatabase.CreateStatement(CHAR_INSTANCE_MAPDATA_SELECT);
    if (auto result = CharacterDatabase.QueryStatement(std::move(stmt2)))
    {
        do
        {
            Field* fields = result->Fetch();
            auto mapid = fields[0].asUint16();
            InstanceDifficulty::Difficulties difficulty = InstanceDifficulty::Difficulties(fields[1].asUint8());
            uint64_t oldresettime = fields[2].asUint64();

            WDB::Structures::MapDifficulty const* mapDiff = getMapDifficultyData(mapid, difficulty);
            if (!mapDiff)
            {
                auto stmt = CharacterDatabase.CreateStatement(CHAR_INSTANCE_RESET_DELETE);
                stmt->Bind(0, mapid);
                stmt->Bind(1, static_cast<uint8_t>(difficulty));

                CharacterDatabase.ExecuteStatement(std::move(stmt));
                continue;
            }

            // update the reset time if the hour in the configs changes
            uint64_t newresettime = Util::getLocalHourTimestamp(oldresettime, resetHour, false);
            if (oldresettime != newresettime)
            {
                auto stmt = CharacterDatabase.CreateStatement(CHAR_INSTANCE_RESET_UPDATE);
                stmt->Bind(0, newresettime);
                stmt->Bind(1, mapid);
                stmt->Bind(2, static_cast<uint8_t>(difficulty));

                CharacterDatabase.ExecuteStatement(std::move(stmt));
            }

            initializeResetTimeFor(mapid, difficulty, newresettime);
        } while (result->NextRow());
    }

    // calculate new global reset times for expired instances and those that have never been reset yet
    // add the global reset times to the priority queue
    for (MapDifficultyMap::const_iterator itr = sMapDifficultyMap.begin(); itr != sMapDifficultyMap.end(); ++itr)
    {
        uint32_t map_diff_pair = itr->first;

        DifficultyMap map = DifficultyMap();
        map.MapPair = itr->first;

        auto mapid = map.MapPairParts.mapid;
        InstanceDifficulty::Difficulties difficulty = InstanceDifficulty::Difficulties(map.MapPairParts.difficulty);
        WDB::Structures::MapDifficulty const* mapDiff = &itr->second;
        if (!mapDiff->resetTime)
            continue;

        // the reset_delay must be at least one day
        uint32_t period = static_cast<uint32_t>((mapDiff->resetTime / DAY) * DAY);
        if (period < DAY)
            period = DAY;

        time_t t = getResetTimeFor(mapid, difficulty);
        if (!t)
        {
            // initialize the reset time
            t = Util::getLocalHourTimestamp(today + period, resetHour);

            auto stmt = CharacterDatabase.CreateStatement(CHAR_INSTANCE_RESET_INSERT);
            stmt->Bind(0, mapid);
            stmt->Bind(1, static_cast<uint8_t>(difficulty));
            stmt->Bind(2, static_cast<uint64_t>(t));

            CharacterDatabase.ExecuteStatement(std::move(stmt));
        }

        if (t < now)
        {
            // assume that expired instances have already been cleaned
            // calculate the next reset time
            time_t day = (t / DAY) * DAY;
            t = Util::getLocalHourTimestamp(day + ((today - day) / period + 1) * period, resetHour);

            auto stmt = CharacterDatabase.CreateStatement(CHAR_INSTANCE_RESET_UPDATE);
            stmt->Bind(0, static_cast<uint64_t>(t));
            stmt->Bind(1, mapid);
            stmt->Bind(2, static_cast<uint8_t>(difficulty));

            CharacterDatabase.ExecuteStatement(std::move(stmt));
        }

        initializeResetTimeFor(mapid, difficulty, t);

        // add the global reset event
        uint8_t type;
        for (type = 1; type < 4; ++type)
            if (t - ResetTimeDelay[type - 1] > now)
                break;

        addResetEvent(true, t - ResetTimeDelay[type - 1], InstResetEvent(type, mapid, difficulty, 0));

        ResetTimeMapDiffInstancesBounds range = mapDiffResetInstances.equal_range(map_diff_pair);
        for (; range.first != range.second; ++range.first)
            addResetEvent(true, t - ResetTimeDelay[type - 1], InstResetEvent(type, mapid, difficulty, static_cast<uint16_t>(range.first->second)));
    }
}

void InstanceMgr::update()
{
    const auto now = Util::getTimeNow();
    time_t t;

    while (!m_resetTimeQueue.empty())
    {
        t = m_resetTimeQueue.begin()->first;
        if (t >= now)
            break;

        InstResetEvent& event = m_resetTimeQueue.begin()->second;
        if (event.type == 0)
        {
            // for individual normal instances, max creature respawn + X hours
            resetInstance(event.mapid, event.instanceId);
            m_resetTimeQueue.erase(m_resetTimeQueue.begin());
        }
        else
        {
            // global reset/warning for a certain map
            time_t resetTime = getResetTimeFor(event.mapid, event.difficulty);
            resetOrWarnAll(event.mapid, event.difficulty, event.type != 4, resetTime);
            if (event.type != 4)
            {
                ++event.type;
                addResetEvent(true, resetTime - ResetTimeDelay[event.type - 1], event);
            }
            m_resetTimeQueue.erase(m_resetTimeQueue.begin());
        }
    }
}

void InstanceMgr::resetSave(InstanceSavedMap::iterator& itr)
{
    lock_instanceLists = true;

    bool shouldDelete = true;
    InstanceSaved::PlayerList& pList = itr->second->m_playerList;
    std::vector<Player*> temp; // list of expired binds that should be unbound
    for (Player* player : pList)
    {
        if (InstancePlayerBind* bind = player->getBoundInstance(itr->second->getMapId(), itr->second->getDifficulty()))
        {
            ASSERT(bind->save == itr->second.get());
            if (bind->perm && bind->extendState) // permanent and not already expired
            {
                bind->extendState = bind->extendState == EXTEND_STATE_EXTENDED ? EXTEND_STATE_NORMAL : EXTEND_STATE_EXPIRED;
                shouldDelete = false;
                continue;
            }
        }
        temp.push_back(player);
    }
    for (Player* player : temp)
    {
        player->unbindInstance(itr->second->getMapId(), itr->second->getDifficulty(), true);
    }

    InstanceSaved::GroupList& gList = itr->second->m_groupList;
    while (!gList.empty())
    {
        Group* group = *(gList.begin());
        group->unbindInstance(itr->second->getMapId(), itr->second->getDifficulty(), true);
    }

    if (shouldDelete)
    {
        itr = m_instanceSaveById.erase(itr);
    }
    else
    {
        ++itr;
    }

    lock_instanceLists = false;
}

void InstanceMgr::resetInstance(uint32_t mapid, uint32_t instanceId)
{
    BaseMap const* map = sMapMgr.findBaseMap(mapid);
    if (!map->instanceable())
        return;

    InstanceSavedMap::iterator itr = m_instanceSaveById.find(instanceId);
    if (itr != m_instanceSaveById.end())
        resetSave(itr);

    deleteInstanceFromDB(instanceId);

    WorldMap* iMap = sMapMgr.findInstanceMap(instanceId);

    if (iMap && iMap->getBaseMap()->isDungeon())
        ((InstanceMap*)iMap)->reset(INSTANCE_RESET_RESPAWN_DELAY);

    if (iMap)
    {
        iMap->deleteRespawnTimes();
    }
    else
    {
        WorldMap::deleteRespawnTimesInDB(mapid, instanceId);
    }

    // Free up the used instanceId from Our instanceId Pool
    sMapMgr.instanceIdPool.freeUsedId(instanceId);
}

void InstanceMgr::resetOrWarnAll(uint32_t mapid, InstanceDifficulty::Difficulties difficulty, bool warn, time_t resetTime)
{
    // global reset for all instances of the given map
    WDB::Structures::MapEntry const* mapEntry = sMapStore.lookupEntry(mapid);
    if (!mapEntry->instanceable())
        return;

    const auto now = Util::getTimeNow();

    if (!warn)
    {
        // calculate the next reset time
        time_t next_reset = getSubsequentResetTime(mapid, difficulty, resetTime);
        if (!next_reset)
            return;

        {
            auto stmt = CharacterDatabase.CreateStatement(CHAR_DELETE_CHARACTER_INSTANCE_FOR_MAP_DIFF);
            stmt->Bind(0, uint16_t(mapid));
            stmt->Bind(1, uint8_t(difficulty));
            CharacterDatabase.ExecuteStatement(std::move(stmt));
        }

        {
            auto stmt = CharacterDatabase.CreateStatement(CHAR_DELETE_GROUP_INSTANCE_FOR_MAP_DIFF);
            stmt->Bind(0, uint16_t(mapid));
            stmt->Bind(1, uint8_t(difficulty));
            CharacterDatabase.ExecuteStatement(std::move(stmt));
        }

        {
            auto stmt = CharacterDatabase.CreateStatement(CHAR_DELETE_INSTANCE_WITHOUT_EXTENDED_CHARACTERS);
            stmt->Bind(0, uint16_t(mapid));
            stmt->Bind(1, uint8_t(difficulty));
            CharacterDatabase.ExecuteStatement(std::move(stmt));
        }

        {
            auto stmt = CharacterDatabase.CreateStatement(CHAR_DECREMENT_EXTENDSTATE_FOR_MAP_DIFF);
            stmt->Bind(0, uint16_t(mapid));
            stmt->Bind(1, uint8_t(difficulty));
            CharacterDatabase.ExecuteStatement(std::move(stmt));
        }

        // promote loaded binds to instances of the given map
        for (InstanceSavedMap::iterator itr = m_instanceSaveById.begin(); itr != m_instanceSaveById.end();)
        {
            if (itr->second->getMapId() == mapid && itr->second->getDifficulty() == difficulty)
                resetSave(itr);
            else
                ++itr;
        }

        setResetTimeFor(static_cast<uint16_t>(mapid), difficulty, next_reset);
        addResetEvent(true, time_t(next_reset - 3600), InstResetEvent(1, mapid, difficulty, 0));

        // Update it in the DB
        auto stmt = CharacterDatabase.CreateStatement(CHAR_INSTANCE_RESET_UPDATE);
        stmt->Bind(0, static_cast<uint64_t>(next_reset));
        stmt->Bind(1, static_cast<uint16_t>(mapid));
        stmt->Bind(2, static_cast<uint8_t>(difficulty));

        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }

    uint32_t timeLeft;
    auto instances = sMapMgr.findInstancedMaps(mapid);

    for (auto const& map : instances)
    {
        if (!map->getBaseMap()->isDungeon())
            continue;

        if (warn)
        {
            if (now >= resetTime)
                timeLeft = 0;
            else
                timeLeft = static_cast<uint32_t>(resetTime - now);

            map->sendResetWarnings(timeLeft);
        }
        else
        {
            map->reset(INSTANCE_RESET_GLOBAL);
        }
    }
}

InstanceSaved* InstanceMgr::addInstanceSave(uint32_t mapId, uint32_t instanceId, InstanceDifficulty::Difficulties difficulty, time_t resetTime, bool canReset, bool load)
{
    if (InstanceSaved* old_save = getInstanceSave(instanceId))
        return old_save;

    WDB::Structures::MapEntry const* entry = sMapStore.lookupEntry(mapId);
    if (!entry)
    {
        sLogger.failure("InstanceMgr::addInstanceSave: wrong mapid = {}, instanceid = {}!", mapId, instanceId);
        return nullptr;
    }

    if (instanceId == 0)
    {
        sLogger.failure("InstanceMgr::addInstanceSave: mapid = {}, wrong instanceid = {}!", mapId, instanceId);
        return nullptr;
    }

    if (difficulty >= (entry->isRaid() ? InstanceDifficulty::Difficulties::MAX_RAID_DIFFICULTY : InstanceDifficulty::Difficulties::MAX_DUNGEON_DIFFICULTY))
    {
        sLogger.failure("InstanceMgr::addInstanceSave: mapid = {}, instanceid = {}, wrong dificalty {}!", mapId, instanceId, difficulty);
        return nullptr;
    }

    if (!resetTime)
    {
        // initialize reset time
        // for normal instances if no creatures are killed the instance will reset in two hours
        if (entry->map_type == MAP_RAID || difficulty > InstanceDifficulty::Difficulties::DUNGEON_NORMAL)
        {
            resetTime = getResetTimeFor(static_cast<uint16_t>(mapId), difficulty);
        }
        else
        {
            const auto now_t = Util::getTimeNow();
            resetTime = now_t + 2 * HOUR;
            // add our Reset Event
            addResetEvent(true, resetTime, InstResetEvent(0, mapId, difficulty, static_cast<uint16_t>(instanceId)));
        }
    }

    sLogger.debug("InstanceMgr::addInstanceSave: mapid = {}, instanceid = {}", mapId, instanceId);

    auto save = std::make_unique<InstanceSaved>(mapId, instanceId, difficulty, resetTime, canReset);
    if (!load)
        save->saveToDB();

    const auto [itr, _] = m_instanceSaveById.try_emplace(instanceId, std::move(save));
    return itr->second.get();
}

void InstanceMgr::deleteInstanceFromDB(uint32_t instanceid)
{
    {
        auto stmt = CharacterDatabase.CreateStatement(CHAR_DELETE_INSTANCE_BY_ID);
        stmt->Bind(0, instanceid);

        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }

    {
        auto stmt = CharacterDatabase.CreateStatement(CHAR_DELETE_CHARACTER_INSTANCE_BY_INSTANCE);
        stmt->Bind(0, instanceid);

        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }

    {
        auto stmt = CharacterDatabase.CreateStatement(CHAR_DELETE_GROUP_INSTANCE_BY_INSTANCE);
        stmt->Bind(0, instanceid);

        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }

}

void InstanceMgr::removeInstanceSave(uint32_t InstanceId)
{
    InstanceSavedMap::iterator itr = m_instanceSaveById.find(InstanceId);
    if (itr != m_instanceSaveById.end())
    {
        // save the resettime for normal instances only when they get unloaded
        if (time_t resettime = itr->second->getResetTimeForDB())
        {
            auto stmt = CharacterDatabase.CreateStatement(CHAR_UPDATE_INSTANCE_RESETTIME_BY_ID);
            stmt->Bind(0, static_cast<uint64_t>(resettime));
            stmt->Bind(1, InstanceId);
            
            CharacterDatabase.ExecuteStatement(std::move(stmt));
        }

        m_instanceSaveById.erase(itr);
    }
}

void InstanceMgr::unloadInstanceSave(uint32_t InstanceId)
{
    if (InstanceSaved* save = getInstanceSave(InstanceId))
    {
        save->unloadIfEmpty();
    }
}

InstanceSaved* InstanceMgr::getInstanceSave(uint32_t InstanceId)
{
    InstanceSavedMap::iterator itr = m_instanceSaveById.find(InstanceId);
    return itr != m_instanceSaveById.end() ? itr->second.get() : nullptr;
}

void InstanceMgr::addResetEvent(bool add, time_t time, InstResetEvent event)
{
    if (!add)
    {
        // find the event in the queue and remove it
        ResetTimeQueue::iterator itr;
        std::pair<ResetTimeQueue::iterator, ResetTimeQueue::iterator> range;
        range = m_resetTimeQueue.equal_range(time);
        for (itr = range.first; itr != range.second; ++itr)
        {
            if (itr->second == event)
            {
                m_resetTimeQueue.erase(itr);
                return;
            }
        }

        if (itr == range.second)
        {
            for (itr = m_resetTimeQueue.begin(); itr != m_resetTimeQueue.end(); ++itr)
            {
                if (itr->second == event)
                {
                    m_resetTimeQueue.erase(itr);
                    return;
                }
            }
        }
    }
    else
    {
        m_resetTimeQueue.insert(std::pair<time_t, InstResetEvent>(time, event));
    }
}

time_t InstanceMgr::getSubsequentResetTime(uint32_t mapid, InstanceDifficulty::Difficulties difficulty, time_t resetTime) const
{
    WDB::Structures::MapDifficulty const* mapDiff = getMapDifficultyData(mapid, difficulty);
    if (!mapDiff || !mapDiff->resetTime)
    {
        sLogger.failure("InstanceMgr::getSubsequentResetTime: not valid difficulty or no reset delay for map {}", mapid);
        return 0;
    }

    auto resetHour = static_cast<uint8_t>(worldConfig.instance.relativeDailyHeroicInstanceResetHour);
    time_t period = static_cast<uint32_t>((mapDiff->resetTime / DAY) * DAY);
    if (period < DAY)
        period = DAY;

    return Util::getLocalHourTimestamp(((resetTime + MINUTE) / DAY * DAY) + period, resetHour);
}

void InstanceMgr::initializeResetTimeFor(uint16_t mapid, InstanceDifficulty::Difficulties d, time_t t)
{
    m_resetTimeByMapDifficulty[Util::MAKE_PAIR32(mapid, d)] = t;
}
