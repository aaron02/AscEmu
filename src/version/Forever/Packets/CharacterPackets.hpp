#pragma once

#include "Network/ByteBuffer.hpp"
#include "world/Server/CharacterErrors.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace AscEmu::Version::Forever::Packets
{
    struct CharacterCustomizationChoice
    {
        uint32_t optionId{0};
        uint32_t choiceId{0};
    };

    struct CreateCharacterRequest
    {
        uint8_t race{0};
        uint8_t charClass{0};
        uint8_t sex{0};
        uint32_t customizationCount{0};
        uint32_t timerunningSeasonId{0};
        uint32_t templateSet{0};
        std::string name;
        std::vector<CharacterCustomizationChoice> customizations;
    };

    struct CheckCharacterNameRequest
    {
        uint32_t sequenceIndex{0};
        std::string name;
    };

    bool parseCreateCharacter(const uint8_t* payload, size_t payloadSize, CreateCharacterRequest& request);
    bool parseCheckCharacterName(const uint8_t* payload, size_t payloadSize, CheckCharacterNameRequest& request);

    uint32_t toCharacterResult(CharacterErrorCodes code);
    uint32_t toDeleteCharacterResult(CharacterErrorCodes code);
    ByteBuffer buildCreateCharacterResponse(uint32_t result, uint32_t realmId, uint64_t characterGuid);
}
