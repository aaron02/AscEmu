/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "ObjectUpdate.hpp"
#include "SelfUpdateTemplate69913.hpp"

#include "Network/ByteBuffer.hpp"

#include <cmath>
#include <cstring>

namespace AscEmu::Version::Forever::ObjectUpdate
{
    namespace
    {
        constexpr uint8_t UPDATE_TYPE_CREATE_OBJECT_2 = 2;
        constexpr uint8_t OBJECT_TYPE_ACTIVE_PLAYER = 7;

        constexpr float ADV_FLYING_AIR_FRICTION = 2.0f;
        constexpr float ADV_FLYING_MAX_VELOCITY = 65.0f;
        constexpr float ADV_FLYING_LIFT_COEFFICIENT = 1.0f;
        constexpr float ADV_FLYING_DOUBLE_JUMP_VELOCITY_MODIFIER = 3.0f;
        constexpr float ADV_FLYING_GLIDE_START_MIN_HEIGHT = 10.0f;
        constexpr float ADV_FLYING_ADD_IMPULSE_MAX_SPEED = 100.0f;
        constexpr float ADV_FLYING_BANKING_RATE_MIN = 1.57079632679f;
        constexpr float ADV_FLYING_BANKING_RATE_MAX = 2.44346095279f;
        constexpr float ADV_FLYING_PITCHING_RATE_DOWN_MIN = 3.14159265359f;
        constexpr float ADV_FLYING_PITCHING_RATE_DOWN_MAX = 6.28318530718f;
        constexpr float ADV_FLYING_PITCHING_RATE_UP_MIN = 1.57079632679f;
        constexpr float ADV_FLYING_PITCHING_RATE_UP_MAX = 4.71238898038f;
        constexpr float ADV_FLYING_TURN_VELOCITY_THRESHOLD_MIN = 30.0f;
        constexpr float ADV_FLYING_TURN_VELOCITY_THRESHOLD_MAX = 80.0f;
        constexpr float ADV_FLYING_SURFACE_FRICTION = 2.75f;
        constexpr float ADV_FLYING_OVER_MAX_DECELERATION = 7.0f;
        constexpr float ADV_FLYING_LAUNCH_SPEED_COEFFICIENT = 0.4f;

        void writeSelfMovement(ByteBuffer& data, const SelfMovementState& state)
        {
            // CreateObjectBits for a self ActivePlayer: HasEntityPosition, ThisIsYou,
            // MovementUpdate and ActivePlayer. 1.60.1.69913 encodes these as 8C 00 80.
            data.writeBit(1); // HasEntityPosition
            data.writeBit(0); // NoBirthAnim
            data.writeBit(0); // EnablePortals
            data.writeBit(0); // PlayHoverAnim
            data.writeBit(1); // ThisIsYou
            data.writeBit(1); // MovementUpdate
            data.writeBit(0); // MovementTransport
            data.writeBit(0); // Stationary
            data.writeBit(0); // CombatVictim
            data.writeBit(0); // ServerTime
            data.writeBit(0); // Vehicle
            data.writeBit(0); // AnimKit
            data.writeBit(0); // Rotation
            data.writeBit(0); // GameObject
            data.writeBit(0); // SmoothPhasing
            data.writeBit(0); // SceneObject
            data.writeBit(1); // ActivePlayer
            data.writeBit(0); // Conversation
            data.writeBit(0); // Room
            data.writeBit(0); // Decor
            data.writeBit(0); // MeshObject
            data.flushBits();

            data << uint32_t(0); // PauseTimes
            data.append(state.packedGuid.data(), state.packedGuid.size());
            data << state.movementFlags << state.moveTime << state.x << state.y << state.z << state.orientation << state.pitch << state.stepUpStartElevation;
            data << uint32_t(0) << uint32_t(0) << float(1.0f); // RemoveForces, MoveIndex, GravityModifier

            data.writeBit(0); // HasStandingOnGameObjectGUID
            data.writeBit(0); // HasTransport
            data.writeBit(0); // HasFall
            data.writeBit(0); // HasSpline
            data.writeBit(0); // HeightChangeFailed
            data.writeBit(0); // RemoteTimeValid
            data.writeBit(0); // HasInertia
            data.writeBit(0); // HasAdvFlying
            data.writeBit(0); // HasDriveStatus
            data.flushBits();

            data << state.walkSpeed << state.runSpeed << state.runBackSpeed << state.swimSpeed << state.swimBackSpeed << state.flightSpeed << state.flightBackSpeed << state.turnRate << state.pitchRate;
            data << uint32_t(0) << float(1.0f); // MovementForces and modifier
            data << ADV_FLYING_AIR_FRICTION << ADV_FLYING_MAX_VELOCITY << ADV_FLYING_LIFT_COEFFICIENT << ADV_FLYING_DOUBLE_JUMP_VELOCITY_MODIFIER << ADV_FLYING_GLIDE_START_MIN_HEIGHT << ADV_FLYING_ADD_IMPULSE_MAX_SPEED;
            data << ADV_FLYING_BANKING_RATE_MIN << ADV_FLYING_BANKING_RATE_MAX << ADV_FLYING_PITCHING_RATE_DOWN_MIN << ADV_FLYING_PITCHING_RATE_DOWN_MAX << ADV_FLYING_PITCHING_RATE_UP_MIN << ADV_FLYING_PITCHING_RATE_UP_MAX;
            data << ADV_FLYING_TURN_VELOCITY_THRESHOLD_MIN << ADV_FLYING_TURN_VELOCITY_THRESHOLD_MAX << ADV_FLYING_SURFACE_FRICTION << ADV_FLYING_OVER_MAX_DECELERATION << ADV_FLYING_LAUNCH_SPEED_COEFFICIENT;

            data.writeBit(0); // HasSpline data block
            data.flushBits();

            data.writeBit(0); // ActivePlayer: HasSceneInstanceIDs
            data.writeBit(0); // ActivePlayer: HasRuneState
            data.flushBits();
        }
    }

    std::vector<uint8_t> buildSelfCreateBlock(const SelfMovementState& state, std::span<const uint8_t> updateFieldPayload)
    {
        if (state.packedGuid.empty() || updateFieldPayload.empty())
            return {};

        ByteBuffer block;
        block << UPDATE_TYPE_CREATE_OBJECT_2;
        block.append(state.packedGuid.data(), state.packedGuid.size());
        block << OBJECT_TYPE_ACTIVE_PLAYER;
        writeSelfMovement(block, state);
        block << uint32_t(updateFieldPayload.size());
        block.append(updateFieldPayload.data(), updateFieldPayload.size());
        return std::vector<uint8_t>(block.contents(), block.contents() + block.size());
    }

    std::vector<uint8_t> buildUpdateObjectPacket(uint16_t mapId, std::span<const uint8_t> updateBlock)
    {
        if (updateBlock.empty())
            return {};

        ByteBuffer packet;
        packet << mapId << uint32_t(1);
        packet.writeBit(1); // 69913 UpdateData leading bit
        packet.writeBit(0); // no destroy/out-of-range GUIDs
        packet.flushBits();
        packet << uint32_t(updateBlock.size());
        packet.append(updateBlock.data(), updateBlock.size());
        return std::vector<uint8_t>(packet.contents(), packet.contents() + packet.size());
    }
    std::vector<uint8_t> buildTemporary69913SelfCreatePacket(uint16_t mapId, std::span<const uint8_t> packedGuid, float x, float y, float z, float orientation)
    {
        if (packedGuid.empty())
            return {};

        std::array<uint8_t, Template69913::MovementTail.size()> movementTail = Template69913::MovementTail;
        std::memcpy(movementTail.data() + 12, &x, sizeof(float));
        std::memcpy(movementTail.data() + 16, &y, sizeof(float));
        std::memcpy(movementTail.data() + 20, &z, sizeof(float));
        std::memcpy(movementTail.data() + 24, &orientation, sizeof(float));

        ByteBuffer block;
        block << uint8_t(UPDATE_TYPE_CREATE_OBJECT_2);
        block.append(packedGuid.data(), packedGuid.size());
        block << uint8_t(OBJECT_TYPE_ACTIVE_PLAYER);
        block << uint8_t(0x8C) << uint8_t(0x00) << uint8_t(0x80);
        block << uint32_t(0);
        block.append(packedGuid.data(), packedGuid.size());
        block.append(movementTail.data(), movementTail.size());
        block << uint32_t(Template69913::FieldPayload.size());
        block.append(Template69913::FieldPayload.data(), Template69913::FieldPayload.size());

        return buildUpdateObjectPacket(mapId, std::span<const uint8_t>(block.contents(), block.size()));
    }

}
