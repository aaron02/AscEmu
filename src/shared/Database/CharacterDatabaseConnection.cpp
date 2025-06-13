/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "CharacterDatabaseConnection.hpp"

CharacterDatabaseConnection::CharacterDatabaseConnection()
{
}

CharacterDatabaseConnection::~CharacterDatabaseConnection()
{
}

void CharacterDatabaseConnection::PrepareStatements()
{
    RegisterStatement(CHAR_VERSION_LAST_UPDATE, "SELECT LastUpdate FROM character_db_version ORDER BY id DESC LIMIT 1");

    // Account
    RegisterStatement(CHAR_UPD_ACCOUNT_DATA, "UPDATE account_data SET uiconfig0 = ?, uiconfig1 = ?, uiconfig2 = ?, uiconfig3 = ?, uiconfig4 = ?, uiconfig5 = ?, uiconfig6 = ?, uiconfig7 = ? WHERE acct = ?");
    //RegisterStatement(CHAR_SEL_ACCOUNT_INFO, "SELECT acct, login, gm, email, lastip, muted FROM accounts WHERE acct = ?"); // Table not in Character Database
    RegisterStatement(CHAR_SEL_ACCT_BY_NAME, "SELECT acct FROM characters WHERE name = ?");

    // Survey
    RegisterStatement(CHAR_SEL_MAX_GM_SURVEY_ID, "SELECT MAX(survey_id) FROM gm_survey");
    RegisterStatement(CHAR_INS_GM_SURVEY_ANSWER, "INSERT INTO gm_survey_answers (survey_id, question_id, answer_id) VALUES (?, ?, ?)");
    RegisterStatement(CHAR_INS_GM_SURVEY, "INSERT INTO gm_survey (survey_id, guid, main_survey, comment, create_time) VALUES (?, ?, ?, ?, UNIX_TIMESTAMP(NOW()))");
    RegisterStatement(CHAR_INS_LAG_REPORT, "INSERT INTO lag_reports (player, account, lag_type, map_id, position_x, position_y, position_z) VALUES (?, ?, ?, ?, ?, ?, ?)");

    // Tickets
    RegisterStatement(CHAR_GM_TICKET_LIST_ALL, "SELECT * FROM gm_tickets");
    RegisterStatement(CHAR_GM_TICKET_LIST_ACTIVE, "SELECT * FROM gm_tickets WHERE deleted=0");
    RegisterStatement(CHAR_GM_TICKET_GET_BY_ID, "SELECT * FROM gm_tickets WHERE ticketid = ?");
    RegisterStatement(CHAR_GM_TICKET_GET_ACTIVE_BY_ID, "SELECT * FROM gm_tickets WHERE ticketid = ? AND deleted = 0");
    RegisterStatement(CHAR_GM_TICKET_CLOSE_BY_ID, "UPDATE gm_tickets SET deleted = 1, comment = CONCAT('GM: ', ?, ' ', ?), assignedto = ? WHERE ticketid = ?");
    RegisterStatement(CHAR_GM_TICKET_GET_DELETED_BY_ID, "SELECT * FROM gm_tickets WHERE ticketid = ? AND deleted = 1");
    RegisterStatement(CHAR_GM_TICKET_DELETE_BY_ID, "DELETE FROM gm_tickets WHERE ticketid = ?");
    RegisterStatement(CHAR_GM_TICKET_MAX_ID, "SELECT MAX(ticketid) FROM gm_tickets");
    RegisterStatement(CHAR_GM_TICKET_SELECT_ALL, "SELECT ticketid, playerGuid, name, level, map, posX, posY, posZ, message, timestamp, deleted, assignedto, comment FROM gm_tickets");
    RegisterStatement(CHAR_GM_TICKET_INSERT, "INSERT INTO gm_tickets (ticketid, playerguid, name, level, map, posX, posY, posZ, message, timestamp, deleted, assignedto, comment) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_GM_TICKET_REPLACE, "REPLACE INTO gm_tickets (ticketid, playerguid, name, level, map, posX, posY, posZ, message, timestamp, deleted, assignedto, comment) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_GM_TICKET_DELETE_DELETED, "DELETE FROM gm_tickets WHERE deleted = 1");
    RegisterStatement(CHAR_INS_BUG_REPORT, "INSERT INTO playerbugreports (UID, AccountID, TimeStamp, Suggestion, Type, Content) VALUES (?, ?, ?, ?, ?, ?)");


    // Commands
    RegisterStatement(CHAR_COMMAND_OVERRIDES_SELECT, "SELECT command_name, access_level FROM command_overrides");

    // Achievements
    RegisterStatement(CHARACTER_ACHIEVEMENT_FIRST_GUID_SELECT, "SELECT guid FROM character_achievement WHERE achievement = ? ORDER BY date LIMIT 1");
    RegisterStatement(CHARACTER_ACHIEVEMENT_DELETE_BY_ID, "DELETE FROM character_achievement WHERE guid = ? AND achievement = ?");
    RegisterStatement(CHARACTER_ACHIEVEMENT_DELETE, "DELETE FROM character_achievement WHERE guid = ?");
    RegisterStatement(CHARACTER_ACHIEVEMENT_INSERT, "INSERT INTO character_achievement (guid, achievement, date) VALUES (?, ?, ?)");
    RegisterStatement(CHARACTER_ACHIEVEMENT_REPLACE, "REPLACE INTO character_achievement (guid, achievement, date) VALUES (?, ?, ?)");
    RegisterStatement(CHARACTER_ACHIEVEMENT_PROGRESS_DELETE_BY_CRITERIA, "DELETE FROM character_achievement_progress WHERE guid = ? AND criteria = ?");
    RegisterStatement(CHARACTER_ACHIEVEMENT_PROGRESS_DELETE, "DELETE FROM character_achievement_progress WHERE guid = ?");
    RegisterStatement(CHARACTER_ACHIEVEMENT_PROGRESS_INSERT, "INSERT INTO character_achievement_progress (guid, criteria, counter, date) VALUES (?, ?, ?, ?)");
    RegisterStatement(CHARACTER_ACHIEVEMENT_PROGRESS_REPLACE, "REPLACE INTO character_achievement_progress (guid, criteria, counter, date) VALUES (?, ?, ?, ?)");
    RegisterStatement(CHAR_CHARACTER_ACHIEVEMENT_SELECT_DISTINCT, "SELECT achievement FROM character_achievement GROUP BY achievement");

    // Addons
    RegisterStatement(CHARACTER_CLIENT_ADDONS_SELECT_ALL, "SELECT id, name, crc, banned, showinlist FROM clientaddons");
    RegisterStatement(CHARACTER_CLIENT_ADDON_INSERT, "INSERT INTO clientaddons (name, crc, banned, showinlist) VALUES (?, ?, ?, ?)");
    RegisterStatement(CHARACTER_CLIENT_ADDON_REPLACE, "REPLACE INTO clientaddons(name, crc) VALUES(?, ?)");
    RegisterStatement(CHARACTER_CLIENT_ADDON_SELECT, "SELECT name, crc FROM clientaddons");
    RegisterStatement(CHARACTER_CLIENT_ADDON_BANNED_SELECT, "SELECT id, name, banned, UNIX_TIMESTAMP(timestamp), version FROM clientaddons WHERE banned = 1");

    // Character
    RegisterStatement(CHAR_SEL_CHARACTER_ENUM, 
        "SELECT guid, level, race, class, gender, bytes, bytes2, name, positionX, positionY, "
        "positionZ, mapId, zoneId, banned, restState, deathstate, login_flags, player_flags, guild_members.guildId "
        "FROM characters "
        "LEFT JOIN guild_members ON characters.guid = guild_members.playerid "
        "WHERE acct = ? "
        "ORDER BY guid LIMIT 10");
    RegisterStatement(CHAR_CHARACTER_NAME_UPDATE, "UPDATE characters SET name = ? WHERE guid = ?");
    RegisterStatement(CHAR_CHARACTER_LOGIN_FLAGS_UPDATE, "UPDATE characters SET login_flags = ? WHERE guid = ?");
    RegisterStatement(CHAR_BANNED_NAME_INSERT, "INSERT INTO banned_names VALUES (?)");
    RegisterStatement(CHAR_CHARACTER_POSITION_UPDATE_BY_GUID, "UPDATE characters SET mapId = ?, positionX = ?, positionY = ?, positionZ = ?, zoneId = ? WHERE guid = ?");
    RegisterStatement(CHAR_CHARACTER_UNBAN_BY_NAME, "UPDATE characters SET banned = 0 WHERE name = ?");
    RegisterStatement(CHAR_CHARACTER_BAN_BY_GUID, "UPDATE characters SET banned = ?, banReason = ? WHERE guid = ?");
    RegisterStatement(CHAR_BANNED_CHAR_LOG_INSERT, "INSERT INTO banned_char_log VALUES (?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_CHARACTERS_SELECT_BASIC_INFO, "SELECT guid, name, race, class, level, gender, zoneid, timestamp, acct FROM characters");
    RegisterStatement(CHAR_DEL_PLAYERITEM, "DELETE FROM playeritems WHERE guid = ?");
    RegisterStatement(CHAR_INS_PLAYERITEM_FULL, "INSERT INTO playeritems (ownerguid, guid, entry, wrapped_item_id, wrapped_creator, creator, count, charges, flags, randomprop, randomsuffix, itemtext, durability, containerslot, slot, enchantments, duration_expireson, refund_purchasedon, refund_costid) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_SEL_PLAYERITEM, "SELECT * FROM playeritems WHERE guid = ?");
    RegisterStatement(CHAR_SEL_MAX_CHARACTER_GUID, "SELECT MAX(guid) FROM characters");
    RegisterStatement(CHAR_SEL_MAX_ITEM_GUID, "SELECT MAX(guid) FROM playeritems");
    RegisterStatement(CHAR_SEL_MAX_CORPSE_GUID, "SELECT MAX(guid) FROM corpses");
    RegisterStatement(CHAR_SEL_MAX_GROUP_ID, "SELECT MAX(group_id) FROM `groups`");
    RegisterStatement(CHAR_SEL_MAX_CHARTER_ID, "SELECT MAX(charterid) FROM charters");
    RegisterStatement(CHAR_SEL_MAX_GUILD_ID, "SELECT MAX(guildid) FROM guilds");
    RegisterStatement(CHAR_SEL_MAX_BUGREPORT_UID, "SELECT MAX(UID) FROM playerbugreports");
    RegisterStatement(CHAR_SEL_MAX_MAIL_ID, "SELECT MAX(message_id) FROM mailbox");
    RegisterStatement(CHAR_SEL_MAX_EQUIP_SET_GUID, "SELECT MAX(setGUID) FROM equipmentsets");
    RegisterStatement(CHAR_SEL_MAX_VOID_ITEM_ID, "SELECT MAX(itemId) FROM character_void_storage");
    RegisterStatement(CHAR_SEL_CHARACTER_ACCOUNTS, "SELECT acct FROM characters");
    RegisterStatement(CHAR_REP_ACCOUNT_PERMISSIONS, "REPLACE INTO account_permissions (`id`, `permissions`, `name`) VALUES (?, ?, ?)");
    RegisterStatement(CHAR_DEL_ACCOUNT_PERMISSIONS, "DELETE FROM account_permissions WHERE id = ?");
    RegisterStatement(CHAR_SEL_GUID_BY_NAME, "SELECT guid FROM characters WHERE name = ?");
    RegisterStatement(CHAR_SEL_ACCOUNT_PERMISSIONS, "SELECT id, permissions FROM account_permissions");
    RegisterStatement(CHAR_SEL_ACCOUNT_DATA, "SELECT * FROM account_data WHERE acct = ?");
    RegisterStatement(CHAR_INS_ACCOUNT_DATA, "INSERT INTO account_data VALUES (?, '', '', '', '', '', '', '', '', '')");
    RegisterStatement(CHAR_UPD_PLAYER_SUMMON_NAME, "UPDATE playersummons SET name = ? WHERE ownerguid = ? AND entry = ?");
    RegisterStatement(CHAR_SEL_PLAYER_PET_SPELLS, "SELECT spellid, flags FROM playerpetspells WHERE ownerguid = ? AND petnumber = ?");
    RegisterStatement(CHAR_SEL_PLAYERSUMMON_NAME, "SELECT name FROM playersummons WHERE ownerguid = ? AND entry = ?");
    RegisterStatement(CHAR_INS_PLAYERSUMMON, "INSERT INTO playersummons (ownerguid, entry, name) VALUES (?, ?, ?)");
    RegisterStatement(CHAR_SEL_BANNED_NAME_CHECK, "SELECT COUNT(*) FROM banned_names WHERE name = ?");
    RegisterStatement(CHAR_UPD_NAME_RACE_FLAGS, "UPDATE characters SET name = ?, login_flags = ?, race = ? WHERE guid = ?");
    RegisterStatement(CHAR_SEL_GUID_CLASS_BY_FLAGS, "SELECT guid, class FROM characters WHERE guid = ? AND login_flags = ?");
    RegisterStatement(CHAR_UPD_NAME, "UPDATE characters SET name = ? WHERE guid = ?");
    RegisterStatement(CHAR_UPD_LOGIN_FLAGS, "UPDATE characters SET login_flags = ? WHERE guid = ?");
    RegisterStatement(CHAR_SEL_LOGIN_FLAGS_BY_GUID, "SELECT login_flags FROM characters WHERE guid = ?");
    RegisterStatement(CHAR_SEL_LOGIN_FLAGS_BY_GUID_ACCT, "SELECT login_flags FROM characters WHERE guid = ? AND acct = ?");
    RegisterStatement(CHAR_SEL_BANNED_NAME, "SELECT COUNT(*) FROM banned_names WHERE name = ?");
    RegisterStatement(CHAR_SEL_CHARACTER_NAME_ACC, "SELECT name FROM characters WHERE guid = ? AND acct = ?");
    RegisterStatement(CHAR_DEL_CHARACTER, "DELETE FROM characters WHERE guid = ?");
    RegisterStatement(CHAR_DEL_PLAYERITEMS, "DELETE FROM playeritems WHERE ownerguid = ?");
    RegisterStatement(CHAR_DEL_GM_TICKETS, "DELETE FROM gm_tickets WHERE playerguid = ?");
    RegisterStatement(CHAR_DEL_PLAYERPETS, "DELETE FROM playerpets WHERE ownerguid = ?");
    RegisterStatement(CHAR_DEL_PLAYERPETSPELLS, "DELETE FROM playerpetspells WHERE ownerguid = ?");
    RegisterStatement(CHAR_DEL_QUESTLOG, "DELETE FROM questlog WHERE player_guid = ?");
    RegisterStatement(CHAR_DEL_COOLDOWNS, "DELETE FROM playercooldowns WHERE player_guid = ?");
    RegisterStatement(CHAR_DEL_MAILBOX, "DELETE FROM mailbox WHERE player_guid = ?");
    RegisterStatement(CHAR_DEL_SOCIAL_FRIENDS, "DELETE FROM social_friends WHERE character_guid = ? OR friend_guid = ?");
    RegisterStatement(CHAR_DEL_SOCIAL_IGNORES, "DELETE FROM social_ignores WHERE character_guid = ? OR ignore_guid = ?");
    RegisterStatement(CHAR_DEL_ACHIEVEMENTS, "DELETE FROM character_achievement WHERE guid = ? AND achievement NOT IN (457,467,466,465,464,463,462,461,460,459,458,1404,1405,1406,1407,1408,1409,1410,1411,1412,1413,1415,1414,1416,1417,1418,1419,1420,1421,1422,1423,1424,1425,1426,1427,1463,1400,456,1402)");
    RegisterStatement(CHAR_DEL_ACHIEVEMENT_PROGRESS, "DELETE FROM character_achievement_progress WHERE guid = ?");
    RegisterStatement(CHAR_DEL_PLAYER_SPELLS, "DELETE FROM playerspells WHERE GUID = ?");
    RegisterStatement(CHAR_DEL_PLAYER_DELETED_SPELLS, "DELETE FROM playerdeletedspells WHERE GUID = ?");
    RegisterStatement(CHAR_DEL_REPUTATIONS, "DELETE FROM playerreputations WHERE guid = ?");
    RegisterStatement(CHAR_DEL_SKILLS, "DELETE FROM playerskills WHERE GUID = ?");
    RegisterStatement(CHAR_DEL_SUMMONS, "DELETE FROM playersummons WHERE ownerguid = ?");
    RegisterStatement(CHAR_DEL_SUMMON_SPELLS, "DELETE FROM playersummonspells WHERE ownerguid = ?");
    RegisterStatement(CHAR_SEL_CHAR_COUNT_BY_ACCOUNT, "SELECT COUNT(*) FROM characters WHERE acct = ?");
    RegisterStatement(CHAR_UPD_CHARACTER_ONLINE, "UPDATE characters SET online = 1 WHERE guid = ?");
    RegisterStatement(CHAR_SEL_PLAYER_ACTIVE_PET, "SELECT entry, model, level FROM playerpets WHERE ownerguid = ? AND active = TRUE AND alive = TRUE LIMIT 1;");
    RegisterStatement(CHAR_SEL_PLAYER_EQUIPPED_ITEMS, "SELECT slot, entry, enchantments FROM playeritems WHERE ownerguid = ? AND containerslot = -1 AND slot BETWEEN 0 AND 22");
    RegisterStatement(CHAR_INS_ACCOUNT_DATA2, "INSERT INTO account_data VALUES(?, '', '', '', '', '', '', '', '', '')");
    RegisterStatement(CHAR_UPD_CHARACTER_GOLD, "UPDATE characters SET gold = ? WHERE guid = ?");
    RegisterStatement(CHAR_UPD_ONLINE_RESET, "UPDATE characters SET online = 0 WHERE online = 1");
    RegisterStatement(CHAR_UPD_BAN_RESET, "UPDATE characters SET banned = 0, banReason = '' WHERE banned > 100 AND banned < ?");
    RegisterStatement(CHAR_SEL_BYTES2_BY_GUID, "SELECT bytes2 FROM characters WHERE guid = ?");
    RegisterStatement(CHAR_UPD_CHARACTER_APPEARANCE, "UPDATE characters SET gender = ?, bytes = ?, bytes2 = ? WHERE guid = ?");
    RegisterStatement(CHAR_DEL_LANG_SPELL, "DELETE FROM playerspells WHERE GUID = ? AND SpellID = ?");
    RegisterStatement(CHAR_ADD_LANGUAGE_SPELL, "INSERT INTO playerspells (GUID, SpellID) VALUES (?, ?)");
    RegisterStatement(CHAR_DEL_PLAYER_COOLDOWNS, "DELETE FROM playercooldowns WHERE player_guid = ?");
    RegisterStatement(CHAR_ADD_PLAYER_COOLDOWN, "INSERT INTO playercooldowns (player_guid, cooldown_type, cooldown_misc, cooldown_expire_time, cooldown_spellid, cooldown_itemid) VALUES (?, ?, ?, ?, ?, ?)");
    
    RegisterStatement(CHAR_SEL_TUTORIALS, "SELECT tut0, tut1, tut2, tut3, tut4, tut5, tut6, tut7 FROM tutorials WHERE playerId = ?");
    RegisterStatement(CHAR_DEL_TUTORIALS, "DELETE FROM tutorials WHERE playerId = ?");
    RegisterStatement(CHAR_INS_TUTORIALS, "INSERT INTO tutorials (playerId, tut0, tut1, tut2, tut3, tut4, tut5, tut6, tut7) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_REP_TUTORIALS, "REPLACE INTO tutorials (playerId, tut0, tut1, tut2, tut3, tut4, tut5, tut6, tut7) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    
    RegisterStatement(CHAR_SEL_SOCIAL_FRIENDS, "SELECT friend_guid, note FROM social_friends WHERE character_guid = ?");
    RegisterStatement(CHAR_SEL_SOCIAL_FRIENDED_BY, "SELECT character_guid FROM social_friends WHERE friend_guid = ?");
    RegisterStatement(CHAR_SEL_SOCIAL_IGNORES, "SELECT ignore_guid FROM social_ignores WHERE character_guid = ?");
    RegisterStatement(CHAR_INS_SOCIAL_FRIEND, "INSERT INTO social_friends (character_guid, friend_guid, note) VALUES (?, ?, ?)");
    RegisterStatement(CHAR_DEL_SOCIAL_FRIEND, "DELETE FROM social_friends WHERE character_guid = ? AND friend_guid = ?");
    RegisterStatement(CHAR_UPDATE_SOCIAL_FRIEND_NOTE, "UPDATE social_friends SET note = ? WHERE character_guid = ? AND friend_guid = ?");
    RegisterStatement(CHAR_ADD_SOCIAL_IGNORE, "INSERT INTO social_ignores (character_guid, ignore_guid) VALUES (?, ?)");
    RegisterStatement(CHAR_DEL_SOCIAL_IGNORE, "DELETE FROM social_ignores WHERE character_guid = ? AND ignore_guid = ?");
    RegisterStatement(CHAR_DEL_PLAYER_PET_SPELLS, "DELETE FROM playerpetspells WHERE ownerguid = ? AND petnumber = ?");
    RegisterStatement(CHAR_UPD_PLAYER_PET_SLOT_ACTIVE, "UPDATE playerpets SET slot = ?, active = ? WHERE ownerguid = ? AND petnumber = ?");
    RegisterStatement(CHAR_DEL_CHARACTER_INSTANCE, "DELETE FROM character_instance WHERE guid = ? AND instance = ?");
    RegisterStatement(CHAR_SEL_CHARACTER_INSTANCE, "SELECT id, permanent, map, difficulty, extendState, resettime FROM character_instance LEFT JOIN instance ON instance = id WHERE guid = ?");
    RegisterStatement(CHAR_UPD_CHARACTER_INSTANCE, "UPDATE character_instance SET instance = ?, permanent = ?, extendState = ? WHERE guid = ? AND instance = ?");
    RegisterStatement(CHAR_INS_CHARACTER_INSTANCE, "INSERT INTO character_instance (guid, instance, permanent, extendState) VALUES (?, ?, ?, ?)");
    RegisterStatement(CHAR_SEL_ACCOUNT_INSTANCE_TIMES, "SELECT instanceId, releaseTime FROM account_instance_times WHERE accountId = ?");
    RegisterStatement(CHAR_DEL_ACCOUNT_INSTANCE_TIMES, "DELETE FROM account_instance_times WHERE accountId = ?");
    RegisterStatement(CHAR_INS_ACCOUNT_INSTANCE_TIMES, "INSERT INTO account_instance_times (accountId, instanceId, releaseTime) VALUES (?, ?, ?)");
    RegisterStatement(CHAR_INS_REPUTATION, "INSERT INTO playerreputations (guid, faction, flag, basestanding, standing) VALUES (?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_INS_PLAYER_SPELL, "INSERT INTO playerspells (GUID, SpellID) VALUES (?, ?)");
    RegisterStatement(CHAR_DEL_DELETED_SPELLS, "DELETE FROM playerdeletedspells WHERE GUID = ?");
    RegisterStatement(CHAR_INS_DELETED_SPELLS, "INSERT INTO playerdeletedspells (GUID, SpellID) VALUES (?, ?)");
    RegisterStatement(CHAR_INS_SKILLS, "INSERT INTO playerskills (GUID, SkillID, CurrentValue, MaximumValue) VALUES (?, ?, ?, ?)");
    RegisterStatement(CHAR_INS_PLAYERPETSPELL, "INSERT INTO playerpetspells (ownerguid, petnumber, spellid, flags) VALUES (?, ?, ?, ?)");
    RegisterStatement(CHAR_REPLACE_PLAYER_PET, "REPLACE INTO playerpets (ownerguid, petnumber, type, name, entry, model, level, xp, slot, active, alive, actionbar, reset_time, reset_cost, spellid, petstate, talentpoints, current_power, current_hp, current_happiness, renamable) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_SEL_PET_SPELL_PETNUMBERS, "SELECT DISTINCT petnumber FROM playerpetspells WHERE ownerguid = ?");
    RegisterStatement(CHAR_DEL_SUMMON_SPELLS, "DELETE FROM playersummonspells WHERE ownerguid = ?");
    RegisterStatement(CHAR_INS_SUMMON_SPELL, "INSERT INTO playersummonspells (ownerguid, entryid, spellid) VALUES (?, ?, ?)");
    RegisterStatement(CHAR_REP_CHARACTER,
        "REPLACE INTO characters (guid, acct, name, race, class, gender, custom_faction, level, xp, active_cheats, "
        "exploration_data, watched_faction_index, selected_pvp_title, available_pvp_titles, available_pvp_titles1, available_pvp_titles2, "
        "gold, ammo_id, available_prof_points, current_hp, current_power, pvprank, bytes, bytes2, player_flags, enabled_actionbars, "
        "positionX, positionY, positionZ, orientation, mapId, zoneId, taximask, banned, banReason, timestamp, online, bindpositionX, "
        "bindpositionY, bindpositionZ, bindpositionO, bindmapId, bindzoneId, isResting, restState, restTime, playedtime, deathstate, "
        "TalentResetTimes, first_login, login_flags, arenaPoints, totalstableslots, instance_id, entrypointmap, entrypointx, entrypointy, "
        "entrypointz, entrypointo, entrypointinstance, taxi_path, taxi_mountid, transporter, transporter_xdiff, transporter_ydiff, transporter_zdiff, "
        "transporter_odiff, actions1, actions2, auras, finished_quests, finisheddailies, honorRolloverTime, killsToday, killsYesterday, killsLifeTime, "
        "honorToday, honorYesterday, honorPoints, drunkValue, glyphs1, talents1, glyphs2, talents2, numspecs, currentspec, talentpoints, "
        "firsttalenttree, phase, CanGainXp, data, resettalents, rbg_daily, dungeon_difficulty, raid_difficulty) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    
RegisterStatement(CHAR_SEL_CHARACTER,
        "SELECT guid, acct, name, race, class, gender, custom_faction, level, xp, active_cheats, exploration_data, "
        "watched_faction_index, selected_pvp_title, available_pvp_titles, available_pvp_titles1, available_pvp_titles2, "
        "gold, ammo_id, available_prof_points, current_hp, current_power, pvprank, bytes, bytes2, player_flags, "
        "enabled_actionbars, positionX, positionY, positionZ, orientation, mapId, zoneId, taximask, banned, banReason, "
        "timestamp, online, bindpositionX, bindpositionY, bindpositionZ, bindpositionO, bindmapId, bindzoneId, isResting, "
        "restState, restTime, playedtime, deathstate, TalentResetTimes, first_login, login_flags, arenaPoints, "
        "totalstableslots, instance_id, entrypointmap, entrypointx, entrypointy, entrypointz, entrypointo, "
        "entrypointinstance, taxi_path, taxi_mountid, transporter, transporter_xdiff, transporter_ydiff, transporter_zdiff, "
        "transporter_odiff, actions1, actions2, auras, finished_quests, finisheddailies, honorRolloverTime, killsToday, "
        "killsYesterday, killsLifeTime, honorToday, honorYesterday, honorPoints, drunkValue, glyphs1, talents1, glyphs2, "
        "talents2, numspecs, currentspec, talentpoints, firsttalenttree, phase, CanGainXp, data, resettalents, rbg_daily, "
        "dungeon_difficulty, raid_difficulty "
        "FROM characters WHERE guid = ? AND login_flags = ?");
    RegisterStatement(CHAR_DEL_QUEST_LOG, "DELETE FROM questlog WHERE player_guid = ? AND quest_id = ?");
    RegisterStatement(CHAR_SEL_COOLDOWNS, "SELECT cooldown_type, cooldown_misc, cooldown_expire_time, cooldown_spellid, cooldown_itemid FROM playercooldowns WHERE player_guid = ?");
    RegisterStatement(CHAR_SEL_QUESTLOG,
        "SELECT player_guid, quest_id, slot, expirytime, explored_area1, explored_area2, explored_area3, explored_area4, "
        "mob_kill1, mob_kill2, mob_kill3, mob_kill4, completed FROM questlog WHERE player_guid = ?");


    RegisterStatement(CHAR_SEL_PLAYER_ITEMS,
        "SELECT ownerguid, guid, entry, wrapped_item_id, wrapped_creator, creator, count, charges, flags, "
        "randomprop, randomsuffix, itemtext, durability, containerslot, slot, enchantments, duration_expireson, "
        "refund_purchasedon, refund_costid, `text` FROM playeritems WHERE ownerguid = ? ORDER BY containerslot ASC");


    RegisterStatement(CHAR_SEL_PLAYER_PETS,
        "SELECT ownerguid, petnumber, type, name, entry, model, level, xp, slot, active, alive, actionbar, "
        "reset_time, reset_cost, spellid, petstate, talentpoints, current_power, current_hp, current_happiness, renamable "
        "FROM playerpets WHERE ownerguid = ? ORDER BY petnumber");


    RegisterStatement(CHAR_SEL_SUMMON_SPELLS, "SELECT ownerguid, entryid, spellid FROM playersummonspells WHERE ownerguid = ? ORDER BY entryid");
    RegisterStatement(CHAR_SEL_MAILBOX, "SELECT message_id, message_type, player_guid, sender_guid, subject, body, money, attached_item_guids, cod, stationary, expiry_time, delivery_time, checked_flag, deleted_flag FROM mailbox WHERE player_guid = ?");
    RegisterStatement(CHAR_SEL_EQUIPMENT_SETS,
        "SELECT ownerguid, setGUID, setid, setname, iconname, head, neck, shoulders, body, chest, waist, legs, feet, wrists, hands, finger1, finger2, trinket1, trinket2, back, mainhand, offhand, ranged, tabard "
        "FROM equipmentsets WHERE ownerguid = ?");
    RegisterStatement(CHAR_SEL_REPUTATION, "SELECT faction, flag, basestanding, standing FROM playerreputations WHERE guid = ?");
    RegisterStatement(CHAR_SEL_SPELLS, "SELECT SpellID FROM playerspells WHERE GUID = ?");
    RegisterStatement(CHAR_SEL_DELETED_SPELLS, "SELECT SpellID FROM playerdeletedspells WHERE GUID = ?");
    RegisterStatement(CHAR_SEL_SKILLS, "SELECT SkillID, CurrentValue, MaximumValue FROM playerskills WHERE GUID = ?");
    RegisterStatement(CHAR_SEL_ACHIEVEMENTS, "SELECT achievement, date FROM character_achievement WHERE guid = ?");
    RegisterStatement(CHAR_SEL_ACHIEVEMENT_PROGRESS, "SELECT criteria, counter, date FROM character_achievement_progress WHERE guid = ?");


    // Corpse
    RegisterStatement(CHAR_CORPSES_SELECT_BY_INSTANCE, "SELECT guid, positionX, positionY, positionZ, orientation, zoneId, mapId, instanceId, data FROM corpses WHERE instanceid = ?");
    RegisterStatement(CHAR_CORPSES_SELECT_BY_GUID, "SELECT guid, positionX, positionY, positionZ, orientation, zoneId, mapId, data, instanceId FROM corpses WHERE guid = ?");
    RegisterStatement(CHAR_REP_CORPSE, "REPLACE INTO corpses (guid, positionX, positionY, positionZ, orientation, zoneId, mapId, data, instanceId) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_DEL_CORPSE, "DELETE FROM corpses WHERE guid = ?");


    // Groups
    RegisterStatement(CHARACTER_GROUP_DELETE, "DELETE FROM `groups` WHERE group_id = ?");
    RegisterStatement(CHARACTER_GROUP_INSERT,
        "INSERT INTO `groups` (group_id, group_type, subgroup_count, loot_method, loot_threshold, difficulty, raiddifficulty, assistant_leader, main_tank, main_assist, "
        "group1member1, group1member2, group1member3, group1member4, group1member5, "
        "group2member1, group2member2, group2member3, group2member4, group2member5, "
        "group3member1, group3member2, group3member3, group3member4, group3member5, "
        "group4member1, group4member2, group4member3, group4member4, group4member5, "
        "group5member1, group5member2, group5member3, group5member4, group5member5, "
        "group6member1, group6member2, group6member3, group6member4, group6member5, "
        "group7member1, group7member2, group7member3, group7member4, group7member5, "
        "group8member1, group8member2, group8member3, group8member4, group8member5, "
        "timestamp, instanceids) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");

    RegisterStatement(CHARACTER_GROUP_INSTANCE_DELETE, "DELETE FROM group_instance WHERE instance = ?");
    RegisterStatement(CHARACTER_GROUP_INSTANCE_DELETE2, "DELETE FROM group_instance WHERE guid = ? AND instance = ?");
    RegisterStatement(CHARACTER_GROUP_INSTANCE_REPLACE, "REPLACE INTO group_instance (guid, instance, permanent) VALUES (?, ?, ?)");
    RegisterStatement(CHAR_GROUPS_SELECT_ALL, "SELECT * FROM `groups`");
    RegisterStatement(CHAR_GROUP_INSTANCE_DELETE_ORPHANS, "DELETE FROM group_instance WHERE guid NOT IN (SELECT guid FROM `groups`)");
    RegisterStatement(CHAR_GROUP_INSTANCE_SELECT_ALL,
        "SELECT gi.guid, i.map, gi.instance, gi.permanent, i.difficulty, i.resettime, "
        "(SELECT COUNT(1) FROM character_instance ci LEFT JOIN `groups` g ON ci.guid = g.group1member1 "
        "WHERE ci.instance = gi.instance AND ci.permanent = 1 LIMIT 1) "
        "FROM group_instance gi LEFT JOIN instance i ON gi.instance = i.id ORDER BY guid");

    // Guild
    RegisterStatement(CHAR_GUILD_NAME_UPDATE_BY_ID, "UPDATE guilds SET guildName = ? WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_EMBLEM_UPDATE, "UPDATE guilds SET emblemStyle = ?, emblemColor = ?, borderStyle = ?, borderColor = ?, backgroundColor = ? WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_LOG_DELETE, "DELETE FROM guild_logs WHERE guildId = ? AND logGuid = ?");
    RegisterStatement(CHAR_GUILD_LOG_INSERT, "INSERT INTO guild_logs VALUES (?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_GUILD_LOG_REPLACE, "REPLACE INTO guild_logs VALUES (?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_GUILD_NEWS_LOG_DELETE, "DELETE FROM guild_news_log WHERE guildId = ? AND logGuid = ?");
    RegisterStatement(CHAR_GUILD_NEWS_LOG_INSERT, "INSERT INTO guild_news_log VALUES (?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_GUILD_NEWS_LOG_REPLACE, "REPLACE INTO guild_news_log VALUES (?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_GUILD_RANK_DELETE, "DELETE FROM guild_ranks WHERE guildId = ? AND rankId = ?");
    RegisterStatement(CHAR_GUILD_RANK_INSERT, "INSERT INTO guild_ranks (guildId, rankId, rankName, rankRights, goldLimitPerDay) VALUES (?, ?, ?, ?, 0)");
    RegisterStatement(CHAR_GUILD_RANK_REPLACE, "REPLACE INTO guild_ranks (guildId, rankId, rankName, rankRights, goldLimitPerDay) VALUES (?, ?, ?, ?, 0)");
    RegisterStatement(CHAR_GUILD_RANK_UPDATE_NAME, "UPDATE guild_ranks SET rankName = ? WHERE guildId = ? AND rankId = ?");
    RegisterStatement(CHAR_GUILD_RANK_UPDATE_RIGHTS, "UPDATE guild_ranks SET rankRights = ? WHERE guildId = ? AND rankId = ?");
    RegisterStatement(CHAR_GUILD_RANK_UPDATE_GOLDLIMIT, "UPDATE guild_ranks SET goldLimitPerDay = ?, rankId = ? WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_BANK_RIGHTS_REPLACE, "REPLACE INTO guild_bank_rights (guildId, tabId, rankId, bankRight, slotPerDay) VALUES (?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_GUILD_DELETE_MEMBERS, "DELETE FROM guild_members WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_INSERT, "REPLACE INTO guilds (guildId, guildName, leaderGuid, emblemStyle, emblemColor, borderStyle, borderColor, backgroundColor, guildInfo, motd, createdate, bankBalance, guildLevel, guildExperience, todayExperience) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, 0)");
    RegisterStatement(CHAR_GUILD_DELETE, "DELETE FROM guilds WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_RANKS_DELETE, "DELETE FROM guild_ranks WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_BANK_TABS_INSERT, "INSERT INTO guild_bank_tabs VALUES(?, ?, '', '', '')");
    RegisterStatement(CHAR_GUILD_BANK_TABS_DELETE, "DELETE FROM guild_bank_tabs WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_BANK_ITEMS_DELETE, "DELETE FROM guild_bank_items WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_BANK_RIGHTS_DELETE, "DELETE FROM guild_bank_rights WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_LOGS_DELETE, "DELETE FROM guild_logs WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_BANK_LOGS_DELETE, "DELETE FROM guild_bank_logs WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_SAVE, "UPDATE guilds SET guildLevel = ?, guildExperience = ?, todayExperience = ? WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_SET_MOTD, "UPDATE guilds SET motd = ? WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_SET_INFO, "UPDATE guilds SET guildInfo = ? WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_BANK_RIGHTS_DELETE_BY_RANK, "DELETE FROM guild_bank_rights WHERE rankId = ? AND guildId = ?");
    RegisterStatement(CHAR_GUILD_RANKS_DELETE_BY_RANK, "DELETE FROM guild_ranks WHERE rankId = ? AND guildId = ?");
    RegisterStatement(CHAR_GUILD_MEMBER_SELECT_BY_PLAYER, "SELECT guildId, playerid FROM guild_members WHERE playerid = ?");
    RegisterStatement(CHAR_GUILD_MEMBER_WITHDRAW_INSERT, "INSERT INTO guild_members_withdraw VALUES(?, 0, 0, 0, 0, 0, 0, 0, 0, 0)");
    RegisterStatement(CHAR_GUILD_UPDATE_BANK_BALANCE, "UPDATE guilds SET bankBalance = ? WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_UPDATE_LEADER, "UPDATE guilds SET leaderGuid = ? WHERE guildId = ?");
    RegisterStatement(CHAR_GUILD_MEMBER_UPDATE_PUBLIC_NOTE, "UPDATE guild_members SET publicNote = ? WHERE playerid = ?");
    RegisterStatement(CHAR_GUILD_MEMBER_UPDATE_OFFICER_NOTE, "UPDATE guild_members SET officerNote = ? WHERE playerid = ?");
    RegisterStatement(CHAR_GUILD_MEMBER_REPLACE, "REPLACE INTO guild_members (guildId, playerid, guildRank, publicNote, officerNote) VALUES (?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_GUILD_MEMBER_UPDATE_RANK, "UPDATE guild_members SET guildRank = ? WHERE playerid = ?");
    RegisterStatement(CHAR_GUILD_MEMBER_WITHDRAW_REPLACE, "REPLACE INTO guild_members_withdraw VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_GUILD_SELECT_BASE,
        "SELECT g.guildId, g.guildName, g.leaderGuid, g.emblemStyle, g.emblemColor, g.borderStyle, g.borderColor, "
        "g.backgroundColor, g.guildInfo, g.motd, g.createdate, g.bankBalance"
#if VERSION_STRING >= Cata
        ", g.guildLevel, g.guildExperience, g.todayExperience"
#endif
        ", COUNT(gbt.guildId) FROM guilds g LEFT JOIN guild_bank_tabs gbt ON g.guildId = gbt.guildId GROUP BY g.guildId ORDER BY g.guildId ASC");
    RegisterStatement(CHAR_GUILD_DELETE_ORPHAN_RANKS, "DELETE gr FROM guild_ranks gr LEFT JOIN guilds g ON gr.guildId = g.guildId WHERE g.guildId IS NULL");
    RegisterStatement(CHAR_GUILD_SELECT_RANKS, "SELECT guildId, rankId, rankName, rankRights, goldLimitPerDay FROM guild_ranks ORDER BY guildId ASC, rankId ASC");
    RegisterStatement(CHAR_GUILD_DELETE_ORPHAN_MEMBERS, "DELETE gm FROM guild_members gm LEFT JOIN guilds g ON gm.guildId = g.guildId WHERE g.guildId IS NULL");
    RegisterStatement(CHAR_GUILD_SELECT_MEMBERS, "SELECT guildId, playerid, guildRank, publicNote, officerNote FROM guild_members");
    RegisterStatement(CHAR_GUILD_SELECT_WITHDRAW, "SELECT guid, tab0, tab1, tab2, tab3, tab4, tab5, tab6, tab7, money FROM guild_members_withdraw");
    RegisterStatement(CHAR_GUILD_DELETE_ORPHAN_BANK_RIGHTS, "DELETE gbr FROM guild_bank_rights gbr LEFT JOIN guilds g ON gbr.guildId = g.guildId WHERE g.guildId IS NULL");
    RegisterStatement(CHAR_GUILD_SELECT_BANK_RIGHTS, "SELECT guildId, tabId, rankId, bankRight, slotPerDay FROM guild_bank_rights ORDER BY guildId ASC, tabId ASC");
    RegisterStatement(CHAR_GUILD_DELETE_OLD_LOGS, "DELETE FROM guild_logs WHERE logGuid > ?");
    RegisterStatement(CHAR_GUILD_DELETE_ORPHAN_LOGS, "DELETE ge FROM guild_logs ge LEFT JOIN guilds g ON ge.guildId = g.guildId WHERE g.guildId IS NULL");
    RegisterStatement(CHAR_GUILD_SELECT_LOGS, "SELECT guildId, logGuid, eventType, playerGuid1, playerGuid2, newRank, timeStamp FROM guild_logs ORDER BY timeStamp DESC, logGuid DESC");
    RegisterStatement(CHAR_GUILD_DELETE_ORPHAN_BANK_LOGS, "DELETE ge FROM guild_bank_logs ge LEFT JOIN guilds g ON ge.guildId = g.guildId WHERE g.guildId IS NULL");
    RegisterStatement(CHAR_GUILD_SELECT_BANK_LOGS, "SELECT guildId, tabId, logGuid, eventType, playerGuid, itemOrMoney, itemStackCount, destTabId, timeStamp FROM guild_bank_logs ORDER BY timeStamp DESC, logGuid DESC");
    RegisterStatement(CHAR_GUILD_DELETE_OLD_NEWS, "DELETE FROM guild_news_log WHERE logGuid > ?");
    RegisterStatement(CHAR_GUILD_DELETE_ORPHAN_NEWS, "DELETE gn FROM guild_news_log gn LEFT JOIN guilds g ON gn.guildId = g.guildId WHERE g.guildId IS NULL");
    RegisterStatement(CHAR_GUILD_SELECT_NEWS, "SELECT guildId, logGuid, eventType, playerGuid, flags, value, timeStamp FROM guild_news_log ORDER BY timeStamp DESC, logGuid DESC");
    RegisterStatement(CHAR_GUILD_DELETE_ORPHAN_TABS, "DELETE gbt FROM guild_bank_tabs gbt LEFT JOIN guilds g ON gbt.guildId = g.guildId WHERE g.guildId IS NULL");
    RegisterStatement(CHAR_GUILD_SELECT_TABS, "SELECT guildId, tabId, tabName, tabIcon, tabInfo FROM guild_bank_tabs ORDER BY guildId ASC, tabId ASC");
    RegisterStatement(CHAR_GUILD_SELECT_BANK_ITEMS, "SELECT guildId, tabId, slotId, itemGuid FROM guild_bank_items");
    RegisterStatement(CHAR_GUILD_SELECT_RANK, "SELECT playerid, guildRank FROM guild_members WHERE playerid = ?");

    // Bank
    RegisterStatement(CHAR_GUILD_BANK_LOG_DELETE, "DELETE FROM guild_bank_logs WHERE guildId = ? AND logGuid = ? AND tabId = ?");
    RegisterStatement(CHAR_GUILD_BANK_LOG_INSERT, "INSERT INTO guild_bank_logs VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_GUILD_BANK_TAB_UPDATE_NAME_ICON, "UPDATE guild_bank_tabs SET tabName = ?, tabIcon = ? WHERE guildId = ? AND tabId = ?");
    RegisterStatement(CHAR_GUILD_BANK_TAB_UPDATE_TEXT_ICON, "UPDATE guild_bank_tabs SET tabInfo = ?, tabIcon = ? WHERE guildId = ? AND tabId = ?");
    RegisterStatement(CHAR_GUILD_BANK_ITEM_DELETE, "DELETE FROM guild_bank_items WHERE guildId = ? AND tabId = ? AND slotId = ?");
    RegisterStatement(CHAR_GUILD_BANK_ITEM_INSERT, "INSERT INTO guild_bank_items (guildId, tabId, slotId, itemGuid) VALUES (?, ?, ?, ?)");
    RegisterStatement(CHAR_GUILD_BANK_ITEM_DELETE_BY_ITEMGUID, "DELETE FROM guild_bank_items WHERE itemGuid = ? AND guildId = ? AND tabId = ?");


    // Charter
    RegisterStatement(CHAR_CHARTER_DELETE, "DELETE FROM charters WHERE charterId = ?");
    RegisterStatement(CHAR_CHARTER_INSERT, "INSERT INTO charters VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_CHARTER_REPLACE, "REPLACE INTO charters VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_CHARTERS_SELECT_ALL, "SELECT * FROM charters");

    // Mailbox
    RegisterStatement(CHARACTER_MAILBOX_DELETE, "DELETE FROM mailbox WHERE message_id = ?");
    RegisterStatement(CHARACTER_MAILBOX_DELETE_BY_ID, "DELETE FROM mailbox WHERE message_id = ?");
    RegisterStatement(CHARACTER_MAILBOX_INSERT, "INSERT INTO mailbox (message_id, message_type, player_guid, sender_guid, subject, body, attached_item_guids, cod, stationary, expiry_time, delivery_time, checked_flag, deleted_flag) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_UPD_MAILBOX_STATE, "UPDATE mailbox SET checked_flag = ?, expiry_time = ? WHERE message_id = ?");
    RegisterStatement(CHAR_UPD_MAILBOX_CLEAR_MONEY, "UPDATE mailbox SET money = 0 WHERE message_id = ?");
    RegisterStatement(CHAR_UPD_MAILBOX_COD, "UPDATE mailbox SET cod = 0 WHERE message_id = ?");

    // Quests
    RegisterStatement(CHARACTER_QUESTLOG_REPLACE, "REPLACE INTO questlog (player_guid, quest_id, slot, expirytime, explored_area1, explored_area2, explored_area3, explored_area4, mob_kill1, mob_kill2, mob_kill3, mob_kill4, completed) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");


    // Auctions
    RegisterStatement(CHARACTER_AUCTION_DELETE, "DELETE FROM auctions WHERE auctionId = ?");
    RegisterStatement(CHARACTER_AUCTION_INSERT, "INSERT INTO auctions VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHARACTER_AUCTION_UPDATE_BID, "UPDATE auctions SET bidder = ?, bid = ? WHERE auctionId = ?");
    RegisterStatement(CHARACTER_AUCTION_SELECT_BY_HOUSE, "SELECT * FROM auctions WHERE auctionhouse = ?");
    RegisterStatement(CHARACTER_AUCTION_DELETE_BY_ID, "DELETE FROM auctions WHERE auctionId = ?");
    RegisterStatement(CHARACTER_AUCTION_MAX_ID_SELECT, "SELECT MAX(auctionId) FROM auctions");

    // Arena
    RegisterStatement(CHARACTER_ARENA_TEAM_DELETE, "DELETE FROM arenateams WHERE id = ?");
    RegisterStatement(CHARACTER_ARENA_TEAM_SAVE, "INSERT INTO arenateams (id, type, leader, name, emblemstyle, emblemcolour, borderstyle, bordercolour, backgroundcolour, rating, data, ranking, player_data1, player_data2, player_data3, player_data4, player_data5, player_data6, player_data7, player_data8, player_data9, player_data10) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

    RegisterStatement(CHAR_ARENA_TEAMS_SELECT_ALL, "SELECT * FROM arenateams");
    RegisterStatement(CHAR_UPD_CHARACTER_ARENA_POINTS, "UPDATE characters SET arenaPoints = ? WHERE guid = ?");
    RegisterStatement(CHAR_REP_SERVER_SETTING_ARENA_UPDATE, "REPLACE INTO server_settings VALUES('last_arena_update_time', ?)");
    RegisterStatement(CHAR_REP_SERVER_SETTING_DAILY_UPDATE, "REPLACE INTO server_settings VALUES('last_daily_update_time', ?)");
    RegisterStatement(CHAR_SEL_SERVER_SETTING, "SELECT setting_value FROM server_settings WHERE setting_id = ?");
    RegisterStatement(CHAR_UPD_RESET_FINISHED_DAILIES, "UPDATE characters SET finisheddailies = ''");
    RegisterStatement(CHAR_UPD_RESET_RBG_DAILY, "UPDATE characters SET rbg_daily = '0'");
    RegisterStatement(CHAR_SEL_GUID_ARENA_POINTS, "SELECT guid, arenaPoints FROM characters");

    // Equipmentmanager
    RegisterStatement(CHARACTER_EQUIPMENT_SETS_DELETE, "DELETE FROM equipmentsets WHERE ownerguid = ?");
    RegisterStatement(CHARACTER_EQUIPMENT_SETS_INSERT, "INSERT INTO equipmentsets (ownerguid, setGUID, setid, setname, iconname, head, neck, shoulders, body, chest, waist, legs, feet, wrists, hands, finger1, finger2, trinket1, trinket2, back, mainhand, offhand, ranged, tabard) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHARACTER_EQUIPMENT_SETS_REPLACE, "REPLACE INTO equipmentsets (ownerguid, setGUID, setid, setname, iconname, head, neck, shoulders, body, chest, waist, legs, feet, wrists, hands, finger1, finger2, trinket1, trinket2, back, mainhand, offhand, ranged, tabard) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");


    // Calendar
    RegisterStatement(CHARACTER_CALENDAR_EVENTS_SELECT, "SELECT entry, creator, title, description, type, dungeon, date, flags FROM calendar_events");
    RegisterStatement(CHARACTER_CALENDAR_INVITES_SELECT, "SELECT id, event, invitee, sender, status, statustime, `rank`, `text` FROM calendar_invites;");


    // Gameevents
    RegisterStatement(CHARACTER_GAMEEVENT_SAVE_REPLACE, "REPLACE INTO gameevent_save (event_entry, state, next_start) VALUES (?, ?, ?)");
    RegisterStatement(CHARACTER_GAMEEVENT_SAVE_CLEAN, "DELETE FROM gameevent_save WHERE state<>4");
    RegisterStatement(CHARACTER_GAMEEVENT_SAVE_SELECT, "SELECT event_entry, state, next_start FROM gameevent_save");
    RegisterStatement(CHARACTER_GAMEEVENT_SAVE_DELETE_BY_ID, "DELETE FROM gameevent_save WHERE event_entry = ?");

    // Instances
    RegisterStatement(CHAR_INSTANCE_INSERT, "INSERT INTO instance (id, map, resettime, difficulty, completedEncounters, data) VALUES (?, ?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_INSTANCE_DELETE_EXPIRED, "DELETE i FROM instance i LEFT JOIN instance_reset ir ON mapid = map AND i.difficulty = ir.difficulty WHERE (i.resettime > 0 AND i.resettime < UNIX_TIMESTAMP()) OR (ir.resettime IS NOT NULL AND ir.resettime < UNIX_TIMESTAMP())");
    RegisterStatement(CHAR_INSTANCE_DELETE_INVALID_CHARACTERS, "DELETE ci.* FROM character_instance AS ci LEFT JOIN characters AS c ON ci.guid = c.guid WHERE c.guid IS NULL");
    RegisterStatement(CHAR_INSTANCE_DELETE_INVALID_GROUPS, "DELETE gi.* FROM group_instance AS gi LEFT JOIN `groups` AS g ON gi.guid = g.group_id WHERE g.group_id IS NULL");
    RegisterStatement(CHAR_INSTANCE_DELETE_ORPHANED_INSTANCES, "DELETE i.* FROM instance AS i LEFT JOIN character_instance AS ci ON i.id = ci.instance LEFT JOIN group_instance AS gi ON i.id = gi.instance WHERE ci.guid IS NULL AND gi.guid IS NULL");
    RegisterStatement(CHAR_RESPAWN_DELETE_INVALID, "DELETE FROM respawn WHERE instanceId > 0 AND instanceId NOT IN (SELECT id FROM instance)");
    RegisterStatement(CHAR_CHARACTER_INSTANCE_DELETE_INVALID, "DELETE tmp.* FROM character_instance AS tmp LEFT JOIN instance ON tmp.instance = instance.id WHERE tmp.instance > 0 AND instance.id IS NULL");
    RegisterStatement(CHAR_GROUP_INSTANCE_DELETE_INVALID, "DELETE tmp.* FROM group_instance AS tmp LEFT JOIN instance ON tmp.instance = instance.id WHERE tmp.instance > 0 AND instance.id IS NULL");
    RegisterStatement(CHAR_CORPSE_CLEAN_INVALID_INSTANCE, "UPDATE corpses SET instanceId = 0 WHERE instanceId > 0 AND instanceId NOT IN (SELECT id FROM instance)");
    RegisterStatement(CHAR_CHARACTER_CLEAN_INVALID_INSTANCE, "UPDATE characters AS tmp LEFT JOIN instance ON tmp.instance_id = instance.id SET tmp.instance_id = 0 WHERE tmp.instance_id > 0 AND instance.id IS NULL");
    RegisterStatement(CHAR_INSTANCE_RESETTIME_SELECT, "SELECT id, map, difficulty, resettime FROM instance ORDER BY id ASC");
    RegisterStatement(CHAR_INSTANCE_MAPDATA_SELECT, "SELECT mapid, difficulty, resettime FROM instance_reset");
    RegisterStatement(CHAR_INSTANCE_RESET_DELETE, "DELETE FROM instance_reset WHERE mapid = ? AND difficulty = ?");
    RegisterStatement(CHAR_INSTANCE_RESET_UPDATE, "UPDATE instance_reset SET resettime = ? WHERE mapid = ? AND difficulty = ?");
    RegisterStatement(CHAR_INSTANCE_RESET_INSERT, "INSERT INTO instance_reset (mapid, difficulty, resettime) VALUES (?, ?, ?)");
    RegisterStatement(CHAR_DELETE_CHARACTER_INSTANCE_FOR_MAP_DIFF, "DELETE FROM character_instance USING character_instance LEFT JOIN instance ON character_instance.instance = id WHERE (extendState = 0 or permanent = 0) and map = ? and difficulty = ?");
    RegisterStatement(CHAR_DELETE_GROUP_INSTANCE_FOR_MAP_DIFF, "DELETE FROM group_instance USING group_instance LEFT JOIN instance ON group_instance.instance = id WHERE map = ? and difficulty = ?");
    RegisterStatement(CHAR_DELETE_INSTANCE_WITHOUT_EXTENDED_CHARACTERS, "DELETE FROM instance WHERE map = ? and difficulty = ? and (SELECT guid FROM character_instance WHERE extendState != 0 AND instance = id LIMIT 1) IS NULL");
    RegisterStatement(CHAR_DECREMENT_EXTENDSTATE_FOR_MAP_DIFF, "UPDATE character_instance LEFT JOIN instance ON character_instance.instance = id SET extendState = extendState-1 WHERE map = ? and difficulty = ?");
    RegisterStatement(CHAR_DELETE_INSTANCE_BY_ID, "DELETE FROM instance WHERE id = ?");
    RegisterStatement(CHAR_DELETE_CHARACTER_INSTANCE_BY_INSTANCE, "DELETE FROM character_instance WHERE instance = ?");
    RegisterStatement(CHAR_DELETE_GROUP_INSTANCE_BY_INSTANCE, "DELETE FROM group_instance WHERE instance = ?");
    RegisterStatement(CHAR_UPDATE_INSTANCE_RESETTIME_BY_ID, "UPDATE instance SET resettime = ? WHERE id = ?");
    RegisterStatement(CHAR_SEL_INSTANCE_DATA_AND_ENCOUNTERS, "SELECT data, completedEncounters FROM instance WHERE map = ? AND id = ?");
    RegisterStatement(CHAR_SEL_PERMANENT_CHARACTER_INSTANCE_BY_ID, "SELECT guid FROM character_instance WHERE instance = ? AND permanent = 1");
    RegisterStatement(CHAR_UPD_INSTANCE_DATA, "UPDATE instance SET completedEncounters = ?, data = ? WHERE id = ?");

    // Respawn
    RegisterStatement(CHAR_SEL_RESPAWN_BY_MAP_AND_INSTANCE, "SELECT type, spawnId, respawnTime FROM respawn WHERE mapId = ? AND instanceId = ?");
    RegisterStatement(CHAR_REPLACE_RESPAWN, "REPLACE INTO respawn (type, spawnId, respawnTime, mapId, instanceId) VALUES (?, ?, ?, ?, ?)");
    RegisterStatement(CHAR_DEL_RESPAWN_BY_MAP_AND_INSTANCE, "DELETE FROM respawn WHERE mapId = ? AND instanceId = ?");
    RegisterStatement(CHAR_DEL_RESPAWN_BY_ALL_KEYS, "DELETE FROM respawn WHERE type = ? AND spawnId = ? AND mapId = ? AND instanceId = ?");



}
