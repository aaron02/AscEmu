/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <cstdint>
#include <span>
#include <vector>

class ByteBuffer;

namespace AscEmu::Version::Forever::Fields
{
    struct ObjectData;
    struct UnitData;
    struct PlayerData;
    struct ActivePlayerData;
}

namespace AscEmu::Version::Forever::ObjectUpdate
{
    void writeObjectDataCreate(ByteBuffer& data, Fields::ObjectData const& fields);
    void writeUnitDataCreate(ByteBuffer& data, Fields::UnitData const& fields, bool ownerVisible);
    bool writePlayerDataCreate(ByteBuffer& data, Fields::PlayerData const& fields, bool partyMemberVisible);
    bool writeActivePlayerDataCreate(ByteBuffer& data, Fields::ActivePlayerData const& fields);

    std::vector<uint8_t> buildHybridSelfFieldPayload69913(
        Fields::ObjectData const& objectFields,
        Fields::UnitData const& unitFields,
        Fields::PlayerData const& playerFields,
        Fields::ActivePlayerData const& activePlayerFields);


    // 69913 create-only local updater path for ordinary world units.
    // This intentionally covers the stationary/minimal creature grammar first;
    // runtime VALUES masks and spline/transport movement remain separate work.
    std::vector<uint8_t> buildCreatureCreateBlock69913(
        std::span<const uint8_t> packedGuid,
        float x, float y, float z, float orientation, uint32_t movementTimeMs,
        Fields::ObjectData const& objectFields,
        Fields::UnitData const& unitFields);

    std::vector<uint8_t> buildUpdateObjectPacket69913(
        uint16_t mapId, uint32_t updateCount, std::span<const uint8_t> updateBlocks);

    std::vector<uint8_t> buildTemporary69913SelfCreatePacketWithFieldPayload(
        uint16_t mapId,
        std::span<const uint8_t> packedGuid,
        float x, float y, float z, float orientation,
        std::span<const uint8_t> fieldPayload);
}
