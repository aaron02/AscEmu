/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "Network/ByteBuffer.hpp"
#include "version/Midnight/Opcodes.hpp"

#include <cstdint>

namespace AscEmu::Version::Midnight::Packets
{
    class Packet : public ByteBuffer
    {
    public:
        Packet() = default;
        explicit Packet(Opcode opcode) : m_opcode(opcode) {}
        Packet(Opcode opcode, const uint8_t* payload, size_t payloadSize) : m_opcode(opcode)
        {
            if (payload != nullptr && payloadSize != 0)
                append(payload, payloadSize);
        }

        [[nodiscard]] Opcode getOpcode() const noexcept { return m_opcode; }
        void setOpcode(Opcode opcode) noexcept { m_opcode = opcode; }

    private:
        Opcode m_opcode{Opcode::NONE};
    };
}
