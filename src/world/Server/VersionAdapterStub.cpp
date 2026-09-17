/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "VersionAdapter.hpp"

#if !AE_HAS_WORLD_V2_PROFILE
namespace AscEmu::VersionAdapter
{
    void startWorldServices(AscEmu::Threading::AEThreadPool&)
    {
    }

    void stopWorldServices()
    {
    }
}
#endif
