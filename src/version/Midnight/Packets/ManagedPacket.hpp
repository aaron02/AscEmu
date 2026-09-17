/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "version/Midnight/Packets/Packet.hpp"

#include <memory>

namespace AscEmu::Version::Midnight::Packets
{
    class ManagedPacket
    {
    protected:
        ManagedPacket(Opcode opcode, size_t minimumSize) : m_opcode(opcode), m_minimumSize(minimumSize) {}
        virtual ~ManagedPacket() = default;

        virtual bool internalSerialise(Packet&) { return true; }
        virtual bool internalDeserialise(Packet&) { return true; }
        virtual size_t expectedSize() const { return 0; }

        Opcode m_opcode;
        size_t m_minimumSize;

    public:
        std::unique_ptr<Packet> serialise()
        {
            auto packet = std::make_unique<Packet>(m_opcode);
            packet->reserve(expectedSize());
            if (!internalSerialise(*packet))
                return nullptr;
            return packet;
        }

        bool deserialise(Packet& packet)
        {
            if (packet.remaining() < m_minimumSize)
                return false;
            return internalDeserialise(packet);
        }
    };
}
