#include "version/Midnight/World/OpcodeHandlerRegistry.hpp"
#include "version/Midnight/OpcodeTable.hpp"
#include "world/Server/WorldSocket.hpp"
#include "Logging/Logger.hpp"

namespace AscEmu::Version::Midnight
{
    OpcodeHandlerRegistry& OpcodeHandlerRegistry::instance()
    {
        static OpcodeHandlerRegistry instance;
        return instance;
    }

    void OpcodeHandlerRegistry::initialize()
    {
        if (m_initialized)
            return;

        // Character select / glue handlers.
        registerOpcode(Opcode::CMSG_ENUM_CHARACTERS, &WorldSocket::handleMidnightCharEnumOpcode);
        registerOpcode(Opcode::CMSG_CHECK_CHARACTER_NAME_AVAILABILITY, &WorldSocket::handleMidnightCheckCharacterNameOpcode);
        registerOpcode(Opcode::CMSG_CREATE_CHARACTER, &WorldSocket::handleMidnightCharCreateOpcode);
        registerOpcode(Opcode::CMSG_GET_UNDELETE_CHARACTER_COOLDOWN_STATUS, &WorldSocket::handleMidnightUndeleteCooldownOpcode);

        // DB2 and system handlers.
        registerOpcode(Opcode::CMSG_DB_QUERY_BULK, &WorldSocket::handleMidnightDbQueryBulkOpcode);
        registerOpcode(Opcode::CMSG_PING, &WorldSocket::handleMidnightPingOpcode);
        registerOpcode(Opcode::CMSG_SOCIAL_CONTRACT_REQUEST, &WorldSocket::handleMidnightSocialContractOpcode);
        registerOpcode(Opcode::CMSG_SERVER_TIME_OFFSET_REQUEST, &WorldSocket::handleMidnightServerTimeOffsetOpcode);

        m_initialized = true;

    }

    bool OpcodeHandlerRegistry::handleOpcode(WorldSocket& socket, Packets::Packet& packet)
    {
        initialize();

        const auto it = m_handlers.find(packet.getOpcode());
        if (it == m_handlers.end())
        {
            sLogger.warning("WorldSocket::Midnight: unhandled {}.",
                sOpcodeTable.getNameForInternalId(packet.getOpcode()));
            return true;
        }

        const auto& entry = it->second;
        if (entry.state == STATUS_AUTHED && socket.getSession() == nullptr)
        {
            sLogger.warning("WorldSocket::Midnight: received {} before WorldSession authentication completed.",
                sOpcodeTable.getNameForInternalId(packet.getOpcode()));
            return false;
        }

        return (socket.*entry.handler)(packet);
    }
}
