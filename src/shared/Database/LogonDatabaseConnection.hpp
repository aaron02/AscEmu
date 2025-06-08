/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "CommonTypes.hpp"
#include "Database.hpp"

enum LogonDatabaseStatements : uint32_t
{
    // Update
    LOGON_VERSION_LAST_UPDATE,
    LOGON_SEL_IP_BANS,
    LOGON_DEL_IP_BAN_EXACT,
    LOGON_UPD_ACCOUNT_UNBAN,
    LOGON_UPD_ACCOUNT_UNMUTE,
    LOGON_DEL_ACCOUNT_BY_ID,
    LOGON_SEL_ACCOUNTS_ALL,
    LOGON_UPD_ACCOUNT_LOGIN_INFO,
    LOGON_UPD_ACCOUNT_LASTLOGIN,
    LOGON_INS_NEW_ACCOUNT,
    LOGON_DEL_ACCOUNT_BY_NAME,
    LOGON_UPD_ACCOUNT_PASSWORD,
    LOGON_SEL_ACCOUNT_BY_PASS_AND_NAME,
    LOGON_UPD_ACCOUNT_PASSWORD_BY_NAME,
    LOGON_SEL_ACCOUNT_BY_NAME_AND_PASSWORD,
    LOGON_UPD_ACCOUNT_BANSTATUS,
    LOGON_UPD_ACCOUNT_MUTE,
    LOGON_INS_IPBAN,
    LOGON_DEL_IPBAN_BY_IP,
    LOGON_SEL_ACCOUNT_VERIFY_PASSWORD,
    LOGON_INS_ACCOUNT_CREATE,
    LOGON_SEL_REALM_INFO,
    LOGON_REP_REALM_STATUS,
    LOGON_UPD_REALM_STATUS,
    LOGON_UPD_REALM_SET_OFFLINE,
    LOGON_UPD_REALM_STATUS_RESET,

    MAX_LOGONDATABASE_STATEMENTS
};

class SERVER_DECL LogonDatabaseConnection : public Database
{
public:
    typedef LogonDatabaseStatements Statements;

    LogonDatabaseConnection();
    ~LogonDatabaseConnection();

    void PrepareStatements() override;
};
