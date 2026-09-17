/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "world/Server/VersionAdapter.hpp"
#include "version/Midnight/World/BattleNetComm/BattleNetCommClient.hpp"

namespace AscEmu::VersionAdapter
{
    void startWorldServices(AscEmu::Threading::AEThreadPool& threadPool)
    {
        BattlenetComm::sBattleNetCommClient.start(threadPool);
    }

    void stopWorldServices()
    {
        BattlenetComm::sBattleNetCommClient.finalize();
    }
}
