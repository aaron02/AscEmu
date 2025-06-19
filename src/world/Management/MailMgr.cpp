/**
 * AscEmu Framework based on ArcEmu MMORPG Server
 * Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "MailMgr.h"

#include <sstream>

#include "Logging/Log.hpp"
#include "Management/ObjectMgr.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Server/DatabaseDefinition.hpp"
#include "Server/Packets/SmsgReceivedMail.h"
#include "CommonTime.hpp"

MailSystem& MailSystem::getInstance()
{
    static MailSystem mInstance;
    return mInstance;
}

/// \todo refactoring
void MailSystem::StartMailSystem()
{}

MailError MailSystem::DeliverMessage(uint64_t recipent, MailMessage* message)
{
    // assign a new id
    message->message_id = sObjectMgr.generateMailId();

    Player* plr = sObjectMgr.getPlayer((uint32_t)recipent);
    if (plr != NULL)
    {
        plr->m_mailBox->AddMessage(message);
        if ((uint32_t)UNIXTIME >= message->delivery_time)
            plr->sendPacket(AscEmu::Packets::SmsgReceivedMail().serialise().get());
    }

    SaveMessageToSQL(message);
    return MAIL_OK;
}

void Mailbox::AddMessage(MailMessage* Message)
{
    Messages[Message->message_id] = *Message;
}

void Mailbox::DeleteMessage(uint32_t MessageId, bool sql)
{
    Messages.erase(MessageId);

    if (sql)
    {
        auto stmt = CharacterDatabase.CreateStatement(CHARACTER_MAILBOX_DELETE);
        stmt->Bind(0, MessageId);

        CharacterDatabase.ExecuteStatement(std::move(stmt));
    }
}

void Mailbox::CleanupExpiredMessages()
{
    MessageMap::iterator itr, it2;
    uint32_t curtime = (uint32_t)UNIXTIME;

    for (itr = Messages.begin(); itr != Messages.end();)
    {
        it2 = itr++;
        if (it2->second.expire_time && it2->second.expire_time < curtime)
        {
            Messages.erase(it2);
        }
    }
}

void MailSystem::SaveMessageToSQL(MailMessage* message)
{
    {
        auto stmtDel = CharacterDatabase.CreateStatement(CHARACTER_MAILBOX_DELETE_BY_ID);
        stmtDel->Bind(0, message->message_id);
        CharacterDatabase.ExecuteStatement(std::move(stmtDel));
    }

    std::ostringstream itemStream;
    for (auto itemId : message->items)
        itemStream << itemId << ",";
    std::string serializedItems = itemStream.str();

    auto stmt = CharacterDatabase.CreateStatement(CHARACTER_MAILBOX_INSERT);
    stmt->Bind(0, message->message_id);
    stmt->Bind(1, message->message_type);
    stmt->Bind(2, message->player_guid);
    stmt->Bind(3, message->sender_guid);
    stmt->Bind(4, message->subject);
    stmt->Bind(5, message->body);
    stmt->Bind(6, serializedItems);
    stmt->Bind(7, message->cod);
    stmt->Bind(8, message->stationery);
    stmt->Bind(9, message->expire_time);
    stmt->Bind(10, message->delivery_time);
    stmt->Bind(11, message->checked_flag);
    stmt->Bind(12, message->deleted_flag);

    CharacterDatabase.ExecuteStatement(std::move(stmt));
}

void MailSystem::RemoveMessageIfDeleted(uint32_t message_id, Player* plr)
{
    MailMessage* msg = plr->m_mailBox->GetMessageById(message_id);
    if (msg == 0) return;

    if (msg->deleted_flag)   // we've deleted from inbox
        plr->m_mailBox->DeleteMessage(message_id, true);   // wipe the message
}

void MailSystem::SendAutomatedMessage(uint32_t type, uint64_t sender, uint64_t receiver, std::string subject, std::string body,
                                      uint32_t money, uint32_t cod, std::vector<uint64_t> &item_guids, uint32_t stationery, MailCheckMask checked, uint32_t deliverdelay)
{
    // This is for sending automated messages, for example from an auction house.
    MailMessage msg;
    msg.message_type = type;
    msg.sender_guid = sender;
    msg.player_guid = receiver;
    msg.subject = subject;
    msg.body = body;
    msg.money = money;
    msg.cod = cod;
    for (std::vector<uint64_t>::iterator itr = item_guids.begin(); itr != item_guids.end(); ++itr)
        msg.items.push_back(WoWGuid::getGuidLowPartFromUInt64(*itr));

    msg.stationery = stationery;
    msg.delivery_time = (uint32_t)UNIXTIME + deliverdelay;

    // 30 days expiration time for unread mail + possible delivery delay.
    if (!sMailSystem.MailOption(MAIL_FLAG_NO_EXPIRY))
        msg.expire_time = (uint32_t)UNIXTIME + deliverdelay + (TimeVars::Day * MAIL_DEFAULT_EXPIRATION_TIME);
    else
        msg.expire_time = 0;

    msg.deleted_flag = false;
    msg.checked_flag = checked;

    // Send the message.
    DeliverMessage(receiver, &msg);
}

//overload to keep backward compatibility (passing just 1 item guid instead of a vector)
void MailSystem::SendAutomatedMessage(uint32_t type, uint64_t sender, uint64_t receiver, std::string subject, std::string body, uint32_t money,
                                      uint32_t cod, uint64_t item_guid, uint32_t stationery, MailCheckMask checked, uint32_t deliverdelay)
{
    std::vector<uint64_t> item_guids;
    if (item_guid != 0)
        item_guids.push_back(item_guid);
    SendAutomatedMessage(type, sender, receiver, subject, body, money, cod, item_guids, stationery, checked, deliverdelay);
}

void MailSystem::SendCreatureGameobjectMail(uint32_t type, uint32_t sender, uint64_t receiver, std::string subject, std::string body, uint32_t money,
                                      uint32_t cod, uint64_t item_guid, uint32_t stationery, MailCheckMask checked, uint32_t deliverdelay)
{
    std::vector<uint64_t> item_guids;
    if (item_guid != 0)
        item_guids.push_back(item_guid);
    SendAutomatedMessage(type, sender, receiver, subject, body, money, cod, item_guids, stationery, checked, deliverdelay);
}

void Mailbox::Load(QueryResult* result)
{
    if (!result)
        return;

    Field* fields;
    MailMessage msg;
    uint32_t i;
    char* str;
    char* p;
    uint32_t itemguid;
    uint32_t now = (uint32_t)UNIXTIME;

    do
    {
        fields = result->Fetch();
        uint32_t expiry_time = fields[10].asUint32();

        // Do not load expired mails!
        if (expiry_time < now)
            continue;

        // Create message struct
        i = 0;
        msg.items.clear();
        msg.message_id = fields[i++].asUint32();
        msg.message_type = fields[i++].asUint32();
        msg.player_guid = fields[i++].asUint32();
        msg.sender_guid = fields[i++].asUint32();
        msg.subject = fields[i++].asString();
        msg.body = fields[i++].asString();
        msg.money = fields[i++].asUint32();

        std::string str = fields[i++].asString();

        std::stringstream ss(str);
        std::string token;

        while (std::getline(ss, token, ','))
        {
            int itemguid = std::atoi(token.c_str());
            if (itemguid != 0)
                msg.items.push_back(itemguid);
        }

        msg.cod = fields[i++].asUint32();
        msg.stationery = fields[i++].asUint32();
        msg.expire_time = fields[i++].asUint32();
        msg.delivery_time = fields[i++].asUint32();
        msg.checked_flag = fields[i++].asUint32();
        msg.deleted_flag = fields[i++].asBool();

        // Add to the mailbox
        AddMessage(&msg);

    }
    while (result->NextRow());
}
