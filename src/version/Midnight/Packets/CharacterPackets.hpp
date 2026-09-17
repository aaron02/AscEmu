#pragma once

#include "version/Midnight/Packets/ManagedPacket.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace AscEmu::Version::Midnight::Packets
{
    struct CustomizationChoice
    {
        uint32_t optionId{0};
        uint32_t choiceId{0};
    };


    // Midnight character-list data is intentionally independent from the
    // legacy CharEnumData/SmsgEnumCharactersResult path. Values that are not
    // available in AscEmu yet are kept neutral until their Midnight DB2/schema
    // source is implemented.
    struct CharacterVisualItem
    {
        uint32_t displayId{0};
        uint8_t inventoryType{0};
        uint32_t enchantmentId{0};
    };

    struct CharacterPetData
    {
        uint32_t displayId{0};
        uint32_t level{0};
        uint32_t family{0};
    };

    struct CharacterEnumEntry
    {
        uint64_t guid{0};
        uint8_t level{0};
        uint8_t race{0};
        uint8_t charClass{0};
        uint8_t gender{0};
        std::string name;
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        uint32_t mapId{0};
        uint32_t zoneId{0};
        uint32_t loginFlags{0};
        uint32_t guildId{0};
        CharacterPetData pet;
        std::array<CharacterVisualItem, 19> visualItems{};
    };

    struct ClassAvailability
    {
        uint8_t classId{0};
    };

    struct RaceClassAvailability
    {
        uint8_t raceId{0};
        std::vector<ClassAvailability> classes;
    };

    class SmsgEnumCharactersResult final : public ManagedPacket
    {
    public:
        SmsgEnumCharactersResult(uint32_t virtualRealmAddress, uint32_t realmId,
            std::vector<CharacterEnumEntry> characters,
            std::vector<RaceClassAvailability> raceClassAvailability)
            : ManagedPacket(Opcode::SMSG_ENUM_CHARACTERS_RESULT, 0),
              virtualRealmAddress(virtualRealmAddress), realmId(realmId),
              characters(std::move(characters)), raceClassAvailability(std::move(raceClassAvailability)) {}

        uint32_t virtualRealmAddress{0};
        uint32_t realmId{0};
        std::vector<CharacterEnumEntry> characters;
        std::vector<RaceClassAvailability> raceClassAvailability;

    protected:
        bool internalSerialise(Packet& packet) override;
    };

    class SmsgAccountItemCollectionData final : public ManagedPacket
    {
    public:
        // Midnight ItemCollectionType::WarbandScene = 7.
        explicit SmsgAccountItemCollectionData(uint8_t type = 7)
            : ManagedPacket(Opcode::SMSG_ACCOUNT_ITEM_COLLECTION_DATA, 10), type(type) {}

        uint8_t type{7};

    protected:
        bool internalSerialise(Packet& packet) override;
        size_t expectedSize() const override { return 10; }
    };

    class CmsgEnumCharacters final : public ManagedPacket
    {
    public:
        CmsgEnumCharacters() : ManagedPacket(Opcode::CMSG_ENUM_CHARACTERS, 0) {}
    };

    class CmsgCheckCharacterNameAvailability final : public ManagedPacket
    {
    public:
        uint32_t sequenceIndex{0};
        std::string name;

        CmsgCheckCharacterNameAvailability() : ManagedPacket(Opcode::CMSG_CHECK_CHARACTER_NAME_AVAILABILITY, 5) {}

    protected:
        bool internalDeserialise(Packet& packet) override;
    };

    class SmsgCheckCharacterNameAvailabilityResult final : public ManagedPacket
    {
    public:
        SmsgCheckCharacterNameAvailabilityResult(uint32_t sequenceIndex, uint32_t result)
            : ManagedPacket(Opcode::SMSG_CHECK_CHARACTER_NAME_AVAILABILITY_RESULT, 0), sequenceIndex(sequenceIndex), result(result) {}

        uint32_t sequenceIndex{0};
        uint32_t result{0};

    protected:
        bool internalSerialise(Packet& packet) override;
        size_t expectedSize() const override { return 8; }
    };

    class CmsgCreateCharacter final : public ManagedPacket
    {
    public:
        std::string name;
        uint8_t race{0};
        uint8_t charClass{0};
        uint8_t sex{0};
        uint32_t customizationCount{0};
        int32_t timerunningSeasonId{0};
        bool hasTemplateSet{false};
        bool isTrialBoost{false};
        bool useNpe{false};
        bool hardcoreSelfFound{false};
        int32_t templateSet{0};
        std::vector<CustomizationChoice> customizations;

        CmsgCreateCharacter() : ManagedPacket(Opcode::CMSG_CREATE_CHARACTER, 12) {}

    protected:
        bool internalDeserialise(Packet& packet) override;
    };


    class SmsgCreateCharacter final : public ManagedPacket
    {
    public:
        SmsgCreateCharacter(uint32_t result, uint64_t characterGuid)
            : ManagedPacket(Opcode::SMSG_CREATE_CHAR, 0),
              result(result), characterGuid(characterGuid) {}

        uint32_t result{0};
        uint64_t characterGuid{0};

    protected:
        bool internalSerialise(Packet& packet) override;
    };

    class SmsgUndeleteCooldownStatusResponse final : public ManagedPacket
    {
    public:
        uint32_t maxCooldown{0};
        uint32_t currentCooldown{0};
        bool onCooldown{false};

        SmsgUndeleteCooldownStatusResponse() : ManagedPacket(Opcode::SMSG_UNDELETE_COOLDOWN_STATUS_RESPONSE, 0) {}

    protected:
        bool internalSerialise(Packet& packet) override;
        size_t expectedSize() const override { return 9; }
    };
}
