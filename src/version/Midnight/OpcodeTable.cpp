/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "version/Midnight/OpcodeTable.hpp"

namespace AscEmu::Version::Midnight
{
    namespace
    {
        const std::vector<OpcodeEntry> OpcodeStore =
        {
#define CMSG_ENTRY(name, value) { Opcode::name, value, #name, OpcodeDirection::Client }
#define SMSG_ENTRY(name, value) { Opcode::name, value, #name, OpcodeDirection::Server }
            CMSG_ENTRY(CMSG_AUTH_SESSION, 0x00440001),
            CMSG_ENTRY(CMSG_ENTER_ENCRYPTED_MODE_ACK, 0x00440005),
            CMSG_ENTRY(CMSG_PING, 0x00440006),
            CMSG_ENTRY(CMSG_ENUM_CHARACTERS, 0x00430014),
            CMSG_ENTRY(CMSG_DB_QUERY_BULK, 0x00430010),
            CMSG_ENTRY(CMSG_CREATE_CHARACTER, 0x00430070),
            CMSG_ENTRY(CMSG_CHECK_CHARACTER_NAME_AVAILABILITY, 0x00430071),
            CMSG_ENTRY(CMSG_KEEP_ALIVE, 0x004300AB),
            CMSG_ENTRY(CMSG_SUSPEND_COMMS_ACK, 0x00440000),
            CMSG_ENTRY(CMSG_AUTH_CONTINUED_SESSION, 0x00440003),
            CMSG_ENTRY(CMSG_ENABLE_NAGLE, 0x00440009),
            CMSG_ENTRY(CMSG_QUEUED_MESSAGES_END, 0x0044000A),
            CMSG_ENTRY(CMSG_LOG_DISCONNECT, 0x00440007),
            CMSG_ENTRY(CMSG_BATTLE_PAY_GET_PRODUCT_LIST, 0x004300E9),
            CMSG_ENTRY(CMSG_BATTLE_PAY_GET_PURCHASE_LIST, 0x004300EA),
            CMSG_ENTRY(CMSG_GET_UNDELETE_CHARACTER_COOLDOWN_STATUS, 0x0043010F),
            CMSG_ENTRY(CMSG_UPDATE_VAS_PURCHASE_STATES, 0x00430123),
            CMSG_ENTRY(CMSG_BATTLENET_REQUEST, 0x00430124),
            CMSG_ENTRY(CMSG_QUICK_JOIN_AUTO_ACCEPT_REQUESTS, 0x00430132),
            CMSG_ENTRY(CMSG_SOCIAL_CONTRACT_REQUEST, 0x00430176),
            CMSG_ENTRY(CMSG_FETCH_BLEEP_PROXIES, 0x004301A1),
            CMSG_ENTRY(CMSG_SERVER_TIME_OFFSET_REQUEST, 0x004300CA),
            CMSG_ENTRY(CMSG_GET_LAST_CATALOG_FETCH, 0x002A0036),

            SMSG_ENTRY(SMSG_AUTH_CHALLENGE, 0x004C0000),
            SMSG_ENTRY(SMSG_ENTER_ENCRYPTED_MODE, 0x004C0004),
            SMSG_ENTRY(SMSG_PONG, 0x004C0009),
            SMSG_ENTRY(SMSG_AUTH_FAILED, 0x00450000),
            SMSG_ENTRY(SMSG_AUTH_RESPONSE, 0x00450001),
            SMSG_ENTRY(SMSG_ENUM_CHARACTERS_RESULT, 0x00450018),
            SMSG_ENTRY(SMSG_CHECK_CHARACTER_NAME_AVAILABILITY_RESULT, 0x0045001B),
            SMSG_ENTRY(SMSG_CREATE_CHAR, 0x004501AC),
            SMSG_ENTRY(SMSG_UNDELETE_COOLDOWN_STATUS_RESPONSE, 0x00450276),
            SMSG_ENTRY(SMSG_SERVER_TIME_OFFSET, 0x004501C0),
            SMSG_ENTRY(SMSG_BATTLE_NET_CONNECTION_STATUS, 0x004502B1),
            SMSG_ENTRY(SMSG_SOCIAL_CONTRACT_REQUEST_RESPONSE, 0x00450325),
            SMSG_ENTRY(SMSG_ACCOUNT_ITEM_COLLECTION_DATA, 0x00450360),
            SMSG_ENTRY(SMSG_DB_REPLY, 0x00490000),
            SMSG_ENTRY(SMSG_AVAILABLE_HOTFIXES, 0x00490001),
            SMSG_ENTRY(SMSG_CACHE_VERSION, 0x0049000E),
            SMSG_ENTRY(SMSG_SET_TIME_ZONE_INFORMATION, 0x00450123),
            SMSG_ENTRY(SMSG_FEATURE_SYSTEM_STATUS_GLUE_SCREEN, 0x00450064),
            SMSG_ENTRY(SMSG_ACCOUNT_DATA_TIMES, 0x004501B6),
            SMSG_ENTRY(SMSG_TUTORIAL_FLAGS, 0x00450268)
#undef CMSG_ENTRY
#undef SMSG_ENTRY
        };
    }

    OpcodeTable& OpcodeTable::instance()
    {
        static OpcodeTable instance;
        return instance;
    }

    Opcode OpcodeTable::getInternalIdForHex(uint32_t rawOpcode) const
    {
        for (const auto& entry : OpcodeStore)
            if (entry.rawOpcode == rawOpcode && entry.direction == OpcodeDirection::Client)
                return entry.id;
        return Opcode::NONE;
    }

    uint32_t OpcodeTable::getHexValueForInternalId(Opcode opcode) const
    {
        for (const auto& entry : OpcodeStore)
            if (entry.id == opcode)
                return entry.rawOpcode;
        return 0;
    }

    std::string_view OpcodeTable::getNameForOpcode(uint32_t rawOpcode) const
    {
        for (const auto& entry : OpcodeStore)
            if (entry.rawOpcode == rawOpcode)
                return entry.name;
        return "UNKNOWN_OPCODE";
    }

    std::string_view OpcodeTable::getNameForInternalId(Opcode opcode) const
    {
        for (const auto& entry : OpcodeStore)
            if (entry.id == opcode)
                return entry.name;
        return "UNKNOWN_OPCODE";
    }

    const std::vector<OpcodeEntry>& OpcodeTable::entries() const
    {
        return OpcodeStore;
    }
}
