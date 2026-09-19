/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "world/Server/WorldSocket.hpp"

#include "Logging/Logger.hpp"
#include "version/Forever/BuildProfile.hpp"
#include "version/Forever/Defines/ObjectGuid.hpp"
#include "version/Forever/Opcodes.hpp"
#include "version/Forever/Packets/CharacterPackets.hpp"
#include "version/Forever/World/CharacterEnumReference69913.hpp"
#include "version/Forever/World/CharacterSelectBootstrap.hpp"
#include "version/Forever/World/ProtocolUtils.hpp"
#include "world/Server/WorldSession.h"
#include "world/Server/DatabaseDefinition.hpp"
#include "world/Objects/Units/Players/PlayerDefines.hpp"
#include "world/Objects/Units/Players/Player.hpp"
#include "world/Server/CharacterErrors.h"
#include "world/Macros/GuildMacros.hpp"
#include "world/Management/ObjectMgr.hpp"
#include "Utilities/Strings.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace
{
    ByteBuffer buildForeverEmptyAccountItemCollectionData()
    {
        ByteBuffer packet;
        packet << uint32_t(0);
        packet << uint8_t(7);
        packet << uint32_t(0);
        packet.writeBit(0);
        packet.flushBits();
        return packet;
    }

    void ensureForeverCharacterCustomizationTable()
    {
        CharacterDatabase.waitExecute("CREATE TABLE IF NOT EXISTS `character_customizations` (" "`guid` BIGINT UNSIGNED NOT NULL, " "`chrCustomizationOptionID` INT UNSIGNED NOT NULL, " "`chrCustomizationChoiceID` INT UNSIGNED NOT NULL, " "PRIMARY KEY (`guid`, `chrCustomizationOptionID`)" ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4");
    }

    bool isForeverRaceClassAvailableInDatabase(uint8_t race, uint8_t classId)
    {
        auto result = WorldDatabase.query("SELECT 1 FROM playercreateinfo pi " "WHERE pi.race=%u AND pi.class=%u AND pi.build=(" "SELECT MAX(build) FROM playercreateinfo buildspecific " "WHERE buildspecific.race=pi.race " "AND buildspecific.class=pi.class " "AND buildspecific.build <= %u) LIMIT 1", static_cast<uint32_t>(race), static_cast<uint32_t>(classId), VERSION_STRING);

        return result != nullptr;
    }

    struct CharacterEnumLayout69913
    {
        static constexpr size_t HeaderSize = 34U;
        static constexpr size_t CharacterSize = 701U;
        static constexpr size_t GuidSize = 8U;
        static constexpr size_t CustomizationOffset = 580U;
        static constexpr size_t ReferenceCustomizationCount = 10U;
        static constexpr size_t CustomizationSize = 8U;
        static constexpr size_t NameBitsAbsolute = 702U;
        static constexpr size_t NameAbsolute = 704U;
        static constexpr size_t ReferenceFirstNameLength = 4U;
        static constexpr size_t ReferenceLastNameLength = 4U;
        static constexpr uint8_t NameFlags = 0x0CU;
        static constexpr size_t CharacterStart = HeaderSize;
        static constexpr size_t CharacterEnd = CharacterStart + CharacterSize;
        static constexpr size_t ReferenceNameLength = ReferenceFirstNameLength + ReferenceLastNameLength;
        static constexpr size_t TailAfterName = NameAbsolute + ReferenceNameLength;

        static constexpr bool isValid(size_t referenceSize)
        {
            return referenceSize >= CharacterEnd && TailAfterName <= CharacterEnd && CharacterStart + GuidSize + CustomizationOffset + ReferenceCustomizationCount * CustomizationSize == NameBitsAbsolute;
        }
    };
}


bool WorldSocket::sendForeverEmptyCharacterList()
{
    using namespace AscEmu::Version::Forever;

    const uint64_t counterBefore = m_foreverCryptoSendCounter;

    // Midnight-style split: AccountDataTimes/TutorialFlags are bootstrap state,
    // not part of each enum reply. The enum request itself gets the character
    // list followed by the empty account-item collection.
    if (!sendForeverPacket(AscEmu::Version::Forever::Opcode::SMSG_ENUM_CHARACTERS_RESULT, CharacterSelectBootstrap::EmptyCharacterList.data(), static_cast<uint32_t>(CharacterSelectBootstrap::EmptyCharacterList.size())))
    {
        sLogger.failure("WorldSocket::Forever: failed to send SMSG_ENUM_CHARACTERS_RESULT.");
        return false;
    }

    ByteBuffer collection = buildForeverEmptyAccountItemCollectionData();
    if (!sendForeverPacket(AscEmu::Version::Forever::Opcode::SMSG_ACCOUNT_ITEM_COLLECTION_DATA, collection.contents(), static_cast<uint32_t>(collection.size())))
    {
        sLogger.failure("WorldSocket::Forever: failed to send SMSG_ACCOUNT_ITEM_COLLECTION_DATA.");
        return false;
    }

    sLogger.info("WorldSocket::Forever: sent request-driven empty character enum Midnight-style: " "enum={} ({} byte(s)), collection={}; crypto_counter={} -> {}.", "SMSG_ENUM_CHARACTERS_RESULT", CharacterSelectBootstrap::EmptyCharacterList.size(), "SMSG_ACCOUNT_ITEM_COLLECTION_DATA", counterBefore, m_foreverCryptoSendCounter);

    return true;
}

bool WorldSocket::handleForeverCreateCharacter(const uint8_t* payload, uint32_t payloadSize)
{
    using namespace AscEmu::Version::Forever;

    auto sendResult = [&](CharacterErrorCodes code, uint64_t guid = 0U) -> bool
    {
        const uint32_t foreverResult = AscEmu::Version::Forever::Packets::toCharacterResult(code);
        ByteBuffer wire = AscEmu::Version::Forever::Packets::buildCreateCharacterResponse(foreverResult, m_foreverRealmId, guid);

        sLogger.info("WorldSocket::Forever: sending SMSG_CREATE_CHAR result={} coreResult={} guid={} payload={} byte(s), hex=[{}].", foreverResult, static_cast<uint32_t>(code), guid, wire.size(), AscEmu::Version::Forever::bytesToHex(wire.contents(), wire.size()));

        return sendForeverPacket(AscEmu::Version::Forever::Opcode::SMSG_CREATE_CHAR, wire.contents(), static_cast<uint32_t>(wire.size()));
    };

    if (m_session == nullptr)
    {
        sLogger.warning("WorldSocket::Forever: CMSG_CREATE_CHARACTER received without WorldSession.");
        return false;
    }

    AscEmu::Version::Forever::Packets::CreateCharacterRequest request;
    if (!AscEmu::Version::Forever::Packets::parseCreateCharacter(payload, payloadSize, request))
    {
        sLogger.warning("WorldSocket::Forever: malformed CMSG_CREATE_CHARACTER payload={} byte(s), hex=[{}].", payloadSize, AscEmu::Version::Forever::bytesToHex(payload, payloadSize));
        return sendResult(E_CHAR_CREATE_FAILED);
    }

    sLogger.info("WorldSocket::Forever: create character name='{}' race={} class={} sex={} customizations={} timerunning={} templateSet={}.", request.name, request.race, request.charClass, request.sex, request.customizationCount, request.timerunningSeasonId, request.templateSet);

    const CharacterErrorCodes nameResult = VerifyName(request.name);
    if (nameResult != E_CHAR_NAME_SUCCESS)
    {
        sLogger.info("WorldSocket::Forever: create rejected by VerifyName name='{}' result={}.", request.name, static_cast<uint32_t>(nameResult));
        return sendResult(nameResult);
    }

    sLogger.info("WorldSocket::Forever: create checkpoint: VerifyName passed.");

    if (sObjectMgr.getCachedCharacterInfoByName(request.name) != nullptr)
    {
        sLogger.info("WorldSocket::Forever: create rejected: name '{}' already exists in character cache.", request.name);
        return sendResult(E_CHAR_CREATE_NAME_IN_USE);
    }

    sLogger.info("WorldSocket::Forever: create checkpoint: cached-name check passed.");

    if (!isForeverRaceClassAvailableInDatabase(request.race, request.charClass))
    {
        sLogger.info("WorldSocket::Forever: create rejected by playercreateinfo race/class check race={} class={} build<={}.", request.race, request.charClass, VERSION_STRING);
        return sendResult(E_CHAR_CREATE_RESTRICTED_RACECLASS);
    }

    sLogger.info("WorldSocket::Forever: create checkpoint: playercreateinfo race/class check passed for race={} class={}.", request.race, request.charClass);

    const auto bannedNamesQuery = CharacterDatabase.query("SELECT COUNT(*) FROM banned_names WHERE name = '%s'", CharacterDatabase.escapeString(request.name).c_str());
    if (bannedNamesQuery && bannedNamesQuery->fetch()[0].asUint32() > 0U)
    {
        sLogger.info("WorldSocket::Forever: create rejected: name '{}' is present in banned_names.", request.name);
        return sendResult(E_CHAR_NAME_PROFANE);
    }

    sLogger.info("WorldSocket::Forever: create checkpoint: banned-name check passed.");

    const auto charactersQuery = CharacterDatabase.query("SELECT COUNT(*) FROM characters WHERE acct = %u", m_session->GetAccountId());

    if (charactersQuery && charactersQuery->fetch()[0].asUint32() >= 60U)
    {
        sLogger.info("WorldSocket::Forever: create rejected: account character limit reached.");
        return sendResult(E_CHAR_CREATE_SERVER_LIMIT);
    }

    sLogger.info("WorldSocket::Forever: create checkpoint: account character-count check passed.");

    CharCreate createInfo{};
    createInfo.name = request.name;
    createInfo._race = request.race;
    createInfo._class = request.charClass;
    createInfo.gender = request.sex;
    createInfo.skin = 0;
    createInfo.face = 0;
    createInfo.hairStyle = 0;
    createInfo.hairColor = 0;
    createInfo.facialHair = 0;
    createInfo.outfitId = 0;

    sLogger.info("WorldSocket::Forever: create checkpoint: calling ObjectMgr::createPlayer(class={}).", createInfo._class);
    Player* newPlayer = sObjectMgr.createPlayer(createInfo._class);
    if (newPlayer == nullptr)
    {
        sLogger.warning("WorldSocket::Forever: ObjectMgr::createPlayer returned nullptr.");
        return sendResult(E_CHAR_CREATE_FAILED);
    }

    sLogger.info("WorldSocket::Forever: create checkpoint: Player allocated; calling Player::create().");
    newPlayer->setSession(m_session);
    if (!newPlayer->create(createInfo))
    {
        sLogger.warning("WorldSocket::Forever: Player::create failed for race={} class={} name='{}'.", request.race, request.charClass, request.name);
        newPlayer->m_isReadyToBeRemoved = true;
        delete newPlayer;
        return sendResult(E_CHAR_CREATE_FAILED);
    }

    sLogger.info("WorldSocket::Forever: create checkpoint: Player::create succeeded guid={}; saving to DB.", newPlayer->getGuidLow());

    newPlayer->unsetBanned();
    newPlayer->saveToDB(true);

    sLogger.info("WorldSocket::Forever: create checkpoint: saveToDB completed; updating character cache.");

    const uint64_t createdGuid = newPlayer->getGuidLow();

    ensureForeverCharacterCustomizationTable();
    CharacterDatabase.waitExecute("DELETE FROM `character_customizations` WHERE `guid`=%llu", static_cast<unsigned long long>(createdGuid));

    for (const auto& customization : request.customizations)
    {
        CharacterDatabase.waitExecute("INSERT INTO `character_customizations` " "(`guid`, `chrCustomizationOptionID`, `chrCustomizationChoiceID`) " "VALUES (%llu, %u, %u)", static_cast<unsigned long long>(createdGuid), customization.optionId, customization.choiceId);
    }

    sLogger.info("WorldSocket::Forever: stored {} customization choice(s) for guid={}.", request.customizations.size(), createdGuid);

    // Official Forever inserts a newly created character at the top of the
    // visible list and shifts the existing characters down by one.
    //
    // Seed any older characters that still have no order row before shifting,
    // so the new character cannot collide with fallback ordering.
    {
        auto missingExisting = CharacterDatabase.query("SELECT c.guid " "FROM characters c " "LEFT JOIN character_list_order o ON o.acct=c.acct AND o.guid=c.guid " "WHERE c.acct=%u AND c.guid<>%llu AND o.guid IS NULL " "ORDER BY c.guid", m_session->GetAccountId(), static_cast<unsigned long long>(createdGuid));

        if (missingExisting)
        {
            uint32_t nextPosition = 0;

            if (auto maxOrder = CharacterDatabase.query("SELECT MAX(listPosition) " "FROM character_list_order WHERE acct=%u", m_session->GetAccountId()))
            {
                Field* maxFields = maxOrder->fetch();
                if (maxFields[0].isSet())
                    nextPosition = static_cast<uint32_t>(maxFields[0].asUint16()) + 1U;
            }

            do
            {
                const uint64_t guid = missingExisting->fetch()[0].asUint64();

                CharacterDatabase.waitExecute("INSERT IGNORE INTO character_list_order (acct, guid, listPosition) " "VALUES (%u, %llu, %u)", m_session->GetAccountId(), static_cast<unsigned long long>(guid), nextPosition);

                ++nextPosition;
            }
            while (missingExisting->nextRow());
        }
    }

    CharacterDatabase.waitExecute("UPDATE character_list_order SET listPosition=listPosition+1 WHERE acct=%u", m_session->GetAccountId());
    CharacterDatabase.waitExecute("INSERT INTO character_list_order (acct, guid, listPosition) VALUES (%u, %llu, 0) " "ON DUPLICATE KEY UPDATE listPosition=0", m_session->GetAccountId(), static_cast<unsigned long long>(createdGuid));

    // Keep ObjectMgr's runtime character cache byte-for-byte equivalent to the
    // state produced by ObjectMgr::loadCharacters() on a world restart.
    //
    // The previous Forever create path manually constructed CachedCharacterInfo
    // and left fields such as lastLevel/lastZone at their defaults. The DB enum
    // itself is correct, but other glue/account-character paths can consult the
    // ObjectMgr cache. That explains why a full world restart (which reloads the
    // cache from DB) repairs the visible character list.
    auto cacheResult = CharacterDatabase.query("SELECT guid, name, race, class, level, gender, zoneid, timestamp, acct " "FROM characters WHERE guid=%u LIMIT 1", static_cast<uint32_t>(createdGuid));

    if (cacheResult)
    {
        auto playerInfo = std::make_unique<CachedCharacterInfo>(cacheResult->fetch());
        sObjectMgr.addCachedCharacterInfo(std::move(playerInfo));

        sLogger.info("WorldSocket::Forever: create checkpoint: character cache reloaded from DB using the same CachedCharacterInfo layout as ObjectMgr::loadCharacters().");
    }
    else
    {
        sLogger.warning("WorldSocket::Forever: character '{}' was saved with guid={} but could not be re-read for ObjectMgr cache population.", request.name, createdGuid);
    }

    newPlayer->m_isReadyToBeRemoved = true;
    delete newPlayer;

    sLogger.info("WorldSocket::Forever: character '{}' created successfully guid={} account={}; sending SMSG_CREATE_CHAR.", request.name, createdGuid, m_session->GetAccountId());

    // The client asks for the character list again immediately after create.
    // The first refresh can still render the pre-create list until another
    // glue tick/refresh occurs, so arm a one-shot follow-up enum after that
    // first post-create CMSG_ENUM_CHARACTERS.
    m_foreverPostCreateEnumRefreshPending = true;
    m_foreverPostCreateEnumRefreshArmed = false;

    return sendResult(E_CHAR_CREATE_SUCCESS, createdGuid);
}

bool WorldSocket::sendForeverCharacterEnumFromDatabase(bool includeCollection)
{
    using namespace AscEmu::Version::Forever;
    using AscEmu::Version::Forever::ObjectGuid;

    if (m_session == nullptr)
    {
        sLogger.warning("WorldSocket::Forever: DB character enum requested without WorldSession.");
        return false;
    }

    struct DbCharacter
    {
        uint64_t guid{0};
        uint8_t level{0};
        uint8_t race{0};
        uint8_t charClass{0};
        uint8_t gender{0};
        std::string firstName;
        std::string lastName;
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        int32_t mapId{0};
        int32_t zoneId{0};
        uint16_t orderPosition{0};
        std::vector<AscEmu::Version::Forever::Packets::CharacterCustomizationChoice> customizations;
    };

    const uint32_t accountId = m_session->GetAccountId();

    // Seed missing order rows for existing characters before reading the enum.
    // If character_list_order is empty after upgrading, existing characters
    // keep their current GUID order until the client sends an explicit reorder.
    auto missingOrder = CharacterDatabase.query("SELECT c.guid " "FROM characters c " "LEFT JOIN character_list_order o ON o.acct=c.acct AND o.guid=c.guid " "WHERE c.acct=%u AND o.guid IS NULL " "ORDER BY c.guid", accountId);

    if (missingOrder)
    {
        uint32_t nextPosition = 0;

        if (auto maxOrder = CharacterDatabase.query("SELECT MAX(listPosition) " "FROM character_list_order WHERE acct=%u", accountId))
        {
            Field* maxFields = maxOrder->fetch();
            if (maxFields[0].isSet())
                nextPosition = static_cast<uint32_t>(maxFields[0].asUint16()) + 1U;
        }

        do
        {
            const uint64_t guid = missingOrder->fetch()[0].asUint64();

            CharacterDatabase.waitExecute("INSERT IGNORE INTO character_list_order (acct, guid, listPosition) " "VALUES (%u, %llu, %u)", accountId, static_cast<unsigned long long>(guid), nextPosition);

            sLogger.info("WorldSocket::Forever: seeded character order account={} guid={} -> position={}.", accountId, guid, nextPosition);

            ++nextPosition;
        }
        while (missingOrder->nextRow());
    }

    auto result = CharacterDatabase.query("SELECT c.guid, c.level, c.race, c.class, c.gender, c.name, " "c.positionX, c.positionY, c.positionZ, c.mapId, c.zoneId, " "COALESCE(o.listPosition, 65535) " "FROM characters c " "LEFT JOIN character_list_order o ON o.acct=c.acct AND o.guid=c.guid " "WHERE c.acct=%u " "ORDER BY COALESCE(o.listPosition, 65535), c.guid " "LIMIT 10", accountId);

    if (result == nullptr)
    {
        sLogger.info("WorldSocket::Forever: account {} has no DB characters; sending the known-good empty enum.", accountId);
        return sendForeverEmptyCharacterList();
    }

    std::vector<DbCharacter> characters;
    characters.reserve(10);

    do
    {
        Field* fields = result->fetch();

        DbCharacter character;
        character.guid = fields[0].asUint64();
        character.level = fields[1].asUint8();
        character.race = fields[2].asUint8();
        character.charClass = fields[3].asUint8();
        character.gender = fields[4].asUint8();

        std::string dbName = fields[5].asCString();

        // The legacy characters table currently has one name column. Forever's
        // CharacterInfo carries separate 6-bit first/last-name lengths.
        // If a future migration stores "First Last" in this field, split it.
        // Otherwise the complete DB name is the first name and last name is empty.
        const size_t separator = dbName.find(' ');
        if (separator == std::string::npos)
        {
            character.firstName = dbName;
        }
        else
        {
            character.firstName = dbName.substr(0, separator);
            character.lastName = dbName.substr(separator + 1);
        }

        if (character.firstName.size() > 63U)
            character.firstName.resize(63U);
        if (character.lastName.size() > 63U)
            character.lastName.resize(63U);

        character.x = fields[6].asFloat();
        character.y = fields[7].asFloat();
        character.z = fields[8].asFloat();
        character.mapId = fields[9].asInt32();
        character.zoneId = fields[10].asInt32();
        character.orderPosition = fields[11].asUint16();

        characters.emplace_back(std::move(character));
    }
    while (characters.size() < 10U && result->nextRow());

    ensureForeverCharacterCustomizationTable();

    if (auto customizationResult = CharacterDatabase.query("SELECT cc.guid, cc.chrCustomizationOptionID, cc.chrCustomizationChoiceID " "FROM character_customizations cc " "INNER JOIN characters c ON c.guid=cc.guid " "WHERE c.acct=%u " "ORDER BY cc.guid, cc.chrCustomizationOptionID", accountId))
    {
        do
        {
            Field* customizationFields = customizationResult->fetch();
            const uint64_t guid = customizationFields[0].asUint64();

            auto characterIt = std::find_if(characters.begin(), characters.end(), [guid](const DbCharacter& character) { return character.guid == guid; });

            if (characterIt == characters.end())
                continue;

            AscEmu::Version::Forever::Packets::CharacterCustomizationChoice customization;
            customization.optionId = customizationFields[1].asUint32();
            customization.choiceId = customizationFields[2].asUint32();
            characterIt->customizations.emplace_back(customization);
        }
        while (customizationResult->nextRow());
    }

    // Proven 69913 CharacterInfo reference accepted by the 69893 client:
    //
    //   34-byte enum header
    //   701-byte CharacterInfo
    //   396-byte race/class availability tail
    //
    // The reference character uses an 8-byte packed GUID and "Test"/"Hims".
    // The two bytes directly before "TestHims" are:
    //   0x10 0x4C = 000100 000100 1100
    //               first=4 last=4 flags=0xC
    //
    // This confirms Forever has two 6-bit name lengths plus four existing
    // CharacterInfo flags. Rebuild those 16 bits for every DB character.
    const auto& reference = CharacterEnumReference69913::OneCharacterEnum460018;
    if (!CharacterEnumLayout69913::isValid(reference.size()))
    {
        sLogger.failure("WorldSocket::Forever: invalid CharacterInfo reference layout; reference_size={}.", reference.size());
        return false;
    }

    std::vector<uint8_t> wire;
    wire.reserve(reference.size() + characters.size() * 704U);

    // Start with the accepted one-character header, then patch only the
    // character count and MaxCharacterLevel.
    wire.insert(wire.end(), reference.begin(), reference.begin() + CharacterEnumLayout69913::HeaderSize);

    const uint32_t characterCount = static_cast<uint32_t>(characters.size());
    std::memcpy(wire.data() + 6U, &characterCount, sizeof(characterCount));

    int32_t maxCharacterLevel = 1;
    for (const DbCharacter& character : characters)
        maxCharacterLevel = std::max<int32_t>(maxCharacterLevel, static_cast<int32_t>(character.level));
    std::memcpy(wire.data() + 10U, &maxCharacterLevel, sizeof(maxCharacterLevel));

    const uint32_t virtualRealmAddress =
        ((m_foreverRegionId & 0xFFU) << 24U) |
        ((m_foreverBattlegroupId & 0xFFU) << 16U) |
        (m_foreverRealmId & 0xFFFFU);

    uint16_t listPosition = 0;
    for (const DbCharacter& character : characters)
    {
        const std::vector<uint8_t> packedGuid =
            ObjectGuid::createPlayer(m_foreverRealmId, character.guid).pack();
        wire.insert(wire.end(), packedGuid.begin(), packedGuid.end());

        // Copy only the fixed CharacterInfo body. The reference contains ten
        // appearance customizations after this point; those must never be
        // cloned to every character.
        std::vector<uint8_t> prefix(reference.begin() + CharacterEnumLayout69913::CharacterStart + CharacterEnumLayout69913::GuidSize, reference.begin() + CharacterEnumLayout69913::CharacterStart + CharacterEnumLayout69913::GuidSize + CharacterEnumLayout69913::CustomizationOffset);

        auto patch = [&](size_t offset, const auto& value)
        {
            std::memcpy(prefix.data() + offset, &value, sizeof(value));
        };

        patch(0U, virtualRealmAddress);
        // Official Forever 69913 two-character capture has this field set
        // to zero for both CharacterInfo records. Visible ordering is carried
        // separately in account-data type 16, not by this field.
        const uint16_t foreverListPosition = 0;
        patch(4U, foreverListPosition);
        prefix[6U] = character.race;
        prefix[7U] = character.gender;
        prefix[8U] = character.charClass;
        const uint32_t customizationCount = static_cast<uint32_t>(character.customizations.size());
        patch(11U, customizationCount);
        prefix[15U] = character.level;
        patch(16U, character.mapId);
        patch(20U, character.zoneId);
        patch(24U, character.x);
        patch(28U, character.y);
        patch(32U, character.z);

        const uint64_t guildClubMemberId =
            character.guid |
            (static_cast<uint64_t>(m_foreverRealmId & 0x0FFFU) << 48U);
        patch(36U, guildClubMemberId);

        wire.insert(wire.end(), prefix.begin(), prefix.end());

        for (const auto& customization : character.customizations)
        {
            const uint8_t* optionBytes = reinterpret_cast<const uint8_t*>(&customization.optionId);
            wire.insert(wire.end(), optionBytes, optionBytes + sizeof(customization.optionId));

            const uint8_t* choiceBytes = reinterpret_cast<const uint8_t*>(&customization.choiceId);
            wire.insert(wire.end(), choiceBytes, choiceBytes + sizeof(customization.choiceId));
        }

        const uint16_t firstLength =
            static_cast<uint16_t>(character.firstName.size());
        const uint16_t lastLength =
            static_cast<uint16_t>(character.lastName.size());

        // MSB-first: [firstName:6][lastName:6][flags:4].
        const uint16_t nameBits =
            static_cast<uint16_t>((firstLength << 10U) | (lastLength << 4U) | CharacterEnumLayout69913::NameFlags);

        wire.push_back(static_cast<uint8_t>((nameBits >> 8U) & 0xFFU));
        wire.push_back(static_cast<uint8_t>(nameBits & 0xFFU));

        wire.insert(wire.end(), character.firstName.begin(), character.firstName.end());
        wire.insert(wire.end(), character.lastName.begin(), character.lastName.end());

        // Preserve the proven post-name CharacterInfo fields.
        wire.insert(wire.end(), reference.begin() + CharacterEnumLayout69913::TailAfterName, reference.begin() + CharacterEnumLayout69913::CharacterEnd);

        sLogger.info("WorldSocket::Forever: enum character #{} guid={} first='{}' last='{}' race={} class={} gender={} level={} map={} zone={} customizations={} packed_guid={} byte(s).", static_cast<uint32_t>(listPosition) + 1U, character.guid, character.firstName, character.lastName, character.race, character.charClass, character.gender, character.level, character.mapId, character.zoneId, character.customizations.size(), packedGuid.size());

        ++listPosition;
    }

    // Race/class availability is account-independent; preserve the accepted
    // reference tail after the single reference CharacterInfo.
    wire.insert(wire.end(), reference.begin() + CharacterEnumLayout69913::CharacterEnd, reference.end());

    if (!sendForeverPacket(AscEmu::Version::Forever::Opcode::SMSG_ENUM_CHARACTERS_RESULT, wire.data(), static_cast<uint32_t>(wire.size())))
        return false;

    // Official Forever sends 0x00460019 immediately after a non-empty
    // character enum. It mirrors the character GUID set and is separate from
    // CharacterInfo/ListPosition.
    ByteBuffer characterListState;
    characterListState << static_cast<uint32_t>(characters.size());

    for (const DbCharacter& character : characters)
    {
        characterListState << uint8_t(0);

        const std::vector<uint8_t> packedGuid =
            ObjectGuid::createPlayer(m_foreverRealmId, character.guid).pack();
        characterListState.append(packedGuid.data(), packedGuid.size());

        characterListState << uint32_t(0);
        characterListState << uint32_t(10);
    }

    if (!sendForeverPacket(AscEmu::Version::Forever::Opcode::SMSG_CHARACTER_LIST_STATE, characterListState.contents(), static_cast<uint32_t>(characterListState.size())))
        return false;

    sLogger.info("WorldSocket::Forever: sent SMSG_CHARACTER_LIST_STATE characters={} payload={} byte(s).", characters.size(), characterListState.size());

    if (includeCollection)
    {
        if (!sendForeverPacket(AscEmu::Version::Forever::Opcode::SMSG_ACCOUNT_ITEM_COLLECTION_DATA, CharacterSelectBootstrap::AccountItemCollection460362.data(), static_cast<uint32_t>(CharacterSelectBootstrap::AccountItemCollection460362.size())))
            return false;
    }

    sLogger.info("WorldSocket::Forever: sent DB-backed character enum account={} characters={} max_level={} payload={} byte(s), collection={}.", accountId, characters.size(), maxCharacterLevel, wire.size(), includeCollection ? "yes" : "no");

    return true;
}
