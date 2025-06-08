/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "Storage/WDB/WDBStores.hpp"
#include "GuildMgr.hpp"
#include "Guild.hpp"
#include "Logging/Logger.hpp"
#include "Management/ObjectMgr.hpp"
#include "Server/DatabaseDefinition.hpp"
#include "Server/World.h"
#include "Utilities/Strings.hpp"

GuildMgr& GuildMgr::getInstance()
{
    static GuildMgr mInstance;
    return mInstance;
}

void GuildMgr::finalize()
{
    GuildStore.clear();
}

void GuildMgr::update(uint32_t /*diff*/)
{
    if (!firstSave)
    {
        lastSave = static_cast<uint32_t>(time(nullptr));
        firstSave = true;
    }

    if (time(nullptr) >= lastSave)
    {
        lastSave = static_cast<uint32_t>(time(nullptr)) + worldConfig.guild.saveInterval;
        sGuildMgr.saveGuilds();
    }
}

void GuildMgr::saveGuilds()
{
#if VERSION_STRING >= Cata
    for (GuildContainer::iterator itr = GuildStore.begin(); itr != GuildStore.end(); ++itr)
        itr->second->saveGuildToDB();
#endif
}

Guild* GuildMgr::createGuild(Player* guildLeader, std::string const& guildName)
{
    auto guild = std::make_unique<Guild>();
    if (!guild->create(guildLeader, guildName))
        return nullptr;

    const auto [guildItr, _] = GuildStore.emplace(guild->getId(), std::move(guild));
    return guildItr->second.get();
}

void GuildMgr::removeGuild(uint32_t guildId)
{
    GuildStore.erase(guildId);
}

Guild* GuildMgr::getGuildById(uint32_t guildId) const
{
    GuildContainer::const_iterator itr = GuildStore.find(guildId);
    if (itr != GuildStore.end())
        return itr->second.get();

    return nullptr;
}

//\brief used only by LuAEngine!
Guild* GuildMgr::getGuildByLeader(uint64_t guid) const
{
    for (GuildContainer::const_iterator itr = GuildStore.begin(); itr != GuildStore.end(); ++itr)
        if (itr->second->getLeaderGUID() == guid)
            return itr->second.get();

    return nullptr;
}

Guild* GuildMgr::getGuildByName(const std::string& guildName) const
{
    std::string search = guildName;
    AscEmu::Util::Strings::toUpperCase(search);
    for (GuildContainer::const_iterator itr = GuildStore.begin(); itr != GuildStore.end(); ++itr)
    {
        std::string gname = itr->second->getName();
        AscEmu::Util::Strings::toUpperCase(gname);
        if (search == gname)
            return itr->second.get();
    }
    return nullptr;
}

void GuildMgr::loadGuildDataFromDB()
{
    sLogger.debug("Loading guild definitions...");
    {
        const auto startTime = Util::TimeNow();

        auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_SELECT_BASE);
        auto result = CharacterDatabase.QueryStatement(std::move(stmt));
        if (!result)
        {
            sLogger.debug("Loaded 0 guild definitions. DB table `guilds` is empty.");
        }
        else
        {
            uint32_t count = 0;
            do
            {
                Field* fields = result->Fetch();
                auto guild = std::make_unique<Guild>();

                if (!guild->loadGuildFromDB(fields))
                    continue;

                GuildStore.emplace(guild->getId(), std::move(guild));
                ++count;
            } while (result->NextRow());

            sLogger.debug("Loaded {} guild definitions in {} ms", count, Util::GetTimeDifferenceToNow(startTime));
        }
    }

    // 2. Load all guild ranks
    sLogger.debug("Loading guild ranks...");
    {
        auto startTime = Util::TimeNow();

        auto deleteStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_DELETE_ORPHAN_RANKS);
        CharacterDatabase.ExecuteStatement(std::move(deleteStmt));

        auto selectStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_SELECT_RANKS);
        auto result = CharacterDatabase.QueryStatement(std::move(selectStmt));

        if (!result)
        {
            sLogger.debug("Loaded 0 guild ranks. DB table `guild_ranks` is empty.");
        }
        else
        {
            uint32_t count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32_t guildId = fields[0].asUint32();
                if (Guild* guild = getGuildById(guildId))
                    guild->loadRankFromDB(fields);
                ++count;
            } while (result->NextRow());

            sLogger.debug("Loaded {} guild ranks in {} ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
        }
    }

    // 3. Load all guild members
    sLogger.debug("Loading guild members...");
    {
        auto startTime = Util::TimeNow();

        auto deleteStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_DELETE_ORPHAN_MEMBERS);
        CharacterDatabase.ExecuteStatement(std::move(deleteStmt));

        auto membersStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_SELECT_MEMBERS);
        auto withdrawStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_SELECT_WITHDRAW);
        auto result = CharacterDatabase.QueryStatement(std::move(membersStmt));
        auto result2 = CharacterDatabase.QueryStatement(std::move(withdrawStmt));

        if (!result || !result2)
        {
            sLogger.debug("Loaded 0 guild members. DB table `guild_members` OR `guild_members_withdraw` is empty.");
        }
        else
        {
            uint32_t count = 0;
            do
            {
                Field* fields = result->Fetch();
                Field* fields2 = result2->Fetch();
                uint32_t guildId = fields[0].asUint32();
                if (Guild* guild = getGuildById(guildId))
                    guild->loadMemberFromDB(fields, fields2);
                ++count;
            } while (result->NextRow() && result2->NextRow());

            sLogger.debug("Loaded {} guild members in {} ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
        }
    }

    // 4. Load all guild bank tab rights
    sLogger.debug("Loading bank tab rights...");
    {
        auto startTime = Util::TimeNow();

        auto deleteStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_DELETE_ORPHAN_BANK_RIGHTS);
        CharacterDatabase.ExecuteStatement(std::move(deleteStmt));

        auto selectStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_SELECT_BANK_RIGHTS);
        auto result = CharacterDatabase.QueryStatement(std::move(selectStmt));

        if (!result)
        {
            sLogger.debug("Loaded 0 guild bank tab rights. DB table `guild_bank_rights` is empty.");
        }
        else
        {
            uint32_t count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32_t guildId = fields[0].asUint32();
                if (Guild* guild = getGuildById(guildId))
                    guild->loadBankRightFromDB(fields);
                ++count;
            } while (result->NextRow());

            sLogger.debug("Loaded {} bank tab rights in {} ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
        }
    }

    // 5. Load all event logs
    sLogger.debug("Loading guild event logs...");
    {
        auto startTime = Util::TimeNow();

        {
            auto deleteOldStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_DELETE_OLD_LOGS);
            deleteOldStmt->Bind(0, 100);
            CharacterDatabase.ExecuteStatement(std::move(deleteOldStmt));

            auto deleteOrphanStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_DELETE_ORPHAN_LOGS);
            CharacterDatabase.ExecuteStatement(std::move(deleteOrphanStmt));
        }

        auto selectStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_SELECT_LOGS);
        auto result = CharacterDatabase.QueryStatement(std::move(selectStmt));

        if (!result)
        {
            sLogger.debug("Loaded 0 guild event logs. DB table `guild_logs` is empty.");
        }
        else
        {
            uint32_t count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32_t guildId = fields[0].asUint32();
                if (Guild* guild = getGuildById(guildId))
                    guild->loadEventLogFromDB(fields);
                ++count;
            } while (result->NextRow());

            sLogger.debug("Loaded {} guild event logs in {} ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
        }
    }

    // 6. Load all bank event logs
    sLogger.debug("Loading guild bank event logs...");
    {
        auto startTime = Util::TimeNow();

        auto deleteStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_DELETE_ORPHAN_BANK_LOGS);
        CharacterDatabase.ExecuteStatement(std::move(deleteStmt));

        auto selectStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_SELECT_BANK_LOGS);
        auto result = CharacterDatabase.QueryStatement(std::move(selectStmt));

        if (!result)
        {
            sLogger.debug("Loaded 0 guild bank event logs. DB table `guild_bank_logs` is empty.");
        }
        else
        {
            uint32_t count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32_t guildId = fields[0].asUint32();
                if (Guild* guild = getGuildById(guildId))
                    guild->loadBankEventLogFromDB(fields);
                ++count;
            } while (result->NextRow());

            sLogger.debug("Loaded {} guild bank event logs in {} ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
        }
    }

    // 7. Load all news event logs
    sLogger.debug("Loading Guild News...");
    {
        auto startTime = Util::TimeNow();

        {
            auto deleteOldStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_DELETE_OLD_NEWS);
            deleteOldStmt->Bind(0, 250);
            CharacterDatabase.ExecuteStatement(std::move(deleteOldStmt));

            auto deleteOrphanStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_DELETE_ORPHAN_NEWS);
            CharacterDatabase.ExecuteStatement(std::move(deleteOrphanStmt));
        }

        auto selectStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_SELECT_NEWS);
        auto result = CharacterDatabase.QueryStatement(std::move(selectStmt));

        if (!result)
        {
            sLogger.debug("Loaded 0 guild event logs. DB table `guild_news_log` is empty.");
        }
        else
        {
            uint32_t count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32_t guildId = fields[0].asUint32();
                if (Guild* guild = getGuildById(guildId))
                    guild->loadGuildNewsLogFromDB(fields);
                ++count;
            } while (result->NextRow());

            sLogger.debug("Loaded {} guild news logs in {} ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
        }
    }

    // 8. Load all guild bank tabs
    sLogger.debug("Loading guild bank tabs...");
    {
        auto startTime = Util::TimeNow();

        auto deleteStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_DELETE_ORPHAN_TABS);
        CharacterDatabase.ExecuteStatement(std::move(deleteStmt));

        auto selectStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_SELECT_TABS);
        auto result = CharacterDatabase.QueryStatement(std::move(selectStmt));

        if (!result)
        {
            sLogger.debug("Loaded 0 guild bank tabs. DB table `guild_bank_tabs` is empty.");
        }
        else
        {
            uint32_t count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32_t guildId = fields[0].asUint32();
                if (Guild* guild = getGuildById(guildId))
                    guild->loadBankTabFromDB(fields);
                ++count;
            } while (result->NextRow());

            sLogger.debug("Loaded {} guild bank tabs in {} ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
        }
    }

    // 9. Fill all guild bank tabs
    sLogger.debug("Filling bank tabs with items...");
    {
        auto startTime = Util::TimeNow();

        auto selectStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_SELECT_BANK_ITEMS);
        auto result = CharacterDatabase.QueryStatement(std::move(selectStmt));

        if (!result)
        {
            sLogger.info("Loaded 0 guild bank tab items. DB table `guild_bank_items` is empty.");
        }
        else
        {
            uint32_t count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint32_t guildId = fields[0].asUint32();
                if (Guild* guild = getGuildById(guildId))
                    guild->loadBankItemFromDB(fields);

                ++count;
            } while (result->NextRow());

            sLogger.info("Loaded {} guild bank items in {} ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
        }
    }

    // 10. Load guild achievements TODO
    {
        //TODO
    }
}

uint32_t GuildMgr::getNextGuildId()
{
    return sObjectMgr.generateGuildId();
}

// Guild collection

std::string GuildMgr::getGuildNameById(uint32_t guildId) const
{
    if (Guild* guild = getGuildById(guildId))
        return guild->getName();

    return "";
}

uint32_t GuildMgr::getXPForGuildLevel(uint8_t level) const
{
    if (level < GuildXPperLevel.size())
    {
        return static_cast<uint32_t>(GuildXPperLevel[level]);
    }

    return 0;
}

void GuildMgr::loadGuildXpForLevelFromDB()
{
    auto startTime = Util::TimeNow();

    GuildXPperLevel.resize(worldConfig.guild.maxLevel);
    for (uint8_t level = 0; level < worldConfig.guild.maxLevel; ++level)
        GuildXPperLevel[level] = 0;

    auto stmt = WorldDatabase.CreateStatement(WORLD_GUILD_XP_FOR_LEVEL_SELECT);
    auto result = WorldDatabase.QueryStatement(std::move(stmt));

    if (!result)
    {
        sLogger.debug("Loaded 0 xp for guild level definitions. DB table `guild_xp_for_level` is empty.");
        return;
    }

    uint32_t count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32_t level = fields[0].asUint8();
        uint32_t requiredXP = static_cast<uint32_t>(fields[1].asUint64());

        if (level >= worldConfig.guild.maxLevel)
        {
            sLogger.debugFlag(AscEmu::Logging::LF_DB_TABLES, "Table `guild_xp_for_level` includes invalid xp definitions for level {} which is higher than the defined levelcap in your config file! <skipped>", level);
            continue;
        }

        GuildXPperLevel[level] = requiredXP;
        ++count;

    } while (result->NextRow());

    // fill level gaps
    for (uint8_t level = 1; level < worldConfig.guild.maxLevel; ++level)
    {
        if (!GuildXPperLevel[level])
        {
            sLogger.failure("Level {} does not have XP for guild level data. Using data of level [{}] + 1660000.", level + 1, level);
            GuildXPperLevel[level] = GuildXPperLevel[level - 1U] + 1660000;
        }
    }

    sLogger.debug("Loaded {} xp for guild level definitions in {} ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
}

void GuildMgr::loadGuildRewardsFromDB()
{
    auto startTime = Util::TimeNow();

    auto stmt = WorldDatabase.CreateStatement(WORLD_GUILD_REWARDS_SELECT);
    auto result = WorldDatabase.QueryStatement(std::move(stmt));

    if (!result)
    {
        sLogger.debug("Loaded 0 guild reward definitions. DB table `guild_rewards` is empty.");
        return;
    }

    uint32_t count = 0;

    do
    {
        GuildReward reward;
        Field* fields = result->Fetch();

        reward.entry = fields[0].asUint32();
        reward.standing = fields[1].asUint8();
        reward.racemask = fields[2].asInt32();
        reward.price = fields[3].asUint64();
        reward.achievementId = fields[4].asUint32();

        if (!sItemStore.lookupEntry(reward.entry))
        {
            sLogger.failure("Guild rewards contains non-existent item entry {}", reward.entry);
            continue;
        }

        if (reward.standing >= 8)
        {
            sLogger.failure("Guild rewards contains invalid reputation standing {}, max is {}", uint32_t(reward.standing), 7);
            continue;
        }

        GuildRewards.push_back(std::move(reward));
        ++count;

    } while (result->NextRow());

    sLogger.debug("Loaded {} guild reward definitions in {} ms", count, static_cast<uint32_t>(Util::GetTimeDifferenceToNow(startTime)));
}

#if VERSION_STRING >= Cata
void GuildMgr::resetTimes(bool week)
{
    for (auto itr = GuildStore.begin(); itr != GuildStore.end(); ++itr)
    {
        if (const auto& guild = itr->second)
            guild->resetTimes(week);
    }
}
#endif
