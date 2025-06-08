/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "AuctionMgr.hpp"
#include "Management/AuctionHouse.h"
#include "Logging/Logger.hpp"
#include "Server/DatabaseDefinition.hpp"

AuctionMgr& AuctionMgr::getInstance()
{
    static AuctionMgr mInstance;
    return mInstance;
}

void AuctionMgr::initialize()
{
}

void AuctionMgr::finalize()
{
    m_auctionHouseEntryMap.clear();
    m_auctionHouses.clear();
}

void AuctionMgr::loadAuctionHouses()
{
    sLogger.info("AuctionMgr : Loading Auction Houses...");

    {
        auto stmt = CharacterDatabase.CreateStatement(CHARACTER_AUCTION_MAX_ID_SELECT);
        auto res = CharacterDatabase.QueryStatement(std::move(stmt));
        if (res)
            m_maxId = res->Fetch()[0].asUint32();
    }

    std::map<uint32_t, AuctionHouse*> tempmap;

    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_AUCTIONHOUSE_GROUPS_SELECT);
        auto res = WorldDatabase.QueryStatement(std::move(stmt));

        if (res)
        {
            const uint32_t rowCount = res->GetRowCount();
            const uint32_t period = (rowCount / 20) + 1;
            uint32_t c = 0;

            do
            {
                uint32_t ahgroup = res->Fetch()[0].asUint32();
                const auto& ah = m_auctionHouses.emplace_back(std::make_unique<AuctionHouse>(ahgroup));
                ah->loadAuctionsFromDB();
                tempmap.try_emplace(ahgroup, ah.get());

                if (!((++c) % period))
                {
                    sLogger.info("AuctionHouse : Done {}/{}, {}% complete.", c, rowCount, c * 100 / rowCount);
                }
            } while (res->NextRow());
        }
    }

    {
        auto stmt = WorldDatabase.CreateStatement(WORLD_AUCTIONHOUSE_ENTRIES_SELECT);
        auto res = WorldDatabase.QueryStatement(std::move(stmt));

        if (res)
        {
            do
            {
                Field* fields = res->Fetch();
                uint32_t entry = fields[0].asUint32();
                uint32_t group = fields[1].asUint32();

                m_auctionHouseEntryMap.try_emplace(entry, tempmap[group]);
            } while (res->NextRow());
        }
    }
}

AuctionHouse* AuctionMgr::getAuctionHouse(uint32_t _entry)
{
    const auto itr = m_auctionHouseEntryMap.find(_entry);
    if (itr == m_auctionHouseEntryMap.end())
        return nullptr;

    return itr->second;
}

uint32_t AuctionMgr::generateAuctionId()
{
    uint32_t id = ++m_maxId;

    return id;
}

void AuctionMgr::update()
{
    if (++m_loopcount % 100)
        return;

    for (auto itr = m_auctionHouses.begin(); itr != m_auctionHouses.end(); ++itr)
    {
        (*itr)->updateDeletionQueue();

        // Actual auction loop is on a separate timer.
        if (!(m_loopcount % 1200))
            (*itr)->updateAuctions();
    }
}
