#include "version/Midnight/Packets/CharacterPackets.hpp"
#include "shared/WoWGuid.hpp"

#include <algorithm>

namespace AscEmu::Version::Midnight::Packets
{

    namespace
    {
        void appendObjectGuid(Packet& packet, const WoWGuid& guid)
        {
            const std::vector<uint8_t> packed = guid.packModern();
            if (!packed.empty())
                packet.append(packed.data(), packed.size());
        }
    }

    bool SmsgEnumCharactersResult::internalSerialise(Packet& packet)
    {
        packet.writeBit(1); // Success
        packet.writeBit(0); // Realmless
        packet.writeBit(0); // IsDeletedCharacters
        packet.writeBit(0); // IgnoreNewPlayerRestrictions
        packet.writeBit(0); // IsRestrictedNewPlayer
        packet.writeBit(0); // IsNewcomerChatCompleted
        packet.writeBit(0); // IsRestrictedTrial
        packet.writeBit(0); // IsAccountLapsedPlayer
        packet.writeBit(1); // ClassDisableMask present
        packet.writeBit(0); // ForceCharacterListSort

        packet << uint32_t(characters.size());
        packet << uint32_t(0); // RegionwideCharacters

        uint32_t maxCharacterLevel = 1;
        for (const CharacterEnumEntry& character : characters)
            maxCharacterLevel = std::max<uint32_t>(maxCharacterLevel, character.level);
        packet << int32_t(maxCharacterLevel);

        packet << uint32_t(raceClassAvailability.size());
        packet << uint32_t(0); // UnlockedConditionalAppearances
        packet << uint32_t(0); // RaceLimitDisables
        packet << uint32_t(0); // WarbandGroups
        packet << uint32_t(0); // ClassDisableMask

        uint16_t listPosition = 0;
        for (const CharacterEnumEntry& character : characters)
        {
            appendObjectGuid(packet, WoWGuid::createModernPlayer(realmId, character.guid));
            packet << uint32_t(virtualRealmAddress);
            packet << uint16_t(listPosition++);
            packet << uint8_t(character.race);
            packet << uint8_t(character.gender);
            packet << uint8_t(character.charClass);
            packet << int16_t(0); // SpecID
            packet << uint32_t(0); // Customizations count
            packet << uint8_t(character.level);
            packet << int32_t(character.mapId);
            packet << int32_t(character.zoneId);
            packet << float(character.x) << float(character.y) << float(character.z);

            const uint64_t guildClubMemberId = character.guid | (static_cast<uint64_t>(realmId & 0x0FFFU) << 48U);
            packet << guildClubMemberId;
            if (character.guildId != 0)
                appendObjectGuid(packet, WoWGuid::createModernGuild(realmId, character.guildId));
            else
                appendObjectGuid(packet, WoWGuid::createModernEmpty());

            packet << uint32_t(0) << uint32_t(0) << uint32_t(0) << uint32_t(0); // Flags 1..4
            packet << uint8_t(0); // CantLoginReason
            packet << uint32_t(character.pet.displayId) << uint32_t(character.pet.level) << uint32_t(character.pet.family);

            for (const CharacterVisualItem& item : character.visualItems)
            {
                packet << uint32_t(0);                  // ItemID
                packet << uint32_t(0);                  // TransmogrifiedItemID
                packet << uint8_t(0);                   // Subclass
                packet << uint8_t(item.inventoryType);  // InvType
                packet << uint32_t(item.displayId);     // DisplayID
                packet << uint32_t(item.enchantmentId); // DisplayEnchantID
                packet << int32_t(0);                   // SecondaryItemModifiedAppearanceID
                packet << uint8_t(0);                   // SheatheCategory
            }

            packet << int32_t(0);  // SaveVersion
            packet << uint64_t(0); // CreateTime
            packet << uint64_t(0); // LastActiveTime
            packet << int32_t(0);  // LastLoginVersion
            for (uint8_t i = 0; i < 5; ++i)
                packet << int32_t(-1); // PersonalTabard
            packet << uint32_t(0) << uint32_t(0); // ProfessionIds
            packet << int32_t(0);  // TimerunningSeasonID
            packet << uint32_t(0); // OverrideSelectScreenFileDataID
            packet << uint32_t(0); // RealmQueue

            const size_t nameLength = std::min<size_t>(character.name.size(), 63U);
            packet.writeBits(static_cast<uint32_t>(nameLength), 6);
            packet.writeBit((character.loginFlags & 0x20U) != 0); // FirstLogin
            packet.writeBit(0); // RealmInfoFound
            packet.writeBit(0); // IsRealmOffline
            packet.flushBits();
            if (nameLength != 0)
                packet.append(reinterpret_cast<const uint8_t*>(character.name.data()), nameLength);

            packet.writeBit(0); // BoostInProgress
            packet.writeBit(0); // RpeAvailable
            packet.flushBits();
            packet << uint32_t(0); // RestrictionFlags
            packet << uint32_t(0); // MailSenders count
            packet << uint32_t(0); // MailSenderTypes count
            packet << uint32_t(4); // NoRpeReason
        }

        for (const RaceClassAvailability& race : raceClassAvailability)
        {
            packet << int8_t(race.raceId);
            packet << uint32_t(race.classes.size());
            for (const ClassAvailability& charClass : race.classes)
            {
                packet << int8_t(charClass.classId);
                packet << uint32_t(0); // AchievementID
                packet.writeBit(1); // HasExpansion
                packet.writeBit(1); // HasUnlockedAchievement
                packet.writeBit(1); // HasEntitlement
                packet.flushBits();
            }

            packet.writeBit(1); // HasUnlockedLicense
            packet.writeBit(0); // HasUnlockedAchievement
            packet.writeBit(0); // HasHeritageArmorUnlockAchievement
            packet.writeBit(1); // HasEntitlement
            packet.writeBit(0); // HideRaceOnClient
            packet.writeBit(0); // FactionBalanceDisabled
            packet.writeBit(0); // DoesNotHaveAvailableClasses
            packet.flushBits();
        }

        return true;
    }

    bool SmsgAccountItemCollectionData::internalSerialise(Packet& packet)
    {
        packet << uint32_t(0); // Unknown1110_1
        packet << uint8_t(type);
        packet << uint32_t(0); // ItemCount
        packet.writeBit(0);    // Unknown1110_2
        packet.flushBits();
        return true;
    }

    bool CmsgCheckCharacterNameAvailability::internalDeserialise(Packet& packet)
    {
        packet >> sequenceIndex;
        const uint32_t nameLength = packet.readBits(6);
        if (nameLength == 0 || nameLength > 63 || packet.rpos() + nameLength > packet.size())
            return false;
        name = packet.readString(nameLength);
        return true;
    }

    bool SmsgCheckCharacterNameAvailabilityResult::internalSerialise(Packet& packet)
    {
        packet << sequenceIndex << result;
        return true;
    }

    bool CmsgCreateCharacter::internalDeserialise(Packet& packet)
    {
        const uint32_t nameLength = packet.readBits(6);
        hasTemplateSet = packet.readBit() != 0;
        isTrialBoost = packet.readBit() != 0;
        useNpe = packet.readBit() != 0;
        hardcoreSelfFound = packet.readBit() != 0;

        packet >> race >> charClass >> sex >> customizationCount >> timerunningSeasonId;

        if (nameLength == 0 || nameLength > 63 || customizationCount > 250 || packet.rpos() + nameLength > packet.size())
            return false;

        name = packet.readString(nameLength);
        if (hasTemplateSet)
            packet >> templateSet;

        const size_t customizationBytes = static_cast<size_t>(customizationCount) * 8U;
        if (packet.rpos() + customizationBytes > packet.size())
            return false;

        customizations.clear();
        customizations.reserve(customizationCount);
        for (uint32_t i = 0; i < customizationCount; ++i)
        {
            CustomizationChoice choice;
            packet >> choice.optionId >> choice.choiceId;
            customizations.emplace_back(choice);
        }

        return true;
    }

    bool SmsgCreateCharacter::internalSerialise(Packet& packet)
    {
        packet << result;

        // Keep the exact build-69814 create-response GUID layout that was
        // already proven to work before the Midnight packet isolation.
        // Unlike character-enum GUIDs, SMSG_CREATE_CHAR used a Player GUID
        // with only the ModernHighGuid::Player bits set and no realm-id bits.
        appendObjectGuid(packet, WoWGuid::createModern(uint64_t(ModernHighGuid::Player) << 58U, characterGuid));
        return true;
    }

    bool SmsgUndeleteCooldownStatusResponse::internalSerialise(Packet& packet)
    {
        packet << maxCooldown << currentCooldown;
        packet.writeBit(onCooldown ? 1 : 0);
        packet.flushBits();
        return true;
    }
}
