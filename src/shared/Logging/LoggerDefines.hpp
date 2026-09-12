/*
Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include <string_view>

namespace AscEmu::Logging
{
    inline constexpr std::string_view CONSOLE_COLOR_RED = "\033[0;31m";
    inline constexpr std::string_view CONSOLE_COLOR_GREEN = "\033[0;32m";
    inline constexpr std::string_view CONSOLE_COLOR_YELLOW = "\033[1;33m";
    inline constexpr std::string_view CONSOLE_COLOR_NORMAL = "\033[0m";
    inline constexpr std::string_view CONSOLE_COLOR_WHITE = "\033[1;37m";
    inline constexpr std::string_view CONSOLE_COLOR_BLUE = "\033[0;34m";
    inline constexpr std::string_view CONSOLE_COLOR_CYAN = "\033[1;36m";
    inline constexpr std::string_view CONSOLE_COLOR_PURPLE = "\033[0;35m";
}
