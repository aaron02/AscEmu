/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "LogonDatabaseConnection.hpp"

LogonDatabaseConnection::LogonDatabaseConnection()
{
}

LogonDatabaseConnection::~LogonDatabaseConnection()
{
}

void LogonDatabaseConnection::PrepareStatements()
{
    RegisterStatement(LOGON_VERSION_LAST_UPDATE, "SELECT LastUpdate FROM logon_db_version ORDER BY id DESC LIMIT 1");
    RegisterStatement(LOGON_SEL_IP_BANS, "SELECT ip, expire FROM ipbans;");
    RegisterStatement(LOGON_DEL_IP_BAN_EXACT, "DELETE FROM ipbans WHERE expire = ? AND ip = ?");
    RegisterStatement(LOGON_UPD_ACCOUNT_UNBAN, "UPDATE accounts SET banned = 0 WHERE id = ?");
    RegisterStatement(LOGON_UPD_ACCOUNT_UNMUTE, "UPDATE accounts SET muted = 0 WHERE id = ?");
    RegisterStatement(LOGON_DEL_ACCOUNT_BY_ID, "DELETE FROM accounts WHERE id = ?");
    RegisterStatement(LOGON_SEL_ACCOUNTS_ALL, "SELECT id, acc_name, encrypted_password, flags, banned, forceLanguage, muted FROM accounts");
    RegisterStatement(LOGON_UPD_ACCOUNT_LOGIN_INFO, "UPDATE accounts SET lastlogin = NOW(), lastip = ? WHERE id = ?");
    RegisterStatement(LOGON_UPD_ACCOUNT_LASTLOGIN, "UPDATE accounts SET lastlogin = NOW(), lastip = ? WHERE id = ?");
    RegisterStatement(LOGON_INS_NEW_ACCOUNT, "INSERT INTO accounts (acc_name, encrypted_password, banned, email, flags, banreason) VALUES (?, SHA(UPPER(?)), ?, ?, ?, '')");
    RegisterStatement(LOGON_DEL_ACCOUNT_BY_NAME, "DELETE FROM accounts WHERE acc_name = ?");
    RegisterStatement(LOGON_UPD_ACCOUNT_PASSWORD, "UPDATE accounts SET encrypted_password = SHA(UPPER(?)) WHERE acc_name = ?");
    RegisterStatement(LOGON_SEL_ACCOUNT_BY_PASS_AND_NAME, "SELECT acc_name, encrypted_password FROM accounts WHERE encrypted_password = SHA(UPPER(?)) AND acc_name = ?");
    RegisterStatement(LOGON_UPD_ACCOUNT_PASSWORD_BY_NAME, "UPDATE accounts SET encrypted_password = SHA(UPPER(?)) WHERE acc_name = ?");
    RegisterStatement(LOGON_SEL_ACCOUNT_BY_NAME_AND_PASSWORD, "SELECT acc_name, encrypted_password FROM accounts WHERE encrypted_password = SHA(UPPER(?)) AND acc_name = ?");
    RegisterStatement(LOGON_UPD_ACCOUNT_BANSTATUS, "UPDATE accounts SET banned = ?, banreason = ? WHERE acc_name = ?");
    RegisterStatement(LOGON_UPD_ACCOUNT_MUTE, "UPDATE accounts SET muted = ? WHERE acc_name = ?");
    RegisterStatement(LOGON_INS_IPBAN, "INSERT INTO ipbans (ip, expire, reason) VALUES (?, ?, ?)");
    RegisterStatement(LOGON_DEL_IPBAN_BY_IP, "DELETE FROM ipbans WHERE ip = ?");
    RegisterStatement(LOGON_SEL_ACCOUNT_VERIFY_PASSWORD, "SELECT acc_name, encrypted_password FROM accounts WHERE encrypted_password = SHA(UPPER(?)) AND acc_name = ?");
    RegisterStatement(LOGON_INS_ACCOUNT_CREATE, "INSERT INTO accounts (acc_name, encrypted_password, banned, email, flags, banreason) VALUES (?, SHA(UPPER(?)), 0, '', 24, '')");
    RegisterStatement(LOGON_SEL_REALM_INFO, "SELECT id, password, status FROM realms");
    RegisterStatement(LOGON_REP_REALM_STATUS, "REPLACE INTO realms (id, status, status_change_time) VALUES (?, ?, NOW())");
    RegisterStatement(LOGON_UPD_REALM_STATUS, "UPDATE realms SET status = ? WHERE id = ?");
    RegisterStatement(LOGON_UPD_REALM_SET_OFFLINE, "UPDATE realms SET status = 0 WHERE id = ?");
    RegisterStatement(LOGON_UPD_REALM_STATUS_RESET, "UPDATE realms SET status = 0 WHERE id = ?");
}
