/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "TicketMgr.hpp"

#include "Logging/Logger.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Server/DatabaseDefinition.hpp"
#include "Server/Packets/CmsgGmTicketCreate.h"
#include "Storage/MySQLDataStore.hpp"
#include "Storage/WDB/WDBStores.hpp"

GM_Ticket::GM_Ticket(Player const* player, AscEmu::Packets::CmsgGmTicketCreate const& srlPacket) :
    deleted(false), assignedToPlayer(0), comment("")
{
    guid = static_cast<uint64_t>(sTicketMgr.generateNextTicketId());
    playerGuid = player->getGuid();
    name = player->getName();
    level = player->getLevel();
    map = srlPacket.map;
    posX = srlPacket.location.x;
    posY = srlPacket.location.y;
    posZ = srlPacket.location.z;
    message = srlPacket.message;
    timestamp = static_cast<uint32_t>(UNIXTIME);
}

GM_Ticket::GM_Ticket(Field const* fields)
{
    guid = fields[0].asUint64();
    playerGuid = fields[1].asUint64();
    name = fields[2].asCString();
    level = fields[3].asUint32();
    map = fields[4].asUint32();
    posX = fields[5].asFloat();
    posY = fields[6].asFloat();
    posZ = fields[7].asFloat();
    message = fields[8].asCString();
    timestamp = fields[9].asUint32();
    deleted = fields[10].asUint32() == 1;
    assignedToPlayer = fields[11].asUint64();
    comment = fields[12].asCString();
}

TicketMgr& TicketMgr::getInstance()
{
    static TicketMgr mInstance;
    return mInstance;
}

void TicketMgr::initialize()
{
    m_nextTicketId = 0;

    auto stmt = CharacterDatabase.CreateStatement(CHAR_GM_TICKET_MAX_ID);
    auto result = CharacterDatabase.QueryStatement(std::move(stmt));

    if (result)
    {
        m_nextTicketId = result->Fetch()[0].asUint32();
    }

    sLogger.info("TicketMgr : HighGuid(TICKET) = {}", m_nextTicketId);
}
void TicketMgr::finalize()
{
    sLogger.info("TicketMgr : Deleting GM Tickets...");
    m_ticketList.clear();
}

uint32_t TicketMgr::generateNextTicketId()
{
    return ++m_nextTicketId;
}

GM_Ticket* TicketMgr::createGMTicket(Player const* player, AscEmu::Packets::CmsgGmTicketCreate const& srlPacket)
{
    auto* ticket = m_ticketList.emplace_back(std::make_unique<GM_Ticket>(player, srlPacket)).get();
    saveGMTicket(ticket);
    return ticket;
}

GM_Ticket* TicketMgr::createGMTicket(Field const* fields)
{
    return m_ticketList.emplace_back(std::make_unique<GM_Ticket>(fields)).get();
}

void TicketMgr::loadGMTickets()
{
    auto stmt = CharacterDatabase.CreateStatement(CHAR_GM_TICKET_SELECT_ALL);
    auto result = CharacterDatabase.QueryStatement(std::move(stmt));

    if (!result)
    {
        sLogger.info("TicketMgr : 0 active GM Tickets loaded.");
        return;
    }

    do
    {
        createGMTicket(result->Fetch());

    } while (result->NextRow());

    sLogger.info("ObjectMgr : {} active GM Tickets loaded.", result->GetRowCount());
}

void TicketMgr::saveGMTicket(GM_Ticket* ticket)
{
    auto stmt = CharacterDatabase.CreateStatement(CHAR_GM_TICKET_REPLACE);

    stmt->Bind(0, ticket->guid);
    stmt->Bind(1, ticket->playerGuid);
    stmt->Bind(2, ticket->name);
    stmt->Bind(3, ticket->level);
    stmt->Bind(4, ticket->map);
    stmt->Bind(5, ticket->posX);
    stmt->Bind(6, ticket->posY);
    stmt->Bind(7, ticket->posZ);
    stmt->Bind(8, ticket->message);
    stmt->Bind(9, ticket->timestamp);
    stmt->Bind(10, static_cast<uint32_t>(ticket->deleted ? 1 : 0));
    stmt->Bind(11, ticket->assignedToPlayer);
    stmt->Bind(12, ticket->comment);

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

void TicketMgr::updateGMTicket(GM_Ticket* ticket)
{
    saveGMTicket(ticket);
}

void TicketMgr::deleteGMTicketPermanently(uint64_t ticketGuid)
{
    std::erase_if(m_ticketList, [ticketGuid](const auto& t) { return t->guid == ticketGuid; });

    auto stmt = CharacterDatabase.CreateStatement(CHAR_GM_TICKET_DELETE_BY_ID);
    stmt->Bind(0, ticketGuid);

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

void TicketMgr::deleteAllRemovedGMTickets()
{
    std::erase_if(m_ticketList, [](const auto& ticket) { return ticket->deleted; });

    auto stmt = CharacterDatabase.CreateStatement(CHAR_GM_TICKET_DELETE_DELETED);

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

void TicketMgr::removeGMTicketByPlayer(uint64_t playerGuid)
{
    for (GmTicketList::iterator i = m_ticketList.begin(); i != m_ticketList.end(); ++i)
    {
        if ((*i)->playerGuid == playerGuid && !(*i)->deleted)
        {
            (*i)->deleted = true;
            saveGMTicket((*i).get());
            break;
        }
    }
}

void TicketMgr::removeGMTicket(uint64_t ticketGuid)
{
    for (GmTicketList::iterator i = m_ticketList.begin(); i != m_ticketList.end(); ++i)
    {
        if ((*i)->guid == ticketGuid && !(*i)->deleted)
        {
            (*i)->deleted = true;
            saveGMTicket((*i).get());
            break;
        }
    }
}

void TicketMgr::closeTicket(uint64_t ticketGuid)
{
    for (GmTicketList::iterator i = m_ticketList.begin(); i != m_ticketList.end(); ++i)
    {
        if ((*i)->guid == ticketGuid && !(*i)->deleted)
        {
            (*i)->deleted = true;
            break;
        }
    }
}

GM_Ticket* TicketMgr::getGMTicketByPlayer(uint64_t playerGuid)
{
    for (GmTicketList::iterator i = m_ticketList.begin(); i != m_ticketList.end(); ++i)
    {
        if ((*i)->playerGuid == playerGuid && !(*i)->deleted)
        {
            return (*i).get();
        }
    }
    return nullptr;
}

GM_Ticket* TicketMgr::getGMTicket(uint64_t ticketGuid)
{
    for (GmTicketList::iterator i = m_ticketList.begin(); i != m_ticketList.end(); ++i)
    {
        if ((*i)->guid == ticketGuid)
        {
            return (*i).get();
        }
    }
    return nullptr;
}
