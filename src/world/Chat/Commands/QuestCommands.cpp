/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "Chat/ChatHandler.hpp"
#include "Logging/Logger.hpp"
#include "Management/ItemInterface.h"
#include "Management/ObjectMgr.hpp"
#include "Management/QuestLogEntry.hpp"
#include "Management/QuestMgr.h"
#include "Map/Management/MapMgr.hpp"
#include "Map/Maps/WorldMap.hpp"
#include "Objects/Item.hpp"
#include "Objects/Units/Creatures/Creature.h"
#include "Objects/Units/Players/Player.hpp"
#include "Server/DatabaseDefinition.hpp"
#include "Server/World.h"
#include "Server/WorldSession.h"
#include "Server/WorldSessionLog.hpp"
#include "Server/Script/HookInterface.hpp"
#include "Server/Script/QuestScript.hpp"
#include "Storage/MySQLDataStore.hpp"
#include "Utilities/Narrow.hpp"

uint32_t GetQuestIDFromLink(const char* questlink)
{
    if (questlink == nullptr)
        return 0;

    const char* ptr = strstr(questlink, "|Hquest:");
    if (ptr == nullptr)
    {
        return 0;
    }

    return std::stoul(ptr + 8);       // quest id is just past "|Hquest:" (8 bytes)
}

std::string RemoveQuestFromPlayer(Player* plr, QuestProperties const* qst)
{
    std::string recout = "|cff00ff00";

    if (plr->hasAnyQuestInQuestSlot())
    {
        if (plr->hasQuestFinished(qst->id))
        {
            recout += "Player has already completed that quest.\n\n";
        }
        else
        {
            if (auto* questLog = plr->getQuestLogByQuestId(qst->id))
            {
                if (const auto questScript = questLog->getQuestScript())
                    questScript->OnQuestCancel(plr);

                questLog->finishAndRemove();

                // Remove all items given by the questgiver at the beginning
                for (uint32_t itemId : qst->receive_items)
                {
                    if (itemId)
                        plr->getItemInterface()->RemoveItemAmt(itemId, 1);
                }

                plr->updateNearbyQuestGameObjects();
            }
            else
            {
                recout += "No quest log entry found for that player.";
            }
        }
    }
    else
    {
        recout += "Player has no quests to remove.";
    }

    recout += "\n\n";

    return recout;
}

bool ChatHandler::HandleQuestStatusCommand(const char* args, WorldSession* m_session)
{
    if (!*args) return false;

    Player* plr = GetSelectedPlayer(m_session, true, true);
    if (plr == nullptr)
        return true;

    uint32_t quest_id = std::stoul(args);
    if (quest_id == 0)
    {
        quest_id = GetQuestIDFromLink(args);
        if (quest_id == 0)
            return false;
    }
    std::string recout = "|cff00ff00";

    if (QuestProperties const* qst = sMySQLStore.getQuestProperties(quest_id))
    {
        if (plr->hasQuestFinished(quest_id))
        {
            recout += "Player has already completed that quest.";
        }
        else
        {
            if (plr->hasQuestInQuestLog(quest_id))
                recout += "Player is currently on that quest.";
            else
                recout += "Player has NOT finished that quest.";
        }
    }
    else
    {
        recout += "Quest Id [";
        recout += args;
        recout += "] was not found and unable to add it to the player's quest log.";
    }

    recout += "\n\n";

    SendMultilineMessage(m_session, recout.c_str());

    return true;
}

bool ChatHandler::HandleQuestStartCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    Player* player = GetSelectedPlayer(m_session, true, true);
    if (player == nullptr)
        return true;

    uint32_t quest_id = std::stoul(args);
    if (quest_id == 0)
    {
        quest_id = GetQuestIDFromLink(args);
        if (quest_id == 0)
            return false;
    }
    std::string recout = "|cff00ff00";

    QuestProperties const* questProperties = sMySQLStore.getQuestProperties(quest_id);
    if (questProperties)
    {
        if (player->hasQuestFinished(quest_id))
            recout += "Player has already completed that quest.";
        else
        {
            if (player->hasQuestInQuestLog(quest_id))
            {
                recout += "Player is currently on that quest.";
            }
            else
            {
                uint8_t open_slot = player->getFreeQuestSlot();
                if (open_slot > MAX_QUEST_SLOT)
                {
                    sQuestMgr.SendQuestLogFull(player);
                    recout += "Player's quest log is full.";
                }
                else
                {
                    if (questProperties->time != 0 && player->hasTimedQuestInQuestSlot())
                    {
                        sQuestMgr.SendQuestInvalid(INVALID_REASON_HAVE_TIMED_QUEST, player);
                        return true;
                    }

                    sGMLog.writefromsession(m_session, "started quest %u [%s] for player %s", questProperties->id, questProperties->title.c_str(), player->getName().c_str());

                    auto* questLogEntry = player->createQuestLogInSlot(questProperties, open_slot);
                    questLogEntry->updatePlayerFields();

                    // If the quest should give any items on begin, give them the items.
                    for (uint32_t receive_item : questProperties->receive_items)
                    {
                        if (receive_item)
                        {
                            auto item = sObjectMgr.createItem(receive_item, player);
                            if (item == nullptr)
                                return false;

                            player->getItemInterface()->AddItemToFreeSlot(std::move(item));
                        }
                    }

                    if (questProperties->srcitem && questProperties->srcitem != questProperties->receive_items[0])
                    {
                        auto item = sObjectMgr.createItem(questProperties->srcitem, player);
                        if (item)
                        {
                            item->setStackCount(questProperties->srcitemcount ? questProperties->srcitemcount : 1);
                            player->getItemInterface()->AddItemToFreeSlot(std::move(item));
                        }
                    }

                    player->updateNearbyQuestGameObjects();
                    sHookInterface.OnQuestAccept(player, questProperties, nullptr);

                    recout += "Quest has been added to the player's quest log.";
                }
            }
        }
    }
    else
    {
        recout += "Quest Id [";
        recout += args;
        recout += "] was not found and unable to add it to the player's quest log.";
    }

    recout += "\n\n";

    SendMultilineMessage(m_session, recout.c_str());

    return true;
}

bool ChatHandler::HandleQuestFinishCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    Player* plr = GetSelectedPlayer(m_session, true, true);
    if (plr == nullptr)
        return true;

    uint32_t quest_id = std::stoul(args);
    // reward_slot is for when quest has choice of rewards (0 is the first choice, 1 is the second choice, ...)
    // reward_slot will default to 0 if none is specified
    uint32_t reward_slot;
    if (quest_id == 0)
    {
        quest_id = GetQuestIDFromLink(args);
        if (quest_id == 0)
            return false;

        if (strstr(args, "|r"))
            reward_slot = std::stoul(strstr(args, "|r") + 2);
        else
            reward_slot = 0;
    }
    else if (strchr(args, ' '))
    {
        reward_slot = std::stoul(strchr(args, ' ') + 1);
    }
    else
    {
        reward_slot = 0;
    }
    // currently Quest::reward_choiceitem declaration is
    // uint32_t reward_choiceitem[6];
    // so reward_slot must be 0 to 5
    if (reward_slot > 5)
        reward_slot = 0;

    std::string recout = "|cff00ff00";

    if (QuestProperties const* qst = sMySQLStore.getQuestProperties(quest_id))
    {
        if (plr->hasQuestFinished(quest_id))
        {
            recout += "Player has already completed that quest.\n\n";
        }
        else
        {
            if (auto* questLog = plr->getQuestLogByQuestId(quest_id))
            {
                uint32_t giver_id = 0;

                auto stmt = WorldDatabase.CreateStatement(WORLD_SEL_CREATURE_QUEST_STARTER);
                stmt->Bind(0, static_cast<uint32_t>(quest_id));
                stmt->Bind(1, static_cast<uint32_t>(VERSION_STRING));
                stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));

                auto creatureResult = WorldDatabase.QueryStatement(std::move(stmt));

                if (creatureResult)
                {
                    Field* creatureFields = creatureResult->Fetch();
                    giver_id = creatureFields[0].asUint32();
                }
                else
                {
                    auto stmt2 = WorldDatabase.CreateStatement(WORLD_SEL_GAMEOBJECT_QUEST_STARTER);
                    stmt2->Bind(0, static_cast<uint32_t>(quest_id));
                    stmt2->Bind(1, static_cast<uint32_t>(VERSION_STRING));
                    stmt2->Bind(2, static_cast<uint32_t>(VERSION_STRING));

                    auto objectResult = WorldDatabase.QueryStatement(std::move(stmt2));

                    if (objectResult)
                    {
                        Field* objectFields = objectResult->Fetch();
                        giver_id = objectFields[0].asUint32();
                    }
                }

                if (giver_id == 0)
                {
                    SystemMessage(m_session, "Unable to find quest giver creature or object.");
                }
                else
                {
                    // I need some way to get the guid without targeting the creature or looking through all the spawns...
                    Object* questGiver = nullptr;

                    for (auto* pCreature: plr->getWorldMap()->getCreatures())
                    {
                        if (pCreature)
                        {
                            if (pCreature->getEntry() == giver_id) //found creature
                            {
                                questGiver = pCreature;
                            }
                        }
                    }

                    if (questGiver)
                    {
                        GreenSystemMessage(m_session, "Found a quest_giver creature.");
                        sQuestMgr.OnActivateQuestGiver(questGiver, plr);
                        sQuestMgr.GiveQuestRewardReputation(plr, qst, questGiver);
                    }
                    else
                        RedSystemMessage(m_session, "Unable to find quest_giver object.");
                }

                questLog->finishAndRemove();
                recout += "Player was on that quest, but has now completed it.";
            }
            else
            {
                recout += "The quest has now been completed for that player.";
            }

            sGMLog.writefromsession(m_session, "completed quest %u [%s] for player %s", quest_id, qst->title.c_str(), plr->getName().c_str());
            sQuestMgr.BuildQuestComplete(plr, qst);
            plr->addQuestToFinished(quest_id);

            // Quest Rewards : Copied from QuestMgr::OnQuestFinished()
            // Reputation reward
            for (uint8_t z = 0; z < 6; z++)
            {
                if (qst->reward_repfaction[z])
                {
                    int32_t amt = 0;
                    uint32_t fact = qst->reward_repfaction[z];
                    if (qst->reward_repvalue[z])
                        amt = qst->reward_repvalue[z];

                    if (qst->reward_replimit && (plr->getFactionStanding(fact) >= (int32_t)qst->reward_replimit))
                        continue;

                    amt = Util::float2int32(amt * worldConfig.getFloatRate(RATE_QUESTREPUTATION));
                    plr->modFactionStanding(fact, amt);
                }
            }
            // Static Item reward
            for (uint8_t i = 0; i < 4; ++i)
            {
                if (qst->reward_item[i])
                {
                    ItemProperties const* proto = sMySQLStore.getItemProperties(qst->reward_item[i]);
                    if (!proto)
                    {
                        sLogger.failure("Invalid item prototype in quest reward! ID {}, quest {}", qst->reward_item[i], qst->id);
                    }
                    else
                    {
                        auto* item_add = plr->getItemInterface()->FindItemLessMax(qst->reward_item[i], qst->reward_itemcount[i], false);
                        if (!item_add)
                        {
                            auto slotresult = plr->getItemInterface()->FindFreeInventorySlot(proto);
                            if (!slotresult.Result)
                            {
                                plr->getItemInterface()->buildInventoryChangeError(nullptr, nullptr, INV_ERR_INVENTORY_FULL);
                            }
                            else
                            {
                                auto item = sObjectMgr.createItem(qst->reward_item[i], plr);
                                if (item)
                                {
                                    item->setStackCount(uint32_t(qst->reward_itemcount[i]));
                                    plr->getItemInterface()->SafeAddItem(std::move(item), slotresult.ContainerSlot, slotresult.Slot);
                                }
                            }
                        }
                        else
                        {
                            item_add->setStackCount(item_add->getStackCount() + qst->reward_itemcount[i]);
                            item_add->m_isDirty = true;
                        }
                    }
                }
            }
            // Choice Rewards -- Defaulting to choice 0 for ".quest complete" command
            if (qst->reward_choiceitem[reward_slot])
            {
                ItemProperties const* proto = sMySQLStore.getItemProperties(qst->reward_choiceitem[reward_slot]);
                if (!proto)
                {
                    sLogger.failure("Invalid item prototype in quest reward! ID {}, quest {}", qst->reward_choiceitem[reward_slot], qst->id);
                }
                else
                {
                    auto item_add = plr->getItemInterface()->FindItemLessMax(qst->reward_choiceitem[reward_slot], qst->reward_choiceitemcount[reward_slot], false);
                    if (!item_add)
                    {
                        auto slotresult = plr->getItemInterface()->FindFreeInventorySlot(proto);
                        if (!slotresult.Result)
                        {
                            plr->getItemInterface()->buildInventoryChangeError(nullptr, nullptr, INV_ERR_INVENTORY_FULL);
                        }
                        else
                        {
                            auto item = sObjectMgr.createItem(qst->reward_choiceitem[reward_slot], plr);
                            if (item)
                            {
                                item->setStackCount(uint32_t(qst->reward_choiceitemcount[reward_slot]));
                                plr->getItemInterface()->SafeAddItem(std::move(item), slotresult.ContainerSlot, slotresult.Slot);
                            }
                        }
                    }
                    else
                    {
                        item_add->setStackCount(item_add->getStackCount() + qst->reward_choiceitemcount[reward_slot]);
                        item_add->m_isDirty = true;
                    }
                }
            }
            // if daily then append to finished dailies
            if (qst->is_repeatable == DEFINE_QUEST_REPEATABLE_DAILY)
                plr->addQuestIdToFinishedDailies(qst->id);
            // Remove quests that are listed to be removed on quest complete.
            std::set<uint32_t>::iterator iter = qst->remove_quest_list.begin();
            for (; iter != qst->remove_quest_list.end(); ++iter)
            {
                if (!plr->hasQuestFinished((*iter)))
                    plr->addQuestToFinished((*iter));
            }

#if VERSION_STRING > TBC
            plr->updateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST_COUNT, 1, 0, 0);
#endif
            if (qst->reward_money > 0)
            {
                // Money reward
                // Check they don't have more than the max gold
                if (worldConfig.player.isGoldCapEnabled && (plr->getCoinage() + qst->reward_money) <= worldConfig.player.limitGoldAmount)
                {
                    plr->modCoinage(qst->reward_money);
                }
#if VERSION_STRING > TBC
                plr->updateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_QUEST_REWARD_GOLD, qst->reward_money, 0, 0);
#endif
            }

            plr->updateNearbyQuestGameObjects();

#if VERSION_STRING > TBC
            plr->updateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUESTS_IN_ZONE, qst->zone_id, 0, 0);
            plr->updateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_COMPLETE_QUEST, qst->id, 0, 0);
#endif
        }
    }
    else
    {
        recout += "Quest Id [";
        recout += args;
        recout += "] was not found and unable to add it to the player's quest log.";
    }

    recout += "\n\n";

    SendMultilineMessage(m_session, recout.c_str());

    return true;
}

bool ChatHandler::HandleQuestFailCommand(const char *args, WorldSession* m_session)
{
    if (args == nullptr)
        return false;

    const uint32_t questId = static_cast<uint32_t>(atoi(args));
    if (questId == 0)
        return false;

    Player* player = m_session->GetPlayer();

    if (auto* questLog = player->getQuestLogByQuestId(questId))
    {
        questLog->sendQuestFailed();
        return true;
    }

    RedSystemMessage(m_session, "quest %u not found in player's questlog", questId);
    return false;

}

bool ChatHandler::HandleQuestItemCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    uint32_t questId = std::stoul(args);

    auto stmt = WorldDatabase.CreateStatement(WORLD_QUEST_ITEM_BINDING_BY_QUEST);
    stmt->Bind(0, questId);

    auto result = WorldDatabase.QueryStatement(std::move(stmt));

    std::string recout;
    if (!result)
    {
        recout = "|cff00ccffNo matches found.\n\n";
        SendMultilineMessage(m_session, recout.c_str());
        return true;
    }

    recout = "|cff00ff00Quest item matches: itemid: count -> Name\n\n";
    SendMultilineMessage(m_session, recout.c_str());

    uint32_t count = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32_t id = fields[0].asUint32();
        std::string itemid = MyConvertIntToString(id);
        std::string itemcnt = MyConvertIntToString(fields[1].asUint32());
        auto tmpItem = sMySQLStore.getItemProperties(id);

        if (tmpItem != nullptr)
        {
            recout = "|cff00ccff" + itemid + ": " + itemcnt + " -> " + tmpItem->Name + "\n";
        }
        else
        {
            recout = "|cffff0000Invalid Item!\n";
        }

        SendMultilineMessage(m_session, recout.c_str());

        ++count;
        if (count == 25)
        {
            RedSystemMessage(m_session, "More than 25 results returned. Aborting.");
            break;
        }

    } while (result->NextRow());

    return true;
}

bool ChatHandler::HandleQuestGiverCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    uint32_t questId = std::stoul(args);
    std::string recout;

    // --- Creature quest starter ---
    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_CREATURE_QUEST_STARTER_BY_QUEST);
        stmt->Bind(0, questId);
        stmt->Bind(1, static_cast<uint32_t>(VERSION_STRING));
        stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));

        auto objectResult1 = WorldDatabase.QueryStatement(std::move(stmt));

        if (objectResult1)
        {
            Field* fields = objectResult1->Fetch();
            uint32_t creatureId = fields[0].asUint32();
            std::string creatureName = "N/A";

            if (const auto* creatureProps = sMySQLStore.getCreatureProperties(creatureId))
            {
                creatureName = creatureProps->Name;

                auto stmt2 = WorldDatabase.CreateStatement(WORLD_CREATURE_SPAWN_BY_ENTRY);
                stmt2->Bind(0, creatureId);
                stmt2->Bind(1, static_cast<uint32_t>(VERSION_STRING));
                stmt2->Bind(2, static_cast<uint32_t>(VERSION_STRING));

                auto spawnResult = WorldDatabase.QueryStatement(std::move(stmt2));

                std::string spawnId = "N/A";
                if (spawnResult)
                    spawnId = spawnResult->Fetch()[0].asCString();

                recout = "|cff00ccffQuest Starter found: creature id, spawnid, name\n\n";
                SendMultilineMessage(m_session, recout.c_str());

                recout = "|cff00ccff" + MyConvertIntToString(creatureId) + ", " + spawnId + ", " + creatureName + "\n\n";
                SendMultilineMessage(m_session, recout.c_str());
            }
            else
            {
                recout = "|cff00ccffNo creature quest starter info found.\n\n";
                SendMultilineMessage(m_session, recout.c_str());
            }
        }
        else
        {
            recout = "|cff00ccffNo creature quest starters found.\n\n";
            SendMultilineMessage(m_session, recout.c_str());
        }
    }

    // --- GameObject quest starter ---
    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_GO_QUEST_STARTER_BY_QUEST);
        stmt->Bind(0, questId);
        stmt->Bind(1, static_cast<uint32_t>(VERSION_STRING));
        stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));

        auto objectResult2 = WorldDatabase.QueryStatement(std::move(stmt));

        if (objectResult2)
        {
            Field* fields = objectResult2->Fetch();
            uint32_t goEntry = fields[0].asUint32();
            std::string goName = "N/A";

            if (const auto* goProps = sMySQLStore.getItemProperties(goEntry))
            {
                goName = goProps->Name;

                auto stmt2 = WorldDatabase.CreateStatement(WORLD_GO_SPAWN_BY_ENTRY);
                stmt2->Bind(0, goEntry);
                stmt2->Bind(1, static_cast<uint32_t>(VERSION_STRING));
                stmt2->Bind(2, static_cast<uint32_t>(VERSION_STRING));

                auto spawnResult = WorldDatabase.QueryStatement(std::move(stmt2));

                std::string spawnId = "N/A";
                if (spawnResult)
                    spawnId = spawnResult->Fetch()[0].asCString();

                recout = "|cff00ccffQuest starter found: object id, spawnid, name\n\n";
                SendMultilineMessage(m_session, recout.c_str());

                recout = "|cff00ccff" + MyConvertIntToString(goEntry) + ", " + spawnId + ", " + goName + "\n\n";
                SendMultilineMessage(m_session, recout.c_str());
            }
            else
            {
                recout = "|cff00ccffNo object quest starter info found.\n\n";
                SendMultilineMessage(m_session, recout.c_str());
            }
        }
        else
        {
            recout = "|cff00ccffNo object quest starters found.\n\n";
            SendMultilineMessage(m_session, recout.c_str());
        }
    }

    return true;
}

bool ChatHandler::HandleQuestListCommand(const char* args, WorldSession* m_session)
{
    uint32_t quest_giver = 0;

    if (*args)
        quest_giver = std::stoul(args);
    else
    {
        WoWGuid wowGuid;
        wowGuid.Init(m_session->GetPlayer()->getTargetGuid());
        if (wowGuid.getRawGuid() == 0)
        {
            SystemMessage(m_session, "You must target an npc or specify an id.");
            return true;
        }

        Creature* unit = m_session->GetPlayer()->getWorldMap()->getCreature(wowGuid.getGuidLowPart());
        if (!unit || !unit->isQuestGiver())
        {
            SystemMessage(m_session, "Unit is not a valid quest giver.");
            return true;
        }

        if (!unit->HasQuests())
        {
            SystemMessage(m_session, "NPC does not have any quests.");
            return true;
        }

        quest_giver = unit->getEntry();
    }

    std::string recout = "|cff00ff00Quest matches: id: title\n\n";
    SendMultilineMessage(m_session, recout.c_str());

    uint32_t count = 0;

    if (quest_giver != 0)
    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_QUEST_LIST_BY_CREATURE);
        stmt->Bind(0, quest_giver);
        stmt->Bind(1, static_cast<uint32_t>(VERSION_STRING));
        stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));

        auto creatureResult = WorldDatabase.QueryStatement(std::move(stmt));

        if (!creatureResult)
        {
            recout = "|cff00ccffNo quests found for the specified NPC id.\n\n";
            SendMultilineMessage(m_session, recout.c_str());
            return true;
        }

        do
        {
            Field* fields = creatureResult->Fetch();
            uint32_t quest_id = fields[0].asUint32();
            const QuestProperties* qst = sMySQLStore.getQuestProperties(quest_id);
            if (!qst)
                continue;

            recout = "|cff00ccff" + MyConvertIntToString(quest_id) + ": " + qst->title + "\n";
            SendMultilineMessage(m_session, recout.c_str());

            ++count;
            if (count == 25)
            {
                RedSystemMessage(m_session, "More than 25 results returned. Aborting.");
                break;
            }
        } while (creatureResult->NextRow());
    }

    if (count == 0)
    {
        recout = "|cff00ccffNo matches found.\n\n";
        SendMultilineMessage(m_session, recout.c_str());
    }

    return true;
}

bool ChatHandler::HandleQuestAddStartCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    WoWGuid wowGuid;
    wowGuid.Init(m_session->GetPlayer()->getTargetGuid());

    if (wowGuid.getGuidLowPart() == 0)
    {
        SystemMessage(m_session, "You must target an npc.");
        return false;
    }

    Creature* unit = m_session->GetPlayer()->getWorldMap()->getCreature(wowGuid.getGuidLowPart());
    if (!unit || !unit->isQuestGiver())
    {
        SystemMessage(m_session, "You must target a valid quest-giving npc.");
        return false;
    }

    uint32_t quest_id = std::stoul(args);
    if (quest_id == 0)
    {
        quest_id = GetQuestIDFromLink(args);
        if (quest_id == 0)
            return false;
    }

    const QuestProperties* qst = sMySQLStore.getQuestProperties(quest_id);
    if (!qst)
    {
        SystemMessage(m_session, "Invalid quest selected, unable to add quest to the specified NPC.");
        return false;
    }

    auto stmt = WorldDatabase.CreateStatement(WORLD_CREATURE_QUEST_STARTER_EXISTS);
    stmt->Bind(0, unit->getEntry());
    stmt->Bind(1, quest_id);
    stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));
    stmt->Bind(3, static_cast<uint32_t>(VERSION_STRING));

    auto selectResult = WorldDatabase.QueryStatement(std::move(stmt));

    if (selectResult)
    {
        SystemMessage(m_session, "Quest was already found for the specified NPC.");
    }
    else
    {
        auto insertStmt = WorldDatabase.CreateStatement(WORLD_CREATURE_QUEST_STARTER_INSERT);
        insertStmt->Bind(0, unit->getEntry());
        insertStmt->Bind(1, quest_id);
        insertStmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));
        insertStmt->Bind(3, static_cast<uint32_t>(VERSION_STRING));

        WorldDatabase.ExecuteStatement(std::move(insertStmt));
    }

    // Rebuild Quests
    sQuestMgr.LoadExtraQuestStuff();

    QuestRelation* qstrel = nullptr;
    qstrel->qst = qst;
    qstrel->type = QUESTGIVER_QUEST_START;

    uint8_t qstrelid;
    if (unit->HasQuests())
    {
        qstrelid = (uint8_t)unit->GetQuestRelation(quest_id);
        unit->DeleteQuest(qstrel);
    }

    unit->_LoadQuests();

    std::string recout = "|cff00ff00Added Quest to NPC as starter: |cff00ccff" + qst->title + "\n\n";
    SendMultilineMessage(m_session, recout.c_str());

    sGMLog.writefromsession(m_session, "added starter of quest %u [%s] to NPC %u [%s]",
        qst->id, qst->title.c_str(), unit->getEntry(), unit->GetCreatureProperties()->Name.c_str());

    return true;
}

bool ChatHandler::HandleQuestAddFinishCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    WoWGuid wowGuid;
    wowGuid.Init(m_session->GetPlayer()->getTargetGuid());

    if (wowGuid.getRawGuid() == 0)
    {
        SystemMessage(m_session, "You must target an npc.");
        return false;
    }

    Creature* unit = m_session->GetPlayer()->getWorldMap()->getCreature(wowGuid.getGuidLowPart());
    if (!unit || !unit->isQuestGiver())
    {
        SystemMessage(m_session, "You must target a valid quest-giving npc.");
        return false;
    }

    uint32_t quest_id = std::stoul(args);
    if (quest_id == 0)
    {
        quest_id = GetQuestIDFromLink(args);
        if (quest_id == 0)
            return false;
    }

    const QuestProperties* qst = sMySQLStore.getQuestProperties(quest_id);
    if (!qst)
    {
        SystemMessage(m_session, "Invalid quest selected, unable to add quest to the specified NPC.");
        return false;
    }

    auto stmt = WorldDatabase.CreateStatement(WORLD_CREATURE_QUEST_FINISHER_EXISTS);
    stmt->Bind(0, unit->getEntry());
    stmt->Bind(1, quest_id);
    stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));
    stmt->Bind(3, static_cast<uint32_t>(VERSION_STRING));

    auto selectResult = WorldDatabase.QueryStatement(std::move(stmt));

    if (selectResult)
    {
        SystemMessage(m_session, "Quest was already found for the specified NPC.");
    }
    else
    {
        auto insertStmt = WorldDatabase.CreateStatement(WORLD_CREATURE_QUEST_FINISHER_INSERT);
        insertStmt->Bind(0, unit->getEntry());
        insertStmt->Bind(1, quest_id);
        insertStmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));
        insertStmt->Bind(3, static_cast<uint32_t>(VERSION_STRING));

        WorldDatabase.ExecuteStatement(std::move(insertStmt));
    }

    sQuestMgr.LoadExtraQuestStuff();

    QuestRelation* qstrel = nullptr;
    qstrel->qst = qst;
    qstrel->type = QUESTGIVER_QUEST_END;

    uint8_t qstrelid;
    if (unit->HasQuests())
    {
        qstrelid = (uint8_t)unit->GetQuestRelation(quest_id);
        unit->DeleteQuest(qstrel);
    }

    unit->_LoadQuests();

    std::string recout = "|cff00ff00Added Quest to NPC as finisher: |cff00ccff" + qst->title + "\n\n";
    SendMultilineMessage(m_session, recout.c_str());

    sGMLog.writefromsession(m_session, "added finisher of quest %u [%s] to NPC %u [%s]",
        qst->id, qst->title.c_str(), unit->getEntry(), unit->GetCreatureProperties()->Name.c_str());

    return true;
}

bool ChatHandler::HandleQuestAddBothCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    bool bValid = ChatHandler::HandleQuestAddStartCommand(args, m_session);

    if (bValid)
        ChatHandler::HandleQuestAddFinishCommand(args, m_session);

    return true;
}

bool ChatHandler::HandleQuestDelStartCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    WoWGuid wowGuid;
    wowGuid.Init(m_session->GetPlayer()->getTargetGuid());

    if (wowGuid.getRawGuid() == 0)
    {
        SystemMessage(m_session, "You must target an npc.");
        return false;
    }

    Creature* unit = m_session->GetPlayer()->getWorldMap()->getCreature(wowGuid.getGuidLowPart());
    if (!unit || !unit->isQuestGiver())
    {
        SystemMessage(m_session, "You must target a valid quest-giving npc.");
        return false;
    }

    uint32_t quest_id = std::stoul(args);
    if (quest_id == 0)
    {
        quest_id = GetQuestIDFromLink(args);
        if (quest_id == 0)
            return false;
    }

    const QuestProperties* qst = sMySQLStore.getQuestProperties(quest_id);
    if (!qst)
    {
        SystemMessage(m_session, "Invalid Quest selected.");
        return false;
    }

    auto stmt = WorldDatabase.CreateStatement(WORLD_CREATURE_QUEST_STARTER_SELECT);
    stmt->Bind(0, unit->getEntry());
    stmt->Bind(1, quest_id);
    stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));
    stmt->Bind(3, static_cast<uint32_t>(VERSION_STRING));

    auto selectResult = WorldDatabase.QueryStatement(std::move(stmt));

    if (!selectResult)
    {
        SystemMessage(m_session, "Quest was NOT found for the specified NPC.");
        return false;
    }

    auto delStmt = WorldDatabase.CreateStatement(WORLD_CREATURE_QUEST_STARTER_DELETE);
    delStmt->Bind(0, unit->getEntry());
    delStmt->Bind(1, quest_id);
    delStmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));
    delStmt->Bind(3, static_cast<uint32_t>(VERSION_STRING));

    WorldDatabase.ExecuteStatement(std::move(delStmt));

    sQuestMgr.LoadExtraQuestStuff();

    QuestRelation* qstrel = nullptr;
    qstrel->qst = qst;
    qstrel->type = QUESTGIVER_QUEST_START;

    uint8_t qstrelid;
    if (unit->HasQuests())
    {
        qstrelid = (uint8_t)unit->GetQuestRelation(quest_id);
        unit->DeleteQuest(qstrel);
    }

    unit->_LoadQuests();

    std::string recout = "|cff00ff00Deleted Quest from NPC: |cff00ccff" + qst->title + "\n\n";
    SendMultilineMessage(m_session, recout.c_str());

    sGMLog.writefromsession(m_session, "deleted starter of quest %u [%s] to NPC %u [%s]",
        qst->id, qst->title.c_str(), unit->getEntry(), unit->GetCreatureProperties()->Name.c_str());

    return true;
}

bool ChatHandler::HandleQuestDelFinishCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    WoWGuid wowGuid;
    wowGuid.Init(m_session->GetPlayer()->getTargetGuid());
    if (wowGuid.getGuidLowPart() == 0)
    {
        SystemMessage(m_session, "You must target an npc.");
        return false;
    }

    Creature* unit = m_session->GetPlayer()->getWorldMap()->getCreature(wowGuid.getGuidLowPart());
    if (!unit || !unit->isQuestGiver())
    {
        SystemMessage(m_session, "You must target a valid quest-giving npc.");
        return false;
    }

    uint32_t quest_id = std::stoul(args);
    if (quest_id == 0)
    {
        quest_id = GetQuestIDFromLink(args);
        if (quest_id == 0)
            return false;
    }

    const QuestProperties* qst = sMySQLStore.getQuestProperties(quest_id);
    if (!qst)
    {
        SystemMessage(m_session, "Invalid Quest selected.");
        return false;
    }

    auto stmt = WorldDatabase.CreateStatement(WORLD_CREATURE_QUEST_FINISHER_SELECT);
    stmt->Bind(0, unit->getEntry());
    stmt->Bind(1, quest_id);
    stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));
    stmt->Bind(3, static_cast<uint32_t>(VERSION_STRING));

    auto selectResult = WorldDatabase.QueryStatement(std::move(stmt));

    if (!selectResult)
    {
        SystemMessage(m_session, "Quest was NOT found for the specified NPC.");
        return true;
    }

    auto delStmt = WorldDatabase.CreateStatement(WORLD_CREATURE_QUEST_FINISHER_DELETE);
    delStmt->Bind(0, unit->getEntry());
    delStmt->Bind(1, quest_id);
    delStmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));
    delStmt->Bind(3, static_cast<uint32_t>(VERSION_STRING));

    WorldDatabase.ExecuteStatement(std::move(delStmt));

    sQuestMgr.LoadExtraQuestStuff();

    QuestRelation* qstrel = nullptr;
    qstrel->qst = qst;
    qstrel->type = QUESTGIVER_QUEST_END;

    uint8_t qstrelid;
    if (unit->HasQuests())
    {
        qstrelid = (uint8_t)unit->GetQuestRelation(quest_id);
        unit->DeleteQuest(qstrel);
    }

    unit->_LoadQuests();

    std::string recout = "|cff00ff00Deleted Quest from NPC: |cff00ccff" + qst->title + "\n\n";
    SendMultilineMessage(m_session, recout.c_str());

    sGMLog.writefromsession(m_session, "deleted finisher of quest %u [%s] to NPC %u [%s]",
        qst->id, qst->title.c_str(), unit->getEntry(), unit->GetCreatureProperties()->Name.c_str());

    return true;
}

bool ChatHandler::HandleQuestDelBothCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    bool bValid = ChatHandler::HandleQuestDelStartCommand(args, m_session);

    if (bValid)
        ChatHandler::HandleQuestDelFinishCommand(args, m_session);

    return true;
}

bool ChatHandler::HandleQuestFinisherCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    uint32_t questId = std::stoul(args);
    std::string recout;

    // --- Creature Finisher ---
    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_QUEST_FINISHER_CREATURE_SELECT);
        stmt->Bind(0, questId);
        stmt->Bind(1, static_cast<uint32_t>(VERSION_STRING));
        stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));

        auto result = WorldDatabase.QueryStatement(std::move(stmt));

        if (result)
        {
            Field* fields = result->Fetch();
            uint32_t creatureId = fields[0].asUint32();

            std::string name = "N/A";
            if (const auto* props = sMySQLStore.getCreatureProperties(creatureId))
                name = props->Name;

            auto stmt2 = WorldDatabase.CreateStatement(WORLD_QUEST_FINISHER_CREATURE_SPAWNID_SELECT);
            stmt2->Bind(0, creatureId);
            stmt2->Bind(1, static_cast<uint32_t>(VERSION_STRING));
            stmt2->Bind(2, static_cast<uint32_t>(VERSION_STRING));

            auto spawnResult = WorldDatabase.QueryStatement(std::move(stmt2));

            std::string spawnId = spawnResult ? spawnResult->Fetch()[0].asCString() : "N/A";

            recout = "|cff00ccffQuest Finisher found: creature id, spawnid, name\n\n";
            SendMultilineMessage(m_session, recout.c_str());

            recout = "|cff00ccff" + std::to_string(creatureId) + ", " + spawnId + ", " + name + "\n\n";
            SendMultilineMessage(m_session, recout.c_str());
        }
        else
        {
            recout = "|cff00ccffNo creature quest finishers found.\n\n";
            SendMultilineMessage(m_session, recout.c_str());
        }
    }

    // --- GameObject Finisher ---
    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_QUEST_FINISHER_OBJECT_SELECT);
        stmt->Bind(0, questId);
        stmt->Bind(1, static_cast<uint32_t>(VERSION_STRING));
        stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));

        auto result = WorldDatabase.QueryStatement(std::move(stmt));

        if (result)
        {
            Field* fields = result->Fetch();
            uint32_t objectId = fields[0].asUint32();

            std::string name = "N/A";
            if (const auto* props = sMySQLStore.getItemProperties(objectId))
                name = props->Name;

            auto stmt2 = WorldDatabase.CreateStatement(WORLD_QUEST_FINISHER_OBJECT_SPAWNID_SELECT);
            stmt2->Bind(0, objectId);
            stmt2->Bind(1, static_cast<uint32_t>(VERSION_STRING));
            stmt2->Bind(2, static_cast<uint32_t>(VERSION_STRING));

            auto spawnResult = WorldDatabase.QueryStatement(std::move(stmt2));

            std::string spawnId = spawnResult ? spawnResult->Fetch()[0].asCString() : "N/A";

            recout = "|cff00ccffQuest Finisher found: object id, spawnid, name\n\n";
            SendMultilineMessage(m_session, recout.c_str());

            recout = "|cff00ccff" + std::to_string(objectId) + ", " + spawnId + ", " + name + "\n\n";
            SendMultilineMessage(m_session, recout.c_str());
        }
        else
        {
            recout = "|cff00ccffNo object quest finishers found.\n\n";
            SendMultilineMessage(m_session, recout.c_str());
        }
    }

    return true;
}

bool ChatHandler::HandleQuestStarterSpawnCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    uint32_t questId = std::stoul(args);
    std::string recout;

    auto stmt = WorldDatabase.CreateStatement(WORLD_QUEST_STARTER_CREATURE_SELECT);
    stmt->Bind(0, questId);
    stmt->Bind(1, static_cast<uint32_t>(VERSION_STRING));
    stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));

    auto result = WorldDatabase.QueryStatement(std::move(stmt));

    if (!result)
    {
        recout = "|cff00ccffNo quest starter NPCs found.\n\n";
        SendMultilineMessage(m_session, recout.c_str());
        return true;
    }

    Field* fields = result->Fetch();
    uint32_t starterId = fields[0].asUint32();
    std::string starterName = "N/A";

    if (const auto* creatureProps = sMySQLStore.getCreatureProperties(starterId))
        starterName = creatureProps->Name;
    else
    {
        recout = "|cff00ccffNo quest starter info found.\n\n";
        SendMultilineMessage(m_session, recout.c_str());
        return true;
    }

    auto stmt2 = WorldDatabase.CreateStatement(WORLD_QUEST_STARTER_CREATURE_SPAWN_LOCATION_SELECT);
    stmt2->Bind(0, starterId);
    stmt2->Bind(1, static_cast<uint32_t>(VERSION_STRING));
    stmt2->Bind(2, static_cast<uint32_t>(VERSION_STRING));

    auto spawnResult = WorldDatabase.QueryStatement(std::move(stmt2));

    if (!spawnResult)
    {
        recout = "|cff00ccffNo spawn location for quest starter was found.\n\n";
        SendMultilineMessage(m_session, recout.c_str());
        return true;
    }

    fields = spawnResult->Fetch();
    uint32_t locmap = fields[0].asUint32();
    float x = fields[1].asFloat();
    float y = fields[2].asFloat();
    float z = fields[3].asFloat();

    recout = "|cff00ccffPorting to Quest Starter/Giver: id, name\n\n";
    SendMultilineMessage(m_session, recout.c_str());

    recout = "|cff00ccff" + std::to_string(starterId) + ", " + starterName + "\n\n";
    SendMultilineMessage(m_session, recout.c_str());

    m_session->GetPlayer()->safeTeleport(locmap, 0, LocationVector(x, y, z));
    return true;
}

bool ChatHandler::HandleQuestFinisherSpawnCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    uint32_t questId = std::stoul(args);
    std::string recout;

    auto stmt = WorldDatabase.CreateStatement(WORLD_QUEST_FINISHER_CREATURE_SELECT);
    stmt->Bind(0, questId);
    stmt->Bind(1, static_cast<uint32_t>(VERSION_STRING));
    stmt->Bind(2, static_cast<uint32_t>(VERSION_STRING));

    auto result = WorldDatabase.QueryStatement(std::move(stmt));

    if (!result)
    {
        recout = "|cff00ccffNo quest finisher NPCs found.\n\n";
        SendMultilineMessage(m_session, recout.c_str());
        return true;
    }

    Field* fields = result->Fetch();
    uint32_t finisherId = fields[0].asUint32();
    std::string finisherName = "N/A";

    if (auto* props = sMySQLStore.getCreatureProperties(finisherId))
        finisherName = props->Name;
    else
    {
        recout = "|cff00ccffNo quest finisher info found.\n\n";
        SendMultilineMessage(m_session, recout.c_str());
        return true;
    }

    auto stmt2 = WorldDatabase.CreateStatement(WORLD_QUEST_FINISHER_CREATURE_SPAWN_LOCATION_SELECT);
    stmt2->Bind(0, finisherId);
    stmt2->Bind(1, static_cast<uint32_t>(VERSION_STRING));
    stmt2->Bind(2, static_cast<uint32_t>(VERSION_STRING));

    auto spawnResult = WorldDatabase.QueryStatement(std::move(stmt2));

    if (!spawnResult)
    {
        recout = "|cff00ccffNo spawn location for quest finisher was found.\n\n";
        SendMultilineMessage(m_session, recout.c_str());
        return true;
    }

    fields = spawnResult->Fetch();
    uint32_t mapId = fields[0].asUint32();
    float x = fields[1].asFloat();
    float y = fields[2].asFloat();
    float z = fields[3].asFloat();

    recout = "|cff00ccffPorting to Quest Finisher: id, name\n\n";
    SendMultilineMessage(m_session, recout.c_str());

    recout = "|cff00ccff" + std::to_string(finisherId) + ", " + finisherName + "\n\n";
    SendMultilineMessage(m_session, recout.c_str());

    m_session->GetPlayer()->safeTeleport(mapId, 0, LocationVector(x, y, z));
    return true;
}

bool ChatHandler::HandleQuestLoadCommand(const char* /*args*/, WorldSession* m_session)
{
    BlueSystemMessage(m_session, "Load of quests from the database has been initiated ...");
    auto startTime = Util::TimeNow();

    sQuestMgr.LoadExtraQuestStuff();

    BlueSystemMessage(m_session, "Load completed in %u ms.", static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));

    WoWGuid wowGuid;
    wowGuid.Init(m_session->GetPlayer()->getTargetGuid());

    if (wowGuid.getRawGuid() == 0)
        return true;

    Creature* unit = m_session->GetPlayer()->getWorldMap()->getCreature(wowGuid.getGuidLowPart());
    if (!unit)
        return true;

    if (!unit->isQuestGiver())
        return true;

    // If player targeted a questgiver assume they want the NPC reloaded, too
    unit->_LoadQuests();

    return true;
}

bool ChatHandler::HandleQuestRemoveCommand(const char* args, WorldSession* m_session)
{
    if (!*args)
        return false;

    Player* plr = GetSelectedPlayer(m_session, true, true);
    if (plr == nullptr)
        return true;

    std::string recout = "";
    uint32_t quest_id = std::stoul(args);
    if (quest_id == 0)
    {
        quest_id = GetQuestIDFromLink(args);
        if (quest_id == 0)
            return false;
    }

    QuestProperties const* qst = sMySQLStore.getQuestProperties(quest_id);
    if (qst)
    {
        recout = RemoveQuestFromPlayer(plr, qst);
        sGMLog.writefromsession(m_session, "removed quest %u [%s] from player %s", qst->id, qst->title.c_str(), plr->getName().c_str());
    }
    else
        recout = "Invalid quest selected, unable to remove.\n\n";

    SystemMessage(m_session, recout.c_str());

    return true;
}

bool ChatHandler::HandleQuestRewardCommand(const char* args, WorldSession* m_session)
{
    if (!*args) return false;

    std::stringstream recout;

    uint32_t qu_id = std::stoul(args);
    if (qu_id == 0)
    {
        qu_id = GetQuestIDFromLink(args);
        if (qu_id == 0)
            return false;
    }

    QuestProperties const* q = sMySQLStore.getQuestProperties(qu_id);
    if (q)
    {
        for (uint32_t r = 0; r < q->count_reward_item; ++r)
        {
            uint32_t itemid = q->reward_item[r];
            ItemProperties const* itemProto = sMySQLStore.getItemProperties(itemid);
            if (!itemProto)
            {
                recout << "Unknown item id %lu" << itemid;
                sLogger.failure("WORLD: Unknown item id 0x%08x", itemid);
            }
            else
            {
                recout << "Reward (" << itemid << "): " << sMySQLStore.getItemLinkByProto(itemProto, m_session->language);
                if (q->reward_itemcount[r] == 1)
                    recout << "\n";
                else
                    recout << "[x" << q->reward_itemcount[r] << "]\n";
            }
        }
        for (uint32_t r = 0; r < q->count_reward_choiceitem; ++r)
        {
            uint32_t itemid = q->reward_choiceitem[r];
            ItemProperties const* itemProto = sMySQLStore.getItemProperties(itemid);
            if (!itemProto)
            {
                recout << "Unknown item id %lu" << itemid;
                sLogger.failure("WORLD: Unknown item id 0x%08x", itemid);
            }
            else
            {
                recout << "Reward choice (" << itemid << "): " << sMySQLStore.getItemLinkByProto(itemProto, m_session->language);
                if (q->reward_choiceitemcount[r] == 1)
                    recout << "\n";
                else
                    recout << "[x" << q->reward_choiceitemcount[r] << "]\n";
            }
        }
        if ((q->count_reward_choiceitem == 0) && (q->count_reward_item == 0))
        {
            recout << "Quest " << qu_id << " has no item rewards.";
        }
    }
    else
    {
        recout << "Quest ID " << qu_id << " not found.\n";
        sLogger.failure("Quest ID {} not found.", qu_id);
    }

    SendMultilineMessage(m_session, recout.str().data());

    return true;
}
