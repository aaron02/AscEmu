/*
Copyright (c) 2014-2025 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "CommonTypes.hpp"
#include "Field.hpp"

#include <memory>
#include <vector>
#include <string>

class QueryResult
{
public:
    QueryResult(uint32_t fields, uint32_t rows) : mFieldCount(fields), mRowCount(rows), mCurrentRow(nullptr) {}
    virtual ~QueryResult() {}

    virtual bool NextRow() = 0;

    inline Field* Fetch() { return mCurrentRow.get(); }
    inline uint32_t GetFieldCount() const { return mFieldCount; }
    inline uint32_t GetRowCount() const { return mRowCount; }

protected:

    uint32_t mFieldCount;
    uint32_t mRowCount;
    std::unique_ptr<Field[]> mCurrentRow;
};

class PreparedQueryResult : public QueryResult
{
public:
    PreparedQueryResult(std::vector<std::vector<std::string>> rows, uint32_t fields);
    ~PreparedQueryResult() override;

    bool NextRow() override;

private:
    std::vector<std::vector<std::string>> m_rows;
    size_t m_rowIndex;
};
