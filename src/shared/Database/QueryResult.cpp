/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#include "QueryResult.hpp"

PreparedQueryResult::PreparedQueryResult(std::vector<std::vector<std::string>> rows, uint32_t fields)
    : QueryResult(fields, static_cast<uint32_t>(rows.size())),
    m_rows(std::move(rows)), m_rowIndex(0)
{
    NextRow();
    /* Deactivated we only want a Result when we call NextRow()
    if (!m_rows.empty())
    {
        mCurrentRow = std::make_unique<Field[]>(mFieldCount);
        for (uint32_t i = 0; i < mFieldCount; ++i)
            mCurrentRow[i].setValue(m_rows[0][i] == "NULL" ? nullptr : m_rows[0][i].c_str());
    }
    */
}

PreparedQueryResult::~PreparedQueryResult() = default;

bool PreparedQueryResult::NextRow()
{
    if (m_rowIndex >= m_rows.size())
        return false;

    mCurrentRow = std::make_unique<Field[]>(mFieldCount);

    const auto& rowData = m_rows[m_rowIndex];
    for (uint32_t i = 0; i < mFieldCount; ++i)
    {
        const std::string& val = rowData[i];

        if (val == "NULL")
            mCurrentRow[i].setValue(nullptr);
        else
            mCurrentRow[i].setValue(val.c_str());
    }

    ++m_rowIndex;
    return true;
}
