#include <fstream>
#include <iostream>

#include "../tests/fixture_builder.hpp"
#include "sqlite_reader.hpp"

int main() {
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

    std::vector<uint8_t> fileBytes = builder.build();
    const std::string dbPath = "bin/fixture_demo.db";
    std::ofstream out(dbPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));
    out.close();

    sqlitereader::Database db = sqlitereader::Database::loadFromFile(dbPath);

    std::cout << "page size: " << db.header().pageSize << "\n";
    std::cout << "page count: " << db.header().pageCount << "\n";

    auto usersSchema = db.findTable("users");
    if (usersSchema) {
        std::cout << "found table 'users' at root page " << usersSchema->rootPage << " with columns:";
        for (const auto& col : usersSchema->columns) {
            std::cout << " " << col;
        }
        std::cout << "\n";
    }

    std::cout << "\nSELECT name, age FROM users\n";
    for (const auto& row : db.select("users", {"name", "age"})) {
        std::cout << "  " << row.values[0].textValue << ", " << row.values[1].intValue << "\n";
    }

    std::cout << "\nSELECT id, name FROM users WHERE name = 'Bob'\n";
    auto whereResult = db.select("users", {"id", "name"},
                                  std::make_pair(std::string("name"), sqlitereader::Value::makeText("Bob")));
    for (const auto& row : whereResult) {
        std::cout << "  " << row.values[0].intValue << ", " << row.values[1].textValue << "\n";
    }

    std::cout << "\nSELECT name FROM cities\n";
    for (const auto& row : db.select("cities", {"name"})) {
        std::cout << "  " << row.values[0].textValue << "\n";
    }

    return 0;
}
