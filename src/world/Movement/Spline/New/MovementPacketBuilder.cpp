/*
Copyright (c) 2014-2021 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "MovementPacketBuilder.h"
#include "ByteBuffer.h"
#include "MoveSpline.h"

namespace MovementNew
{
    inline void operator<<(ByteBuffer& b, Vector3 const& v)
    {
        b << v.x << v.y << v.z;
    }

    inline void operator>>(ByteBuffer& b, Vector3& v)
    {
        b >> v.x >> v.y >> v.z;
    }

    enum MonsterMoveType
    {
        MonsterMoveNormal       = 0,
        MonsterMoveStop         = 1,
        MonsterMoveFacingSpot   = 2,
        MonsterMoveFacingTarget = 3,
        MonsterMoveFacingAngle  = 4
    };

    void PacketBuilder::WriteCommonMonsterMovePart(MoveSpline const& move_spline, ByteBuffer& data)
    {
        MoveSplineFlag splineflags = move_spline.splineflags;

        data << uint8_t(0);
        data << move_spline.spline.getPoint(move_spline.spline.first());
        data << move_spline.GetId();

        switch (splineflags & MoveSplineFlag::Mask_Final_Facing)
        {
            case MoveSplineFlag::Final_Target:
                data << uint8_t(MonsterMoveFacingTarget);
                data << move_spline.facing.target;
                break;
            case MoveSplineFlag::Final_Angle:
                data << uint8_t(MonsterMoveFacingAngle);
                data << move_spline.facing.angle;
                break;
            case MoveSplineFlag::Final_Point:
                data << uint8_t(MonsterMoveFacingSpot);
                data << move_spline.facing.f.x << move_spline.facing.f.y << move_spline.facing.f.z;
                break;
            default:
                data << uint8_t(MonsterMoveNormal);
                break;
        }

        data << uint32_t(splineflags & uint32_t(~MoveSplineFlag::Mask_No_Monster_Move));

        if (splineflags.animation)
        {
            data << splineflags.animTier;
            data << move_spline.effect_start_time;
        }

        data << move_spline.Duration();

        if (splineflags.parabolic)
        {
            data << move_spline.vertical_acceleration;
            data << move_spline.effect_start_time;
        }
    }

    void PacketBuilder::WriteStopMovement(G3D::Vector3 const& pos, uint32_t splineId, ByteBuffer& data)
    {
        data << uint8_t(0);
        data << pos;
        data << splineId;
        data << uint8_t(MonsterMoveStop);
    }

    void WriteLinearPath(Spline<int32> const& spline, ByteBuffer& data)
    {
        uint32_t last_idx = spline.getPointCount() - 3;
        G3D::Vector3 const* real_path = &spline.getPoint(1);

        data << last_idx;
        data << real_path[last_idx];   // destination
        if (last_idx > 1)
        {
            G3D::Vector3 middle = (real_path[0] + real_path[last_idx]) / 2.f;
            G3D::Vector3 offset;
            // first and last points already appended
            for (uint32_t i = 1; i < last_idx; ++i)
            {
                offset = middle - real_path[i];
                data << LocationVector(offset.x, offset.y, offset.z);
            }
        }
    }

    void WriteCatmullRomPath(Spline<int32> const& spline, ByteBuffer& data)
    {
        uint32_t count = spline.getPointCount() - 3;
        data << count;
        data.append<G3D::Vector3>(&spline.getPoint(2), count);
    }

    void WriteCatmullRomCyclicPath(Spline<int32> const& spline, ByteBuffer& data)
    {
        uint32_t count = spline.getPointCount() - 4;
        data << count;
        data.append<Vector3>(&spline.getPoint(2), count);
    }

    void PacketBuilder::WriteMonsterMove(MoveSpline const& move_spline, ByteBuffer& data)
    {
        WriteCommonMonsterMovePart(move_spline, data);

        const Spline<int32_t>& spline = move_spline.spline;
        MoveSplineFlag splineflags = move_spline.splineflags;
        if (splineflags & MoveSplineFlag::Mask_CatmullRom)
        {
            if (splineflags.cyclic)
                WriteCatmullRomCyclicPath(spline, data);
            else
                WriteCatmullRomPath(spline, data);
        }
        else
        {
            WriteLinearPath(spline, data);
        }
    }

    void PacketBuilder::WriteCreate(MoveSpline const& move_spline, ByteBuffer& data)
    {
        {
            MoveSplineFlag const& splineFlags = move_spline.splineflags;

            data << splineFlags.raw();

            if (splineFlags.final_angle)
            {
                data << move_spline.facing.angle;
            }
            else if (splineFlags.final_target)
            {
                data << move_spline.facing.target;
            }
            else if (splineFlags.final_point)
            {
                data << move_spline.facing.f.x << move_spline.facing.f.y << move_spline.facing.f.z;
            }

            data << move_spline.timePassed();
            data << move_spline.Duration();
            data << move_spline.GetId();

            data << float(1.f);                             // splineInfo.duration_mod; added in 3.1
            data << float(1.f);                             // splineInfo.duration_mod_next; added in 3.1

            data << move_spline.vertical_acceleration;      // added in 3.1
            data << move_spline.effect_start_time;          // added in 3.1

            uint32_t nodes = move_spline.getPath().size();
            data << nodes;
            data.append<G3D::Vector3>(&move_spline.getPath()[0], nodes);
            data << uint8_t(move_spline.spline.mode());       // added in 3.1
            data << (move_spline.isCyclic() ? G3D::Vector3::zero() : move_spline.FinalDestination());
        }
    }

    void PacketBuilder::WriteSplineSync(MoveSpline const& move_spline, ByteBuffer& data)
    {
        data << (float)move_spline.timePassed() / move_spline.Duration();
    }
}