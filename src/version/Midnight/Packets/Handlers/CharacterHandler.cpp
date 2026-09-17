/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "version/Midnight/Packets/CharacterPackets.hpp"
#include "world/Server/WorldSocket.hpp"
#include "world/Server/WorldSession.h"
#include "version/Midnight/World/CharacterService.hpp"
#include "world/Server/CharacterErrors.h"
#include "world/Management/ObjectMgr.hpp"
#include "world/Storage/MySQLDataStore.hpp"
#include "Logging/Logger.hpp"

namespace
{
    uint32_t toMidnightNameCheckResult(CharacterErrorCodes code)
    {
        switch (code)
        {
            case E_CHAR_NAME_SUCCESS: return 0U;
            case E_CHAR_NAME_FAILURE: return 97U;
            case E_CHAR_NAME_NO_NAME: return 98U;
            case E_CHAR_NAME_TOO_SHORT: return 99U;
            case E_CHAR_NAME_TOO_LONG: return 100U;
            case E_CHAR_NAME_INVALID_CHARACTER: return 101U;
            case E_CHAR_NAME_MIXED_LANGUAGES: return 102U;
            case E_CHAR_NAME_PROFANE: return 103U;
            case E_CHAR_NAME_RESERVED: return 104U;
            case E_CHAR_NAME_INVALID_APOSTROPHE: return 105U;
            case E_CHAR_NAME_MULTIPLE_APOSTROPHES: return 106U;
            case E_CHAR_NAME_THREE_CONSECUTIVE: return 107U;
            case E_CHAR_NAME_INVALID_SPACE: return 108U;
            case E_CHAR_NAME_CONSECUTIVE_SPACES: return 109U;
            case E_CHAR_NAME_RUSSIAN_CONSECUTIVE_SILENT_CHARACTERS: return 110U;
            case E_CHAR_NAME_RUSSIAN_SILENT_CHARACTER_AT_BEGINNING_OR_END: return 111U;
            case E_CHAR_NAME_DECLENSION_DOESNT_MATCH_BASE_NAME: return 112U;
            default: return 97U;
        }
    }
}

bool WorldSocket::handleMidnightCharEnumOpcode(AscEmu::Version::Midnight::Packets::Packet& packet)
{
    if (packet.remaining() != 0)
    {
        sLogger.warning("WorldSocket::Midnight: CMSG_ENUM_CHARACTERS expected empty payload, got {} byte(s).", packet.remaining());
        return true;
    }

    if (m_session == nullptr)
    {
        sLogger.warning("WorldSocket::Midnight: CMSG_ENUM_CHARACTERS received without WorldSession.");
        return true;
    }

    AscEmu::Version::Midnight::CharacterService::instance().requestCharacterEnum(*this);
    return true;
}

bool WorldSocket::handleMidnightCheckCharacterNameOpcode(AscEmu::Version::Midnight::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Midnight::Packets;

    CmsgCheckCharacterNameAvailability request;
    if (!request.deserialise(packet))
    {
        sLogger.warning("WorldSocket::Midnight: malformed CMSG_CHECK_CHARACTER_NAME_AVAILABILITY.");
        return true;
    }

    const CharacterErrorCodes validation = VerifyName(request.name);
    uint32_t result = toMidnightNameCheckResult(validation);

    if (validation == E_CHAR_NAME_SUCCESS)
    {
        if (!sMySQLStore.isCharacterNameAllowed(request.name))
            result = 103U;
        else if (sObjectMgr.getCachedCharacterInfoByName(request.name) != nullptr)
            result = 27U;
    }

    SmsgCheckCharacterNameAvailabilityResult response(request.sequenceIndex, result);
    auto wire = response.serialise();
    return wire != nullptr && sendMidnightPacket(*wire);
}

bool WorldSocket::handleMidnightCharCreateOpcode(AscEmu::Version::Midnight::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Midnight::Packets;

    CmsgCreateCharacter request;
    if (!request.deserialise(packet))
    {
        sLogger.warning("WorldSocket::Midnight: malformed CMSG_CREATE_CHARACTER.");
        return true;
    }

    sLogger.info("WorldSocket::Midnight: create character name='{}' race={} class={} sex={} customizations={}.",
        request.name, request.race, request.charClass, request.sex, request.customizationCount);

    return AscEmu::Version::Midnight::CharacterService::instance().createCharacter(*this, request);
}

bool WorldSocket::handleMidnightUndeleteCooldownOpcode(AscEmu::Version::Midnight::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Midnight::Packets;

    if (packet.remaining() != 0)
    {
        sLogger.warning("WorldSocket::Midnight: CMSG_GET_UNDELETE_CHARACTER_COOLDOWN_STATUS expected empty payload.");
        return true;
    }

    SmsgUndeleteCooldownStatusResponse response;
    auto wire = response.serialise();
    return wire != nullptr && sendMidnightPacket(*wire);
}
