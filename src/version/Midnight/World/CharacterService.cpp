/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "version/Midnight/World/CharacterService.hpp"

#include "version/Midnight/Packets/CharacterPackets.hpp"
#include "world/Server/World.h"
#include "world/Server/WorldSession.h"
#include "world/Server/WorldSocket.hpp"
#include "world/Server/DatabaseDefinition.hpp"
#include "world/Storage/MySQLDataStore.hpp"
#include "world/Storage/WDB/WDBStores.hpp"
#include "world/Objects/Units/Players/PlayerDefines.hpp"
#include "world/Objects/Units/Players/Player.hpp"
#include "world/Server/CharacterErrors.h"
#include "world/Macros/GuildMacros.hpp"
#include "WoWGuid.hpp"
#include "Logging/Logger.hpp"
#include "Utilities/Strings.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace AscEmu::Version::Midnight
{
    namespace
    {
        using namespace Packets;

        uint32_t toMidnightCharacterCreateResult(CharacterErrorCodes code)
        {
            switch (code)
            {
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

        bool sendCreateResult(WorldSocket& socket, CharacterErrorCodes result, uint64_t characterGuid = 0)
        {
            SmsgCreateCharacter response(
                toMidnightCharacterCreateResult(result),
                characterGuid);

            auto wire = response.serialise();
            return wire != nullptr && socket.sendMidnightPacket(*wire);
        }

        std::vector<RaceClassAvailability> buildRaceClassAvailability()
        {
            std::vector<RaceClassAvailability> result;
            result.reserve(DBC_NUM_RACES);

            for (uint8_t race = RACE_HUMAN; race < DBC_NUM_RACES; ++race)
            {
                RaceClassAvailability availability;
                availability.raceId = race;

                for (uint8_t classId = WARRIOR; classId < MAX_PLAYER_CLASSES; ++classId)
                {
                    if (sMySQLStore.getPlayerCreateInfo(race, classId) == nullptr)
                        continue;

                    availability.classes.push_back({ classId });
                }

                if (!availability.classes.empty())
                    result.emplace_back(std::move(availability));
            }

            return result;
        }

        std::vector<CharacterEnumEntry> buildCharacterList(QueryResult* result)
        {
            std::vector<CharacterEnumEntry> characters;
            if (result == nullptr)
                return characters;

            characters.reserve(result->getRowCount());

            // Data loading is copied from the legacy characterEnumProc as a
            // temporary database baseline. The handler, data model and wire
            // serialization are Midnight-only and do not call the legacy enum
            // path.
            do
            {
                Field* fields = result->fetch();

                CharacterEnumEntry character;
                character.guid = fields[0].asUint64();
                character.level = fields[1].asUint8();
                character.race = fields[2].asUint8();
                character.charClass = fields[3].asUint8();

                if (!isClassRaceCombinationPossible(static_cast<Classes>(character.charClass), static_cast<Races>(character.race)))
                {
                    continue;
                }

                character.gender = fields[4].asUint8();
                character.name = fields[7].asCString();
                character.x = fields[8].asFloat();
                character.y = fields[9].asFloat();
                character.z = fields[10].asFloat();
                character.mapId = fields[11].asUint32();
                character.zoneId = fields[12].asUint32();
                character.loginFlags = fields[16].asUint32();
                character.guildId = fields[18].asUint32();

                if (character.charClass == WARLOCK || character.charClass == HUNTER ||
                    character.charClass == DEATHKNIGHT || character.charClass == MAGE)
                {
                    auto petResult = CharacterDatabase.query(
                        "SELECT entry, model, level FROM playerpets WHERE ownerguid = %u "
                        "AND active = TRUE AND alive = TRUE LIMIT 1;",
                        WoWGuid::getLowGuidFromRaw(character.guid));

                    if (petResult)
                    {
                        Field* petFields = petResult->fetch();
                        if (const auto petInfo = sMySQLStore.getCreatureProperties(petFields[0].asUint32()))
                        {
                            character.pet.displayId = petFields[1].asUint32();
                            character.pet.level = petFields[2].asUint32();
                            character.pet.family = petInfo->Family;
                        }
                    }
                }

                auto itemResult = CharacterDatabase.query(
                    "SELECT slot, entry, enchantments FROM playeritems "
                    "WHERE ownerguid=%u AND containerslot = '-1' AND slot BETWEEN '0' AND '22'",
                    WoWGuid::getLowGuidFromRaw(character.guid));

                if (itemResult)
                {
                    do
                    {
                        Field* itemFields = itemResult->fetch();
                        const int8_t itemSlot = itemFields[0].asInt8();
                        if (itemSlot < 0 || static_cast<size_t>(itemSlot) >= character.visualItems.size())
                            continue;

                        const auto itemProperties = sMySQLStore.getItemProperties(itemFields[1].asUint32());
                        if (itemProperties == nullptr)
                            continue;

                        CharacterVisualItem& visual = character.visualItems[static_cast<size_t>(itemSlot)];
                        visual.displayId = itemProperties->DisplayInfoID;
                        visual.inventoryType = static_cast<uint8_t>(itemProperties->InventoryType);

                        const std::string enchantField = itemFields[2].asCString();
                        if (enchantField.empty())
                            continue;

                        const std::vector<std::string> enchants = AscEmu::Util::Strings::split(enchantField, ";");
                        for (const std::string& enchant : enchants)
                        {
                            uint32_t enchantId = 0;
                            uint32_t enchantSlot = 0;
                            if (std::sscanf(enchant.c_str(), "%u,0,%u", &enchantId, &enchantSlot) != 2)
                                continue;

                            if ((itemSlot == EQUIPMENT_SLOT_MAINHAND || itemSlot == EQUIPMENT_SLOT_OFFHAND) &&
                                enchantSlot == PERM_ENCHANTMENT_SLOT)
                            {
                                if (const auto enchantEntry = sSpellItemEnchantmentStore.lookupEntry(enchantId))
                                    visual.enchantmentId = enchantEntry->visual;
                            }
                        }
                    } while (itemResult->nextRow());
                }

                characters.emplace_back(std::move(character));
            } while (result->nextRow());

            return characters;
        }
    }

    CharacterService& CharacterService::instance()
    {
        static CharacterService service;
        return service;
    }

    void CharacterService::requestCharacterEnum(WorldSocket& socket)
    {
        WorldSession* session = socket.getSession();
        if (session == nullptr)
        {
            sLogger.warning("Midnight::CharacterService: character enum requested without WorldSession.");
            return;
        }

        const uint32_t accountId = session->GetAccountId();

        // Keep the Midnight character-select path self-contained. The outer enum
        // query is intentionally synchronous for now: the character screen is a
        // one-shot glue operation and buildCharacterList already performs the
        // legacy-schema item/pet lookups synchronously. This also avoids routing
        // a Midnight response back through World/legacy callback ownership.
        auto result = CharacterDatabase.query(
            "SELECT guid, level, race, class, gender, bytes, bytes2, name, positionX, positionY, "
            "positionZ, mapId, zoneId, banned, restState, deathstate, login_flags, player_flags, guild_members.guildId "
            "FROM characters LEFT JOIN guild_members ON characters.guid = guild_members.playerid "
            "WHERE acct=%u ORDER BY guid LIMIT 10",
            accountId);

        std::vector<CharacterEnumEntry> characters = buildCharacterList(result.get());
        std::vector<RaceClassAvailability> availability = buildRaceClassAvailability();

        const uint32_t virtualRealmAddress =
            ((socket.getMidnightRegionId() & 0xFFU) << 24U) |
            (1U << 16U) |
            (socket.getMidnightRealmId() & 0xFFFFU);

        SmsgEnumCharactersResult response(
            virtualRealmAddress,
            socket.getMidnightRealmId(),
            std::move(characters),
            std::move(availability));

        auto wire = response.serialise();
        if (wire == nullptr)
        {
            sLogger.failure("Midnight::CharacterService: failed to serialize SMSG_ENUM_CHARACTERS_RESULT for account {}.", accountId);
            return;
        }

        if (!socket.sendMidnightPacket(*wire))
        {
            sLogger.failure("Midnight::CharacterService: failed to send SMSG_ENUM_CHARACTERS_RESULT for account {}.", accountId);
            return;
        }

        // Midnight glue expects the account-wide Warband Scene collection
        // immediately after a successful character enumeration. Keep this as
        // a dedicated Midnight packet rather than raw bytes in WorldSocket.
        SmsgAccountItemCollectionData collection;
        auto collectionWire = collection.serialise();
        if (collectionWire == nullptr || !socket.sendMidnightPacket(*collectionWire))
            sLogger.failure("Midnight::CharacterService: failed to send SMSG_ACCOUNT_ITEM_COLLECTION_DATA for account {}.", accountId);
    }

    bool CharacterService::createCharacter(WorldSocket& socket, const Packets::CmsgCreateCharacter& request)
    {
        WorldSession* session = socket.getSession();
        if (session == nullptr)
            return false;

        const CharacterErrorCodes nameResult = VerifyName(request.name);
        if (nameResult != E_CHAR_NAME_SUCCESS)
            return sendCreateResult(socket, nameResult);

        if (!sMySQLStore.isCharacterNameAllowed(request.name))
            return sendCreateResult(socket, E_CHAR_NAME_PROFANE);

        if (sObjectMgr.getCachedCharacterInfoByName(request.name) != nullptr)
            return sendCreateResult(socket, E_CHAR_CREATE_NAME_IN_USE);

        if (sMySQLStore.getPlayerCreateInfo(request.race, request.charClass) == nullptr)
        {
            sLogger.warning("Midnight::CharacterService: character create rejected: missing PlayerCreateInfo for race={} class={} name='{}'.",
                request.race, request.charClass, request.name);
            return sendCreateResult(socket, E_CHAR_CREATE_RESTRICTED_RACECLASS);
        }

        // Player::create() still consumes the core race/class stores. Validate
        // them before calling it because the legacy helper disconnects the
        // session when either entry is unavailable. Midnight must return a
        // create result instead of tearing down the World V2 connection.
        if (sChrRacesStore.lookupEntry(request.race) == nullptr || sChrClassesStore.lookupEntry(request.charClass) == nullptr)
        {
            sLogger.warning("Midnight::CharacterService: character create rejected: missing core race/class data for race={} class={} name='{}'.", request.race, request.charClass, request.name);
            return sendCreateResult(socket, E_CHAR_CREATE_RESTRICTED_RACECLASS);
        }

        const auto bannedNamesQuery = CharacterDatabase.query(
            "SELECT COUNT(*) FROM banned_names WHERE name = '%s'",
            CharacterDatabase.escapeString(request.name).c_str());
        if (bannedNamesQuery && bannedNamesQuery->fetch()[0].asUint32() > 0)
            return sendCreateResult(socket, E_CHAR_NAME_PROFANE);

        const auto charactersQuery = CharacterDatabase.query(
            "SELECT COUNT(*) FROM characters WHERE acct = %u", session->GetAccountId());
        if (charactersQuery && charactersQuery->fetch()[0].asUint32() >= 60U)
            return sendCreateResult(socket, E_CHAR_CREATE_SERVER_LIMIT);

        // Midnight character customizations are not equivalent to the legacy
        // skin/face/hair byte layout. Keep the old fields neutral until the
        // Midnight customization DB2 mapping is implemented.
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

        Player* newPlayer = sObjectMgr.createPlayer(createInfo._class);
        if (newPlayer == nullptr)
            return sendCreateResult(socket, E_CHAR_CREATE_FAILED);

        newPlayer->setSession(session);
        if (!newPlayer->create(createInfo))
        {
            sLogger.warning("Midnight::CharacterService: Player::create failed for race={} class={} name='{}'.",
                request.race, request.charClass, request.name);
            newPlayer->m_isReadyToBeRemoved = true;
            delete newPlayer;
            return sendCreateResult(socket, E_CHAR_CREATE_FAILED);
        }

        newPlayer->unsetBanned();
        newPlayer->saveToDB(true);

        auto playerInfo = std::make_unique<CachedCharacterInfo>();
        playerInfo->guid = newPlayer->getGuidLow();
        utf8_string name = newPlayer->getName();
        AscEmu::Util::Strings::capitalize(name);
        playerInfo->name = name;
        playerInfo->cl = newPlayer->getClass();
        playerInfo->race = newPlayer->getRace();
        playerInfo->gender = newPlayer->getGender();
        playerInfo->acct = session->GetAccountId();
        playerInfo->m_Group = nullptr;
        playerInfo->subGroup = 0;
        playerInfo->team = newPlayer->getTeam();
        playerInfo->m_guild = 0;
        playerInfo->guildRank = GUILD_RANK_NONE;
        playerInfo->lastOnline = UNIXTIME;

        const uint64_t createdGuid = newPlayer->getGuidLow();
        sObjectMgr.addCachedCharacterInfo(std::move(playerInfo));

        newPlayer->m_isReadyToBeRemoved = true;
        delete newPlayer;

        return sendCreateResult(socket, E_CHAR_CREATE_SUCCESS, createdGuid);
    }


}
