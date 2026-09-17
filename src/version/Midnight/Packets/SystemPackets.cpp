#include "version/Midnight/Packets/SystemPackets.hpp"

namespace AscEmu::Version::Midnight::Packets
{
    bool CmsgPing::internalDeserialise(Packet& packet)
    {
        packet >> serial >> latency;
        return packet.remaining() == 0;
    }

    bool SmsgPong::internalSerialise(Packet& packet)
    {
        packet << serial;
        return true;
    }

    bool SmsgSocialContractRequestResponse::internalSerialise(Packet& packet)
    {
        packet.writeBit(showSocialContract ? 1 : 0);
        packet.flushBits();
        return true;
    }

    bool SmsgServerTimeOffset::internalSerialise(Packet& packet)
    {
        packet << serverTime;
        return true;
    }
}
