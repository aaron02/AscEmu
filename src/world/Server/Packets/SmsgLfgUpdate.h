/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "ManagedPacket.h"
#include <cstdint>

namespace AscEmu::Packets
{
    // TBC-only. Not to be confused with the WotLK+ Dungeon Finder's SMSG_LFG_UPDATE_STATUS;
    // Classic has no equivalent packet at all.
    class SmsgLfgUpdate : public ManagedPacket
    {
    public:
        bool queued = false;
        bool lfg = false;
        bool lfm = false;
        uint32_t lfmData = 0;

        SmsgLfgUpdate() : SmsgLfgUpdate(false, false, false, 0)
        {
        }

        SmsgLfgUpdate(bool queued, bool lfg, bool lfm, uint32_t lfmData) :
            ManagedPacket(SMSG_LFG_UPDATE, 0),
            queued(queued),
            lfg(lfg),
            lfm(lfm),
            lfmData(lfmData)
        {
        }

    protected:
        size_t expectedSize() const override { return 3 + (lfm ? 4 : 0); }

        bool internalSerialise(WorldPacket& packet) override
        {
            if (!m_protocol.isTbc())
                return false;

            packet << uint8_t(queued) << uint8_t(lfg) << uint8_t(lfm);
            if (lfm)
                packet << lfmData;

            return true;
        }
    };
}
