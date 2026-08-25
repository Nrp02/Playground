#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "../src/external_merge_sort.hpp"
#include "../src/record.hpp"

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

std::vector<ems::Record> makeRandomRecords(std::size_t count, unsigned seed) {
    std::vector<ems::Record> records;
    records.reserve(count);
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::int64_t> keyDist(-5000, 5000);
    for (std::size_t i = 0; i < count; ++i) {
        records.push_back(ems::Record{keyDist(rng), static_cast<std::int64_t>(i)});
    }
    return records;
}

bool keyLess(const ems::Record& a, const ems::Record& b) {
    return a.key < b.key;
}

std::size_t countFilesWithPrefix(const std::string& dir, const std::string& prefix) {
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
            ++count;
        }
    }
    return count;
}

}

int main() {
    const std::string tempDir = ".";

    {
        std::vector<ems::Record> records = makeRandomRecords(2000, 42);
        const std::string inputPath = "test_ems_input_random.bin";
        const std::string outputPath = "test_ems_output_random.bin";
        ems::ExternalMergeSort<ems::Record>::writeRecords(inputPath, records);

        ems::ExternalMergeSort<ems::Record> sorter(100, 4, tempDir);
        ems::SortStats stats = sorter.sort(inputPath, outputPath);

        std::vector<ems::Record> result =
            ems::ExternalMergeSort<ems::Record>::readAllRecords(outputPath);

        std::vector<ems::Record> expected = records;
        std::sort(expected.begin(), expected.end(), keyLess);

        expectEq(result.size(), expected.size(), "random data: output size matches input size");

        bool sortedByKey = std::is_sorted(result.begin(), result.end(), keyLess);
        expectTrue(sortedByKey, "random data: output sorted by key");

        std::vector<std::int64_t> resultKeys;
        std::vector<std::int64_t> expectedKeys;
        for (const auto& r : result) resultKeys.push_back(r.key);
        for (const auto& r : expected) expectedKeys.push_back(r.key);
        expectEq(resultKeys, expectedKeys, "random data: keys match std::sort reference");

        expectTrue(stats.runCount == 20, "random data: run count matches ceil(2000/100)");
        expectTrue(stats.passCount >= 1, "random data: at least one merge pass performed");

        std::remove(inputPath.c_str());
        std::remove(outputPath.c_str());
    }

    {
        const std::string inputPath = "test_ems_input_empty.bin";
        const std::string outputPath = "test_ems_output_empty.bin";
        ems::ExternalMergeSort<ems::Record>::writeRecords(inputPath, {});

        ems::ExternalMergeSort<ems::Record> sorter(10, 4, tempDir);
        ems::SortStats stats = sorter.sort(inputPath, outputPath);

        std::vector<ems::Record> result =
            ems::ExternalMergeSort<ems::Record>::readAllRecords(outputPath);

        expectTrue(result.empty(), "empty input: output is empty");
        expectEq<std::size_t>(stats.runCount, 0, "empty input: zero runs created");
        expectEq<std::size_t>(stats.passCount, 0, "empty input: zero merge passes");

        std::remove(inputPath.c_str());
        std::remove(outputPath.c_str());
    }

    {
        std::vector<ems::Record> records = makeRandomRecords(5, 7);
        const std::string inputPath = "test_ems_input_single_run.bin";
        const std::string outputPath = "test_ems_output_single_run.bin";
        ems::ExternalMergeSort<ems::Record>::writeRecords(inputPath, records);

        ems::ExternalMergeSort<ems::Record> sorter(1000, 4, tempDir);
        ems::SortStats stats = sorter.sort(inputPath, outputPath);

        std::vector<ems::Record> result =
            ems::ExternalMergeSort<ems::Record>::readAllRecords(outputPath);

        std::vector<ems::Record> expected = records;
        std::sort(expected.begin(), expected.end(), keyLess);

        bool sortedByKey = std::is_sorted(result.begin(), result.end(), keyLess);
        expectTrue(sortedByKey, "single run: output sorted");
        expectEq(result.size(), records.size(), "single run: output size preserved");
        expectEq<std::size_t>(stats.runCount, 1, "single run: exactly one run created");
        expectEq<std::size_t>(stats.passCount, 1, "single run: one pass to copy to output");

        std::remove(inputPath.c_str());
        std::remove(outputPath.c_str());
    }

    {
        std::vector<ems::Record> records = makeRandomRecords(600, 99);
        const std::string inputPath = "test_ems_input_exact_multiple.bin";
        const std::string outputPath = "test_ems_output_exact_multiple.bin";
        ems::ExternalMergeSort<ems::Record>::writeRecords(inputPath, records);

        ems::ExternalMergeSort<ems::Record> sorter(200, 5, tempDir);
        ems::SortStats stats = sorter.sort(inputPath, outputPath);

        std::vector<ems::Record> result =
            ems::ExternalMergeSort<ems::Record>::readAllRecords(outputPath);

        std::vector<ems::Record> expected = records;
        std::sort(expected.begin(), expected.end(), keyLess);

        bool sortedByKey = std::is_sorted(result.begin(), result.end(), keyLess);
        expectTrue(sortedByKey, "exact multiple: output sorted");
        expectEq(result.size(), records.size(), "exact multiple: output size preserved");
        expectEq<std::size_t>(stats.runCount, 3, "exact multiple: run count matches 600/200");

        std::remove(inputPath.c_str());
        std::remove(outputPath.c_str());
    }

    {
        std::vector<ems::Record> records = makeRandomRecords(3000, 55);
        const std::string inputPath = "test_ems_input_cleanup.bin";
        const std::string outputPath = "test_ems_output_cleanup.bin";
        ems::ExternalMergeSort<ems::Record>::writeRecords(inputPath, records);

        ems::ExternalMergeSort<ems::Record> sorter(50, 3, tempDir);
        ems::SortStats stats = sorter.sort(inputPath, outputPath);

        expectTrue(stats.runCount > 0, "cleanup: sort produced at least one run");

        std::size_t leftoverRunFiles = countFilesWithPrefix(tempDir, "ems_run_");
        std::size_t leftoverMergeFiles = countFilesWithPrefix(tempDir, "ems_merge_");
        expectEq<std::size_t>(leftoverRunFiles, 0, "cleanup: no leftover run temp files");
        expectEq<std::size_t>(leftoverMergeFiles, 0, "cleanup: no leftover merge temp files");

        std::remove(inputPath.c_str());
        std::remove(outputPath.c_str());
    }

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all tests passed\n";
    return 0;
}
