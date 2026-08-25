#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fixturebuilder {

struct Field {
    enum class Kind { Null, Integer, Text } kind = Kind::Null;
    int64_t intValue = 0;
    std::string textValue;

    static Field null() { return Field{}; }

    static Field integer(int64_t v) {
        Field f;
        f.kind = Kind::Integer;
        f.intValue = v;
        return f;
    }

    static Field text(std::string v) {
        Field f;
        f.kind = Kind::Text;
        f.textValue = std::move(v);
        return f;
    }
};

inline std::vector<uint8_t> encodeVarint(uint64_t value) {
    uint8_t buffer[10];
    int length = 0;
    do {
        buffer[length++] = static_cast<uint8_t>(value & 0x7f);
        value >>= 7;
    } while (value != 0);
    std::vector<uint8_t> result;
    result.reserve(static_cast<size_t>(length));
    for (int i = length - 1; i >= 0; --i) {
        uint8_t byte = buffer[i];
        if (i != 0) byte |= 0x80;
        result.push_back(byte);
    }
    return result;
}

inline std::vector<uint8_t> buildRecord(const std::vector<Field>& fields) {
    std::vector<uint8_t> serialTypes;
    std::vector<uint8_t> body;
    for (const auto& f : fields) {
        if (f.kind == Field::Kind::Null) {
            auto st = encodeVarint(0);
            serialTypes.insert(serialTypes.end(), st.begin(), st.end());
        } else if (f.kind == Field::Kind::Integer) {
            uint64_t serialType;
            size_t len;
            if (f.intValue >= -128 && f.intValue <= 127) {
                serialType = 1;
                len = 1;
            } else if (f.intValue >= -32768 && f.intValue <= 32767) {
                serialType = 2;
                len = 2;
            } else if (f.intValue >= -2147483648LL && f.intValue <= 2147483647LL) {
                serialType = 4;
                len = 4;
            } else {
                serialType = 6;
                len = 8;
            }
            auto st = encodeVarint(serialType);
            serialTypes.insert(serialTypes.end(), st.begin(), st.end());
            uint64_t unsignedValue = static_cast<uint64_t>(f.intValue);
            for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
                body.push_back(static_cast<uint8_t>((unsignedValue >> (8 * i)) & 0xff));
            }
        } else {
            uint64_t serialType = static_cast<uint64_t>(f.textValue.size()) * 2 + 13;
            auto st = encodeVarint(serialType);
            serialTypes.insert(serialTypes.end(), st.begin(), st.end());
            body.insert(body.end(), f.textValue.begin(), f.textValue.end());
        }
    }

    size_t headerLenSize = 1;
    std::vector<uint8_t> headerLenVarint;
    while (true) {
        headerLenVarint = encodeVarint(serialTypes.size() + headerLenSize);
        if (headerLenVarint.size() == headerLenSize) break;
        headerLenSize = headerLenVarint.size();
    }

    std::vector<uint8_t> record;
    record.insert(record.end(), headerLenVarint.begin(), headerLenVarint.end());
    record.insert(record.end(), serialTypes.begin(), serialTypes.end());
    record.insert(record.end(), body.begin(), body.end());
    return record;
}

inline std::vector<uint8_t> buildLeafPage(uint32_t pageSize, size_t headerOffset,
                                           const std::vector<std::pair<int64_t, std::vector<uint8_t>>>& cells) {
    std::vector<uint8_t> page(pageSize, 0);
    std::vector<uint16_t> cellOffsets;
    size_t contentEnd = pageSize;

    for (const auto& cellEntry : cells) {
        int64_t rowid = cellEntry.first;
        const std::vector<uint8_t>& record = cellEntry.second;
        auto payloadLenVarint = encodeVarint(record.size());
        auto rowidVarint = encodeVarint(static_cast<uint64_t>(rowid));
        std::vector<uint8_t> cellBytes;
        cellBytes.insert(cellBytes.end(), payloadLenVarint.begin(), payloadLenVarint.end());
        cellBytes.insert(cellBytes.end(), rowidVarint.begin(), rowidVarint.end());
        cellBytes.insert(cellBytes.end(), record.begin(), record.end());
        contentEnd -= cellBytes.size();
        std::copy(cellBytes.begin(), cellBytes.end(), page.begin() + static_cast<long>(contentEnd));
        cellOffsets.push_back(static_cast<uint16_t>(contentEnd));
    }

    page[headerOffset] = 0x0d;
    page[headerOffset + 1] = 0;
    page[headerOffset + 2] = 0;
    uint16_t numCells = static_cast<uint16_t>(cells.size());
    page[headerOffset + 3] = static_cast<uint8_t>(numCells >> 8);
    page[headerOffset + 4] = static_cast<uint8_t>(numCells & 0xff);
    uint16_t contentStart = static_cast<uint16_t>(contentEnd);
    page[headerOffset + 5] = static_cast<uint8_t>(contentStart >> 8);
    page[headerOffset + 6] = static_cast<uint8_t>(contentStart & 0xff);
    page[headerOffset + 7] = 0;

    size_t ptrArrayStart = headerOffset + 8;
    for (size_t i = 0; i < cellOffsets.size(); ++i) {
        uint16_t off = cellOffsets[i];
        page[ptrArrayStart + 2 * i] = static_cast<uint8_t>(off >> 8);
        page[ptrArrayStart + 2 * i + 1] = static_cast<uint8_t>(off & 0xff);
    }
    return page;
}

inline std::vector<uint8_t> buildFileHeader(uint32_t pageSize, uint32_t pageCount) {
    std::vector<uint8_t> h(100, 0);
    const char magic[] = "SQLite format 3";
    for (int i = 0; i < 15; ++i) h[static_cast<size_t>(i)] = static_cast<uint8_t>(magic[i]);
    h[15] = 0;
    uint16_t pageSizeField = (pageSize == 65536) ? 1 : static_cast<uint16_t>(pageSize);
    h[16] = static_cast<uint8_t>(pageSizeField >> 8);
    h[17] = static_cast<uint8_t>(pageSizeField & 0xff);
    h[18] = 1;
    h[19] = 1;
    h[20] = 0;
    h[21] = 64;
    h[22] = 32;
    h[23] = 32;
    auto putU32 = [&](size_t offset, uint32_t value) {
        h[offset] = static_cast<uint8_t>((value >> 24) & 0xff);
        h[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xff);
        h[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xff);
        h[offset + 3] = static_cast<uint8_t>(value & 0xff);
    };
    putU32(24, 1);
    putU32(28, pageCount);
    putU32(32, 0);
    putU32(36, 0);
    putU32(40, 1);
    putU32(44, 4);
    putU32(48, 0);
    putU32(52, 0);
    putU32(56, 1);
    putU32(60, 0);
    putU32(64, 0);
    putU32(68, 0);
    putU32(92, 1);
    putU32(96, 3045000);
    return h;
}

inline std::string buildCreateTableSql(const std::string& tableName, const std::vector<std::string>& columnNames) {
    std::string sql = "CREATE TABLE " + tableName + " (";
    for (size_t i = 0; i < columnNames.size(); ++i) {
        if (i != 0) sql += ", ";
        sql += columnNames[i] + " TEXT";
    }
    sql += ")";
    return sql;
}

class DatabaseBuilder {
public:
    explicit DatabaseBuilder(uint32_t pageSize = 4096) : pageSize_(pageSize) {}

    uint32_t addTable(const std::string& name, const std::vector<std::string>& columnNames,
                       const std::vector<std::vector<Field>>& rows) {
        uint32_t rootPage = static_cast<uint32_t>(tablePages_.size() + 2);
        std::vector<std::pair<int64_t, std::vector<uint8_t>>> cells;
        for (size_t i = 0; i < rows.size(); ++i) {
            cells.emplace_back(static_cast<int64_t>(i + 1), buildRecord(rows[i]));
        }
        tablePages_.push_back(buildLeafPage(pageSize_, 0, cells));
        tables_.push_back(TableMeta{name, rootPage, buildCreateTableSql(name, columnNames)});
        return rootPage;
    }

    std::vector<uint8_t> build() const {
        std::vector<std::pair<int64_t, std::vector<uint8_t>>> masterCells;
        for (size_t i = 0; i < tables_.size(); ++i) {
            const TableMeta& t = tables_[i];
            std::vector<Field> f;
            f.push_back(Field::text("table"));
            f.push_back(Field::text(t.name));
            f.push_back(Field::text(t.name));
            f.push_back(Field::integer(static_cast<int64_t>(t.rootPage)));
            f.push_back(Field::text(t.sql));
            masterCells.emplace_back(static_cast<int64_t>(i + 1), buildRecord(f));
        }
        std::vector<uint8_t> page1 = buildLeafPage(pageSize_, 100, masterCells);
        uint32_t totalPages = static_cast<uint32_t>(1 + tablePages_.size());
        std::vector<uint8_t> header = buildFileHeader(pageSize_, totalPages);
        for (size_t i = 0; i < 100; ++i) page1[i] = header[i];

        std::vector<uint8_t> out;
        out.reserve(static_cast<size_t>(pageSize_) * totalPages);
        out.insert(out.end(), page1.begin(), page1.end());
        for (const auto& p : tablePages_) out.insert(out.end(), p.begin(), p.end());
        return out;
    }

private:
    struct TableMeta {
        std::string name;
        uint32_t rootPage;
        std::string sql;
    };

    uint32_t pageSize_;
    std::vector<std::vector<uint8_t>> tablePages_;
    std::vector<TableMeta> tables_;
};

}
