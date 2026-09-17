/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "world/Server/WorldSocket.hpp"
#include "Logging/Logger.hpp"
#include "version/Midnight/World/OpcodeHandlerRegistry.hpp"
#include "version/Midnight/OpcodeTable.hpp"
#include "version/Midnight/BattleNet/Protocol.hpp"
#include "version/Midnight/Packets/Packet.hpp"

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    uint32_t readUInt32LE(const uint8_t* data)
    {
        uint32_t value = 0;
        std::memcpy(&value, data, sizeof(value));
        return value;
    }

    std::string bytesToHex(const uint8_t* data, size_t size)
    {
        static constexpr char Hex[] = "0123456789ABCDEF";
        std::string result(size * 2, '\0');
        for (size_t i = 0; i < size; ++i)
        {
            result[i * 2] = Hex[(data[i] >> 4) & 0x0F];
            result[i * 2 + 1] = Hex[data[i] & 0x0F];
        }
        return result;
    }
}

bool WorldSocket::sendMidnightPacket(AscEmu::Version::Midnight::Packets::Packet& packet)
{
    const auto& opcodeTable = AscEmu::Version::Midnight::sOpcodeTable;
    const uint32_t rawOpcode = opcodeTable.getHexValueForInternalId(packet.getOpcode());
    const auto opcodeName = opcodeTable.getNameForInternalId(packet.getOpcode());
    if (rawOpcode == 0)
    {
        sLogger.failure("WorldSocket::Midnight: no wire opcode registered for {}.", opcodeName);
        return false;
    }

    const bool sent = sendBattleNetV2Packet(rawOpcode, packet.contents(), static_cast<uint32_t>(packet.size()));
    if (sent)
    {
        // DB2 bulk traffic can generate hundreds of tiny SMSG_DB_REPLY packets
        // during glue-screen startup. Keep those out of INFO while retaining
        // normal packet visibility and all warnings/errors.
        if (packet.getOpcode() != AscEmu::Version::Midnight::Opcode::SMSG_DB_REPLY)
        {
            if (packet.size() <= 64U)
            {
                sLogger.info("WorldSocket::Midnight: {} sent size={} bytes={}.",
                    opcodeName, packet.size(), bytesToHex(packet.contents(), packet.size()));
            }
            else
            {
                sLogger.info("WorldSocket::Midnight: {} sent size={}.", opcodeName, packet.size());
            }
        }
    }

    return sent;
}

bool WorldSocket::processBattleNetV2EncryptedPacket()
{
    using namespace AscEmu::Version::Midnight;

    if (m_battleNetV2State != BattleNetV2State::Encrypted)
        return false;

    if (!m_battleNetV2EncryptedHeaderReady)
    {
        if (readBuffer.GetSize() < BattleNet::ClientHeaderSize)
        {
            return false;
        }

        std::array<uint8_t, BattleNet::ClientHeaderSize> header{};
        if (!readBuffer.Read(header.data(), header.size()))
            return false;

        const uint32_t packetSize = readUInt32LE(header.data());
        if (packetSize < sizeof(uint32_t) || packetSize > BattleNet::MaxPacketSize)
        {
            sLogger.failure("WorldSocket::Midnight: invalid encrypted packet size {} from {}:{}; closing socket.",
                packetSize, getRemoteIp(), getRemotePort());
            disconnect();
            return false;
        }

        m_battleNetV2EncryptedPacketSize = packetSize;
        std::memcpy(m_battleNetV2EncryptedPacketTag.data(), header.data() + sizeof(uint32_t), m_battleNetV2EncryptedPacketTag.size());
        std::memcpy(m_battleNetV2EncryptedOpcode.data(), header.data() + BattleNet::ServerHeaderSize, m_battleNetV2EncryptedOpcode.size());
        m_battleNetV2PacketRemaining = packetSize - static_cast<uint32_t>(sizeof(uint32_t));
        m_battleNetV2EncryptedHeaderReady = true;

    }

    if (readBuffer.GetSize() < m_battleNetV2PacketRemaining)
    {
        return false;
    }

    std::vector<uint8_t> encrypted(m_battleNetV2EncryptedPacketSize);
    std::memcpy(encrypted.data(), m_battleNetV2EncryptedOpcode.data(), m_battleNetV2EncryptedOpcode.size());
    if (m_battleNetV2PacketRemaining > 0 &&
        !readBuffer.Read(encrypted.data() + sizeof(uint32_t), m_battleNetV2PacketRemaining))
    {
        return false;
    }

    const uint64_t counter = m_battleNetV2CryptoRecvCounter;
    const std::array<uint8_t, BattleNet::AuthTagSize> tag = m_battleNetV2EncryptedPacketTag;

    m_battleNetV2EncryptedHeaderReady = false;
    m_battleNetV2EncryptedPacketSize = 0;
    m_battleNetV2PacketRemaining = 0;
    m_battleNetV2EncryptedPacketTag.fill(0);
    m_battleNetV2EncryptedOpcode.fill(0);

    if (!decryptBattleNetV2Payload(encrypted, tag))
    {
        sLogger.failure("WorldSocket::Midnight: AES-256-GCM authentication failed at recv counter {}; closing socket.", counter);
        disconnect();
        return false;
    }

    ++m_battleNetV2RecvCounter;

    const uint32_t rawOpcode = readUInt32LE(encrypted.data());
    const size_t payloadSize = encrypted.size() - sizeof(uint32_t);
    const uint8_t* payload = payloadSize != 0 ? encrypted.data() + sizeof(uint32_t) : nullptr;
    const Opcode internalOpcode = sOpcodeTable.getInternalIdForHex(rawOpcode);

    // The first encrypted response completes the World V2 handoff. Keep this
    // transport state transition outside the opcode registry: it is framing/auth
    // state, not a gameplay packet handler.
    if (m_session == nullptr && m_battleNetGameAccountId != 0)
    {
        if (!finalizeBattleNetV2WorldSession())
        {
            sLogger.failure("WorldSocket::Midnight: failed to finalize WorldSession after encrypted handoff.");
            disconnect();
            return false;
        }
    }

    if (internalOpcode == Opcode::NONE)
    {
        sLogger.warning("WorldSocket::Midnight: unhandled opcode 0x{:08X} received with size={} and bytes={}.",
            rawOpcode, payloadSize, bytesToHex(payload, payloadSize));
        return true;
    }

    // DB2 bulk requests are expected during glue startup and are intentionally
    // omitted from INFO to avoid drowning useful protocol events.
    if (internalOpcode != Opcode::CMSG_DB_QUERY_BULK)
    {
        if (payloadSize <= 64U)
        {
            sLogger.info("WorldSocket::Midnight: {} received size={} bytes={}.",
                sOpcodeTable.getNameForInternalId(internalOpcode), payloadSize, bytesToHex(payload, payloadSize));
        }
        else
        {
            sLogger.info("WorldSocket::Midnight: {} received size={}.",
                sOpcodeTable.getNameForInternalId(internalOpcode), payloadSize);
        }
    }

    Packets::Packet packet(internalOpcode, payload, payloadSize);
    return OpcodeHandlerRegistry::instance().handleOpcode(*this, packet);
}
