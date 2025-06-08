/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "WaypointManager.h"
#include "Utilities/Util.hpp"
#include "Logging/Logger.hpp"
#include "Storage/MySQLDataStore.hpp"
#include "Objects/MovementInfo.hpp"
#include "Server/DatabaseDefinition.hpp"

void WaypointMgr::load()
{
    auto oldMSTime = Util::TimeNow();
    _waypointStore.clear();

    auto stmt = WorldDatabase.CreateStatement(WORLD_SEL_CREATURE_WAYPOINTS);
    auto result = WorldDatabase.QueryStatement(std::move(stmt));

    if (!result)
    {
        sLogger.info("WaypointMgr : Loaded 0 waypoints. DB table `creature_waypoints` is empty!");
        return;
    }

    uint32_t count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32_t pathId = fields[0].asUint32();
        float x = fields[2].asFloat();
        float y = fields[3].asFloat();
        float z = fields[4].asFloat();
        float o = fields[5].asFloat();

        normalizeMapCoord(x);
        normalizeMapCoord(y);

        WaypointNode waypoint;
        waypoint.id = fields[1].asUint32();
        waypoint.x = x;
        waypoint.y = y;
        waypoint.z = z;
        waypoint.orientation = o;
        waypoint.moveType = fields[6].asUint32();

        if (waypoint.moveType >= WAYPOINT_MOVE_TYPE_MAX)
        {
            sLogger.failure("WaypointMgr Waypoint {} in creature_waypoints has invalid move_type, ignoring", waypoint.id);
            continue;
        }

        waypoint.delay = fields[7].asUint32();
        waypoint.eventId = fields[8].asUint32();
        waypoint.eventChance = fields[9].asInt8();

        WaypointPath& path = _waypointStore[pathId];
        path.id = pathId;
        path.nodes.push_back(std::move(waypoint));
        ++count;
    }
    while (result->NextRow());

    sLogger.info("WaypointMgr : Loaded {} waypoints in {} ms", count, Util::GetTimeDifferenceToNow(oldMSTime));
}

void WaypointMgr::loadCustomWaypoints()
{
    auto oldMSTime = Util::TimeNow();

    auto stmt = WorldDatabase.CreateStatement(WORLD_CREATURE_SELECT_WAYPOINTS);
    auto result = WorldDatabase.QueryStatement(std::move(stmt));

    if (!result)
    {
        sLogger.info("WaypointMgr : Loaded 0 waypoints. DB table `creature_script_waypoints` is empty!");
        return;
    }

    uint32_t count = 0;

    do
    {
        Field* fields = result->Fetch();
        uint32_t pathId = fields[0].asUint32();
        float x = fields[2].asFloat();
        float y = fields[3].asFloat();
        float z = fields[4].asFloat();
        float o = fields[5].asFloat();

        normalizeMapCoord(x);
        normalizeMapCoord(y);

        WaypointNode waypoint;
        waypoint.id = fields[1].asUint32();
        waypoint.x = x;
        waypoint.y = y;
        waypoint.z = z;
        waypoint.orientation = o;
        waypoint.moveType = fields[6].asUint32();

        if (waypoint.moveType >= WAYPOINT_MOVE_TYPE_MAX)
        {
            sLogger.failure("WaypointMgr Waypoint {} in creature_waypoints has invalid move_type, ignoring", waypoint.id);
            continue;
        }

        waypoint.delay = fields[7].asUint32();
        waypoint.eventId = fields[8].asUint32();
        waypoint.eventChance = fields[9].asInt8();

        WaypointPath& path = _waypointStore[pathId];
        path.id = pathId;
        path.nodes.push_back(std::move(waypoint));
        ++count;
    } while (result->NextRow());

    sLogger.info("WaypointMgr : Loaded {} custom waypoints in {} ms", count, Util::GetTimeDifferenceToNow(oldMSTime));
}

WaypointMgr* WaypointMgr::getInstance()
{
    static WaypointMgr mInstance;
    return &mInstance;
}

WaypointPath* WaypointMgr::getPath(uint32_t id)
{
    auto itr = _waypointStore.find(id);
    if (itr != _waypointStore.end())
        return &itr->second;

    return nullptr;
}

WaypointPath* WaypointMgr::getCustomScriptWaypointPath(uint32_t id)
{
    auto itr = _customWaypointStore.find(id);
    if (itr != _customWaypointStore.end())
        return &itr->second;

    return nullptr;
}

uint32_t WaypointMgr::generateWaypointPathId()
{
    auto stmt = WorldDatabase.CreateStatement(WORLD_SEL_MAX_CREATURE_WAYPOINT_ID);
    auto result = WorldDatabase.QueryStatement(std::move(stmt));

    if (result)
    {
        uint32_t maxPathId = result->Fetch()[0].asUint32();

        WaypointPath& path = _waypointStore[maxPathId];
        path.id = maxPathId;
        path.nodes.clear();

        return maxPathId + 1;
    }

    return 0;
}

void WaypointMgr::addWayPoint(uint32_t pathid, WaypointNode waypoint, bool saveToDB /*=false*/)
{
    WaypointPath& path = _waypointStore[pathid];
    path.id = pathid;
    path.nodes.push_back(std::move(waypoint));

    if (saveToDB)
    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_CREATURE_INSERT_WAYPOINT);
        stmt->Bind(0, pathid);
        stmt->Bind(1, waypoint.id);
        stmt->Bind(2, waypoint.x);
        stmt->Bind(3, waypoint.y);
        stmt->Bind(4, waypoint.z);
        stmt->Bind(5, waypoint.orientation);
        stmt->Bind(6, waypoint.delay);
        stmt->Bind(7, waypoint.moveType);
        stmt->Bind(8, waypoint.eventId);
        stmt->Bind(9, waypoint.eventChance);
        stmt->Bind(10, uint32_t(0));

        WorldDatabase.ExecuteStatement(std::move(stmt));
    }
}

void WaypointMgr::deleteWayPointById(uint32_t pathid, uint32_t waypointId)
{
    auto stmt = WorldDatabase.CreateStatement(WORLD_CREATURE_DELETE_WAYPOINT);
    stmt->Bind(0, pathid);
    stmt->Bind(1, waypointId);

    WorldDatabase.ExecuteStatement(std::move(stmt));

    load();
}

void WaypointMgr::deleteAllWayPoints(uint32_t pathid)
{
    auto stmt = WorldDatabase.CreateStatement(WORLD_CREATURE_DELETE_ALL_WAYPOINTS_BY_ID);
    stmt->Bind(0, pathid);

    WorldDatabase.ExecuteStatement(std::move(stmt));

    auto itr = _waypointStore.find(pathid);
    if (itr != _waypointStore.end())
        _waypointStore.erase(pathid);

    sLogger.debug("Deleted waypoints for pathID {}", pathid);
}
