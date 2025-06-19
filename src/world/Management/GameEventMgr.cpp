/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "GameEventMgr.hpp"
#include "GameEvent.hpp"
#include "GameEventDefines.hpp"
#include "Server/World.h"
#include "Storage/MySQLDataStore.hpp"
#include "Debugging/CrashHandler.h"
#include "Logging/Logger.hpp"
#include "Server/DatabaseDefinition.hpp"
#include "Objects/GameObjectProperties.hpp"

GameEventMgr& GameEventMgr::getInstance()
{
    static GameEventMgr mInstance;
    return mInstance;
}

void GameEventMgr::initialize()
{
    mGameEvents.clear();
}

GameEvent* GameEventMgr::GetEventById(uint32_t pEventId)
{
    auto rEvent = mGameEvents.find(pEventId);
    if (rEvent == mGameEvents.end())
        return nullptr;
    else
        return rEvent->second.get();
}

void GameEventMgr::StartArenaEvents()
{
    for (auto i = 57; i <= 60; ++i)
    {
        auto gameEvent = GetEventById(i);
        if (gameEvent == nullptr)
        {
            sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "Missing arena event (id: {})", i);
            continue;
        }

        if (i - 52 == worldConfig.arena.arenaSeason && worldConfig.arena.arenaProgress == 1)
            gameEvent->SetState(GAMEEVENT_ACTIVE_FORCED);
        else
            gameEvent->SetState(GAMEEVENT_INACTIVE_FORCED);
    }
}

void GameEventMgr::LoadFromDB()
{
    // Clean event_saves from CharacterDB
    sLogger.info("GameEventMgr : Start cleaning gameevent_save");
    {
        auto stmt = CharacterDatabase.CreateStatement(CHARACTER_GAMEEVENT_SAVE_CLEAN);
        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }

    // Loading event_properties
    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_GAMEEVENT_PROPERTIES_SELECT);
        stmt->Bind(0, getAEVersion());
        stmt->Bind(1, getAEVersion());

        auto result = WorldDatabase.QueryStatement(std::move(stmt));
        if (!result)
        {
            sLogger.failure("GameEventMgr : gameevent_properties can not be read or does not include any version specific events!");
            return;
        }

        uint32_t pCount = 0;
        do
        {
            Field* field = result->Fetch();

            EventNamesQueryResult dbResult;
            dbResult.entry = field[0].asUint32();
            dbResult.start_time = field[1].asUint32();
            dbResult.end_time = field[2].asUint32();
            dbResult.occurence = field[3].asUint32();
            dbResult.length = field[4].asUint32();
            dbResult.holiday_id = HolidayIds(field[5].asUint32());
            dbResult.description = field[6].asString();
            dbResult.world_event = GameEventState(field[7].asUint8());
            dbResult.announce = field[8].asUint8();

            mGameEvents.emplace(dbResult.entry, std::make_unique<GameEvent>(dbResult));
            sLogger.debug("GameEventMgr : {}, Entry: {}, State: {}, Holiday: {} loaded",
                dbResult.description, dbResult.entry, dbResult.world_event, dbResult.holiday_id);
            ++pCount;
        } while (result->NextRow());

        sLogger.info("GameEventMgr : {} events loaded from table event_properties", pCount);
    }

    // Loading event_saves from CharacterDB
    sLogger.info("GameEventMgr : Start loading gameevent_save");
    {
        auto stmt = CharacterDatabase.CreateStatement(CHARACTER_GAMEEVENT_SAVE_SELECT);
        bool success = false;
        auto result = CharacterDatabase.QueryStatement(&success, std::move(stmt));

        if (!success)
        {
            sLogger.failure("Query failed: CHARACTER_GAMEEVENT_SAVE_SELECT");
            return;
        }

        uint32_t pCount = 0;
        if (result)
        {
            do
            {
                Field* field = result->Fetch();
                uint32_t event_id = field[0].asUint8();

                auto gameEvent = GetEventById(event_id);
                if (gameEvent == nullptr)
                {
                    auto delStmt = CharacterDatabase.CreateStatement(CHARACTER_GAMEEVENT_SAVE_DELETE_BY_ID);
                    delStmt->Bind(0, event_id);
                    CharacterDatabase.QueryStatement(std::move(delStmt));
                    sLogger.info("Deleted invalid gameevent_save with entry {}", event_id);
                    continue;
                }

                gameEvent->state = GameEventState(field[1].asUint8());
                gameEvent->nextstart = static_cast<time_t>(field[2].asUint32());

                ++pCount;
            } while (result->NextRow());
        }

        sLogger.info("GameEventMgr : Loaded {} saved events from table gameevent_save", pCount);
    }

    // Loading event_creature from WorldDB
    sLogger.info("GameEventMgr : Start loading game event creature spawns");
    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_EVENT_CREATURE_SPAWNS_SELECT);
        stmt->Bind(0, VERSION_STRING);
        stmt->Bind(1, VERSION_STRING);

        bool success = false;
        auto result = WorldDatabase.QueryStatement(&success, std::move(stmt));

        if (!success)
        {
            sLogger.failure("Query failed: WORLD_EVENT_CREATURE_SPAWNS_SELECT");
            return;
        }

        uint32_t pCount = 0;
        if (result)
        {
            do
            {
                Field* field = result->Fetch();

                uint32_t event_id = field[28].asUint32();
                auto gameEvent = GetEventById(event_id);
                if (gameEvent == nullptr)
                {
                    sLogger.failure("Could not find event for creature_spawns entry {}", event_id);
                    continue;
                }

                EventCreatureSpawnsQueryResult dbResult;
                dbResult.event_entry = event_id;
                dbResult.id = field[0].asUint32();
                dbResult.entry = field[1].asUint32();

                auto creature_properties = sMySQLStore.getCreatureProperties(dbResult.entry);
                if (!creature_properties)
                {
                    sLogger.failure("Missing creature_properties for entry {}", dbResult.entry);
                    continue;
                }

                dbResult.map_id = field[2].asUint16();
                dbResult.position_x = field[3].asFloat();
                dbResult.position_y = field[4].asFloat();
                dbResult.position_z = field[5].asFloat();
                dbResult.orientation = field[6].asFloat();
                dbResult.movetype = field[7].asUint8();
                dbResult.displayid = field[8].asUint32();
                dbResult.faction = field[9].asUint32();
                dbResult.flags = field[10].asUint32();
                dbResult.pvp_flagged = field[11].asUint8();
                dbResult.bytes0 = field[12].asUint32();
                dbResult.emote_state = field[13].asUint16();
                dbResult.npc_respawn_link = field[14].asUint32();
                dbResult.channel_spell = field[15].asUint32();
                dbResult.channel_target_sqlid = field[16].asUint32();
                dbResult.channel_target_sqlid_creature = field[17].asUint32();
                dbResult.standstate = field[18].asUint8();
                dbResult.death_state = field[19].asUint8();
                dbResult.mountdisplayid = field[20].asUint32();
                dbResult.sheath_state = field[21].asUint8();
                dbResult.slot1item = field[22].asUint32();
                dbResult.slot2item = field[23].asUint32();
                dbResult.slot3item = field[24].asUint32();
                dbResult.CanFly = field[25].asUint16();
                dbResult.phase = field[26].asUint32();
                dbResult.waypoint_group = field[27].asUint32();

                gameEvent->npc_data.push_back(dbResult);
                ++pCount;

            } while (result->NextRow());
        }

        sLogger.info("GameEventMgr : {} creature spawns for {} events loaded from table creature_spawns.", pCount, static_cast<uint32_t>(mGameEvents.size()));
    }

    // Loading event_gameobject from WorldDB
    sLogger.info("GameEventMgr : Start loading game event gameobject spawns");
    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_EVENT_GAMEOBJECT_SPAWNS_SELECT);
        stmt->Bind(0, VERSION_STRING);
        stmt->Bind(1, VERSION_STRING);

        bool success = false;
        auto result = WorldDatabase.QueryStatement(&success, std::move(stmt));

        if (!success)
        {
            sLogger.failure("Query failed: WORLD_EVENT_GAMEOBJECT_SPAWNS_SELECT");
            return;
        }

        uint32_t pCount = 0;
        if (result)
        {
            do
            {
                Field* field = result->Fetch();
                uint32_t event_id = field[14].asUint32();

                auto gameEvent = GetEventById(event_id);
                if (!gameEvent)
                {
                    sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "Could not find event for gameobject_spawns entry {}", event_id);
                    continue;
                }

                EventGameObjectSpawnsQueryResult dbResult;
                dbResult.event_entry = event_id;
                dbResult.id = field[0].asUint32();
                dbResult.entry = field[1].asUint32();

                auto gameobject_info = sMySQLStore.getGameObjectProperties(dbResult.entry);
                if (!gameobject_info)
                {
                    sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "Could not create GameobjectSpawn for invalid entry {} (missing in table gameobject_properties)", dbResult.entry);
                    continue;
                }

                dbResult.map_id = field[2].asUint32();
                dbResult.phase = field[3].asUint32();
                dbResult.position_x = field[4].asFloat();
                dbResult.position_y = field[5].asFloat();
                dbResult.position_z = field[6].asFloat();
                dbResult.facing = field[7].asFloat();
                dbResult.orientation1 = field[8].asFloat();
                dbResult.orientation2 = field[9].asFloat();
                dbResult.orientation3 = field[10].asFloat();
                dbResult.orientation4 = field[11].asFloat();
                dbResult.spawnTimesecs = field[12].asUint32();
                dbResult.state = field[13].asUint32();

                gameEvent->gameobject_data.push_back(dbResult);
                ++pCount;

            } while (result->NextRow());
        }

        sLogger.info("GameEventMgr : {} gameobject spawns for {} events loaded from table gameobject_spawns.", pCount, static_cast<uint32_t>(mGameEvents.size()));
    }

    StartArenaEvents();
}

GameEventMgr::GameEventMgrThread& GameEventMgr::GameEventMgrThread::getInstance()
{
    static GameEventMgr::GameEventMgrThread mInstance;
    return mInstance;
}

void GameEventMgr::GameEventMgrThread::initialize()
{
    m_reloadThread = std::make_unique<AscEmu::Threading::AEThread>("UpdateGameEvents", [this](AscEmu::Threading::AEThread& /*thread*/) { this->Update(); }, std::chrono::seconds(1));
}

void GameEventMgr::GameEventMgrThread::finalize()
{
    sLogger.info("GameEventMgrThread : Stop Manager...");
    m_reloadThread->killAndJoin();
}

void GameEventMgr::GameEventMgrThread::Update()
{
    //sLogger.info("GameEventMgr : Tick!");
    auto now = time(0);

    for (const auto& gameEventPair : sGameEventMgr.mGameEvents)
    {
        GameEvent* gameEvent = gameEventPair.second.get();

        // Don't alter manual events
        if (!gameEvent->isValid())
            continue;

        auto startTime = time_t(gameEvent->start);
        if (startTime < now && now < gameEvent->end)
        {
            if ((now - startTime) % (gameEvent->occurence * 60) < gameEvent->length * 60)
            {
                // Event should start
                if (gameEvent->state != GAMEEVENT_INACTIVE_FORCED)
                {
                    gameEvent->StartEvent();
                    continue;
                }
            }
            continue;
        }

        // Event should stop
        if (gameEvent->state != GAMEEVENT_ACTIVE_FORCED)
        {
            gameEvent->StopEvent();
        }
    }
}
