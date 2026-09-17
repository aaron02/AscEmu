#include "version/Midnight/Packets/Db2Packets.hpp"

namespace AscEmu::Version::Midnight::Packets
{
    bool CmsgDbQueryBulk::internalDeserialise(Packet& packet)
    {
        packet >> tableHash;
        const uint32_t queryCount = packet.readBits(13);
        if (packet.rpos() + static_cast<size_t>(queryCount) * sizeof(uint32_t) > packet.size())
            return false;

        recordIds.clear();
        recordIds.reserve(queryCount);
        for (uint32_t i = 0; i < queryCount; ++i)
        {
            uint32_t recordId = 0;
            packet >> recordId;
            recordIds.emplace_back(recordId);
        }
        return true;
    }

    bool SmsgDbReply::internalSerialise(Packet& packet)
    {
        packet << tableHash << recordId << timestamp;
        packet.writeBits<uint8_t>(status, 3);
        packet.flushBits();
        packet << uint32_t(0); // empty data block
        return true;
    }
}
