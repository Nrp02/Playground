#include <iostream>
#include <string>
#include <vector>

#include "../src/sqlite_reader.hpp"
#include "fixture_builder.hpp"

namespace {

int g_failures = 0;

void expectTrue(bool condition, const std::string& testName) {
    if (!condition) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

template <typename T>
void expectEq(const T& actual, const T& expected, const std::string& testName) {
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << testName << "\n";
        ++g_failures;
        return;
    }
    std::cout << "PASS: " << testName << "\n";
}

sqlitereader::Database buildFixtureDatabase() {
    using fixturebuilder::DatabaseBuilder;
    using fixturebuilder::Field;

    DatabaseBuilder builder(4096);
    builder.addTable("users", {"id", "name", "age"},
                      {
                          {Field::integer(1), Field::text("Alice"), Field::integer(30)},
                          {Field::integer(2), Field::text("Bob"), Field::integer(25)},
                          {Field::integer(3), Field::text("Carol"), Field::integer(41)},
                      });
    builder.addTable("cities", {"id", "name"},
                      {
                          {Field::integer(1), Field::text("Bangkok")},
                          {Field::integer(2), Field::text("Chiang Mai")},
                      });
    return sqlitereader::Database(builder.build());
}

}

int main() {
    {
        std::vector<uint8_t> data(100, 0);
        const char magic[] = "SQLite format 3";
        for (int i = 0; i < 15; ++i) data[static_cast<size_t>(i)] = static_cast<uint8_t>(magic[i]);
        data[15] = 0;
        data[16] = 0x10;
        data[17] = 0x00;
        data[20] = 0;
        data[28] = 0;
        data[29] = 0;
        data[30] = 0;
        data[31] = 3;
        auto header = sqlitereader::parseFileHeader(data);
        expectEq<uint32_t>(header.pageSize, 4096u, "header: page size 0x1000 decodes to 4096");
        expectEq<uint32_t>(header.pageCount, 3u, "header: page count decodes from big-endian u32");
    }
    {
        std::vector<uint8_t> data(100, 0);
        const char magic[] = "SQLite format 3";
        for (int i = 0; i < 15; ++i) data[static_cast<size_t>(i)] = static_cast<uint8_t>(magic[i]);
        data[15] = 0;
        data[16] = 0x00;
        data[17] = 0x01;
        auto header = sqlitereader::parseFileHeader(data);
        expectEq<uint32_t>(header.pageSize, 65536u, "header: page size field 1 means 65536");
    }
    {
        uint8_t oneByteVarint[1] = {0x42};
        auto result = sqlitereader::decodeVarint(oneByteVarint);
        expectEq<uint64_t>(result.value, 0x42u, "varint: single-byte value");
        expectEq<size_t>(result.bytesRead, 1u, "varint: single-byte length");
    }
    {
        uint8_t twoByteVarint[2] = {0x81, 0x00};
        auto result = sqlitereader::decodeVarint(twoByteVarint);
        expectEq<uint64_t>(result.value, 0x80u, "varint: two-byte value 0x81 0x00 == 128");
        expectEq<size_t>(result.bytesRead, 2u, "varint: two-byte length");
    }
    {
        uint8_t threeByteVarint[3] = {0x81, 0x80, 0x01};
        auto result = sqlitereader::decodeVarint(threeByteVarint);
        expectEq<uint64_t>(result.value, (1u << 14) + 1u, "varint: three-byte value decodes correctly");
        expectEq<size_t>(result.bytesRead, 3u, "varint: three-byte length");
    }
    {
        auto encoded = fixturebuilder::encodeVarint(300);
        auto decoded = sqlitereader::decodeVarint(encoded.data());
        expectEq<uint64_t>(decoded.value, 300u, "varint: round trip encode/decode of 300");
        expectEq<size_t>(decoded.bytesRead, encoded.size(), "varint: round trip byte length matches");
    }
    {
        auto columns = sqlitereader::parseCreateTableColumns("CREATE TABLE users (id INTEGER, name TEXT, age INTEGER)");
        std::vector<std::string> expected = {"id", "name", "age"};
        expectTrue(columns == expected, "create table sql: parses column names in order");
    }
    {
        sqlitereader::Database db = buildFixtureDatabase();
        expectEq<uint32_t>(db.header().pageSize, 4096u, "fixture: page size matches builder configuration");
        expectEq<uint32_t>(db.header().pageCount, 3u, "fixture: page count is master page plus two table pages");
    }
    {
        sqlitereader::Database db = buildFixtureDatabase();
        auto schema = db.findTable("users");
        expectTrue(schema.has_value(), "sqlite_master: finds the users table");
        if (schema) {
            expectEq<uint32_t>(schema->rootPage, 2u, "sqlite_master: users table root page is 2");
            std::vector<std::string> expectedCols = {"id", "name", "age"};
            expectTrue(schema->columns == expectedCols, "sqlite_master: users table columns parsed correctly");
        }
        auto citiesSchema = db.findTable("cities");
        expectTrue(citiesSchema.has_value(), "sqlite_master: finds the cities table");
        if (citiesSchema) {
            expectEq<uint32_t>(citiesSchema->rootPage, 3u, "sqlite_master: cities table root page is 3");
        }
        auto missing = db.findTable("nonexistent");
        expectTrue(!missing.has_value(), "sqlite_master: missing table returns nullopt");
    }
    {
        sqlitereader::Database db = buildFixtureDatabase();
        auto rows = db.select("users", {"name", "age"});
        expectEq<size_t>(rows.size(), 3u, "select: no-where select returns all rows");
        expectTrue(rows[0].values[0].type == sqlitereader::Value::Type::Text, "select: text column decodes as text");
        expectEq<std::string>(rows[0].values[0].textValue, std::string("Alice"), "select: first row name is Alice");
        expectEq<int64_t>(rows[0].values[1].intValue, 30, "select: first row age is 30");
        expectEq<std::string>(rows[1].values[0].textValue, std::string("Bob"), "select: second row name is Bob");
        expectEq<std::string>(rows[2].values[0].textValue, std::string("Carol"), "select: third row name is Carol");
    }
    {
        sqlitereader::Database db = buildFixtureDatabase();
        auto rows = db.select("users", {"id", "name"},
                               std::make_pair(std::string("name"), sqlitereader::Value::makeText("Bob")));
        expectEq<size_t>(rows.size(), 1u, "select where: equality filter returns exactly one row");
        expectEq<int64_t>(rows[0].values[0].intValue, 2, "select where: matched row has id 2");
        expectEq<std::string>(rows[0].values[1].textValue, std::string("Bob"), "select where: matched row name is Bob");
    }
    {
        sqlitereader::Database db = buildFixtureDatabase();
        auto rows = db.select("users", {"name"},
                               std::make_pair(std::string("age"), sqlitereader::Value::makeInteger(999)));
        expectEq<size_t>(rows.size(), 0u, "select where: no match returns empty result");
    }
    {
        sqlitereader::Database db = buildFixtureDatabase();
        auto rows = db.select("cities", {"id", "name"});
        expectEq<size_t>(rows.size(), 2u, "select: cities table returns both rows");
        expectEq<std::string>(rows[1].values[1].textValue, std::string("Chiang Mai"), "select: cities second row is Chiang Mai");
    }

    if (g_failures == 0) {
        std::cout << "\nAll tests passed.\n";
    } else {
        std::cout << "\n" << g_failures << " test(s) failed.\n";
    }
    return g_failures == 0 ? 0 : 1;
}
