#pragma once

#include "version/Midnight/Packets/ManagedPacket.hpp"

#include <cstdint>
#include <vector>

namespace AscEmu::Version::Midnight::Packets
{
    class CmsgDbQueryBulk final : public ManagedPacket
    {
    public:
        uint32_t tableHash{0};
        std::vector<uint32_t> recordIds;

        CmsgDbQueryBulk() : ManagedPacket(Opcode::CMSG_DB_QUERY_BULK, 6) {}

    protected:
        bool internalDeserialise(Packet& packet) override;
    };

    class SmsgDbReply final : public ManagedPacket
    {
    public:
        SmsgDbReply(uint32_t tableHash, uint32_t recordId, uint32_t timestamp, uint8_t status)
            : ManagedPacket(Opcode::SMSG_DB_REPLY, 0), tableHash(tableHash), recordId(recordId), timestamp(timestamp), status(status) {}

        uint32_t tableHash{0};
        uint32_t recordId{0};
        uint32_t timestamp{0};
        uint8_t status{0};

    protected:
        bool internalSerialise(Packet& packet) override;
    };
}
