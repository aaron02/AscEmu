/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace AscEmu::Version::Forever::ObjectUpdate
{
    struct SelfMovementState
    {
        std::vector<uint8_t> packedGuid;
        uint64_t movementFlags = 0;
        uint32_t moveTime = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float orientation = 0.0f;
        float pitch = 0.0f;
        float stepUpStartElevation = 0.0f;
        float walkSpeed = 2.5f;
        float runSpeed = 7.0f;
        float runBackSpeed = 4.5f;
        float swimSpeed = 4.722222f;
        float swimBackSpeed = 2.5f;
        float flightSpeed = 7.0f;
        float flightBackSpeed = 4.5f;
        float turnRate = 3.141594f;
        float pitchRate = 3.14f;
    };

    std::vector<uint8_t> buildSelfCreateBlock(const SelfMovementState& state, std::span<const uint8_t> updateFieldPayload);
    std::vector<uint8_t> buildUpdateObjectPacket(uint16_t mapId, std::span<const uint8_t> updateBlock);
    std::vector<uint8_t> buildTemporary69913SelfCreatePacket(uint16_t mapId, std::span<const uint8_t> packedGuid, float x, float y, float z, float orientation);
}
