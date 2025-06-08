/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "CommonTypes.hpp"

#include "ThreadSafeQueue.hpp"
#include "Threading/Mutex.hpp"
#include "Threading/AEThread.h"

#include <winsock2.h>
#include <windows.h>
#include <mysql.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>
#include <functional>

#include <queue>
#include <condition_variable>
#include <thread>
#include <atomic>

#include "PreparedStatement.hpp"
#include "QueryResult.hpp"

using StatemendData = std::unordered_map<uint32_t, MYSQL_STMT*>;

struct DatabaseConnection
{
    MYSQL* handle;
    std::mutex mutex;
    StatemendData preparedStatements;
};

struct LockedConnection
{
    DatabaseConnection* conn = nullptr;
    std::unique_lock<std::mutex> lock;

    LockedConnection() = default;
    LockedConnection(DatabaseConnection* c, std::unique_lock<std::mutex> l) : conn(c), lock(std::move(l)) {}

    DatabaseConnection* GetConnection()
    {
        return conn;
    }

    MYSQL* GetMYSQLHandle() const
    {
        return conn ? conn->handle : nullptr;
    }

    std::mutex& GetMutex() const
    {
        return conn->mutex;
    }

    bool IsValid() const
    {
        return conn != nullptr;
    }
};

using QueryCallback = std::function<void(std::unique_ptr<QueryResult>)>;

struct QueryTask
{
    std::shared_ptr<PreparedStatement> stmt;
    QueryCallback callback = nullptr;

    QueryTask(std::shared_ptr<PreparedStatement> s, QueryCallback cb)
        : stmt(std::move(s)), callback(std::move(cb)) {}
};

using MultiQueryCallback = std::function<void(std::vector<std::unique_ptr<QueryResult>>)>;
using QueryResultRows = std::vector<std::vector<std::string>>;

class SERVER_DECL Database
{
public:

    Database();
    virtual ~Database();

    // Core
    virtual bool Initialize(const std::string& host, uint32_t port, const std::string& user, const std::string& pass, const std::string& dbname, uint32_t connections, bool useLegacyAuth);
    virtual void Shutdown();

    // Prepared Statements
    virtual void PrepareStatements() = 0;
    void RegisterStatement(const uint32_t index, const std::string& sql);
    void PrepareStatementsForConnection(MYSQL* mysql, std::unordered_map<uint32_t, MYSQL_STMT*>& stmtMap);

    std::unique_ptr<PreparedStatement> CreateStatement(const uint32_t index);
    MYSQL_STMT* GetPreparedStatement(DatabaseConnection* connection,const uint32_t index);

    // Single Query
    std::unique_ptr<QueryResult> QueryStatement(std::shared_ptr<PreparedStatement> stmt);
    std::unique_ptr<QueryResult> QueryStatement(bool* success, std::shared_ptr<PreparedStatement> stmt);
    bool ExecuteStatement(std::shared_ptr<PreparedStatement> stmt);

    // Async Query Callback
    void AsyncQueryStatement(std::shared_ptr<PreparedStatement> stmt, QueryCallback callback);
    void AsyncMultiQueryStatement(std::vector<std::shared_ptr<PreparedStatement>> stmts, MultiQueryCallback cb);

    // Async Query Fire-and-forget!
    void AsyncExecuteStatement(std::shared_ptr<PreparedStatement> stmt);

    const std::string& GetHostName() const { return mHost; }
    const std::string& GetDatabaseName() const { return mDBName; }
    const uint32_t GetQueueSize() const;

protected:
    QueryResultRows QueryStatementRaw(MYSQL_STMT* stmtHandle, PreparedStatement* stmt);
    std::string ConvertFieldToString(const MYSQL_FIELD& field, const MYSQL_BIND& bind, unsigned long length);
    LockedConnection GetFreeConnection();

private:
    void QueryWorker();

    std::vector<std::unique_ptr<DatabaseConnection>> m_connections;
    std::unordered_map<uint32_t, std::string> m_registeredStatements;

    // Threading
    std::queue<QueryTask> m_queryQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_condition;

    std::thread m_workerThread;
    std::atomic<bool> m_running = false;

    // Connection info
    std::string mHost;
    std::string mUser;
    std::string mPass;
    std::string mDBName;
    uint32_t mPort = 3306;

    bool mConnected = false;
};
