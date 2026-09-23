#include "version/Forever/Opcodes.hpp"
#include "version/Forever/OpcodeTable.hpp"
#include "version/Forever/Packets/Packet.hpp"
#include "version/Forever/Packets/MovementPackets.hpp"
#include "version/Forever/World/CharacterSelectBootstrap.hpp"
#include "version/Forever/World/ProtocolUtils.hpp"
#include "world/Server/WorldSocket.hpp"
#include "world/Server/WorldSession.h"
#include "world/Server/World.h"
#include "world/Server/Opcodes.hpp"
#include "world/Storage/VersionDataBridge.hpp"
#include "world/Objects/Units/Creatures/CreatureDefines.hpp"
#include "Objects/Units/Players/Player.hpp"
#include "Objects/Units/Unit.hpp"
#include "Map/Visibility/VisibilityTypes.hpp"
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

    sLogger.debugFlag(AscEmu::Logging::LF_OPCODE, "WorldSocket::Forever: CMSG_PING serial={} latency={} -> SMSG_PONG; session heartbeat refreshed.", serial, latency);

    return sendForeverPacket(Opcode::SMSG_PONG, pong.data(), static_cast<uint32_t>(pong.size()));
}

bool WorldSocket::handleForeverLogoutRequestOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    if (m_session == nullptr)
        return false;

    if (packet.remaining() != 1)
    {
        sLogger.warning("WorldSocket::Forever: CMSG_LOGOUT_REQUEST expected 1 byte, got {}.", packet.remaining());
        return true;
    }

    const bool idleLogout = packet.readBit();

    sLogger.debugFlag(AscEmu::Logging::LF_OPCODE, "WorldSocket::Forever: CMSG_LOGOUT_REQUEST idleLogout={}.", idleLogout ? 1 : 0);

    m_session->handleForeverLogoutRequest(idleLogout);
    return true;
}

bool WorldSocket::handleForeverLogoutCancelOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    if (m_session == nullptr)
        return false;

    if (packet.remaining() != 0)
        sLogger.warning("WorldSocket::Forever: CMSG_LOGOUT_CANCEL expected empty payload, got {} byte(s).", packet.remaining());

    sLogger.debugFlag(AscEmu::Logging::LF_OPCODE, "WorldSocket::Forever: CMSG_LOGOUT_CANCEL.");
    m_session->handleForeverLogoutCancel();
    return true;
}

bool WorldSocket::handleForeverUnknown003E002DOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    // Observed as 0x003E002D with a 5-byte payload. Semantics are not verified yet.
    sLogger.debugFlag(AscEmu::Logging::LF_OPCODE, "WorldSocket::Forever: UNKNOWN_003E002D received size={}.", packet.size());
    return true;
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

    sLogger.info("WorldSocket::Forever: CMSG_QUERY_CREATURE entry={} name='{}' title='{}' displays={} -> SMSG_QUERY_CREATURE_RESPONSE opcode=0x004A0006 payload={} byte(s).", creatureId, name, title, displayCount, response.size());

    return sendForeverPacket(Opcode::SMSG_QUERY_CREATURE_RESPONSE, response.contents(), static_cast<uint32_t>(response.size()));
}

bool WorldSocket::handleForeverQueryGameObjectOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    if (packet.remaining() < sizeof(uint32_t) + 2U)
    {
        sLogger.warning("WorldSocket::Forever: CMSG_QUERY_GAME_OBJECT payload too small: {} byte(s).", packet.remaining());
        return true;
    }

    uint32_t gameObjectEntry = 0;
    packet >> gameObjectEntry;

    WoWGuid modernGuid;
    std::size_t consumed = 0;
    if (!WoWGuid::unpackModern(packet.contents() + packet.rpos(), packet.remaining(), modernGuid, consumed) || consumed != packet.remaining())
    {
        sLogger.warning("WorldSocket::Forever: malformed CMSG_QUERY_GAME_OBJECT entry={} payload={} byte(s), guidBytes={} consumed={}.", gameObjectEntry, packet.size(), packet.remaining(), consumed);
        return true;
    }

    if (modernGuid.getModernHighType() != ModernHighGuid::GameObject && modernGuid.getModernHighType() != ModernHighGuid::Transport && modernGuid.getModernHighType() != ModernHighGuid::StaticDoor && modernGuid.getModernHighType() != ModernHighGuid::DynamicDoor)
    {
        sLogger.warning("WorldSocket::Forever: CMSG_QUERY_GAME_OBJECT entry={} target has unexpected modern high type={} guidEntry={} counter={}.", gameObjectEntry, static_cast<uint32_t>(modernGuid.getModernHighType()), modernGuid.getModernEntry(), modernGuid.getModernCounter());
        return true;
    }

    sLogger.debugFlag(AscEmu::Logging::LF_OPCODE, "WorldSocket::Forever: CMSG_QUERY_GAME_OBJECT entry={} guidEntry={} counter={} modernLow=0x{:016X} modernHigh=0x{:016X}.", gameObjectEntry, modernGuid.getModernEntry(), modernGuid.getModernCounter(), modernGuid.getModernLow(), modernGuid.getModernHigh());
    return true;
}

bool WorldSocket::handleForeverUnknownNpcInteract003F0029Opcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    WoWGuid modernGuid;
    std::size_t consumed = 0;

    if (!WoWGuid::unpackModern(packet.contents(), packet.size(), modernGuid, consumed) || consumed != packet.size())
    {
        sLogger.warning("WorldSocket::Forever: malformed UNKNOWN_NPC_INTERACT_003F0029 payload={} byte(s), consumed={}.", packet.size(), consumed);
        return true;
    }

    if (modernGuid.getModernHighType() != ModernHighGuid::Creature && modernGuid.getModernHighType() != ModernHighGuid::Vehicle)
    {
        sLogger.warning("WorldSocket::Forever: UNKNOWN_NPC_INTERACT_003F0029 target has unexpected modern high type={} entry={} counter={}.", static_cast<uint32_t>(modernGuid.getModernHighType()), modernGuid.getModernEntry(), modernGuid.getModernCounter());
        return true;
    }

    // Observed as 0x003F0029 with a modern packed creature GUID near NPC interaction.
    // The exact semantic meaning is intentionally left unassigned until verified.
    sLogger.debugFlag(AscEmu::Logging::LF_OPCODE, "WorldSocket::Forever: UNKNOWN_NPC_INTERACT_003F0029 entry={} counter={} modernLow=0x{:016X} modernHigh=0x{:016X}.", modernGuid.getModernEntry(), modernGuid.getModernCounter(), modernGuid.getModernLow(), modernGuid.getModernHigh());

    return true;
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

bool WorldSocket::handleMovementOpcodes(AscEmu::Version::Forever::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Forever;
    using namespace AscEmu::Version::Forever::Packets;

    if (m_session == nullptr || m_session->GetPlayer() == nullptr)
        return false;

    Player* const player = m_session->GetPlayer();
    if (player->isTransferPending() || player->isOnTaxi() || player->justDied())
        return true;

    Unit* const mover = player->m_controledUnit;
    if (mover == nullptr)
        return true;

    MovementStatus status;
    if (!readMovementStatus(packet, status))
    {
        sLogger.warning("WorldSocket::Forever: malformed {} movement payload size={} consumed={}.", sOpcodeTable.getNameForInternalId(packet.getOpcode()), packet.size(), packet.rpos());
        return true;
    }

    const uint64_t moverGuid = status.moverGuid.toLegacyRaw();
    if (moverGuid == 0 || moverGuid != mover->getGuid())
    {
        sLogger.warning("WorldSocket::Forever: {} rejected mover=0x{:016X}; controlled mover=0x{:016X}.", sOpcodeTable.getNameForInternalId(packet.getOpcode()), moverGuid, mover->getGuid());
        return true;
    }

    if (!isValidMapCoord(status.position.x, status.position.y, status.position.z, status.position.o))
    {
        sLogger.warning("WorldSocket::Forever: {} rejected invalid position x={} y={} z={} o={}.", sOpcodeTable.getNameForInternalId(packet.getOpcode()), status.position.x, status.position.y, status.position.z, status.position.o);
        return true;
    }

    MovementInfo movementInfo = toLegacyMovementInfo(status);

    // Feed the decoded modern snapshot into the established WorldSession
    // movement bookkeeping before changing the mover's authoritative position.
    // This keeps distance/turn checks based on the previous server state.
    m_session->sessionMovementInfo = movementInfo;

    uint16_t legacyMovementOpcode = 0;
    switch (packet.getOpcode())
    {
        case Opcode::CMSG_MOVE_START_FORWARD:      legacyMovementOpcode = MSG_MOVE_START_FORWARD; break;
        case Opcode::CMSG_MOVE_START_BACKWARD:     legacyMovementOpcode = MSG_MOVE_START_BACKWARD; break;
        case Opcode::CMSG_MOVE_START_STRAFE_LEFT:  legacyMovementOpcode = MSG_MOVE_START_STRAFE_LEFT; break;
        case Opcode::CMSG_MOVE_START_STRAFE_RIGHT: legacyMovementOpcode = MSG_MOVE_START_STRAFE_RIGHT; break;
        case Opcode::CMSG_MOVE_STOP_STRAFE:        legacyMovementOpcode = MSG_MOVE_STOP_STRAFE; break;
        case Opcode::CMSG_MOVE_JUMP:               legacyMovementOpcode = MSG_MOVE_JUMP; break;
        case Opcode::CMSG_MOVE_FALL_LAND:          legacyMovementOpcode = MSG_MOVE_FALL_LAND; break;
        case Opcode::CMSG_MOVE_STOP:               legacyMovementOpcode = MSG_MOVE_STOP; break;
        default: break;
    }

    if (mover == player)
    {
        if (legacyMovementOpcode != 0 && m_session->isHackDetectedInMovementData(legacyMovementOpcode))
            return true;

        m_session->updatePlayerMovementVars(legacyMovementOpcode);
    }

    if (mover->getStandState() != STANDSTATE_STAND && packet.getOpcode() == Opcode::CMSG_MOVE_START_FORWARD)
        mover->setStandState(STANDSTATE_STAND);

    if (mover->getEmoteState())
        mover->setEmoteState(EMOTE_ONESHOT_NONE);

    // SetPosition drives WorldMap::onObjectMoved(), so the new spatial/visibility
    // index follows Forever clients just like legacy clients.
    mover->obj_movement_info = movementInfo;
    mover->SetPosition(status.position.x, status.position.y, status.position.z, status.position.o);

    if (packet.getOpcode() == Opcode::CMSG_MOVE_FALL_LAND)
        mover->handleFall(movementInfo);
    else if ((movementInfo.flags & MOVEFLAG_FALLING) == 0)
        mover->m_zAxisPosition = movementInfo.position.z;

    sLogger.debugMove("Forever movement {} mover=0x{:016X} pos=({:.3f}, {:.3f}, {:.3f}, {:.3f}) flags=0x{:016X} time={}.", sOpcodeTable.getNameForInternalId(packet.getOpcode()), moverGuid, status.position.x, status.position.y, status.position.z, status.position.o, status.flags, status.moveTime);

    return true;
}
