/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "Network/NetworkIncludes.hpp"
#include <mysql.h>

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <cstdint>
#include <chrono>

using SystemTimePoint = std::chrono::system_clock::time_point;

using BindValue = std::variant
<
    bool,
    uint8_t, uint16_t, uint32_t, uint64_t,
    int8_t, int16_t, int32_t, int64_t,
    float, double,
    std::string,
    std::vector<uint8_t>,
    SystemTimePoint,
    std::nullptr_t
>;

class PreparedStatement
{
public:
    explicit PreparedStatement(uint32_t index);

    void prepareMYSQLBinds();
    const std::vector<MYSQL_BIND>& getMYSQLBinds() const;

    void Bind(size_t index, const BindValue& value);

    uint32_t GetIndex() const;
    const std::vector<BindValue>& GetBinds() const;

private:
    uint32_t m_index;
    std::vector<BindValue> m_bindValues;
    std::vector<MYSQL_BIND> m_binds;
    std::vector<std::shared_ptr<void>> m_bindStorage;

    bool m_trueLiteral = true;
};
