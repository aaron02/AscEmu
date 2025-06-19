/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "CommandOverrides.hpp"

#include "Server/DatabaseDefinition.hpp"

void CommandOverrides::loadOverrides()
{
    overrides.clear();

    auto stmt = CharacterDatabase.CreateStatement(CHAR_COMMAND_OVERRIDES_SELECT);
    auto result = CharacterDatabase.QueryStatement(std::move(stmt));

    if (!result)
        return;

    do
    {
        std::string command(result->Fetch()[0].asString());
        std::string permission(result->Fetch()[1].asString());

        overrides[command] = permission;
    } while (result->NextRow());
}

const std::string* CommandOverrides::getOverride(const std::string& command) const
{
    auto it = overrides.find(command);
    if (it != overrides.end())
        return &it->second;  // Return the override permission

    return nullptr;  // No override found
}

size_t CommandOverrides::getSize() const
{
    return overrides.size();
}
