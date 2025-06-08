/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "GuildRankInfo.hpp"
#include "GuildBankRightsAndSlots.hpp"
#include "GuildDefinitions.hpp"
#include "Logging/Log.hpp"
#include "Database/Database.hpp"
#include "Logging/Logger.hpp"
#include "Server/DatabaseDefinition.hpp"

GuildRankInfo::GuildRankInfo() : mGuildId(0), mRankId(GUILD_RANK_NONE), mRights(GR_RIGHT_EMPTY), mBankMoneyPerDay(0)
{
}

GuildRankInfo::GuildRankInfo(uint32_t guildId) : mGuildId(guildId), mRankId(GUILD_RANK_NONE), mRights(GR_RIGHT_EMPTY), mBankMoneyPerDay(0)
{
}

GuildRankInfo::GuildRankInfo(uint32_t guildId, uint8_t rankId, std::string const& name, uint32_t rights, uint32_t money) :
    mGuildId(guildId), mRankId(rankId), mName(name), mRights(rights), mBankMoneyPerDay(money)
{
}

void GuildRankInfo::loadGuildRankFromDB(Field* fields)
{
    mRankId = fields[1].asUint8();
    mName = fields[2].asCString();
    mRights = fields[3].asUint32();
    mBankMoneyPerDay = fields[4].asUint32();

    if (mRankId == GR_GUILDMASTER)
    {
        mRights |= GR_RIGHT_ALL;
    }
}

void GuildRankInfo::saveGuildRankToDB(bool _delete) const
{
    if (_delete)
    {
        auto delStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_RANK_DELETE);
        delStmt->Bind(0, mGuildId);
        delStmt->Bind(1, static_cast<uint32_t>(mRankId));

        CharacterDatabase.ExecuteStatement(std::move(delStmt));
    }
    else
    {
        auto repStmt = CharacterDatabase.CreateStatement(CHAR_GUILD_RANK_REPLACE);
        repStmt->Bind(0, mGuildId);
        repStmt->Bind(1, static_cast<uint32_t>(mRankId));
        repStmt->Bind(2, mName);
        repStmt->Bind(3, static_cast<uint32_t>(mRights));

        CharacterDatabase.ExecuteStatement(std::move(repStmt));
    }
}

uint8_t GuildRankInfo::getId() const
{
    return mRankId;
}

std::string const& GuildRankInfo::getName() const
{
    return mName;
}

void GuildRankInfo::setName(std::string const& name)
{
    if (mName == name)
        return;

    mName = name;

    auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_RANK_UPDATE_NAME);
    stmt->Bind(0, mName);
    stmt->Bind(1, mGuildId);
    stmt->Bind(2, static_cast<uint32_t>(mRankId));

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

uint32_t GuildRankInfo::getRights() const
{
    return mRights;
}

void GuildRankInfo::setRights(uint32_t rights)
{
    if (mRankId == GR_GUILDMASTER)
        rights = GR_RIGHT_ALL;

    if (mRights == rights)
        return;

    mRights = rights;

    auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_RANK_UPDATE_RIGHTS);
    stmt->Bind(0, static_cast<uint32_t>(mRights));
    stmt->Bind(1, mGuildId);
    stmt->Bind(2, static_cast<uint32_t>(mRankId));

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

uint32_t GuildRankInfo::getBankMoneyPerDay() const
{
    return mBankMoneyPerDay;
}

void GuildRankInfo::setBankMoneyPerDay(uint32_t money)
{
    if (mRankId == GR_GUILDMASTER)
        money = uint32_t(UNLIMITED_WITHDRAW_MONEY);

    if (mBankMoneyPerDay == money)
        return;

    mBankMoneyPerDay = money;

    auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_RANK_UPDATE_GOLDLIMIT);
    stmt->Bind(0, static_cast<uint32_t>(money));
    stmt->Bind(1, static_cast<uint32_t>(mRankId));
    stmt->Bind(2, mGuildId);

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

int8_t GuildRankInfo::getBankTabRights(uint8_t tabId) const
{
    return tabId < MAX_GUILD_BANK_TABS ? mBankTabRightsAndSlots[tabId].getRights() : 0;
}

int32_t GuildRankInfo::getBankTabSlotsPerDay(uint8_t tabId) const
{
    return tabId < MAX_GUILD_BANK_TABS ? mBankTabRightsAndSlots[tabId].getSlots() : 0;
}

void GuildRankInfo::createMissingTabsIfNeeded(uint8_t tabs, bool /*_delete*/, bool logOnCreate)
{
    for (uint8_t i = 0; i < tabs; ++i)
    {
        GuildBankRightsAndSlots& rightsAndSlots = mBankTabRightsAndSlots[i];
        if (rightsAndSlots.getTabId() == i)
            continue;

        rightsAndSlots.setTabId(i);
        if (mRankId == GR_GUILDMASTER)
            rightsAndSlots.SetGuildMasterValues();

        if (logOnCreate)
            sLogger.failure("Guild {} has broken Tab {} for rank {}. Created default tab.", mGuildId, i, static_cast<uint32_t>(mRankId));

        auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_BANK_RIGHTS_REPLACE);
        stmt->Bind(0, mGuildId);
        stmt->Bind(1, i);
        stmt->Bind(2, static_cast<uint32_t>(mRankId));
        stmt->Bind(3, static_cast<uint32_t>(rightsAndSlots.getRights()));
        stmt->Bind(4, rightsAndSlots.getSlots());

        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }
}

void GuildRankInfo::setBankTabSlotsAndRights(GuildBankRightsAndSlots rightsAndSlots, bool saveToDB)
{
    if (mRankId == GR_GUILDMASTER)
        rightsAndSlots.SetGuildMasterValues();

    GuildBankRightsAndSlots& guildBR = mBankTabRightsAndSlots[rightsAndSlots.getTabId()];
    guildBR = rightsAndSlots;

    if (saveToDB)
    {
        auto stmt = CharacterDatabase.CreateStatement(CHAR_GUILD_BANK_RIGHTS_REPLACE);
        stmt->Bind(0, mGuildId);
        stmt->Bind(1, static_cast<uint32_t>(guildBR.getTabId()));
        stmt->Bind(2, static_cast<uint32_t>(mRankId));
        stmt->Bind(3, static_cast<uint32_t>(guildBR.getRights()));
        stmt->Bind(4, guildBR.getSlots());

        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }
}
