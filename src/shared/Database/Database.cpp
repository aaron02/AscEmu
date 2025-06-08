/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "Logging/Logger.hpp"
#include "Database.hpp"
#include <stdexcept>
#include <iostream>

using SystemTimePoint = std::chrono::system_clock::time_point;

Database::Database() = default;

Database::~Database() 
{
    Shutdown();
}

bool Database::Initialize(const std::string& host, uint32_t port, const std::string& user, const std::string& pass, const std::string& dbname, uint32_t connections, bool useLegacyAuth)
{
    mHost = host;
    mPort = port;
    mUser = user;
    mPass = pass;
    mDBName = dbname;

    sLogger.info("MySQLDatabase : Connecting to `{}`, database `{}`...", mHost.c_str(), mDBName.c_str());
    
    // Prepare Statements
    PrepareStatements();

    m_connections.reserve(connections);
    for (uint32_t i = 0; i < connections; ++i)
    {
        MYSQL* mysql = mysql_init(nullptr);
        if (!mysql)
        {
            sLogger.failure("[MySQL] mysql_init failed for connection " + std::to_string(i));
            continue;
        }

        // Set UTF8 charset
        if (mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8"))
            sLogger.failure(std::string("[MySQL] Warning: Failed to set UTF-8 charset: ") + mysql_error(mysql));


        // Enable Reconnect
        bool reconnect = true;
        if (mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect))
            sLogger.failure(std::string("[MySQL] Warning: Failed to enable reconnect: ") + mysql_error(mysql));


        // MySQL 8+ authentication plugin
        if (!useLegacyAuth)
        {
            if (mysql_options(mysql, MYSQL_DEFAULT_AUTH, "caching_sha2_password"))
            {
                sLogger.failure(std::string("[MySQL] Failed to set MySQL 8 auth plugin: ") + mysql_error(mysql));
                mysql_close(mysql);
                return false;
            }
        }

        // Connect
        if (!mysql_real_connect(mysql, host.c_str(), user.c_str(), pass.c_str(), dbname.c_str(), port, nullptr, 0))
        {
            sLogger.failure(std::string("[MySQL] Connection failed: ") + mysql_error(mysql));
            mysql_close(mysql);
            return false;
        }

        // Setup Connection
        auto conn = std::make_unique<DatabaseConnection>();
        conn->handle = mysql;

        // Prepare Statements for Connection
        PrepareStatementsForConnection(mysql, conn->preparedStatements);

        m_connections.push_back(std::move(conn));
    }

    // Queue Threading
    m_running = true;
    m_workerThread = std::thread(&Database::QueryWorker, this);

    mConnected = true;
    return true;
}

void Database::Shutdown()
{
    m_running = false;
    m_condition.notify_one();

    if (m_workerThread.joinable())
        m_workerThread.join();

    for (auto& conn : m_connections)
    {
        if (conn && conn->handle)
        {
            for (auto& [idx, stmt] : conn->preparedStatements)
                mysql_stmt_close(stmt);

            conn->preparedStatements.clear();

            mysql_close(conn->handle);
            conn->handle = nullptr;
        }
    }

    m_connections.clear();
    mysql_library_end();
}

void Database::RegisterStatement(const uint32_t index, const std::string& sql)
{
    m_registeredStatements[index] = sql;
}

void Database::PrepareStatementsForConnection(MYSQL* mysql, std::unordered_map<uint32_t, MYSQL_STMT*>& stmtMap)
{
    for (const auto& [index, sql] : m_registeredStatements)
    {
        MYSQL_STMT* stmt = mysql_stmt_init(mysql);
        if (!stmt)
            sLogger.failure("Failed to init statement for: " + sql);

        if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.length())) != 0)
        {
            mysql_stmt_close(stmt);
            sLogger.failure("Failed to prepare statement: " + sql + " error: " + mysql_stmt_error(stmt));
        }

        stmtMap[index] = stmt;
    }
}

std::unique_ptr<PreparedStatement> Database::CreateStatement(const uint32_t index)
{
    auto it = m_registeredStatements.find(index);
    if (it == m_registeredStatements.end())
        sLogger.failure("Statement not registered: " + std::to_string(index));

    return std::make_unique<PreparedStatement>(index);
}

MYSQL_STMT* Database::GetPreparedStatement(DatabaseConnection* connection, const uint32_t index)
{
    auto it = connection->preparedStatements.find(index);
    if (it == connection->preparedStatements.end())
        sLogger.failure("Prepared statement index not found");

    return it->second;
}

bool Database::ExecuteStatement(std::shared_ptr<PreparedStatement> stmt)
{
    LockedConnection conn = GetFreeConnection();
    std::unique_lock<std::mutex>& guard = conn.lock;

    MYSQL_STMT* native = GetPreparedStatement(conn.GetConnection(), stmt->GetIndex());
    if (!native)
        return false;

    mysql_stmt_free_result(native);
    mysql_stmt_reset(native);

    std::vector<MYSQL_BIND> binds = stmt->BindToMYSQL();

    if (!binds.empty() && mysql_stmt_bind_param(native, binds.data()) != 0)
    {
        std::cerr << "[MySQL] Bind failed: " << mysql_stmt_error(native) << "\n";
        return false;
    }

    bool result = mysql_stmt_execute(native) == 0;

    // Clean up result buffer again in case user runs more queries on the same handle
    mysql_stmt_free_result(native);
    mysql_stmt_reset(native);

    return result;
}

std::unique_ptr<QueryResult> Database::QueryStatement(std::shared_ptr<PreparedStatement> stmt)
{
    LockedConnection conn = GetFreeConnection();
    std::unique_lock<std::mutex>& guard = conn.lock;

    MYSQL_STMT* native = GetPreparedStatement(conn.GetConnection(), stmt->GetIndex());
    if (!native)
        return nullptr;

    std::vector<MYSQL_BIND> binds = stmt->BindToMYSQL();

    if (!binds.empty() && mysql_stmt_bind_param(native, binds.data()) != 0)
    {
        std::cerr << "[MySQL] Bind failed: " << mysql_stmt_error(native) << "\n";
        return nullptr;
    }

    auto rows = QueryStatementRaw(native, stmt.get());

    if (rows.empty())
        return nullptr;

    uint32_t fieldCount = static_cast<uint32_t>(rows[0].size());
    return std::make_unique<PreparedQueryResult>(std::move(rows), fieldCount);
}

std::unique_ptr<QueryResult> Database::QueryStatement(bool* success, std::shared_ptr<PreparedStatement> stmt)
{
    if (success)
        *success = false;

    LockedConnection conn = GetFreeConnection();
    std::unique_lock<std::mutex>& guard = conn.lock;

    MYSQL_STMT* native = GetPreparedStatement(conn.GetConnection(), stmt->GetIndex());
    if (!native)
        return nullptr;

    std::vector<MYSQL_BIND> binds = stmt->BindToMYSQL();

    if (!binds.empty() && mysql_stmt_bind_param(native, binds.data()) != 0)
    {
        std::cerr << "[MySQL] Bind failed: " << mysql_stmt_error(native) << "\n";
        return nullptr;
    }

    auto rows = QueryStatementRaw(native, stmt.get());
    if (rows.empty())
    {
        if (success)
            *success = true;
        return nullptr;
    }

    uint32_t fieldCount = rows.empty() ? 0 : static_cast<uint32_t>(rows[0].size());
    return std::make_unique<PreparedQueryResult>(std::move(rows), fieldCount);
}

void Database::AsyncMultiQueryStatement(std::vector<std::shared_ptr<PreparedStatement>> stmts, MultiQueryCallback cb)
{
    std::thread([this, stmts = std::move(stmts), cb = std::move(cb)]() mutable {
        std::vector<std::unique_ptr<QueryResult>> results;
        for (auto& stmt : stmts)
        {
            auto res = QueryStatement(stmt);
            results.push_back(std::move(res));
        }
        if (cb)
            cb(std::move(results));
        }).detach();
}

void Database::AsyncQueryStatement(std::shared_ptr<PreparedStatement> stmt, QueryCallback callback)
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queryQueue.push({ std::move(stmt), std::move(callback) });
    }
    m_condition.notify_one();
}

void Database::AsyncExecuteStatement(std::shared_ptr<PreparedStatement> stmt)
{
    AsyncQueryStatement(std::move(stmt), nullptr);
}

const uint32_t Database::GetQueueSize() const
{
    return m_queryQueue.size();
}

QueryResultRows Database::QueryStatementRaw(MYSQL_STMT* stmtHandle, PreparedStatement* stmt)
{
    QueryResultRows result;

    if (!stmtHandle)
        return result;

    mysql_stmt_free_result(stmtHandle);
    mysql_stmt_reset(stmtHandle);

    // Bind input parameters
    std::vector<MYSQL_BIND> binds = stmt->BindToMYSQL();
    if (!binds.empty() && mysql_stmt_bind_param(stmtHandle, binds.data()) != 0)
    {
        std::cerr << "[MySQL] Bind failed: " << mysql_stmt_error(stmtHandle) << "\n";
        return result;
    }

    if (mysql_stmt_execute(stmtHandle) != 0 || mysql_stmt_store_result(stmtHandle) != 0)
        return result;

    int numFields = mysql_stmt_field_count(stmtHandle);
    if (numFields == 0)
        return result;

    MYSQL_RES* metadata = mysql_stmt_result_metadata(stmtHandle);
    if (!metadata)
        return result;

    MYSQL_FIELD* fields = mysql_fetch_fields(metadata);
    std::vector<MYSQL_BIND> resultBinds(numFields);
    std::vector<std::vector<char>> buffers(numFields, std::vector<char>(1024));
    std::vector<unsigned long> lengths(numFields);
    std::vector<char> isNulls(numFields);

    for (int i = 0; i < numFields; ++i)
    {
        resultBinds[i].buffer_type = fields[i].type;
        resultBinds[i].buffer = buffers[i].data();
        resultBinds[i].buffer_length = static_cast<unsigned long>(buffers[i].size());
        resultBinds[i].length = &lengths[i];
        resultBinds[i].is_null = reinterpret_cast<bool*>(&isNulls[i]);
        resultBinds[i].is_unsigned = (fields[i].flags & UNSIGNED_FLAG) != 0;
    }

    if (mysql_stmt_bind_result(stmtHandle, resultBinds.data()) != 0)
        return result;

    while (mysql_stmt_fetch(stmtHandle) == 0)
    {
        std::vector<std::string> row;
        for (int i = 0; i < numFields; ++i)
        {
            row.emplace_back(ConvertFieldToString(fields[i], resultBinds[i], lengths[i]));
        }
        result.emplace_back(std::move(row));
    }

    mysql_stmt_free_result(stmtHandle);
    mysql_stmt_reset(stmtHandle);
    mysql_free_result(metadata);

    return result;
}

std::string Database::ConvertFieldToString(const MYSQL_FIELD& field, const MYSQL_BIND& bind, unsigned long length)
{
    if (*bind.is_null)
        return "NULL";

    switch (field.type)
    {
        case MYSQL_TYPE_INT24:
        {
            if (bind.is_unsigned)
                return std::to_string(*reinterpret_cast<uint32_t*>(bind.buffer));
            else
                return std::to_string(*reinterpret_cast<int32_t*>(bind.buffer));
        } break;

        case MYSQL_TYPE_TINY:
        {
            if (bind.is_unsigned)
                return std::to_string(*reinterpret_cast<uint8_t*>(bind.buffer));
            else
                return(std::to_string(*reinterpret_cast<int8_t*>(bind.buffer)));
        } break;

        case MYSQL_TYPE_SHORT:
        {
            if (bind.is_unsigned)
                return std::to_string(*reinterpret_cast<uint16_t*>(bind.buffer));
            else
                return std::to_string(*reinterpret_cast<int16_t*>(bind.buffer));
        } break;

        case MYSQL_TYPE_LONG:
        {
            if (bind.is_unsigned)
                return std::to_string(*reinterpret_cast<uint32_t*>(bind.buffer));
            else
                return std::to_string(*reinterpret_cast<int32_t*>(bind.buffer));
        } break;

        case MYSQL_TYPE_LONGLONG:
        {
            if (bind.is_unsigned)
                return std::to_string(*reinterpret_cast<uint64_t*>(bind.buffer));
            else
                return std::to_string(*reinterpret_cast<int64_t*>(bind.buffer));
        } break;

        case MYSQL_TYPE_FLOAT:
        {
            return std::to_string(*reinterpret_cast<float*>(bind.buffer));
        } break;

        case MYSQL_TYPE_DOUBLE:
        {
            return std::to_string(*reinterpret_cast<double*>(bind.buffer));
        } break;

        case MYSQL_TYPE_DATETIME:
        case MYSQL_TYPE_TIMESTAMP:
        case MYSQL_TYPE_DATE:
        {
            const MYSQL_TIME* t = reinterpret_cast<const MYSQL_TIME*>(bind.buffer);

            std::tm tm{};
            tm.tm_year = t->year - 1900;
            tm.tm_mon = t->month - 1;
            tm.tm_mday = t->day;
            tm.tm_hour = t->hour;
            tm.tm_min = t->minute;
            tm.tm_sec = t->second;
            tm.tm_isdst = -1;

            std::time_t tt = std::mktime(&tm);
            SystemTimePoint tp = std::chrono::system_clock::from_time_t(tt);

            std::time_t time = std::chrono::system_clock::to_time_t(tp);
            std::tm tm_buf{};
            localtime_s(&tm_buf, &time);

            std::stringstream ss;
            ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
            return ss.str();
        }

        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_VAR_STRING:
            return std::string(static_cast<const char*>(bind.buffer), length);

        default:
            return "[unsupported type]";
    }
}


LockedConnection Database::GetFreeConnection()
{
    while (true)
    {
        for (auto& conn : m_connections)
        {
            std::unique_lock<std::mutex> lock(conn->mutex, std::try_to_lock);
            if (lock.owns_lock())
                return LockedConnection(conn.get(), std::move(lock));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Database::QueryWorker()
{
    while (m_running)
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_condition.wait(lock, [this]() { return !m_queryQueue.empty() || !m_running; });

        while (!m_queryQueue.empty())
        {
            QueryTask task = std::move(m_queryQueue.front());
            m_queryQueue.pop();
            lock.unlock();

            try
            {
                if (task.callback)
                {
                    auto result = QueryStatement(std::move(task.stmt));
                    task.callback(std::move(result));
                }
                else
                {
                    ExecuteStatement(std::move(task.stmt));
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "[AsyncQuery] Exception: " << e.what() << "\n";
            }

            lock.lock();
        }
    }
}
