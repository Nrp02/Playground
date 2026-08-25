#pragma once

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "varint.hpp"

namespace sqlitereader {

struct FileHeader {
    uint32_t pageSize = 0;
    uint8_t reservedSpace = 0;
    uint32_t pageCount = 0;
};

inline FileHeader parseFileHeader(const std::vector<uint8_t>& data) {
    if (data.size() < 100) {
        throw std::runtime_error("file too small to contain a sqlite header");
    }
    static const char kMagic[] = "SQLite format 3";
    for (int i = 0; i < 15; ++i) {
        if (data[static_cast<size_t>(i)] != static_cast<uint8_t>(kMagic[i])) {
            throw std::runtime_error("bad sqlite magic string");
        }
    }
    if (data[15] != 0) {
        throw std::runtime_error("bad sqlite magic string");
    }
    FileHeader header;
    uint16_t rawPageSize = static_cast<uint16_t>((data[16] << 8) | data[17]);
    header.pageSize = (rawPageSize == 1) ? 65536u : static_cast<uint32_t>(rawPageSize);
    header.reservedSpace = data[20];
    header.pageCount = (static_cast<uint32_t>(data[28]) << 24) | (static_cast<uint32_t>(data[29]) << 16) |
                        (static_cast<uint32_t>(data[30]) << 8) | static_cast<uint32_t>(data[31]);
    return header;
}

struct Value {
    enum class Type { Null, Integer, Text } type = Type::Null;
    int64_t intValue = 0;
    std::string textValue;

    static Value makeNull() { return Value{}; }

    static Value makeInteger(int64_t v) {
        Value value;
        value.type = Type::Integer;
        value.intValue = v;
        return value;
    }

    static Value makeText(std::string v) {
        Value value;
        value.type = Type::Text;
        value.textValue = std::move(v);
        return value;
    }

    bool operator==(const Value& other) const {
        if (type != other.type) return false;
        switch (type) {
            case Type::Null: return true;
            case Type::Integer: return intValue == other.intValue;
            case Type::Text: return textValue == other.textValue;
        }
        return false;
    }
};

struct Row {
    std::vector<Value> values;
};

inline std::vector<std::string> parseCreateTableColumns(const std::string& sql) {
    std::vector<std::string> columns;
    size_t openParen = sql.find('(');
    size_t closeParen = sql.rfind(')');
    if (openParen == std::string::npos || closeParen == std::string::npos || closeParen <= openParen) {
        return columns;
    }
    std::string inner = sql.substr(openParen + 1, closeParen - openParen - 1);
    size_t start = 0;
    while (start <= inner.size()) {
        size_t comma = inner.find(',', start);
        std::string part = (comma == std::string::npos) ? inner.substr(start) : inner.substr(start, comma - start);
        size_t firstNonSpace = part.find_first_not_of(" \t\n\r");
        if (firstNonSpace != std::string::npos) {
            size_t tokenEnd = part.find_first_of(" \t\n\r", firstNonSpace);
            std::string name = (tokenEnd == std::string::npos) ? part.substr(firstNonSpace)
                                                                 : part.substr(firstNonSpace, tokenEnd - firstNonSpace);
            columns.push_back(name);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return columns;
}

class Database {
public:
    explicit Database(std::vector<uint8_t> bytes) : data_(std::move(bytes)) {
        header_ = parseFileHeader(data_);
    }

    static Database loadFromFile(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("could not open file: " + path);
        }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return Database(std::move(bytes));
    }

    const FileHeader& header() const { return header_; }

    struct TableSchema {
        std::string name;
        uint32_t rootPage = 0;
        std::vector<std::string> columns;
    };

    std::optional<TableSchema> findTable(const std::string& tableName) const {
        std::vector<RawRecord> rows;
        readTableBtreeInto(1, rows);
        for (const auto& r : rows) {
            if (r.values.size() < 5) continue;
            if (r.values[0].type != Value::Type::Text || r.values[0].textValue != "table") continue;
            if (r.values[1].type != Value::Type::Text || r.values[1].textValue != tableName) continue;
            if (r.values[3].type != Value::Type::Integer) continue;
            TableSchema schema;
            schema.name = tableName;
            schema.rootPage = static_cast<uint32_t>(r.values[3].intValue);
            std::string sql = (r.values[4].type == Value::Type::Text) ? r.values[4].textValue : std::string();
            schema.columns = parseCreateTableColumns(sql);
            return schema;
        }
        return std::nullopt;
    }

    std::vector<Row> select(const std::string& tableName,
                             const std::vector<std::string>& selectColumns,
                             const std::optional<std::pair<std::string, Value>>& whereClause = std::nullopt) const {
        auto schemaOpt = findTable(tableName);
        if (!schemaOpt) {
            throw std::runtime_error("table not found: " + tableName);
        }
        const TableSchema& schema = *schemaOpt;

        std::vector<size_t> selectIndices;
        for (const auto& col : selectColumns) {
            auto it = std::find(schema.columns.begin(), schema.columns.end(), col);
            if (it == schema.columns.end()) {
                throw std::runtime_error("column not found: " + col);
            }
            selectIndices.push_back(static_cast<size_t>(it - schema.columns.begin()));
        }

        bool hasWhere = whereClause.has_value();
        size_t whereIndex = 0;
        if (hasWhere) {
            auto it = std::find(schema.columns.begin(), schema.columns.end(), whereClause->first);
            if (it == schema.columns.end()) {
                throw std::runtime_error("where column not found: " + whereClause->first);
            }
            whereIndex = static_cast<size_t>(it - schema.columns.begin());
        }

        std::vector<RawRecord> rawRows;
        readTableBtreeInto(schema.rootPage, rawRows);

        std::vector<Row> result;
        for (const auto& raw : rawRows) {
            if (hasWhere && !(raw.values[whereIndex] == whereClause->second)) {
                continue;
            }
            Row row;
            for (size_t idx : selectIndices) {
                row.values.push_back(raw.values[idx]);
            }
            result.push_back(std::move(row));
        }
        return result;
    }

private:
    struct RawRecord {
        int64_t rowid = 0;
        std::vector<Value> values;
    };

    std::vector<uint8_t> data_;
    FileHeader header_;

    const uint8_t* pagePtr(uint32_t pageNumber) const {
        size_t offset = static_cast<size_t>(pageNumber - 1) * header_.pageSize;
        if (offset >= data_.size()) {
            throw std::runtime_error("page out of range");
        }
        return data_.data() + offset;
    }

    static Value decodeValue(uint64_t serialType, const uint8_t* ptr) {
        if (serialType == 0) return Value::makeNull();
        if (serialType >= 1 && serialType <= 6) {
            size_t len = serialTypeValueSize(serialType);
            return Value::makeInteger(decodeBigEndianSigned(ptr, len));
        }
        if (serialType == 8) return Value::makeInteger(0);
        if (serialType == 9) return Value::makeInteger(1);
        if (serialType >= 13 && serialType % 2 == 1) {
            size_t len = serialTypeValueSize(serialType);
            return Value::makeText(std::string(reinterpret_cast<const char*>(ptr), len));
        }
        return Value::makeNull();
    }

    RawRecord parseLeafCell(const uint8_t* cellStart) const {
        auto payloadLenVarint = decodeVarint(cellStart);
        const uint8_t* p = cellStart + payloadLenVarint.bytesRead;
        auto rowidVarint = decodeVarint(p);
        int64_t rowid = static_cast<int64_t>(rowidVarint.value);
        p += rowidVarint.bytesRead;

        const uint8_t* payloadStart = p;
        auto headerLenVarint = decodeVarint(payloadStart);
        const uint8_t* serialTypePtr = payloadStart + headerLenVarint.bytesRead;
        const uint8_t* headerEnd = payloadStart + headerLenVarint.value;

        std::vector<uint64_t> serialTypes;
        while (serialTypePtr < headerEnd) {
            auto stVarint = decodeVarint(serialTypePtr);
            serialTypes.push_back(stVarint.value);
            serialTypePtr += stVarint.bytesRead;
        }

        const uint8_t* valuePtr = headerEnd;
        std::vector<Value> values;
        values.reserve(serialTypes.size());
        for (uint64_t st : serialTypes) {
            values.push_back(decodeValue(st, valuePtr));
            valuePtr += serialTypeValueSize(st);
        }

        return RawRecord{rowid, std::move(values)};
    }

    void readTableBtreeInto(uint32_t pageNumber, std::vector<RawRecord>& out) const {
        const uint8_t* page = pagePtr(pageNumber);
        size_t headerOffset = (pageNumber == 1) ? 100 : 0;
        uint8_t pageType = page[headerOffset];
        uint16_t numCells = static_cast<uint16_t>((page[headerOffset + 3] << 8) | page[headerOffset + 4]);

        if (pageType == 0x0d) {
            size_t ptrArrayStart = headerOffset + 8;
            for (uint16_t i = 0; i < numCells; ++i) {
                uint16_t cellOffset = static_cast<uint16_t>((page[ptrArrayStart + 2 * i] << 8) |
                                                              page[ptrArrayStart + 2 * i + 1]);
                out.push_back(parseLeafCell(page + cellOffset));
            }
        } else if (pageType == 0x05) {
            size_t ptrArrayStart = headerOffset + 12;
            for (uint16_t i = 0; i < numCells; ++i) {
                uint16_t cellOffset = static_cast<uint16_t>((page[ptrArrayStart + 2 * i] << 8) |
                                                              page[ptrArrayStart + 2 * i + 1]);
                const uint8_t* cell = page + cellOffset;
                uint32_t childPage = (static_cast<uint32_t>(cell[0]) << 24) |
                                      (static_cast<uint32_t>(cell[1]) << 16) |
                                      (static_cast<uint32_t>(cell[2]) << 8) | static_cast<uint32_t>(cell[3]);
                readTableBtreeInto(childPage, out);
            }
            uint32_t rightPage = (static_cast<uint32_t>(page[headerOffset + 8]) << 24) |
                                  (static_cast<uint32_t>(page[headerOffset + 9]) << 16) |
                                  (static_cast<uint32_t>(page[headerOffset + 10]) << 8) |
                                  static_cast<uint32_t>(page[headerOffset + 11]);
            readTableBtreeInto(rightPage, out);
        } else {
            throw std::runtime_error("unsupported b-tree page type");
        }
    }
};

}
