/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <cstdint>

namespace AscEmu::Version::Midnight
{
    // Internal opcode ids. Raw build-specific values live in OpcodeTable.cpp,
    // just like AscEmu's legacy multi-version opcode table separates semantic ids
    // from the wire opcode used by a concrete client build.
    enum class Opcode : uint16_t
    {
        NONE = 0,

        CMSG_AUTH_SESSION,
        CMSG_ENTER_ENCRYPTED_MODE_ACK,
        CMSG_PING,
        CMSG_ENUM_CHARACTERS,
        CMSG_DB_QUERY_BULK,
        CMSG_CREATE_CHARACTER,
        CMSG_CHECK_CHARACTER_NAME_AVAILABILITY,
        CMSG_KEEP_ALIVE,
        CMSG_SUSPEND_COMMS_ACK,
        CMSG_AUTH_CONTINUED_SESSION,
        CMSG_ENABLE_NAGLE,
        CMSG_QUEUED_MESSAGES_END,
        CMSG_LOG_DISCONNECT,
        CMSG_BATTLE_PAY_GET_PRODUCT_LIST,
        CMSG_BATTLE_PAY_GET_PURCHASE_LIST,
        CMSG_GET_UNDELETE_CHARACTER_COOLDOWN_STATUS,
        CMSG_UPDATE_VAS_PURCHASE_STATES,
        CMSG_BATTLENET_REQUEST,
        CMSG_QUICK_JOIN_AUTO_ACCEPT_REQUESTS,
        CMSG_SOCIAL_CONTRACT_REQUEST,
        CMSG_FETCH_BLEEP_PROXIES,
        CMSG_SERVER_TIME_OFFSET_REQUEST,
        CMSG_GET_LAST_CATALOG_FETCH,

        SMSG_AUTH_CHALLENGE,
        SMSG_ENTER_ENCRYPTED_MODE,
        SMSG_PONG,
        SMSG_AUTH_FAILED,
        SMSG_AUTH_RESPONSE,
        SMSG_ENUM_CHARACTERS_RESULT,
        SMSG_CHECK_CHARACTER_NAME_AVAILABILITY_RESULT,
        SMSG_CREATE_CHAR,
        SMSG_UNDELETE_COOLDOWN_STATUS_RESPONSE,
        SMSG_SERVER_TIME_OFFSET,
        SMSG_BATTLE_NET_CONNECTION_STATUS,
        SMSG_SOCIAL_CONTRACT_REQUEST_RESPONSE,
        SMSG_ACCOUNT_ITEM_COLLECTION_DATA,
        SMSG_DB_REPLY,
        SMSG_AVAILABLE_HOTFIXES,
        SMSG_CACHE_VERSION,
        SMSG_SET_TIME_ZONE_INFORMATION,
        SMSG_FEATURE_SYSTEM_STATUS_GLUE_SCREEN,
        SMSG_ACCOUNT_DATA_TIMES,
        SMSG_TUTORIAL_FLAGS
    };
}
