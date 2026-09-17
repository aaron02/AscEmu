#pragma once

#include "version/Midnight/Packets/ManagedPacket.hpp"

#include <cstdint>

namespace AscEmu::Version::Midnight::Packets
{
    class CmsgPing final : public ManagedPacket
    {
    public:
        uint32_t serial{0};
        uint32_t latency{0};

        CmsgPing() : ManagedPacket(Opcode::CMSG_PING, 8) {}

    protected:
        bool internalDeserialise(Packet& packet) override;
    };

    class SmsgPong final : public ManagedPacket
    {
    public:
        explicit SmsgPong(uint32_t serial) : ManagedPacket(Opcode::SMSG_PONG, 0), serial(serial) {}
        uint32_t serial{0};

    protected:
        bool internalSerialise(Packet& packet) override;
        size_t expectedSize() const override { return 4; }
    };

    class SmsgSocialContractRequestResponse final : public ManagedPacket
    {
    public:
        explicit SmsgSocialContractRequestResponse(bool show) : ManagedPacket(Opcode::SMSG_SOCIAL_CONTRACT_REQUEST_RESPONSE, 0), showSocialContract(show) {}
        bool showSocialContract{false};

    protected:
        bool internalSerialise(Packet& packet) override;
        size_t expectedSize() const override { return 1; }
    };

    class SmsgServerTimeOffset final : public ManagedPacket
    {
    public:
        explicit SmsgServerTimeOffset(uint32_t serverTime) : ManagedPacket(Opcode::SMSG_SERVER_TIME_OFFSET, 0), serverTime(serverTime) {}
        uint32_t serverTime{0};

    protected:
        bool internalSerialise(Packet& packet) override;
        size_t expectedSize() const override { return 4; }
    };
}
