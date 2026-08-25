#include <algorithm>
#include <cstdio>
#include <iostream>
#include <random>
#include <vector>

#include "external_merge_sort.hpp"
#include "record.hpp"

namespace {

std::vector<ems::Record> generateDataset(std::size_t count, unsigned seed) {
    std::vector<ems::Record> records;
    records.reserve(count);
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<std::int64_t> keyDist(-1000000, 1000000);
    for (std::size_t i = 0; i < count; ++i) {
        records.push_back(ems::Record{keyDist(rng), static_cast<std::int64_t>(i)});
    }
    return records;
}

}

int main() {
    const std::size_t datasetSize = 200000;
    const std::size_t memoryBudgetRecords = 4000;
    const std::size_t mergeFanIn = 8;

    std::vector<ems::Record> dataset = generateDataset(datasetSize, 1234);

    const std::string inputPath = "external_merge_sort_input.bin";
    const std::string outputPath = "external_merge_sort_output.bin";

    ems::ExternalMergeSort<ems::Record>::writeRecords(inputPath, dataset);

    ems::ExternalMergeSort<ems::Record> sorter(memoryBudgetRecords, mergeFanIn);
    ems::SortStats stats = sorter.sort(inputPath, outputPath);

    std::vector<ems::Record> sorted =
        ems::ExternalMergeSort<ems::Record>::readAllRecords(outputPath);

    bool isSorted = std::is_sorted(
        sorted.begin(), sorted.end(),
        [](const ems::Record& a, const ems::Record& b) { return a.key < b.key; });

    std::vector<ems::Record> expected = dataset;
    std::sort(expected.begin(), expected.end(),
              [](const ems::Record& a, const ems::Record& b) { return a.key < b.key; });

    bool sameSize = sorted.size() == dataset.size();

    std::cout << "dataset size: " << dataset.size() << "\n";
    std::cout << "memory budget (records): " << memoryBudgetRecords << "\n";
    std::cout << "merge fan-in: " << mergeFanIn << "\n";
    std::cout << "run count: " << stats.runCount << "\n";
    std::cout << "merge pass count: " << stats.passCount << "\n";
    std::cout << "output record count: " << sorted.size() << "\n";
    std::cout << "output is sorted: " << (isSorted ? "yes" : "no") << "\n";
    std::cout << "output size matches input size: " << (sameSize ? "yes" : "no") << "\n";

    std::remove(inputPath.c_str());
    std::remove(outputPath.c_str());

    return (isSorted && sameSize) ? 0 : 1;
}
