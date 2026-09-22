#include "version/Forever/Opcodes.hpp"
#include "version/Forever/OpcodeTable.hpp"
#include "version/Forever/Packets/Packet.hpp"
#include "version/Forever/World/CharacterSelectBootstrap.hpp"
#include "version/Forever/World/ProtocolUtils.hpp"
#include "world/Server/WorldSocket.hpp"
#include "world/Server/WorldSession.h"
#include "world/Server/World.h"
#include "world/Storage/VersionDataBridge.hpp"
#include "world/Objects/Units/Creatures/CreatureDefines.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "shared/WoWGuid.hpp"
#include "Logging/Logger.hpp"

#include <array>
#include <ctime>
#include <cstring>

bool WorldSocket::handleForeverPingOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Forever;

    if (packet.remaining() != sizeof(uint64_t))
    {
        sLogger.warning("WorldSocket::Forever: CMSG_PING expected 8 bytes, got {}.", packet.remaining());
        return true;
    }

    uint32_t serial = 0;
    uint32_t latency = 0;
    packet >> serial >> latency;

    // Keep the WorldSocket/WorldSession heartbeat state in sync exactly like
    // the Midnight path. Merely replying with SMSG_PONG is not enough: the
    // session timeout logic uses m_lastPing to decide whether the client is
    // still alive.
    m_latency = latency;

    if (m_session != nullptr)
    {
        m_session->_latency = latency;
        m_session->m_lastPing = static_cast<uint32_t>(UNIXTIME);
        m_session->m_clientTimeDelay = 0;
    }

    std::array<uint8_t, sizeof(uint32_t)> pong{};
    std::memcpy(pong.data(), &serial, sizeof(serial));

    sLogger.info("WorldSocket::Forever: CMSG_PING serial={} latency={} -> SMSG_PONG; session heartbeat refreshed.", serial, latency);

    return sendForeverPacket(Opcode::SMSG_PONG, pong.data(), static_cast<uint32_t>(pong.size()));
}

bool WorldSocket::handleForeverSocialContractOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Forever;

    if (packet.remaining() != 0)
        sLogger.warning("WorldSocket::Forever: CMSG_SOCIAL_CONTRACT_REQUEST expected empty payload.");

    return sendForeverPacket(Opcode::SMSG_SOCIAL_CONTRACT_REQUEST_RESPONSE, CharacterSelectBootstrap::SocialContract460325.data(), static_cast<uint32_t>(CharacterSelectBootstrap::SocialContract460325.size()));
}

bool WorldSocket::handleForeverSocialContractAcceptOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    if (packet.remaining() != 0)
        sLogger.warning("WorldSocket::Forever: CMSG_SOCIAL_CONTRACT_ACCEPT expected empty payload.");

    sLogger.info("WorldSocket::Forever: CMSG_SOCIAL_CONTRACT_ACCEPT received; no response required.");
    return true;
}

bool WorldSocket::handleForeverServerTimeOffsetOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Forever;

    if (packet.remaining() != 0)
        sLogger.warning("WorldSocket::Forever: CMSG_SERVER_TIME_OFFSET_REQUEST expected empty payload.");

    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    std::array<uint8_t, sizeof(now)> response{};
    std::memcpy(response.data(), &now, sizeof(now));

    return sendForeverPacket(Opcode::SMSG_SERVER_TIME_OFFSET, response.data(), static_cast<uint32_t>(response.size()));
}


bool WorldSocket::handleForeverQueryCreatureOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Forever;

    if (packet.remaining() != sizeof(uint32_t))
    {
        sLogger.warning("WorldSocket::Forever: CMSG_QUERY_CREATURE expected 4 bytes, got {}.", packet.remaining());
        return true;
    }

    uint32_t creatureId = 0;
    packet >> creatureId;

    CreatureProperties const* creature = AscEmu::World::Storage::getCreaturePropertiesForVersionClient(creatureId);

    ByteBuffer response;
    response << creatureId;
    response.writeBit(creature != nullptr);
    response.flushBits();

    if (creature == nullptr)
    {
        sLogger.info("WorldSocket::Forever: CMSG_QUERY_CREATURE entry={} -> not found; sending Allow=0.", creatureId);
        return sendForeverPacket(Opcode::SMSG_QUERY_CREATURE_RESPONSE, response.contents(), static_cast<uint32_t>(response.size()));
    }

    // Forever 69893/69913 uses the modern CreatureQuery response shape. This
    // mirrors the 12.x QueryCreatureResponse layout and intentionally keeps
    // fields AscEmu does not currently store at safe defaults.
    const std::string& name = creature->Name;
    const std::string& title = creature->SubName;

    // Bit-packed CString lengths. CString sizes include the terminating NUL.
    response.writeBits(title.empty() ? 0U : static_cast<uint32_t>(title.size() + 1U), 11);
    response.writeBits(0U, 11); // TitleAlt
    response.writeBits(creature->icon_name.empty() ? 0U : static_cast<uint32_t>(creature->icon_name.size() + 1U), 6);
    response.writeBit(creature->Leader != 0);

    // Name[4] + NameAlt[4]. We currently have only the primary localized name.
    response.writeBits(name.empty() ? 0U : static_cast<uint32_t>(name.size() + 1U), 11);
    response.writeBits(0U, 11); // NameAlt[0]
    for (uint8_t i = 1; i < 4; ++i)
    {
        response.writeBits(0U, 11);
        response.writeBits(0U, 11);
    }
    response.flushBits();

    if (!name.empty())
        response << name;

    // Flags[3]
    response << creature->typeFlags << uint32_t(0) << uint32_t(0);
    response << static_cast<uint8_t>(creature->Type);
    response << static_cast<int32_t>(creature->Family);
    response << static_cast<int8_t>(creature->Rank);

    // ProxyCreatureID[2]
    response << creature->killcredit[0] << creature->killcredit[1];

    struct ForeverCreatureDisplay
    {
        uint32_t id;
        float scale;
    };

    std::array<ForeverCreatureDisplay, 4> displays{{
        { creature->Male_DisplayID, creature->Scale > 0.0f ? creature->Scale : 1.0f },
        { creature->Female_DisplayID, creature->Scale > 0.0f ? creature->Scale : 1.0f },
        { creature->Male_DisplayID2, creature->Scale > 0.0f ? creature->Scale : 1.0f },
        { creature->Female_DisplayID2, creature->Scale > 0.0f ? creature->Scale : 1.0f }
    }};

    uint32_t displayCount = 0;
    for (auto const& display : displays)
        if (display.id != 0)
            ++displayCount;

    response << displayCount;
    response << (displayCount != 0 ? 1.0f : 0.0f); // TotalProbability

    const float probability = displayCount != 0 ? 1.0f / static_cast<float>(displayCount) : 0.0f;
    for (auto const& display : displays)
    {
        if (display.id == 0)
            continue;

        response << display.id;
        response << display.scale;
        response << probability;
    }

    // Health/energy multipliers. AscEmu's legacy fields are attack modifiers,
    // not the modern query multipliers, so do not repurpose them here.
    response << float(1.0f) << float(1.0f);

    uint32_t questItemCount = 0;
    for (uint32_t questItem : creature->QuestItems)
        if (questItem != 0)
            ++questItemCount;

    response << questItemCount;
    response << uint32_t(0); // QuestCurrencies count

    // Fields introduced/retained by the modern response and not represented
    // by the legacy CreatureProperties schema yet.
    response << int32_t(0);  // CreatureMovementInfoID
    response << int32_t(0);  // HealthScalingExpansion
    response << int32_t(0);  // RequiredExpansion
    response << int32_t(0);  // VignetteID
    response << int32_t(0);  // Class
    response << int32_t(0);  // CreatureDifficultyID
    response << int32_t(0);  // WidgetSetID
    response << int32_t(0);  // WidgetSetUnitConditionID

    if (!title.empty())
        response << title;
    if (!creature->icon_name.empty())
        response << creature->icon_name;

    if (questItemCount != 0)
    {
        for (uint32_t questItem : creature->QuestItems)
            if (questItem != 0)
                response << static_cast<int32_t>(questItem);
    }

    sLogger.info(
        "WorldSocket::Forever: CMSG_QUERY_CREATURE entry={} name='{}' title='{}' displays={} -> SMSG_QUERY_CREATURE_RESPONSE opcode=0x004A0006 payload={} byte(s).",
        creatureId,
        name,
        title,
        displayCount,
        response.size());

    return sendForeverPacket(Opcode::SMSG_QUERY_CREATURE_RESPONSE, response.contents(), static_cast<uint32_t>(response.size()));
}

bool WorldSocket::handleForeverListInventoryOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    WoWGuid modernGuid;
    std::size_t consumed = 0;

    if (!WoWGuid::unpackModern(packet.contents(), packet.size(), modernGuid, consumed) || consumed != packet.size())
    {
        sLogger.warning("WorldSocket::Forever: malformed CMSG_LIST_INVENTORY payload={} byte(s), consumed={}.", packet.size(), consumed);
        return true;
    }

    if (modernGuid.getModernHighType() != ModernHighGuid::Creature && modernGuid.getModernHighType() != ModernHighGuid::Vehicle)
    {
        sLogger.warning("WorldSocket::Forever: CMSG_LIST_INVENTORY target has unexpected modern high type={} entry={} counter={}.", static_cast<uint32_t>(modernGuid.getModernHighType()), modernGuid.getModernEntry(), modernGuid.getModernCounter());
        return true;
    }

    if (m_session == nullptr)
    {
        sLogger.warning("WorldSocket::Forever: CMSG_LIST_INVENTORY received without an attached WorldSession.");
        return false;
    }

    const uint64_t legacyGuid = modernGuid.toLegacyRaw();

    sLogger.info("WorldSocket::Forever: CMSG_LIST_INVENTORY target entry={} counter={} modernLow=0x{:016X} modernHigh=0x{:016X} -> legacyGuid=0x{:016X}.", modernGuid.getModernEntry(), modernGuid.getModernCounter(), modernGuid.getModernLow(), modernGuid.getModernHigh(), legacyGuid);

    m_session->handleListInventoryGuid(legacyGuid);
    return true;
}

bool WorldSocket::handleForeverSetSelectionOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    if (m_session == nullptr || m_session->GetPlayer() == nullptr)
        return false;

    WoWGuid modernGuid;
    std::size_t consumed = 0;
    if (!WoWGuid::unpackModern(packet.contents(), packet.size(), modernGuid, consumed) || consumed != packet.size())
    {
        sLogger.warning("WorldSocket::Forever: malformed CMSG_SET_SELECTION payload={} consumed={}.", packet.size(), consumed);
        return true;
    }

    Player* const player = m_session->GetPlayer();
    const uint64_t legacyGuid = modernGuid.toLegacyRaw();
    player->setTargetGuid(legacyGuid);

    if (player->getComboPoints())
        player->updateComboPoints();

    sLogger.debug("WorldSocket::Forever: CMSG_SET_SELECTION target=0x{:016X} type={} entry={} counter={}.", legacyGuid, static_cast<uint32_t>(modernGuid.getModernHighType()), modernGuid.getModernEntry(), modernGuid.getModernCounter());
    return true;
}
