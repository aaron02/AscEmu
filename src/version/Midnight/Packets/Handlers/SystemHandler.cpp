#include "version/Midnight/Packets/SystemPackets.hpp"
#include "world/Server/WorldSocket.hpp"
#include "world/Server/WorldSession.h"
#include "world/Server/World.h"
#include "Logging/Logger.hpp"

bool WorldSocket::handleMidnightPingOpcode(AscEmu::Version::Midnight::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Midnight::Packets;

    CmsgPing request;
    if (!request.deserialise(packet))
    {
        sLogger.warning("WorldSocket::Midnight: malformed CMSG_PING.");
        return true;
    }

    m_latency = request.latency;
    m_session->_latency = request.latency;
    m_session->m_lastPing = static_cast<uint32_t>(UNIXTIME);
    m_session->m_clientTimeDelay = 0;

    SmsgPong response(request.serial);
    auto wire = response.serialise();
    return wire != nullptr && sendMidnightPacket(*wire);
}

bool WorldSocket::handleMidnightSocialContractOpcode(AscEmu::Version::Midnight::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Midnight::Packets;

    if (packet.remaining() != 0)
    {
        sLogger.warning("WorldSocket::Midnight: CMSG_SOCIAL_CONTRACT_REQUEST expected empty payload.");
        return true;
    }

    SmsgSocialContractRequestResponse response(false);
    auto wire = response.serialise();
    return wire != nullptr && sendMidnightPacket(*wire);
}

bool WorldSocket::handleMidnightServerTimeOffsetOpcode(AscEmu::Version::Midnight::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Midnight::Packets;

    if (packet.remaining() != 0)
    {
        sLogger.warning("WorldSocket::Midnight: CMSG_SERVER_TIME_OFFSET_REQUEST expected empty payload.");
        return true;
    }

    SmsgServerTimeOffset response(static_cast<uint32_t>(UNIXTIME));
    auto wire = response.serialise();
    return wire != nullptr && sendMidnightPacket(*wire);
}
