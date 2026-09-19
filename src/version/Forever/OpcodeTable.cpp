/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "version/Forever/OpcodeTable.hpp"

namespace AscEmu::Version::Forever
{
    namespace
    {
        const std::vector<OpcodeEntry> OpcodeStore =
        {
#define CMSG_ENTRY(name, value) { Opcode::name, value, #name, OpcodeDirection::Client }
#define SMSG_ENTRY(name, value) { Opcode::name, value, #name, OpcodeDirection::Server }
            CMSG_ENTRY(CMSG_PING, 0x00450006),
            CMSG_ENTRY(CMSG_ENUM_CHARACTERS, 0x00440014),
            CMSG_ENTRY(CMSG_CHECK_CHARACTER_NAME_AVAILABILITY, 0x00440071),
            CMSG_ENTRY(CMSG_CREATE_CHARACTER, 0x00440070),
            CMSG_ENTRY(CMSG_PLAYER_LOGIN, 0x00440016),
            CMSG_ENTRY(CMSG_CHAR_DELETE, 0x004400CB),
            CMSG_ENTRY(CMSG_DB_QUERY_BULK, 0x00440010),
            CMSG_ENTRY(CMSG_HOTFIX_REQUEST, 0x00440011),
            CMSG_ENTRY(CMSG_GET_UNDELETE_CHARACTER_COOLDOWN_STATUS, 0x0044010F),
            CMSG_ENTRY(CMSG_BATTLE_PAY_GET_PURCHASE_LIST, 0x004400EA),
            CMSG_ENTRY(CMSG_BATTLE_PAY_GET_PRODUCT_LIST, 0x004400E9),
            CMSG_ENTRY(CMSG_UPDATE_VAS_PURCHASE_STATES, 0x00440123),
            CMSG_ENTRY(CMSG_QUICK_JOIN_AUTO_ACCEPT_REQUESTS, 0x00440132),
            CMSG_ENTRY(CMSG_GET_LAST_CATALOG_FETCH, 0x002B0036),
            CMSG_ENTRY(CMSG_SOCIAL_CONTRACT_REQUEST, 0x00440176),
            CMSG_ENTRY(CMSG_SOCIAL_CONTRACT_ACCEPT, 0x00440177),
            CMSG_ENTRY(CMSG_SERVER_TIME_OFFSET_REQUEST, 0x004400CA),
            CMSG_ENTRY(CMSG_CHARACTER_SELECT_GATE_ACK, 0x0044013A),
            CMSG_ENTRY(CMSG_CHARACTER_LIST_ACK, 0x00440197),
            CMSG_ENTRY(CMSG_UPDATE_ACCOUNT_DATA, 0x004400C4),

            SMSG_ENTRY(SMSG_PONG, 0x004D0009),
            SMSG_ENTRY(SMSG_ENUM_CHARACTERS_RESULT, 0x00460018),
            SMSG_ENTRY(SMSG_CHARACTER_LIST_STATE, 0x00460019),
            SMSG_ENTRY(SMSG_CHECK_CHARACTER_NAME_AVAILABILITY_RESULT, 0x0046001B),
            SMSG_ENTRY(SMSG_CREATE_CHAR, 0x004601AB),
            SMSG_ENTRY(SMSG_DELETE_CHAR, 0x004601AC),
            SMSG_ENTRY(SMSG_DB_REPLY, 0x004A0000),
            SMSG_ENTRY(SMSG_AVAILABLE_HOTFIXES, 0x004A0001),
            SMSG_ENTRY(SMSG_CACHE_VERSION, 0x004A000E),
            SMSG_ENTRY(SMSG_UNDELETE_COOLDOWN_STATUS_RESPONSE, 0x00460276),
            SMSG_ENTRY(SMSG_SOCIAL_CONTRACT_REQUEST_RESPONSE, 0x00460325),
            SMSG_ENTRY(SMSG_SERVER_TIME_OFFSET, 0x004601BF),
            SMSG_ENTRY(SMSG_ACCOUNT_ITEM_COLLECTION_DATA, 0x00460362),
            SMSG_ENTRY(SMSG_UPDATE_ACCOUNT_DATA_COMPLETE, 0x004601B4),
            SMSG_ENTRY(SMSG_ACCOUNT_DATA_TIMES, 0x004601B5),
            SMSG_ENTRY(SMSG_FEATURE_SYSTEM_STATUS, 0x00460063),
            SMSG_ENTRY(SMSG_SET_TIME_ZONE_INFORMATION, 0x00460123),
            SMSG_ENTRY(SMSG_LOGIN_VERIFY_WORLD, 0x0046002F),
            SMSG_ENTRY(SMSG_LOGIN_SET_TIME_SPEED, 0x004601B8),
            SMSG_ENTRY(SMSG_CHARACTER_ENUM_PRELUDE, 0x0046021D),
            SMSG_ENTRY(SMSG_CHARACTER_ENUM_PRELUDE_EXTENDED, 0x0046021C),
            SMSG_ENTRY(SMSG_CHARACTER_SELECT_STATUS, 0x0046029D),
            SMSG_ENTRY(SMSG_CHARACTER_SELECT_GATE, 0x00460382)
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
