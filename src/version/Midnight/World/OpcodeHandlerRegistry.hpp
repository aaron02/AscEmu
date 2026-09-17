/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "version/Midnight/Opcodes.hpp"
#include "version/Midnight/Packets/Packet.hpp"

#include <cstdint>
#include <unordered_map>

class WorldSocket;

namespace AscEmu::Version::Midnight
{
    enum OpcodeState : uint8_t
    {
        STATUS_CONNECTED = 0,
        STATUS_AUTHED
    };

    struct OpcodeHandlerEntry
    {
        using Handler = bool (WorldSocket::*)(Packets::Packet&);
        Handler handler{nullptr};
        OpcodeState state{STATUS_AUTHED};
    };

    class OpcodeHandlerRegistry
    {
    public:
        static OpcodeHandlerRegistry& instance();

        template <OpcodeState State = STATUS_AUTHED>
        void registerOpcode(Opcode opcode, OpcodeHandlerEntry::Handler handler)
        {
            m_handlers[opcode] = OpcodeHandlerEntry{handler, State};
        }

        void initialize();
        bool handleOpcode(WorldSocket& socket, Packets::Packet& packet);

    private:
        OpcodeHandlerRegistry() = default;

        struct OpcodeHash
        {
            size_t operator()(Opcode opcode) const noexcept
            {
                return static_cast<size_t>(opcode);
            }
        };

        std::unordered_map<Opcode, OpcodeHandlerEntry, OpcodeHash> m_handlers;
        bool m_initialized{false};
    };

}
