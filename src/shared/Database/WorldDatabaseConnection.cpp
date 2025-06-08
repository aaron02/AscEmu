/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "WorldDatabaseConnection.hpp"

WorldDatabaseConnection::WorldDatabaseConnection()
{
}

WorldDatabaseConnection::~WorldDatabaseConnection()
{
}

void WorldDatabaseConnection::PrepareStatements()
{
    // Update
    RegisterStatement(WORLD_VERSION_LAST_UPDATE, "SELECT LastUpdate FROM world_db_version ORDER BY id DESC LIMIT 1");

    // Server
    RegisterStatement(WORLD_SEL_TABLE_EXISTS, "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = ? AND table_name = ?");
    RegisterStatement(WORLD_SEL_CREATURE_MOVEMENT_PROPS, "SELECT CreatureId, Ground, Swim, Flight, Rooted, Chase, Random, InteractionPauseTimer FROM creature_properties_movement;");
    RegisterStatement(WORLD_SEL_GAMEOBJECT_QUEST_ITEM_BINDING, "SELECT entry, quest, item, item_count FROM gameobject_quest_item_binding;");

    // Auctions
    RegisterStatement(WORLD_AUCTIONHOUSE_GROUPS_SELECT, "SELECT DISTINCT ahgroup FROM auctionhouse");
    RegisterStatement(WORLD_AUCTIONHOUSE_ENTRIES_SELECT, "SELECT creature_entry, ahgroup FROM auctionhouse");

    // Achievements
    RegisterStatement(WORLD_ACHIEVEMENT_REWARD_SELECT_ALL, "SELECT entry, gender, title_A, title_H, item, sender, subject, text FROM achievement_reward");

    // Quests
    RegisterStatement(WORLD_QUEST_ITEM_BINDING_BY_QUEST, "SELECT item, item_count FROM gameobject_quest_item_binding WHERE quest = ?");
    RegisterStatement(WORLD_CREATURE_QUEST_STARTER_BY_QUEST, "SELECT id FROM creature_quest_starter WHERE quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_SPAWN_BY_ENTRY, "SELECT id FROM creature_spawns WHERE entry = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_GO_QUEST_STARTER_BY_QUEST, "SELECT id FROM gameobject_quest_starter WHERE quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_GO_SPAWN_BY_ENTRY, "SELECT id FROM gameobject_spawns WHERE entry = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_QUEST_LIST_BY_CREATURE, "SELECT quest FROM creature_quest_starter WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_QUEST_STARTER_EXISTS, "SELECT id FROM creature_quest_starter WHERE id = ? AND quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_QUEST_STARTER_INSERT, "INSERT INTO creature_quest_starter (id, quest, min_build, max_build) VALUES (?, ?, ?, ?)");
    RegisterStatement(WORLD_CREATURE_QUEST_FINISHER_EXISTS, "SELECT id FROM creature_quest_finisher WHERE id = ? AND quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_QUEST_FINISHER_INSERT, "INSERT INTO creature_quest_finisher (id, quest, min_build, max_build) VALUES (?, ?, ?, ?)");
    RegisterStatement(WORLD_CREATURE_QUEST_STARTER_SELECT, "SELECT id FROM creature_quest_starter WHERE id = ? AND quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_QUEST_STARTER_DELETE, "DELETE FROM creature_quest_starter WHERE id = ? AND quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_QUEST_FINISHER_SELECT, "SELECT id FROM creature_quest_finisher WHERE id = ? AND quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_QUEST_FINISHER_DELETE, "DELETE FROM creature_quest_finisher WHERE id = ? AND quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_QUEST_FINISHER_CREATURE_SPAWNID_SELECT, "SELECT id FROM creature_spawns WHERE entry = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_QUEST_FINISHER_OBJECT_SELECT, "SELECT id FROM gameobject_quest_finisher WHERE quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_QUEST_FINISHER_OBJECT_SPAWNID_SELECT, "SELECT id FROM gameobject_spawns WHERE entry = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_QUEST_STARTER_CREATURE_SELECT, "SELECT id FROM creature_quest_starter WHERE quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_QUEST_STARTER_CREATURE_SPAWN_LOCATION_SELECT, "SELECT map, position_x, position_y, position_z FROM creature_spawns WHERE entry = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_QUEST_FINISHER_CREATURE_SELECT, "SELECT id FROM creature_quest_finisher WHERE quest = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_QUEST_FINISHER_CREATURE_SPAWN_LOCATION_SELECT, "SELECT map, position_x, position_y, position_z FROM creature_spawns WHERE entry = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_ITEM_QUEST_ASSOCIATION_SELECT, "SELECT item, quest, item_count FROM item_quest_association");
    RegisterStatement(WORLD_QUEST_POI_SELECT, "SELECT questId, poiId, objIndex, mapId, mapAreaId, floorId, unk3, unk4 FROM quest_poi");
    RegisterStatement(WORLD_QUEST_POI_POINTS_SELECT, "SELECT questId, poiId, x, y FROM quest_poi_points");


    // Recall
    RegisterStatement(WORLD_RECALL_INSERT, "INSERT INTO recall (name, min_build, max_build, mapid, positionX, positionY, positionZ, Orientation) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(WORLD_RECALL_DELETE_BY_NAME, "DELETE FROM recall WHERE name = ?");

    // Creatures
    RegisterStatement(WORLD_CREATURE_ENTRIES_BY_MAP, "SELECT entry FROM creature_spawns WHERE map = ? GROUP BY(entry)");
    RegisterStatement(WORLD_CREATURE_SPAWN_WAYPOINT_UPDATE, "UPDATE creature_spawns SET movetype = ?, waypoint_group = ? WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_CANFLY_UPDATE, "UPDATE creature_spawns SET CanFly = ? WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_EMOTE_STATE_UPDATE, "UPDATE creature_spawns SET emote_state = ? WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_FLAGS_UPDATE, "UPDATE creature_spawns SET flags = ? WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_SPAWN_PHASE_UPDATE, "UPDATE creature_spawns SET phase = ? WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_SPAWN_STANDSTATE_UPDATE, "UPDATE creature_spawns SET standstate = ? WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_CREATURE_MOVEMENT_OVERRIDE_SELECT_ALL, "SELECT SpawnId, Ground, Swim, Flight, Rooted, Chase, Random FROM creature_movement_override");
    RegisterStatement(WORLD_CREATURE_SPAWN_EXISTS_BY_ID, "SELECT id FROM creature_spawns WHERE id = ?");
    RegisterStatement(WORLD_CREATURE_TIMED_EMOTES_SELECT_ALL, "SELECT * FROM creature_timed_emotes ORDER BY rowid ASC");
    RegisterStatement(WORLD_CREATURE_SELECT_WAYPOINTS, "SELECT id, point, position_x, position_y, position_z, orientation, move_type, delay, action, action_chance FROM creature_script_waypoints ORDER BY id, point");
    RegisterStatement(WORLD_CREATURE_INSERT_WAYPOINT, "INSERT INTO creature_waypoints VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(WORLD_CREATURE_DELETE_WAYPOINT, "DELETE FROM creature_waypoints WHERE id = ? AND point = ?");
    RegisterStatement(WORLD_CREATURE_DELETE_ALL_WAYPOINTS_BY_ID, "DELETE FROM creature_waypoints WHERE id = ?");
    RegisterStatement(WORLD_SEL_MAX_ITEMPAGE_ENTRY, "SELECT MAX(entry) FROM item_pages");
    RegisterStatement(WORLD_SEL_CREATURE_FORMATIONS, "SELECT leaderGUID, memberGUID, dist, angle, groupAI, point_1, point_2 FROM creature_formations ORDER BY leaderGUID");
    RegisterStatement(WORLD_SEL_CREATURE_SPAWN_BY_ID, "SELECT * FROM creature_spawns WHERE id = ?");
    RegisterStatement(WORLD_INS_GAMEOBJECT_SPAWN,
        "INSERT INTO gameobject_spawns "
        "(id, min_build, max_build, entry, map, phase, position_x, position_y, position_z, orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs, state, event_entry) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

    RegisterStatement(WORLD_UPD_GAMEOBJECT_SPAWN,
        "UPDATE gameobject_spawns SET "
        "phase = ?, position_x = ?, position_y = ?, position_z = ?, orientation = ?, "
        "rotation0 = ?, rotation1 = ?, rotation2 = ?, rotation3 = ?, "
        "spawntimesecs = ?, state = ?, event_entry = ? "
        "WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_DEL_GAMEOBJECT_SPAWN, "DELETE FROM gameobject_spawns WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_DEL_GAMEOBJECT_SPAWN_EXTRA, "DELETE FROM gameobject_spawns_extra WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_DEL_GAMEOBJECT_SPAWN_OVERRIDES, "DELETE FROM gameobject_spawns_overrides WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_DEL_CREATURE_SPAWN_BY_ID_AND_BUILD, "DELETE FROM creature_spawns WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_INS_CREATURE_SPAWN,
        "INSERT INTO creature_spawns "
        "(id, min_build, max_build, entry, map, "
        "position_x, position_y, position_z, orientation, "
        "movetype, displayid, faction, flags, pvp_flagged, "
        "bytes0, emote_state, npc_respawn_link, channel_spell, "
        "channel_target_sqlid, channel_target_sqlid_creature, "
        "standstate, death_state, mountdisplayid, sheath_state, "
        "slot1item, slot2item, slot3item, "
        "CanFly, phase, event_entry, wander_distance, waypoint_group) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    RegisterStatement(WORLD_DEL_CREATURE_SPAWN_VERSIONED, "DELETE FROM creature_spawns WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_SEL_CREATURE_DIFFICULTY, "SELECT entry, difficulty_1, difficulty_2, difficulty_3 FROM creature_difficulty;");
    RegisterStatement(WORLD_SEL_DISPLAY_BOUNDING_BOXES, "SELECT displayid, highz FROM display_bounding_boxes;");
    RegisterStatement(WORLD_SEL_VENDOR_RESTRICTIONS, "SELECT entry, racemask, classmask, reqrepfaction, reqrepfactionvalue, canbuyattextid, cannotbuyattextid, flags FROM vendor_restrictions;");
    RegisterStatement(WORLD_SEL_NPC_GOSSIP_TEXTS,
        "SELECT entry, "
        "prob0, text0_0, text0_1, lang0, EmoteDelay0_0, Emote0_0, EmoteDelay0_1, Emote0_1, EmoteDelay0_2, Emote0_2, "
        "prob1, text1_0, text1_1, lang1, EmoteDelay1_0, Emote1_0, EmoteDelay1_1, Emote1_1, EmoteDelay1_2, Emote1_2, "
        "prob2, text2_0, text2_1, lang2, EmoteDelay2_0, Emote2_0, EmoteDelay2_1, Emote2_1, EmoteDelay2_2, Emote2_2, "
        "prob3, text3_0, text3_1, lang3, EmoteDelay3_0, Emote3_0, EmoteDelay3_1, Emote3_1, EmoteDelay3_2, Emote3_2, "
        "prob4, text4_0, text4_1, lang4, EmoteDelay4_0, Emote4_0, EmoteDelay4_1, Emote4_1, EmoteDelay4_2, Emote4_2, "
        "prob5, text5_0, text5_1, lang5, EmoteDelay5_0, Emote5_0, EmoteDelay5_1, Emote5_1, EmoteDelay5_2, Emote5_2, "
        "prob6, text6_0, text6_1, lang6, EmoteDelay6_0, Emote6_0, EmoteDelay6_1, Emote6_1, EmoteDelay6_2, Emote6_2, "
        "prob7, text7_0, text7_1, lang7, EmoteDelay7_0, Emote7_0, EmoteDelay7_1, Emote7_1, EmoteDelay7_2, Emote7_2 "
        "FROM npc_gossip_texts;");
    RegisterStatement(WORLD_SEL_NPC_SCRIPT_TEXT, "SELECT entry, text, creature_entry, id, type, language, probability, emote, duration, sound, broadcast_id FROM npc_script_text;");
    RegisterStatement(WORLD_SEL_GOSSIP_MENU_OPTION, "SELECT entry, option_text FROM gossip_menu_option;");
    RegisterStatement(WORLD_SEL_GRAVEYARDS, "SELECT id, position_x, position_y, position_z, orientation, zoneid, adjacentzoneid, mapid, faction FROM graveyards;");
    RegisterStatement(WORLD_SEL_SPELL_TELEPORT_COORDS, "SELECT id, mapId, position_x, position_y, position_z FROM spell_teleport_coords;");
    RegisterStatement(WORLD_SEL_FISHING, "SELECT zone, MinSkill, MaxSkill FROM fishing;");
    RegisterStatement(WORLD_SEL_WORLDMAP_INFO,
        "SELECT entry, screenid, type, maxplayers, minlevel, minlevel_heroic, repopx, repopy, repopz, repopentry, "
        "area_name, flags, cooldown, lvl_mod_a, required_quest_A, required_quest_H, required_item, "
        "heroic_keyid_1, heroic_keyid_2, viewingDistance, required_checkpoint "
        "FROM worldmap_info base "
        "WHERE build=(SELECT MAX(build) FROM worldmap_info buildspecific WHERE base.entry = buildspecific.entry AND build <= ?)");
    RegisterStatement(WORLD_SEL_ZONE_GUARDS, "SELECT zone, horde_entry, alliance_entry FROM zoneguards;");
    RegisterStatement(WORLD_SEL_BATTLEMASTERS, "SELECT creature_entry, battleground_id FROM battlemasters;");
    RegisterStatement(WORLD_SEL_TOTEMDISPLAYIDS,
        "SELECT race, totem, displayid FROM totemdisplayids base "
        "WHERE build=(SELECT MAX(build) FROM totemdisplayids spec WHERE base.race = spec.race AND base.totem = spec.totem AND build <= ?)");
    RegisterStatement(WORLD_SEL_SPELLCLICKSPELLS, "SELECT npc_entry, spell_id, cast_flags, user_type FROM npc_spellclick_spells;");
    RegisterStatement(WORLD_SEL_WORLDSTRING_TABLES, "SELECT entry, text FROM worldstring_tables;");
    RegisterStatement(WORLD_SEL_POINTS_OF_INTEREST, "SELECT entry, x, y, icon, flags, data, icon_name FROM points_of_interest;");
    RegisterStatement(WORLD_SEL_LINKED_SET_BONUS, "SELECT itemset, itemset_bonus FROM itemset_linked_itemsetbonus;");
    RegisterStatement(WORLD_SEL_INITIAL_EQUIPMENT, "SELECT creature_entry, itemslot_1, itemslot_2, itemslot_3 FROM creature_initial_equip;");
    RegisterStatement(WORLD_SEL_PLAYER_CREATE_INFO,
        "SELECT race, class, mapID, zoneID, positionX, positionY, positionZ, orientation "
        "FROM playercreateinfo pi "
        "WHERE build=(SELECT MAX(build) FROM playercreateinfo buildspecific "
        "WHERE pi.race = buildspecific.race AND pi.class = buildspecific.class AND build <= ?)");
    RegisterStatement(WORLD_SEL_PLAYER_CREATE_INFO_BARS, "SELECT race, class, button, action, type, misc FROM playercreateinfo_bars WHERE build = ?");
    RegisterStatement(WORLD_SEL_PLAYER_CREATE_INFO_ITEMS, "SELECT race, class, protoid, slotid, amount FROM playercreateinfo_items WHERE build = ?");
    RegisterStatement(WORLD_SEL_PLAYER_CREATE_INFO_SKILLS, "SELECT raceMask, classMask, skillid, level FROM playercreateinfo_skills WHERE min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_SEL_PLAYER_CREATE_INFO_SPELLS, "SELECT raceMask, classMask, spellid FROM playercreateinfo_spell_learn WHERE min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_SEL_PLAYER_CREATE_INFO_SPELLS_CAST, "SELECT raceMask, classMask, spellid FROM playercreateinfo_spell_cast WHERE build = ?");
    RegisterStatement(WORLD_SEL_PLAYER_LEVELSTATS, "SELECT race, class, level, BaseStrength, BaseAgility, BaseStamina, BaseIntellect, BaseSpirit FROM player_levelstats WHERE build = ?");
    RegisterStatement(WORLD_SEL_PLAYER_CLASSLEVELSTATS, "SELECT class, level, BaseHealth, BaseMana FROM player_classlevelstats WHERE build = ?");
    RegisterStatement(WORLD_SEL_PLAYER_XP_TO_LEVEL, "SELECT player_lvl, next_lvl_req_xp FROM player_xp_for_level base WHERE build=(SELECT MAX(build) FROM player_xp_for_level spec WHERE base.player_lvl = spec.player_lvl AND build <= ?)");
    RegisterStatement(WORLD_SEL_SPELLOVERRIDE, "SELECT DISTINCT overrideId FROM spelloverride;");
    RegisterStatement(WORLD_SEL_SPELLID_FOR_OVERRIDEID, "SELECT spellId FROM spelloverride WHERE overrideId = ?");
    RegisterStatement(WORLD_SEL_NPC_GOSSIP_PROPERTIES, "SELECT creatureid, textid FROM npc_gossip_properties;");
    RegisterStatement(WORLD_SEL_PET_LEVEL_ABILITIES, "SELECT level, health, armor, strength, agility, stamina, intellect, spirit FROM pet_level_abilities;");
    RegisterStatement(WORLD_SEL_AREA_TRIGGER, "SELECT entry, type, map, screen, name, position_x, position_y, position_z, orientation, required_honor_rank, required_level FROM areatriggers;");
    RegisterStatement(WORLD_SEL_FILTER_CHARACTER_NAMES, "SELECT * FROM wordfilter_character_names;");
    RegisterStatement(WORLD_SEL_FILTER_CHAT, "SELECT * FROM wordfilter_chat;");
    RegisterStatement(WORLD_SEL_LOCALES_CREATURE, "SELECT id, language_code, name, subname FROM locales_creature;");
    RegisterStatement(WORLD_SEL_LOCALES_GAMEOBJECT, "SELECT entry, language_code, name FROM locales_gameobject;");
    RegisterStatement(WORLD_SEL_LOCALES_GOSSIP_MENU_OPTION, "SELECT entry, language_code, option_text FROM locales_gossip_menu_option;");
    RegisterStatement(WORLD_SEL_LOCALES_ITEM, "SELECT entry, language_code, name, description FROM locales_item;");
    RegisterStatement(WORLD_SEL_LOCALES_ITEM_PAGES, "SELECT entry, language_code, text FROM locales_item_pages;");
    RegisterStatement(WORLD_SEL_LOCALES_NPC_SCRIPT_TEXT, "SELECT entry, language_code, text FROM locales_npc_script_text;");
    RegisterStatement(WORLD_SEL_LOCALES_NPC_GOSSIP_TEXTS, "SELECT entry, language_code, text0, text0_1, text1, text1_1, text2, text2_1, text3, text3_1, text4, text4_1, text5, text5_1, text6, text6_1, text7, text7_1 FROM locales_npc_gossip_texts;");
    RegisterStatement(WORLD_SEL_LOCALES_QUEST, "SELECT entry, language_code, Title, Details, Objectives, CompletionText, IncompleteText, EndText, ObjectiveText1, ObjectiveText2, ObjectiveText3, ObjectiveText4 FROM locales_quest;");
    RegisterStatement(WORLD_SEL_LOCALES_WORLDBROADCAST, "SELECT entry, language_code, text FROM locales_worldbroadcast;");
    RegisterStatement(WORLD_SEL_LOCALES_WORLDMAP_INFO, "SELECT entry, language_code, text FROM locales_worldmap_info;");
    RegisterStatement(WORLD_SEL_LOCALES_WORLDSTRING_TABLE, "SELECT entry, language_code, text FROM locales_worldstring_table;");
    RegisterStatement(WORLD_SEL_PROFESSION_DISCOVERIES, "SELECT SpellId, SpellToDiscover, SkillValue, Chance FROM professiondiscoveries;");
    RegisterStatement(WORLD_SEL_TRANSPORT_DATA, "SELECT entry, name FROM transport_data WHERE min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_SEL_GAMEOBJECT_PROPERTIES_TYPE_15, "SELECT entry FROM gameobject_properties WHERE type = 15 AND build <= ? ORDER BY entry ASC;");
    RegisterStatement(WORLD_SEL_GO_PROPERTIES_TYPE_15_PARAM6, "SELECT parameter_6 FROM gameobject_properties WHERE type = 15 AND build <= ? ORDER BY entry ASC;");
    RegisterStatement(WORLD_SEL_GOSSIP_MENU, "SELECT gossip_menu, text_id FROM gossip_menu ORDER BY gossip_menu;");
    RegisterStatement(WORLD_SEL_GOSSIP_MENU_ITEMS,
        "SELECT id, item_order, menu_option, icon, on_choose_action, on_choose_data, on_choose_data2, "
        "on_choose_data3, on_choose_data4, next_gossip_menu, next_gossip_text, requirement_type, requirement_data "
        "FROM gossip_menu_items ORDER BY id, item_order;");
    RegisterStatement(WORLD_SEL_CREATURE_AI_SCRIPTS, "SELECT * FROM creature_ai_scripts WHERE min_build <= ? AND max_build >= ? ORDER BY entry, event;");
    RegisterStatement(WORLD_SEL_SPAWN_GROUP_ID, "SELECT * FROM spawn_group_id ORDER BY groupId;");
    RegisterStatement(WORLD_SEL_CREATURE_GROUP_SPAWN, "SELECT * FROM creature_group_spawn ORDER BY groupId;");
    RegisterStatement(WORLD_SEL_SCRIPT_SPLINE_CHAIN_META, "SELECT entry, chainId, splineId, expectedDuration, msUntilNext, velocity FROM script_spline_chain_meta ORDER BY entry ASC, chainId ASC, splineId ASC;");
    RegisterStatement(WORLD_SEL_SCRIPT_SPLINE_CHAIN_WAYPOINTS, "SELECT entry, chainId, splineId, wpId, x, y, z FROM script_spline_chain_waypoints ORDER BY entry ASC, chainId ASC, splineId ASC, wpId ASC;");
    RegisterStatement(WORLD_SEL_ITEM_PROPERTIES, "SELECT * FROM item_properties base WHERE build=(SELECT MAX(build) FROM item_properties spec WHERE base.entry = spec.entry AND build <= ?)");
    RegisterStatement(WORLD_SEL_ITEM_PAGES, "SELECT entry, text, next_page FROM item_pages;");
    RegisterStatement(WORLD_SEL_CREATURE_PROPERTIES,
        "SELECT entry, killcredit1, killcredit2, male_displayid, female_displayid, male_displayid2, female_displayid2, "
        "name, subname, icon_name, type_flags, type, family, `rank`, encounter, base_attack_mod, range_attack_mod, leader, "
        "minlevel, maxlevel, faction, minhealth, maxhealth, mana, scale, npcflags, attacktime, attack_school, "
        "mindamage, maxdamage, can_ranged, rangedattacktime, rangedmindamage, rangedmaxdamage, respawntime, armor, "
        "resistance1, resistance2, resistance3, resistance4, resistance5, resistance6, combat_reach, bounding_radius, "
        "auras, boss, money, isTriggerNpc, walk_speed, run_speed, fly_speed, extra_a9_flags, spell1, spell2, spell3, "
        "spell4, spell5, spell6, spell7, spell8, spell_flags, modImmunities, isTrainingDummy, guardtype, summonguard, spelldataid, "
        "vehicleid, rooted, questitem1, questitem2, questitem3, questitem4, questitem5, questitem6, waypointid, gossipId "
        "FROM creature_properties base "
        "WHERE build=(SELECT MAX(build) FROM creature_properties buildspecific WHERE base.entry = buildspecific.entry AND build <= ?)");
    RegisterStatement(WORLD_SEL_GAMEOBJECT_PROPERTIES,
        "SELECT entry, type, display_id, name, category_name, cast_bar_text, UnkStr, parameter_0, parameter_1, parameter_2, "
        "parameter_3, parameter_4, parameter_5, parameter_6, parameter_7, parameter_8, parameter_9, parameter_10, parameter_11, "
        "parameter_12, parameter_13, parameter_14, parameter_15, parameter_16, parameter_17, parameter_18, parameter_19, "
        "parameter_20, parameter_21, parameter_22, parameter_23, size, QuestItem1, QuestItem2, QuestItem3, QuestItem4, "
        "QuestItem5, QuestItem6 "
        "FROM gameobject_properties base "
        "WHERE build=(SELECT MAX(build) FROM gameobject_properties buildspecific "
        "WHERE base.entry = buildspecific.entry AND build <= ?)");
    RegisterStatement(WORLD_SEL_GAMEOBJECT_SPAWNS_EXTRA, "SELECT id, parent_rotation0, parent_rotation1, parent_rotation2, parent_rotation3 FROM gameobject_spawns_extra WHERE min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_SEL_GAMEOBJECT_SPAWNS_OVERRIDES, "SELECT id, scale, faction, flags FROM gameobject_spawns_overrides WHERE min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_SEL_QUEST_PROPERTIES,
        "SELECT entry, ZoneId, sort, flags, MinLevel, questlevel, Type, RequiredRaces, RequiredClass, RequiredTradeskill, "
        "RequiredTradeskillValue, RequiredRepFaction, RequiredRepValue, LimitTime, SpecialFlags, PrevQuestId, NextQuestId, srcItem, "
        "SrcItemCount, Title, Details, Objectives, CompletionText, IncompleteText, EndText, ObjectiveText1, ObjectiveText2, "
        "ObjectiveText3, ObjectiveText4, ReqItemId1, ReqItemId2, ReqItemId3, ReqItemId4, ReqItemId5, ReqItemId6, ReqItemCount1, "
        "ReqItemCount2, ReqItemCount3, ReqItemCount4, ReqItemCount5, ReqItemCount6, ReqKillMobOrGOId1, ReqKillMobOrGOId2, "
        "ReqKillMobOrGOId3, ReqKillMobOrGOId4, ReqKillMobOrGOCount1, ReqKillMobOrGOCount2, ReqKillMobOrGOCount3, ReqKillMobOrGOCount4, "
        "ReqCastSpellId1, ReqCastSpellId2, ReqCastSpellId3, ReqCastSpellId4, ReqEmoteId1, ReqEmoteId2, ReqEmoteId3, ReqEmoteId4, "
        "RewChoiceItemId1, RewChoiceItemId2, RewChoiceItemId3, RewChoiceItemId4, RewChoiceItemId5, RewChoiceItemId6, RewChoiceItemCount1, "
        "RewChoiceItemCount2, RewChoiceItemCount3, RewChoiceItemCount4, RewChoiceItemCount5, RewChoiceItemCount6, RewItemId1, RewItemId2, "
        "RewItemId3, RewItemId4, RewItemCount1, RewItemCount2, RewItemCount3, RewItemCount4, RewRepFaction1, RewRepFaction2, RewRepFaction3, "
        "RewRepFaction4, RewRepFaction5, RewRepFaction6, RewRepValue1, RewRepValue2, RewRepValue3, RewRepValue4, RewRepValue5, RewRepValue6, "
        "RewRepLimit, RewMoney, RewXP, RewSpell, CastSpell, MailTemplateId, MailDelaySecs, MailSendItem, PointMapId, PointX, PointY, PointOpt, "
        "RewardMoneyAtMaxLevel, ExploreTrigger1, ExploreTrigger2, ExploreTrigger3, ExploreTrigger4, RequiredOneOfQuest, RequiredQuest1, "
        "RequiredQuest2, RequiredQuest3, RequiredQuest4, RemoveQuests, ReceiveItemId1, ReceiveItemId2, ReceiveItemId3, ReceiveItemId4, "
        "ReceiveItemCount1, ReceiveItemCount2, ReceiveItemCount3, ReceiveItemCount4, IsRepeatable, bonushonor, bonusarenapoints, rewardtitleid, "
        "rewardtalents, suggestedplayers, detailemotecount, detailemote1, detailemote2, detailemote3, detailemote4, detailemotedelay1, "
        "detailemotedelay2, detailemotedelay3, detailemotedelay4, completionemotecnt, completionemote1, completionemote2, completionemote3, "
        "completionemote4, completionemotedelay1, completionemotedelay2, completionemotedelay3, completionemotedelay4, completeemote, "
        "incompleteemote, iscompletedbyspelleffect, RewXPId "
        "FROM quest_properties base "
        "WHERE build=(SELECT MAX(build) FROM quest_properties buildspecific WHERE base.entry = buildspecific.entry AND build <= ?)");
    RegisterStatement(WORLD_SEL_WORLDBROADCAST, "SELECT * FROM worldbroadcast;");
    RegisterStatement(WORLD_SEL_CREATURE_SPAWNS_BASE, "SELECT * FROM creature_spawns WHERE min_build <= ? AND max_build >= ? AND event_entry = 0;");
    RegisterStatement(WORLD_SEL_GAMEOBJECT_SPAWNS_BASE, "SELECT * FROM gameobject_spawns WHERE min_build <= ? AND max_build >= ? AND event_entry = 0;");
    RegisterStatement(WORLD_SEL_RECALL, "SELECT id, name, MapId, positionX, positionY, positionZ, Orientation FROM recall WHERE min_build <= ? AND max_build >= ?;");
    RegisterStatement(WORLD_SEL_CREATURE_QUEST_STARTER, "SELECT id FROM creature_quest_starter WHERE quest = ? AND min_build <= ? AND max_build >= ?;");
    RegisterStatement(WORLD_SEL_GAMEOBJECT_QUEST_STARTER, "SELECT id FROM gameobject_quest_starter WHERE quest = ? AND min_build <= ? AND max_build >= ?;");
    RegisterStatement(WORLD_SEL_ALL_CREATURE_QUEST_STARTER, "SELECT * FROM creature_quest_starter WHERE min_build <= ? AND max_build >= ?;");
    RegisterStatement(WORLD_SEL_ALL_CREATURE_QUEST_FINISHER, "SELECT * FROM creature_quest_finisher WHERE min_build <= ? AND max_build >= ?;");
    RegisterStatement(WORLD_SEL_ALL_GAMEOBJECT_QUEST_STARTER, "SELECT * FROM gameobject_quest_starter WHERE min_build <= ? AND max_build >= ?;");
    RegisterStatement(WORLD_SEL_ALL_GAMEOBJECT_QUEST_FINISHER, "SELECT * FROM gameobject_quest_finisher WHERE min_build <= ? AND max_build >= ?;");
    RegisterStatement(WORLD_SEL_VENDORS, "SELECT * FROM vendors;");
    RegisterStatement(WORLD_SEL_TRAINER_PROPERTIES_SPELLSET, "SELECT * FROM trainer_properties_spellset WHERE min_build <= ? AND max_build >= ?;");
    RegisterStatement(WORLD_SEL_TRAINER_PROPERTIES, "SELECT * FROM trainer_properties WHERE build <= ?;");
    RegisterStatement(WORLD_SEL_MAX_CREATURE_SPAWN_ID, "SELECT MAX(id) FROM creature_spawns WHERE min_build <= ? AND max_build >= ? AND event_entry = 0;");
    RegisterStatement(WORLD_SEL_MAX_GAMEOBJECT_SPAWN_ID, "SELECT MAX(id) FROM gameobject_spawns WHERE min_build <= ? AND max_build >= ? AND event_entry = 0;");
    RegisterStatement(WORLD_SEL_CREATURE_WAYPOINTS, "SELECT id, point, position_x, position_y, position_z, orientation, move_type, delay, action, action_chance FROM creature_waypoints ORDER BY id, point;");
    RegisterStatement(WORLD_SEL_MAX_CREATURE_WAYPOINT_ID, "SELECT MAX(id) FROM creature_waypoints;");
    RegisterStatement(WORLD_SEL_LOOT_CREATURES, "SELECT * FROM loot_creatures ORDER BY entryid ASC;");
    RegisterStatement(WORLD_SEL_LOOT_GAMEOBJECTS, "SELECT * FROM loot_gameobjects ORDER BY entryid ASC;");
    RegisterStatement(WORLD_SEL_LOOT_SKINNING, "SELECT * FROM loot_skinning ORDER BY entryid ASC;");
    RegisterStatement(WORLD_SEL_LOOT_FISHING, "SELECT * FROM loot_fishing ORDER BY entryid ASC;");
    RegisterStatement(WORLD_SEL_LOOT_ITEMS, "SELECT * FROM loot_items ORDER BY entryid ASC;");
    RegisterStatement(WORLD_SEL_LOOT_PICKPOCKETING, "SELECT * FROM loot_pickpocketing ORDER BY entryid ASC;");





    // Loot
    RegisterStatement(WORLD_LOOT_CREATURES_BY_ENTRY, "SELECT itemid, normal10percentchance, heroic10percentchance, normal25percentchance, heroic25percentchance, mincount, maxcount FROM loot_creatures WHERE entryid = ?");

    // Reputation
    RegisterStatement(WORLD_REPUTATION_CREATURE_ONKILL_SELECT, "SELECT creature_id, faction_change_alliance, faction_change_horde, change_value, rep_limit FROM reputation_creature_onkill");
    RegisterStatement(WORLD_REPUTATION_FACTION_ONKILL_SELECT, "SELECT faction_id, change_factionid_alliance,change_deltamin_alliance, change_deltamax_alliance, change_factionid_horde FROM reputation_faction_onkill");
    RegisterStatement(WORLD_REPUTATION_INSTANCE_ONKILL_SELECT, "SELECT mapid, mob_rep_reward, mob_rep_limit, boss_rep_reward, boss_rep_limit, faction_change_alliance, faction_change_horde FROM reputation_instance_onkill");

    // Vendor
    RegisterStatement(WORLD_VENDOR_INSERT, "INSERT INTO vendors VALUES (?, ?, ?, 0, 0, ?)");
    RegisterStatement(WORLD_VENDOR_DELETE, "DELETE FROM vendors WHERE entry = ? AND item = ?");

    // Gameobjects
    RegisterStatement(WORLD_GAMEOBJECT_POSITION_UPDATE, "UPDATE gameobject_spawns SET position_x = ?, position_y = ?, position_z = ? WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_GAMEOBJECT_SPAWN_OVERRIDE_REPLACE, "REPLACE INTO gameobject_spawns_overrides VALUES (?, ?, ?, ?, ?, ?)");
    RegisterStatement(WORLD_GAMEOBJECT_SPAWN_PHASE_UPDATE, "UPDATE gameobject_spawns SET phase = ? WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_GAMEOBJECT_SPAWN_STATE_UPDATE, "UPDATE gameobject_spawns SET state = ? WHERE id = ? AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_SEL_GAMEOBJECT_QUEST_PICKUP_BINDING, "SELECT entry, quest, required_count FROM gameobject_quest_pickup_binding;");

    // Vehicles
    RegisterStatement(WORLD_VEHICLE_ACCESSORY_SELECT_ALL, "SELECT entry, accessory_entry, seat_id, minion, summontype, summontimer FROM vehicle_accessories");
    RegisterStatement(WORLD_VEHICLE_SEAT_ADDON_SELECT_ALL, "SELECT SeatEntry, SeatOrientation, ExitParamX, ExitParamY, ExitParamZ, ExitParamO, ExitParamValue FROM vehicle_seat_addon");

    // Items
    RegisterStatement(WORLD_ITEM_RANDOMPROP_GROUPS_SELECT, "SELECT entry_id, randomprops_entryid, chance FROM item_randomprop_groups");
    RegisterStatement(WORLD_ITEM_RANDOMSUFFIX_GROUPS_SELECT, "SELECT entry_id, randomsuffix_entryid, chance FROM item_randomsuffix_groups");

    // Gameevents
    RegisterStatement(WORLD_GAMEEVENT_PROPERTIES_SELECT, "SELECT entry, UNIX_TIMESTAMP(start_time), UNIX_TIMESTAMP(end_time), occurence, length, holiday, description, world_event, announce FROM gameevent_properties WHERE entry > 0 AND min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_EVENT_CREATURE_SPAWNS_SELECT, "SELECT id, entry, map, position_x, position_y, position_z, orientation, movetype, displayid, faction, flags, pvp_flagged, bytes0, emote_state, npc_respawn_link, channel_spell, channel_target_sqlid, channel_target_sqlid_creature, standstate, death_state, mountdisplayid, sheath_state, slot1item, slot2item, slot3item, CanFly, phase, waypoint_group, event_entry FROM creature_spawns WHERE min_build <= ? AND max_build >= ? AND event_entry > 0");
    RegisterStatement(WORLD_EVENT_GAMEOBJECT_SPAWNS_SELECT, "SELECT id, entry, map, phase, position_x, position_y, position_z, orientation, rotation0, rotation1, rotation2, rotation3, spawntimesecs, state, event_entry FROM gameobject_spawns WHERE min_build <= ? AND max_build >= ? AND event_entry > 0");

    // Guild
    RegisterStatement(WORLD_GUILD_XP_FOR_LEVEL_SELECT, "SELECT lvl, xp_for_next_level FROM guild_xp_for_level");
    RegisterStatement(WORLD_GUILD_REWARDS_SELECT, "SELECT entry, standing, racemask, price, achievement FROM guild_rewards");

    // Taxi
    RegisterStatement(WORLD_SELECT_TAXI_LEVEL_DATA, "SELECT TaxiNodeId, `Level` FROM taxi_level_data ORDER BY TaxiNodeId ASC");

    // Weather
    RegisterStatement(WORLD_SELECT_WEATHER_DATA, "SELECT zoneId, high_chance, high_type, med_chance, med_type, low_chance, low_type FROM weather");

    // Script
    RegisterStatement(WORLD_EVENT_SCRIPTS_SELECT_ALL, "SELECT event_id, `function`, script_type, data_1, data_2, data_3, data_4, data_5, x, y, z, o, delay, next_event FROM event_scripts WHERE event_id > 0 ORDER BY event_id");

    // Instance
    RegisterStatement(WORLD_INSTANCE_ENCOUNTERS_SELECT_ALL, "SELECT entry, creditType, creditEntry, lastEncounterDungeon, comment, mapid FROM instance_encounters WHERE entry > 0");

    // LFG
    RegisterStatement(WORLD_SEL_LFG_DUNGEON_REWARDS, "SELECT dungeon_id, max_level, quest_id_1, money_var_1, xp_var_1, quest_id_2, money_var_2, xp_var_2 FROM lfg_dungeon_rewards ORDER BY dungeon_id, max_level ASC");

    // Worldstate
    RegisterStatement(WORLD_WORLDSTATE_TEMPLATES_SELECT_MAPS, "SELECT DISTINCT map FROM worldstate_templates ORDER BY map");
    RegisterStatement(WORLD_WORLDSTATE_TEMPLATES_SELECT_ALL, "SELECT map, zone, field, value FROM worldstate_templates");

    // Spell
    RegisterStatement(WORLD_SEL_SPELL_COEFF_OVERRIDE, "SELECT spell_id, direct_coefficient, overtime_coefficient FROM spell_coefficient_override WHERE min_build <= ? AND max_build >= ?");
    RegisterStatement(WORLD_SEL_SPELL_CUSTOM_OVERRIDE, "SELECT spell_id, assign_on_target_flag, assign_self_cast_only, assign_c_is_flag FROM spell_custom_override");
    RegisterStatement(WORLD_SEL_AI_THREAT_TO_SPELLID, "SELECT * FROM ai_threattospellid");
    RegisterStatement(WORLD_SEL_SPELL_EFFECTS_OVERRIDE, "SELECT * FROM spell_effects_override");
    RegisterStatement(WORLD_SEL_SPELL_AREA, "SELECT spell, area, quest_start, quest_start_active, quest_end, aura_spell, racemask, gender, autocast FROM spell_area");
    RegisterStatement(WORLD_SEL_SPELL_REQUIRED, "SELECT spell_id, req_spell FROM spell_required");
    RegisterStatement(WORLD_SEL_SPELL_TARGET_CONSTRAINTS, "SELECT * FROM spelltargetconstraints WHERE SpellID > 0 ORDER BY SpellID");
    RegisterStatement(WORLD_SEL_SPELL_RANKS, "SELECT spell_id, first_spell, `rank` FROM spell_ranks WHERE min_build <= ? AND max_build >= ? ORDER BY first_spell, `rank`");
    RegisterStatement(WORLD_SEL_SPELL_DISABLE, "SELECT spellid, replacement_spellid FROM spell_disable");


}
