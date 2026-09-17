/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

namespace AscEmu::Threading
{
    class AEThreadPool;
}

namespace AscEmu::VersionAdapter
{
    // Lifecycle hooks for client-version-specific world services.
    // Legacy builds use no-op implementations; modern profiles provide their
    // implementation under src/version/<profile>/World.
    void startWorldServices(AscEmu::Threading::AEThreadPool& threadPool);
    void stopWorldServices();
}
