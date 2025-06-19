/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "PreparedStatement.hpp"

#include <stdexcept>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#define LOCALTIME_SAFE(t, result) localtime_s(result, t)
#else
#define LOCALTIME_SAFE(t, result) localtime_r(t, result)
#endif

namespace
{
    template <typename T>
    struct always_false_helper : std::false_type {};
}

PreparedStatement::PreparedStatement(uint32_t index)
    : m_index(index)
{
}

void PreparedStatement::prepareMYSQLBinds()
{
    m_binds.clear();
    m_bindStorage.clear();

    std::vector<MYSQL_BIND> binds(m_bindValues.size());
    std::memset(binds.data(), 0, sizeof(MYSQL_BIND) * binds.size());

    for (size_t i = 0; i < m_bindValues.size(); ++i)
    {
        MYSQL_BIND& b = binds[i];
        const auto& val = m_bindValues[i];

        std::visit([&](auto&& arg)
            {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (
                    std::is_same_v<T, bool> ||
                    std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> || std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t> ||
                    std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> || std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>)
                {
                    auto ptr = std::make_shared<int64_t>(static_cast<int64_t>(arg));
                    b.buffer_type = MYSQL_TYPE_LONGLONG;
                    b.buffer = ptr.get();
                    b.is_null = nullptr;
                    m_bindStorage.push_back(ptr);
                }
                else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
                {
                    auto ptr = std::make_shared<double>(static_cast<double>(arg));
                    b.buffer_type = MYSQL_TYPE_DOUBLE;
                    b.buffer = ptr.get();
                    b.is_null = nullptr;
                    m_bindStorage.push_back(ptr);
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    auto str = std::make_shared<std::string>(arg);
                    auto len = std::make_shared<unsigned long>(str->size());

                    b.buffer_type = MYSQL_TYPE_STRING;
                    b.buffer = (void*)str->c_str();
                    b.buffer_length = static_cast<unsigned long>(str->size());
                    b.length = len.get();
                    b.is_null = nullptr;

                    m_bindStorage.push_back(str);
                    m_bindStorage.push_back(len);
                }
                else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
                {
                    auto blob = std::make_shared<std::vector<uint8_t>>(arg);
                    auto len = std::make_shared<unsigned long>(blob->size());

                    b.buffer_type = MYSQL_TYPE_BLOB;
                    b.buffer = blob->data();
                    b.buffer_length = *len;
                    b.length = len.get();
                    b.is_null = nullptr;

                    m_bindStorage.push_back(blob);
                    m_bindStorage.push_back(len);
                }
                else if constexpr (std::is_same_v<T, SystemTimePoint>)
                {
                    auto tmPtr = std::make_shared<MYSQL_TIME>();
                    std::time_t t = std::chrono::system_clock::to_time_t(arg);
                    std::tm tm;
                    LOCALTIME_SAFE(&t, &tm);

                    tmPtr->year = tm.tm_year + 1900;
                    tmPtr->month = tm.tm_mon + 1;
                    tmPtr->day = tm.tm_mday;
                    tmPtr->hour = tm.tm_hour;
                    tmPtr->minute = tm.tm_min;
                    tmPtr->second = tm.tm_sec;

                    b.buffer_type = MYSQL_TYPE_DATETIME;
                    b.buffer = tmPtr.get();
                    b.is_null = nullptr;

                    m_bindStorage.push_back(tmPtr);
                }
                else if constexpr (std::is_same_v<T, std::nullptr_t>)
                {
                    b.buffer_type = MYSQL_TYPE_NULL;
                    b.is_null = &m_trueLiteral;
                }
                else
                {
                    throw std::runtime_error("Unsupported bind type");
                }

            }, val);
    }

    m_binds = std::move(binds);
}

const std::vector<MYSQL_BIND>& PreparedStatement::getMYSQLBinds() const
{
    return m_binds;
}

void PreparedStatement::Bind(size_t index, const BindValue& value)
{
    if (index >= m_bindValues.size())
        m_bindValues.resize(index + 1);

    m_bindValues[index] = value;
}

uint32_t PreparedStatement::GetIndex() const { return m_index; }
const std::vector<BindValue>& PreparedStatement::GetBinds() const { return m_bindValues; }
