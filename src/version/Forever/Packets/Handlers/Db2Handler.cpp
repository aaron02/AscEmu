#include "version/Forever/Opcodes.hpp"
#include "version/Forever/Packets/Packet.hpp"
#include "version/Forever/World/Db2Registry.hpp"
#include "version/Forever/World/ProtocolUtils.hpp"
#include "world/Server/WorldSocket.hpp"
#include "Logging/Logger.hpp"

#include <algorithm>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

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

    // The status field is three bits wide. Forever captures established 4 as
    // the invalid/missing-record status. Status 0 is the normal record reply.
    constexpr uint8_t ForeverDb2StatusValid = 0U;
    constexpr uint8_t ForeverDb2StatusInvalid = 4U;

    uint32_t served = 0;
    uint32_t missing = 0;
    std::string resolvedTable;
    std::vector<uint32_t> requestedIds;
    requestedIds.reserve(std::min<uint32_t>(queryCount, 16U));

    for (uint32_t i = 0; i < queryCount; ++i)
    {
        uint32_t recordId = 0;
        request >> recordId;
        if (requestedIds.size() < 16U)
            requestedIds.push_back(recordId);

        Db2::RecordView record;
        const bool found = Db2::getRecord(tableHash, recordId, record);
        if (resolvedTable.empty() && !record.tableName.empty())
            resolvedTable = record.tableName;

        ByteBuffer response;
        response << tableHash << recordId << timestamp;
        response.writeBits<uint8_t>(found ? ForeverDb2StatusValid : ForeverDb2StatusInvalid, 3);
        response.flushBits();

        if (found)
        {
            response << static_cast<uint32_t>(record.data.size());
            response.append(record.data.data(), record.data.size());
            ++served;
        }
        else
        {
            response << uint32_t(0);
            ++missing;
        }

        if (!sendForeverPacket(Opcode::SMSG_DB_REPLY, response.contents(), static_cast<uint32_t>(response.size())))
            return false;
    }

    ++m_foreverDbQueryBulkCount;
    if (resolvedTable.empty())
        resolvedTable = "<unresolved>";

    std::ostringstream requestedIdText;
    for (size_t index = 0; index < requestedIds.size(); ++index)
    {
        if (index != 0)
            requestedIdText << ", ";
        requestedIdText << requestedIds[index];
    }

    sLogger.info("WorldSocket::Forever: CMSG_DB_QUERY_BULK #{} table=0x{:08X} source='{}' records={} served={} missing={} requestedIds(first {})=[{}].", m_foreverDbQueryBulkCount, tableHash, resolvedTable, queryCount, served, missing, requestedIds.size(), requestedIdText.str());

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
