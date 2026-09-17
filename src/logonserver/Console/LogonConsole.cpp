/*
 * AscEmu Framework based on ArcEmu MMORPG Server
 * Copyright (c) 2014-2026 AscEmu Team <http://www.ascemu.org>
 * Copyright (C) 2008-2012 ArcEmu Team <http://www.ArcEmu.org/>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "LogonConsole.h"
#include "Server/Logon.h"
#include <Logging/Logger.hpp>
#include <Server/Master.hpp>
#include <Server/AccountMgr.h>
#include <Server/IpBanMgr.h>
#include <Network/Network.hpp>
#include <LogonConf.hpp>
#include <sstream>
#include <Utilities/Strings.hpp>
#include <Cryptography/BNetSrpV1.hpp>
#include "Database/Database.hpp"
#include <algorithm>
#include "Threading/ThreadPool.hpp"


LogonConsole& LogonConsole::getInstance()
{
    static LogonConsole mInstance;
    return mInstance;
}

void LogonConsole::TranslateRehash(char* /*str*/)
{
    sLogger.info("rehashing config file...");
    if (sMasterLogon.LoadLogonConfiguration())
        sLogger.info("Rehashing config file finished succesfull!");
}

void LogonConsole::demoTicker(AscEmu::Threading::AEThread& /*thread*/)
{
    fmt::println("Thread ticker: {}", m_demoCounter);
    ++m_demoCounter;
}

void LogonConsole::threadDemoCmd(char* /*str*/)
{
    fmt::println("Thread Demo init");

    if (m_demoCounter != 0)
    {
        fmt::println("Existing thread found, rebooting");
        m_demoThread->reboot();
        return;
    }

    std::function<void(AscEmu::Threading::AEThread&)> f = [this](AscEmu::Threading::AEThread& thread) { this->demoTicker(thread); };
    m_demoThread = std::make_unique<AscEmu::Threading::AEThread>(std::string("DemoThread"), f, std::chrono::milliseconds(100));
}

void LogonConsole::Kill()
{
    if (_thread != nullptr)
        _thread->kill = true;
#ifdef WIN32
    /* write the return keydown/keyup event */
    DWORD dwTmp;
    INPUT_RECORD ir[2];
    ir[0].EventType = KEY_EVENT;
    ir[0].Event.KeyEvent.bKeyDown = TRUE;
    ir[0].Event.KeyEvent.dwControlKeyState = 288;
    ir[0].Event.KeyEvent.uChar.AsciiChar = 13;
    ir[0].Event.KeyEvent.wRepeatCount = 1;
    ir[0].Event.KeyEvent.wVirtualKeyCode = 13;
    ir[0].Event.KeyEvent.wVirtualScanCode = 28;
    ir[1].EventType = KEY_EVENT;
    ir[1].Event.KeyEvent.bKeyDown = FALSE;
    ir[1].Event.KeyEvent.dwControlKeyState = 288;
    ir[1].Event.KeyEvent.uChar.AsciiChar = 13;
    ir[1].Event.KeyEvent.wRepeatCount = 1;
    ir[1].Event.KeyEvent.wVirtualKeyCode = 13;
    ir[1].Event.KeyEvent.wVirtualScanCode = 28;
    WriteConsoleInput(GetStdHandle(STD_INPUT_HANDLE), ir, 2, &dwTmp);
#endif
    sLogger.info("Waiting for console thread to terminate....");
    while (_thread != nullptr)
    {
        AscEmu::Threading::sleep(100);
    }
    sLogger.info("Console shut down.");
}

void LogonConsoleThread::run(AscEmu::Threading::AEThread& thread)
{
    sLogonConsole._thread = this;
    kill = false;

    size_t i = 0;
    size_t len = 0;
    char cmd[96];

#ifndef WIN32
    fd_set fds;
    struct timeval tv;
#endif

    while (!kill.load() && !thread.isKilled())
    {
#ifndef WIN32
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        if (select(1, &fds, NULL, NULL, &tv) <= 0)
        {
            if (!kill.load() && !thread.isKilled())
                continue;
            else
                break;
        }
#endif

        memset(cmd, 0, sizeof(cmd));
        fgets(cmd, 80, stdin);

        if (kill.load() || thread.isKilled())
            break;

        len = strlen(cmd);
        for (i = 0; i < len; ++i)
        {
            if (cmd[i] == '\n' || cmd[i] == '\r')
                cmd[i] = '\0';
        }

        sLogonConsole.ProcessCmd(cmd);
    }

    sLogonConsole._thread = nullptr;
}

void LogonConsole::ProcessCmd(char* cmd)
{
    using PTranslater = void (LogonConsole::*)(char*);

    struct SCmd
    {
        std::string_view name;
        PTranslater tr;
    };

    static constexpr SCmd cmds[] =
    {
        { "?", &LogonConsole::TranslateHelp },
        { "z", &LogonConsole::threadDemoCmd },
        { "help", &LogonConsole::TranslateHelp },
        { "account create", &LogonConsole::AccountCreate },
        { "account delete", &LogonConsole::AccountDelete },
        { "account set password", &LogonConsole::AccountSetPassword },
        { "account change password", &LogonConsole::AccountChangePassword },
        { "bnet account create", &LogonConsole::BNetAccountCreate },
        { "bnet account set password", &LogonConsole::BNetAccountSetPassword },
        { "bnet account link", &LogonConsole::BNetAccountLink },
        { "reload", &LogonConsole::ReloadAccts },
        { "rehash", &LogonConsole::TranslateRehash },
        { "netstatus", &LogonConsole::NetworkStatus },
        { "shutdown", &LogonConsole::TranslateQuit },
        { "exit", &LogonConsole::TranslateQuit },
        { "info", &LogonConsole::Info },
    };

    if (!cmd)
        return;

    std::string input(cmd);
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const auto& c : cmds)
    {
        if (input.starts_with(c.name))
        {
            char* args = cmd + c.name.size();
            (this->*(c.tr))(args);
            return;
        }
    }

    fmt::println("[!]Error, Command '{}' doesn't exist. Type '?' or 'help' to get a command overview.", cmd);
}

void LogonConsole::ReloadAccts(char* /*str*/)
{
    sAccountMgr.reloadAccounts(false);
    sIpBanMgr.reload();
}

void LogonConsole::NetworkStatus(char* /*str*/)
{
    sSocketMgr.ShowStatus();
}

// quit | exit
void LogonConsole::TranslateQuit(char* str)
{
    int delay = str != NULL ? atoi(str) : 5000;
    if (!delay)
        delay = 5000;
    else
        delay *= 1000;

    ProcessQuit(delay);
}
void LogonConsole::ProcessQuit(int /*delay*/)
{
    mrunning = false;
}

///////////////////////////////////////////////////////////////////////////////
// Console commands - help | ?
void LogonConsole::TranslateHelp(char* /*str*/)
{
    ProcessHelp(NULL);
}

void LogonConsole::ProcessHelp(char* /*command*/)
{
    fmt::println("Console::Help");
    fmt::println("=============");
    fmt::println("Help, ?                   : Prints this help text.");
    fmt::println("Account create            : Creates a new account.");
    fmt::println("Account delete            : Deletes an account.");
    fmt::println("Account set password      : Sets a new password for an account.");
    fmt::println("Account change password   : Change the current password for a WoW game account.");
    fmt::println("BNet account create       : Creates a Battle.net login account.");
    fmt::println("BNet account set password : Changes a Battle.net login password.");
    fmt::println("BNet account link         : Links a WoW game account to a Battle.net account.");
    fmt::println("Info                      : Shows some information about the server.");
    fmt::println("Netstatus                 : Shows network status.");
    fmt::println("Rehash                    : Rehashing config file.");
    fmt::println("Reload                    : Reloads accounts.");
    fmt::println("Shutdown, Exit            : Closes the logonserver.");
}

void LogonConsole::Info(char* /*str*/)
{
    fmt::println("LogonServer information");
    fmt::println("=======================");
    fmt::println("CPU Usage : {}%", sLogon.getCPUUsage());
    fmt::println("RAM Usage : {}MB", sLogon.getRAMUsage());
}

void LogonConsole::AccountCreate(char* str)
{
    char name[512];
    char password[512];
    char email[512];

    int count = sscanf(str, "%s %s %s", name, password, email);
    if (count != 3)
    {
        fmt::println("usage: account create <name> <password> <email>");
        fmt::println("example: account create ghostcrawler Ih4t3p4l4dins greg.street@blizzard.com");
        return;
    }

    checkAccountName(name, ACC_NAME_NOT_EXIST);

    std::string pass;
    pass.assign(name);
    pass.push_back(':');
    pass.append(password);

    std::stringstream query;
    query << "INSERT INTO `accounts`(`acc_name`,`encrypted_password`,`banned`,`email`,`flags`,`banreason`) VALUES ('";
    query << name << "',";
    query << "SHA(UPPER('" << pass << "')),'0','";
    query << email << "','";
    query << AE_EXPANSION_VERSION << "','');";

    if (!sLogonSQL->waitExecuteNA(query.str().c_str()))
    {
        fmt::println("Couldn't save new account to database. Aborting.");
        return;
    }

    // If the supplied e-mail already belongs to a Battle.net account, link
    // the newly created WoW game account automatically.
    if (email[0] != '\0')
    {
        const std::string escapedEmail = sLogonSQL->escapeString(email);
        const std::string escapedName = sLogonSQL->escapeString(name);
        auto linkData = sLogonSQL->query(
            "SELECT ba.id, a.id FROM battlenet_accounts ba, accounts a "
            "WHERE UPPER(ba.email)=UPPER('%s') AND UPPER(a.acc_name)=UPPER('%s') LIMIT 1",
            escapedEmail.c_str(), escapedName.c_str());
        if (linkData && linkData->fetch())
        {
            Field* fields = linkData->fetch();
            sLogonSQL->waitExecute(
                "INSERT INTO battlenet_game_accounts(battlenet_account_id, game_account_id) VALUES(%u, %u) "
                "ON DUPLICATE KEY UPDATE battlenet_account_id=VALUES(battlenet_account_id)",
                fields[0].asUint32(), fields[1].asUint32());
        }
    }

    sAccountMgr.reloadAccounts(true);

    fmt::println("WoW game account created.");
}

void LogonConsole::AccountDelete(char* str)
{
    char name[512];

    int count = sscanf(str, "%s", name);
    if (count != 1)
    {
        fmt::println("usage: account delete <name>");
        fmt::println("example: account delete ghostcrawler");
        return;
    }

    checkAccountName(name, ACC_NAME_DO_EXIST);

    const std::string escapedName = sLogonSQL->escapeString(name);
    auto accountResult = sLogonSQL->query(
        "SELECT id FROM accounts WHERE UPPER(acc_name) = UPPER('%s') LIMIT 1",
        escapedName.c_str());
    if (accountResult && accountResult->fetch())
    {
        const uint32_t gameAccountId = accountResult->fetch()[0].asUint32();
        sLogonSQL->waitExecute(
            "DELETE FROM battlenet_game_accounts WHERE game_account_id = %u",
            gameAccountId);
    }

    std::stringstream query;
    query << "DELETE FROM `accounts` WHERE `acc_name` = '";
    query << name << "';";

    if (!sLogonSQL->waitExecuteNA(query.str().c_str()))
    {
        fmt::println("Couldn't delete account. Aborting.");
        return;
    }

    sAccountMgr.reloadAccounts(true);

    fmt::println("Account deleted.");
}

void LogonConsole::AccountSetPassword(char* str)
{
    char name[512];
    char password[512];

    int count = sscanf(str, "%s %s", name, password);
    if (count != 2)
    {
        fmt::println("usage: account set password <name> <password>");
        fmt::println("example: account set password ghostcrawler NewPassWoRd");
        return;
    }

    checkAccountName(name, ACC_NAME_DO_EXIST);

    std::string pass;
    pass.assign(name);
    pass.push_back(':');
    pass.append(password);

    std::stringstream query;
    query << "UPDATE `accounts` SET `encrypted_password` = ";
    query << "SHA(UPPER('" << pass << "')) ";
    query << "WHERE `acc_name` = '" << name << "'";

    if (!sLogonSQL->waitExecuteNA(query.str().c_str()))
    {
        fmt::println("Couldn't update password in database. Aborting.");
        return;
    }

    sAccountMgr.reloadAccounts(true);

    fmt::println("Account password updated.");
}

void LogonConsole::AccountChangePassword(char* str)
{
    char account_name[512];
    char old_password[512];
    char new_password_1[512];
    char new_password_2[512];

    int count = sscanf(str, "%s %s %s %s", account_name, old_password, new_password_1, new_password_2);
    if (count != 4)
    {
        fmt::println("usage: account change password <account> <old_password> <new_password> <new_password>");
        fmt::println("example: account change password ghostcrawler OldPasSworD FreshNewPassword FreshNewPassword");
        return;
    }

    checkAccountName(account_name, ACC_NAME_DO_EXIST);

    if (std::string(new_password_1) != std::string(new_password_2))
    {
        fmt::println("The new passwords don't match!");
        return;
    }

    std::string pass;
    pass.assign(account_name);
    pass.push_back(':');
    pass.append(old_password);

    auto check_oldpass_query = sLogonSQL->query("SELECT acc_name, encrypted_password FROM accounts WHERE encrypted_password = SHA(UPPER('%s')) AND acc_name = '%s'", pass.c_str(), std::string(account_name).c_str());

    if (!check_oldpass_query)
    {
        fmt::println("Your current password doesn't match your input.");
        return;
    }
    else
    {
        std::string new_pass;
        new_pass.assign(account_name);
        new_pass.push_back(':');
        new_pass.append(new_password_1);

        const bool updated = sLogonSQL->waitExecute(
            "UPDATE accounts SET encrypted_password = SHA(UPPER('%s')) WHERE acc_name = '%s'",
            new_pass.c_str(),
            std::string(account_name).c_str());

        if (!updated)
            return;
    }

    sAccountMgr.reloadAccounts(true);

    fmt::println("Account password changed.");
}

void LogonConsole::BNetAccountCreate(char* str)
{
    char email[512];
    char password[512];

    if (sscanf(str, "%511s %511s", email, password) != 2)
    {
        fmt::println("usage: bnet account create <email> <password>");
        return;
    }

    if (!sLogonSQL)
        return;

    const std::string escapedEmail = sLogonSQL->escapeString(email);
    if (sLogonSQL->query(
            "SELECT id FROM battlenet_accounts WHERE UPPER(email) = UPPER('%s') LIMIT 1",
            escapedEmail.c_str()))
    {
        fmt::println("Battle.net account '{}' already exists.", email);
        return;
    }

    AscEmu::Cryptography::BNetSrpV1::RegistrationData registration;
    if (!AscEmu::Cryptography::BNetSrpV1::makeRegistrationData(email, password, registration))
    {
        fmt::println("Couldn't generate Battle.net SRP credentials. Aborting.");
        return;
    }

    std::string battleTag(email);
    if (const size_t at = battleTag.find('@'); at != std::string::npos)
        battleTag.resize(at);
    battleTag += "#1";

    const std::string escapedBattleTag = sLogonSQL->escapeString(battleTag);
    if (!sLogonSQL->waitExecute(
            "INSERT INTO battlenet_accounts(email, srp_version, srp_salt, srp_verifier, battle_tag, country) "
            "VALUES('%s', 1, '%s', '%s', '%s', 'CH')",
            escapedEmail.c_str(),
            registration.saltHex.c_str(),
            registration.verifierHex.c_str(),
            escapedBattleTag.c_str()))
    {
        fmt::println("Couldn't save Battle.net account to database. Aborting.");
        return;
    }

    auto created = sLogonSQL->query(
        "SELECT id FROM battlenet_accounts WHERE UPPER(email)=UPPER('%s') LIMIT 1",
        escapedEmail.c_str());
    if (created && created->fetch())
    {
        const uint32_t bnetId = created->fetch()[0].asUint32();
        // Preserve the intuitive old workflow: existing WoW accounts that use
        // the same e-mail are linked automatically when the Battle.net account
        // is created. Additional/different accounts can be linked explicitly.
        sLogonSQL->waitExecute(
            "INSERT INTO battlenet_game_accounts(battlenet_account_id, game_account_id) "
            "SELECT %u, id FROM accounts WHERE UPPER(email)=UPPER('%s') "
            "ON DUPLICATE KEY UPDATE battlenet_account_id=VALUES(battlenet_account_id)",
            bnetId, escapedEmail.c_str());
    }

    fmt::println("Battle.net account '{}' created and matching WoW accounts linked.", email);
}

void LogonConsole::BNetAccountSetPassword(char* str)
{
    char email[512];
    char password[512];

    if (sscanf(str, "%511s %511s", email, password) != 2)
    {
        fmt::println("usage: bnet account set password <email> <password>");
        return;
    }

    if (!sLogonSQL)
        return;

    const std::string escapedEmail = sLogonSQL->escapeString(email);
    auto result = sLogonSQL->query(
        "SELECT id, email FROM battlenet_accounts WHERE UPPER(email) = UPPER('%s') LIMIT 1",
        escapedEmail.c_str());
    if (!result)
    {
        fmt::println("Battle.net account '{}' does not exist.", email);
        return;
    }

    Field* fields = result->fetch();
    if (!fields)
        return;

    const uint32_t id = fields[0].asUint32();
    const std::string identity = fields[1].asCString() != nullptr ? fields[1].asCString() : email;

    AscEmu::Cryptography::BNetSrpV1::RegistrationData registration;
    if (!AscEmu::Cryptography::BNetSrpV1::makeRegistrationData(identity, password, registration))
    {
        fmt::println("Couldn't generate Battle.net SRP credentials. Aborting.");
        return;
    }

    if (!sLogonSQL->waitExecute(
            "UPDATE battlenet_accounts SET srp_version=1, srp_salt='%s', srp_verifier='%s' WHERE id=%u",
            registration.saltHex.c_str(),
            registration.verifierHex.c_str(),
            id))
    {
        fmt::println("Couldn't update Battle.net password.");
        return;
    }

    fmt::println("Battle.net password updated for '{}'.", identity);
}

void LogonConsole::BNetAccountLink(char* str)
{
    char email[512];
    char gameAccountName[512];

    if (sscanf(str, "%511s %511s", email, gameAccountName) != 2)
    {
        fmt::println("usage: bnet account link <email> <wow-account-name>");
        return;
    }

    if (!sLogonSQL)
        return;

    const std::string escapedEmail = sLogonSQL->escapeString(email);
    const std::string escapedGameAccount = sLogonSQL->escapeString(gameAccountName);

    auto bnetResult = sLogonSQL->query(
        "SELECT id FROM battlenet_accounts WHERE UPPER(email) = UPPER('%s') LIMIT 1",
        escapedEmail.c_str());
    if (!bnetResult || !bnetResult->fetch())
    {
        fmt::println("Battle.net account '{}' does not exist.", email);
        return;
    }
    const uint32_t bnetId = bnetResult->fetch()[0].asUint32();

    auto gameResult = sLogonSQL->query(
        "SELECT id, acc_name FROM accounts WHERE UPPER(acc_name) = UPPER('%s') LIMIT 1",
        escapedGameAccount.c_str());
    if (!gameResult || !gameResult->fetch())
    {
        fmt::println("WoW game account '{}' does not exist.", gameAccountName);
        return;
    }
    const uint32_t gameId = gameResult->fetch()[0].asUint32();

    if (!sLogonSQL->waitExecute(
            "INSERT INTO battlenet_game_accounts(battlenet_account_id, game_account_id) VALUES(%u, %u) "
            "ON DUPLICATE KEY UPDATE battlenet_account_id=VALUES(battlenet_account_id)",
            bnetId,
            gameId))
    {
        fmt::println("Couldn't link WoW game account.");
        return;
    }

    fmt::println("Linked WoW game account '{}' ({}) to Battle.net account '{}' ({}).",
        gameAccountName, gameId, email, bnetId);
}

void LogonConsole::checkAccountName(std::string name, uint8_t type)
{
    std::string aname(name);

    AscEmu::Util::Strings::toUpperCase(aname);

    switch (type)
    {
        case ACC_NAME_DO_EXIST:
        {
            if (sAccountMgr.getAccountByName(aname) == nullptr)
            {
                fmt::println("There's no account with name {}", name);
            }

        } break;
        case ACC_NAME_NOT_EXIST:
        {
            if (sAccountMgr.getAccountByName(aname) != nullptr)
            {
                fmt::println("There's already an account with name {}", name);
            }
        } break;
    }
}

LogonConsoleThread::LogonConsoleThread()
{
    kill = false;
}

LogonConsoleThread::~LogonConsoleThread()
{
}
