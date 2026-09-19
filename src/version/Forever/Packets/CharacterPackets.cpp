#include "version/Forever/Packets/CharacterPackets.hpp"
#include "version/Forever/Defines/ObjectGuid.hpp"

#include <algorithm>
#include <cstring>

namespace AscEmu::Version::Forever::Packets
{
    namespace
    {
        uint32_t readUInt32LE(const uint8_t* data)
        {
            uint32_t value = 0;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }
    }

    bool parseCreateCharacter(const uint8_t* payload, size_t payloadSize, CreateCharacterRequest& request)
    {
        constexpr size_t FixedPrefix = 18U;
        if (payload == nullptr || payloadSize < FixedPrefix)
            return false;

        request.race = payload[3];
        request.charClass = payload[4];
        request.sex = payload[5];
        request.customizationCount = readUInt32LE(payload + 6);
        request.timerunningSeasonId = readUInt32LE(payload + 10);
        request.templateSet = readUInt32LE(payload + 14);

        if (request.customizationCount > 250U)
            return false;

        const size_t customizationBytes = static_cast<size_t>(request.customizationCount) * 8U;
        if (payloadSize < FixedPrefix + customizationBytes)
            return false;

        const size_t nameLength = payloadSize - FixedPrefix - customizationBytes;
        if (nameLength == 0U || nameLength > 63U)
            return false;

        request.name.assign(reinterpret_cast<const char*>(payload + FixedPrefix), nameLength);

        request.customizations.clear();
        request.customizations.reserve(request.customizationCount);

        const uint8_t* customizationData = payload + FixedPrefix + nameLength;
        for (uint32_t i = 0; i < request.customizationCount; ++i)
        {
            CharacterCustomizationChoice customization;
            customization.optionId = readUInt32LE(customizationData + static_cast<size_t>(i) * 8U);
            customization.choiceId = readUInt32LE(customizationData + static_cast<size_t>(i) * 8U + 4U);
            request.customizations.emplace_back(customization);
        }

        std::sort(request.customizations.begin(), request.customizations.end(), [](const CharacterCustomizationChoice& left, const CharacterCustomizationChoice& right) { return left.optionId < right.optionId; });

        return true;
    }

    bool parseCheckCharacterName(const uint8_t* payload, size_t payloadSize, CheckCharacterNameRequest& request)
    {
        if (payload == nullptr || payloadSize < 6U)
            return false;

        ByteBuffer packet(payloadSize);
        packet.append(payload, payloadSize);
        packet >> request.sequenceIndex;

        const uint32_t nameLength = packet.readBits(6);
        if (nameLength == 0U || nameLength > 63U || packet.rpos() + nameLength > packet.size())
            return false;

        request.name = packet.readString(nameLength);
        return true;
    }

    uint32_t toCharacterResult(CharacterErrorCodes code)
    {
        switch (code)
        {
            case E_CHAR_NAME_SUCCESS: return 0U;
            case E_CHAR_CREATE_SUCCESS: return 24U;
            case E_CHAR_CREATE_ERROR: return 25U;
            case E_CHAR_CREATE_FAILED: return 26U;
            case E_CHAR_CREATE_NAME_IN_USE: return 27U;
            case E_CHAR_CREATE_DISABLED: return 28U;
            case E_CHAR_CREATE_PVP_TEAMS_VIOLATION: return 29U;
            case E_CHAR_CREATE_SERVER_LIMIT: return 30U;
            case E_CHAR_CREATE_ACCOUNT_LIMIT: return 31U;
            case E_CHAR_CREATE_SERVER_QUEUE: return 32U;
            case E_CHAR_CREATE_ONLY_EXISTING: return 33U;
            case E_CHAR_CREATE_EXPANSION: return 34U;
            case E_CHAR_CREATE_EXPANSION_CLASS: return 35U;
            case E_CHAR_CREATE_CHARACTER_IN_GUILD: return 36U;
            case E_CHAR_CREATE_RESTRICTED_RACECLASS: return 37U;
            case E_CHAR_CREATE_CHARACTER_CHOOSE_RACE: return 38U;
            case E_CHAR_CREATE_CHARACTER_ARENA_LEADER: return 39U;
            case E_CHAR_CREATE_CHARACTER_DELETE_MAIL: return 41U;
            case E_CHAR_CREATE_CHARACTER_SWAP_FACTION: return 42U;
            case E_CHAR_CREATE_CHARACTER_RACE_ONLY: return 43U;
            case E_CHAR_CREATE_CHARACTER_GOLD_LIMIT: return 44U;
            case E_CHAR_CREATE_FORCE_LOGIN: return 45U;
            case E_CHAR_CREATE_TRIAL: return 46U;
            case E_CHAR_CREATE_UNIQUE_CLASS_LIMIT: return 56U;
            case E_CHAR_CREATE_LEVEL_REQUIREMENT: return 57U;
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
            default: return 25U;
        }
    }

    uint32_t toDeleteCharacterResult(CharacterErrorCodes code)
    {
        switch (code)
        {
            case E_CHAR_DELETE_SUCCESS: return 67U;
            case E_CHAR_DELETE_FAILED_GUILD_LEADER: return 70U;
            case E_CHAR_DELETE_FAILED_ARENA_CAPTAIN: return 71U;
            default: return 68U;
        }
    }

    ByteBuffer buildCreateCharacterResponse(uint32_t result, uint32_t realmId, uint64_t characterGuid)
    {
        ByteBuffer packet;
        packet << result;

        const auto packedGuid = ObjectGuid::createPlayer(realmId, characterGuid).pack();
        packet.append(packedGuid.data(), packedGuid.size());
        return packet;
    }
}
