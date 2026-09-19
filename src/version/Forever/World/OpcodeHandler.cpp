#include "version/Forever/OpcodeTable.hpp"
#include "version/Forever/Packets/Packet.hpp"
#include "version/Forever/World/OpcodeHandlerRegistry.hpp"
#include "version/Forever/World/ProtocolUtils.hpp"
#include "world/Server/WorldSocket.hpp"
#include "Logging/Logger.hpp"

bool WorldSocket::sendForeverPacket(AscEmu::Version::Forever::Opcode opcode, const uint8_t* payload, uint32_t payloadSize)
{
    using namespace AscEmu::Version::Forever;

    const uint32_t rawOpcode = sOpcodeTable.getHexValueForInternalId(opcode);
    if (rawOpcode == 0)
    {
        sLogger.failure("WorldSocket::Forever: no wire opcode registered for {}.", sOpcodeTable.getNameForInternalId(opcode));
        return false;
    }

    return sendForeverWorldPacket(rawOpcode, payload, payloadSize);
}

bool WorldSocket::dispatchForeverOpcode(uint32_t rawOpcode, const uint8_t* payload, size_t payloadSize)
{
    using namespace AscEmu::Version::Forever;

    const Opcode opcode = sOpcodeTable.getInternalIdForHex(rawOpcode);
    if (opcode == Opcode::NONE)
    {
        sLogger.info("WorldSocket::Forever: encrypted RX opcode=0x{:08X}, payload={} byte(s), hex=[{}].", rawOpcode, payloadSize, bytesToHex(payload, payloadSize));
        return true;
    }

    Packets::Packet packet(opcode, payload, payloadSize);

    bool consumed = false;
    if (!processForeverGlueState(packet, consumed))
        return false;
    if (consumed)
        return true;

    if (opcode != Opcode::CMSG_DB_QUERY_BULK)
    {
        sLogger.info("WorldSocket::Forever: {} received size={}{}.", sOpcodeTable.getNameForInternalId(opcode), payloadSize, payloadSize <= 64U ? " bytes=" + bytesToHex(payload, payloadSize) : "");
    }

    return OpcodeHandlerRegistry::instance().handleOpcode(*this, packet);
}
