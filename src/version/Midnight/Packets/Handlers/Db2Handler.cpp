#include "version/Midnight/Packets/Db2Packets.hpp"
#include "world/Server/WorldSocket.hpp"
#include "world/Server/World.h"
#include "Logging/Logger.hpp"

bool WorldSocket::handleMidnightDbQueryBulkOpcode(AscEmu::Version::Midnight::Packets::Packet& packet)
{
    using namespace AscEmu::Version::Midnight::Packets;

    CmsgDbQueryBulk request;
    if (!request.deserialise(packet))
    {
        sLogger.warning("WorldSocket::Midnight: malformed CMSG_DB_QUERY_BULK.");
        return true;
    }

    constexpr uint8_t Db2StatusInvalid = 3U;
    for (const uint32_t recordId : request.recordIds)
    {
        SmsgDbReply response(request.tableHash, recordId, static_cast<uint32_t>(UNIXTIME), Db2StatusInvalid);
        auto wire = response.serialise();
        if (wire == nullptr || !sendMidnightPacket(*wire))
            return false;
    }

    return true;
}
