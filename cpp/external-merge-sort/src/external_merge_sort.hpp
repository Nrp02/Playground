#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <queue>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace ems {

struct SortStats {
    std::size_t runCount = 0;
    std::size_t passCount = 0;
};

namespace detail {

class TempFileGuard {
public:
    explicit TempFileGuard(std::string path) : path_(std::move(path)), armed_(true) {}

    TempFileGuard(const TempFileGuard&) = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;

    TempFileGuard(TempFileGuard&& other) noexcept
        : path_(std::move(other.path_)), armed_(other.armed_) {
        other.armed_ = false;
    }

    TempFileGuard& operator=(TempFileGuard&& other) noexcept {
        if (this != &other) {
            disarm_and_remove();
            path_ = std::move(other.path_);
            armed_ = other.armed_;
            other.armed_ = false;
        }
        return *this;
    }

    ~TempFileGuard() {
        disarm_and_remove();
    }

    const std::string& path() const { return path_; }

    void release() { armed_ = false; }

private:
    void disarm_and_remove() {
        if (armed_) {
            std::remove(path_.c_str());
            armed_ = false;
        }
    }

    std::string path_;
    bool armed_;
};

inline std::atomic<std::uint64_t>& globalTempCounter() {
    static std::atomic<std::uint64_t> counter{0};
    return counter;
}

inline std::string makeTempPath(const std::string& tempDir, const std::string& tag,
                                 std::size_t id) {
    std::string dir = tempDir.empty() ? std::string(".") : tempDir;
    if (dir.back() != '/') {
        dir.push_back('/');
    }
    std::uint64_t unique = globalTempCounter().fetch_add(1);
    return dir + "ems_" + tag + "_" + std::to_string(id) + "_" + std::to_string(unique) + ".tmp";
}

}

template <typename T, typename Compare = std::less<T>>
class ExternalMergeSort {
public:
    static_assert(std::is_trivially_copyable<T>::value,
                  "ExternalMergeSort requires a trivially copyable record type");

    ExternalMergeSort(std::size_t memoryBudgetRecords, std::size_t mergeFanIn,
                       std::string tempDir = ".", Compare comp = Compare())
        : memoryBudgetRecords_(memoryBudgetRecords),
          mergeFanIn_(mergeFanIn),
          tempDir_(std::move(tempDir)),
          comp_(comp),
          nextTempId_(0) {
        if (memoryBudgetRecords_ == 0) {
            throw std::invalid_argument("memoryBudgetRecords must be positive");
        }
        if (mergeFanIn_ < 2) {
            throw std::invalid_argument("mergeFanIn must be at least 2");
        }
    }

    static void writeRecords(const std::string& path, const std::vector<T>& records) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to open file for writing: " + path);
        }
        if (!records.empty()) {
            out.write(reinterpret_cast<const char*>(records.data()),
                       static_cast<std::streamsize>(records.size() * sizeof(T)));
        }
    }

    static std::vector<T> readAllRecords(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("failed to open file for reading: " + path);
        }
        std::vector<T> result;
        T record;
        while (in.read(reinterpret_cast<char*>(&record), sizeof(T))) {
            result.push_back(record);
        }
        return result;
    }

    SortStats sort(const std::string& inputPath, const std::string& outputPath) {
        std::vector<detail::TempFileGuard> runGuards = createSortedRuns(inputPath);
        SortStats stats;
        stats.runCount = runGuards.size();

        if (runGuards.empty()) {
            std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
            if (!out) {
                throw std::runtime_error("failed to open file for writing: " + outputPath);
            }
            stats.passCount = 0;
            return stats;
        }

        std::size_t passCount = 0;
        while (runGuards.size() > mergeFanIn_) {
            runGuards = mergePass(std::move(runGuards));
            ++passCount;
        }

        std::vector<std::string> finalPaths;
        finalPaths.reserve(runGuards.size());
        for (const auto& guard : runGuards) {
            finalPaths.push_back(guard.path());
        }
        mergeGroup(finalPaths, outputPath);
        ++passCount;
        stats.passCount = passCount;

        return stats;
    }

private:
    struct HeapEntry {
        T value;
        std::size_t runIndex;
    };

    struct HeapEntryCompare {
        Compare comp;
        bool operator()(const HeapEntry& a, const HeapEntry& b) const {
            return comp(b.value, a.value);
        }
    };

    std::vector<detail::TempFileGuard> createSortedRuns(const std::string& inputPath) {
        std::ifstream in(inputPath, std::ios::binary);
        if (!in) {
            throw std::runtime_error("failed to open file for reading: " + inputPath);
        }

        std::vector<detail::TempFileGuard> runGuards;
        std::vector<T> buffer;
        buffer.reserve(memoryBudgetRecords_);

        T record;
        while (in.read(reinterpret_cast<char*>(&record), sizeof(T))) {
            buffer.push_back(record);
            if (buffer.size() >= memoryBudgetRecords_) {
                runGuards.push_back(flushRun(buffer));
                buffer.clear();
            }
        }
        if (!buffer.empty()) {
            runGuards.push_back(flushRun(buffer));
            buffer.clear();
        }

        return runGuards;
    }

    detail::TempFileGuard flushRun(std::vector<T>& buffer) {
        std::sort(buffer.begin(), buffer.end(), comp_);
        std::string path = detail::makeTempPath(tempDir_, "run", nextTempId_++);
        writeRecords(path, buffer);
        return detail::TempFileGuard(path);
    }

    std::vector<detail::TempFileGuard> mergePass(std::vector<detail::TempFileGuard> runGuards) {
        std::vector<detail::TempFileGuard> nextGuards;
        std::size_t index = 0;
        while (index < runGuards.size()) {
            std::size_t groupEnd = std::min(index + mergeFanIn_, runGuards.size());
            std::vector<std::string> groupPaths;
            groupPaths.reserve(groupEnd - index);
            for (std::size_t i = index; i < groupEnd; ++i) {
                groupPaths.push_back(runGuards[i].path());
            }

            std::string outPath = detail::makeTempPath(tempDir_, "merge", nextTempId_++);
            mergeGroup(groupPaths, outPath);
            nextGuards.emplace_back(outPath);

            index = groupEnd;
        }
        return nextGuards;
    }

    void mergeGroup(const std::vector<std::string>& inputPaths, const std::string& outputPath) {
        std::vector<std::ifstream> streams;
        streams.reserve(inputPaths.size());
        for (const auto& path : inputPaths) {
            streams.emplace_back(path, std::ios::binary);
            if (!streams.back()) {
                throw std::runtime_error("failed to open run file for reading: " + path);
            }
        }

        std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to open file for writing: " + outputPath);
        }

        HeapEntryCompare heapCompare{comp_};
        std::priority_queue<HeapEntry, std::vector<HeapEntry>, HeapEntryCompare> heap(
            heapCompare);

        for (std::size_t i = 0; i < streams.size(); ++i) {
            T record;
            if (streams[i].read(reinterpret_cast<char*>(&record), sizeof(T))) {
                heap.push(HeapEntry{record, i});
            }
        }

        while (!heap.empty()) {
            HeapEntry top = heap.top();
            heap.pop();
            out.write(reinterpret_cast<const char*>(&top.value), sizeof(T));

            T record;
            if (streams[top.runIndex].read(reinterpret_cast<char*>(&record), sizeof(T))) {
                heap.push(HeapEntry{record, top.runIndex});
            }
        }
    }

    std::size_t memoryBudgetRecords_;
    std::size_t mergeFanIn_;
    std::string tempDir_;
    Compare comp_;
    std::size_t nextTempId_;
};

}
