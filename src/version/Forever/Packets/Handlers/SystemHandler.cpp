#include "version/Forever/Opcodes.hpp"
#include "version/Forever/OpcodeTable.hpp"
#include "version/Forever/Packets/Packet.hpp"
#include "version/Forever/World/CharacterSelectBootstrap.hpp"
#include "version/Forever/World/ProtocolUtils.hpp"
#include "world/Server/WorldSocket.hpp"
#include "world/Server/WorldSession.h"
#include "world/Server/World.h"
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
