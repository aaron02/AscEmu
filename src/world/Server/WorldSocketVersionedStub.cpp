/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "WorldSocket.hpp"

#if !AE_HAS_WORLD_V2_PROFILE

bool WorldSocket::initializeVersionedConnection()
{
    return false;
}

bool WorldSocket::processVersionedRead()
{
    return false;
}

bool WorldSocket::sendVersionedPacket(WorldPacket*)
{
    return false;
}

bool WorldSocket::setVersionedClientProtocolByBuild(uint32_t)
{
    return false;
}


#endif
