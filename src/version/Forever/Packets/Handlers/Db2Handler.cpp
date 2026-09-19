#include "version/Forever/Opcodes.hpp"
#include "version/Forever/Packets/Packet.hpp"
#include "version/Forever/World/ProtocolUtils.hpp"
#include "world/Server/WorldSocket.hpp"
#include "Logging/Logger.hpp"

#include <ctime>

bool WorldSocket::handleForeverDbQueryBulkOpcode(AscEmu::Version::Forever::Packets::Packet& request)
{
    using namespace AscEmu::Version::Forever;

    if (request.remaining() < sizeof(uint32_t))
    {
        sLogger.warning("WorldSocket::Forever: malformed CMSG_DB_QUERY_BULK.");
        return true;
    }

    uint32_t tableHash = 0;
    request >> tableHash;
    const uint32_t queryCount = request.readBits(13);

    if (request.rpos() + static_cast<size_t>(queryCount) * sizeof(uint32_t) > request.size())
    {
        sLogger.warning("WorldSocket::Forever: malformed CMSG_DB_QUERY_BULK table=0x{:08X}, count={}, remaining={}.", tableHash, queryCount, request.remaining());
        return true;
    }

    const uint32_t timestamp = static_cast<uint32_t>(std::time(nullptr));
    constexpr uint8_t ForeverDb2StatusInvalid = 4U;

    for (uint32_t i = 0; i < queryCount; ++i)
    {
        uint32_t recordId = 0;
        request >> recordId;

        ByteBuffer response;
        response << tableHash << recordId << timestamp;
        response.writeBits<uint8_t>(ForeverDb2StatusInvalid, 3);
        response.flushBits();
        response << uint32_t(0);

        if (!sendForeverPacket(Opcode::SMSG_DB_REPLY, response.contents(), static_cast<uint32_t>(response.size())))
            return false;
    }

    ++m_foreverDbQueryBulkCount;
    sLogger.info("WorldSocket::Forever: CMSG_DB_QUERY_BULK #{} table=0x{:08X}, records={} -> Invalid(4).", m_foreverDbQueryBulkCount, tableHash, queryCount);

    if (m_foreverPostDbEnumRefreshPending && !m_foreverPostDbEnumRefreshSent && m_foreverDbQueryBulkCount >= 2U)
    {
        m_foreverPostDbEnumRefreshSent = true;
        m_foreverPostDbEnumRefreshPending = false;

        sLogger.info("WorldSocket::Forever: DB2 bootstrap reached batch #{}; refreshing DB-backed character enum once.", m_foreverDbQueryBulkCount);

        return sendForeverCharacterEnumFromDatabase(true);
    }

    return true;
}

bool WorldSocket::handleForeverHotfixRequestOpcode(AscEmu::Version::Forever::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Forever;

    sLogger.info("WorldSocket::Forever: CMSG_HOTFIX_REQUEST received with {} byte(s); no hotfix records configured, request ignored. bytes=[{}].", packet.size(), bytesToHex(packet.contents(), packet.size()));
    return true;
}
