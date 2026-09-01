#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace suffix_array {

inline std::vector<int> buildSuffixArray(const std::string& text) {
    const int n = static_cast<int>(text.size());
    std::vector<int> order(n);
    if (n == 0) {
        return order;
    }

    std::vector<int> rankArr(n), secondKey(n), tmpRank(n);
    int maxRank = 0;
    for (int i = 0; i < n; ++i) {
        order[i] = i;
        rankArr[i] = static_cast<unsigned char>(text[i]);
        maxRank = std::max(maxRank, rankArr[i]);
    }

    auto countingSortByKey = [&](std::vector<int>& ord, const std::vector<int>& key, int keyRange) {
        std::vector<int> count(static_cast<std::size_t>(keyRange) + 3, 0);
        for (int idx = 0; idx < n; ++idx) {
            count[key[idx] + 2]++;
        }
        for (std::size_t idx = 1; idx < count.size(); ++idx) {
            count[idx] += count[idx - 1];
        }
        std::vector<int> result(n);
        for (int idx = 0; idx < n; ++idx) {
            int i = ord[idx];
            result[count[key[i] + 1]++] = i;
        }
        ord = result;
    };

    for (int k = 1;; k *= 2) {
        for (int i = 0; i < n; ++i) {
            secondKey[i] = (i + k < n) ? rankArr[i + k] : -1;
        }
        countingSortByKey(order, secondKey, maxRank);
        countingSortByKey(order, rankArr, maxRank);

        tmpRank[order[0]] = 0;
        int newMax = 0;
        for (int i = 1; i < n; ++i) {
            const int prev = order[i - 1];
            const int cur = order[i];
            const bool same = rankArr[prev] == rankArr[cur] && secondKey[prev] == secondKey[cur];
            tmpRank[cur] = tmpRank[prev] + (same ? 0 : 1);
            newMax = std::max(newMax, tmpRank[cur]);
        }
        rankArr.swap(tmpRank);
        maxRank = newMax;
        if (maxRank == n - 1 || k >= n) {
            break;
        }
    }

    return order;
}

inline std::vector<int> buildLcpArray(const std::string& text, const std::vector<int>& sa) {
    const int n = static_cast<int>(text.size());
    std::vector<int> lcp(n, 0);
    if (n == 0) {
        return lcp;
    }

    std::vector<int> rankOfSuffix(n);
    for (int i = 0; i < n; ++i) {
        rankOfSuffix[sa[i]] = i;
    }

    int h = 0;
    for (int i = 0; i < n; ++i) {
        if (rankOfSuffix[i] > 0) {
            const int j = sa[rankOfSuffix[i] - 1];
            while (i + h < n && j + h < n && text[i + h] == text[j + h]) {
                ++h;
            }
            lcp[rankOfSuffix[i]] = h;
            if (h > 0) {
                --h;
            }
        } else {
            h = 0;
        }
    }

    return lcp;
}

class SuffixArray {
public:
    explicit SuffixArray(std::string text)
        : text_(std::move(text)), sa_(buildSuffixArray(text_)), lcp_(buildLcpArray(text_, sa_)) {}

    const std::string& text() const { return text_; }
    const std::vector<int>& sa() const { return sa_; }
    const std::vector<int>& lcp() const { return lcp_; }

    std::pair<int, int> matchRange(const std::string& pattern) const {
        return {lowerBound(pattern), upperBound(pattern)};
    }

    std::size_t count(const std::string& pattern) const {
        const auto range = matchRange(pattern);
        return static_cast<std::size_t>(range.second - range.first);
    }

    std::vector<int> findAll(const std::string& pattern) const {
        const auto range = matchRange(pattern);
        std::vector<int> result(sa_.begin() + range.first, sa_.begin() + range.second);
        std::sort(result.begin(), result.end());
        return result;
    }

    std::string longestRepeatedSubstring() const {
        if (lcp_.empty()) {
            return "";
        }
        int bestIdx = 0;
        int bestLen = 0;
        for (int i = 1; i < static_cast<int>(lcp_.size()); ++i) {
            if (lcp_[i] > bestLen) {
                bestLen = lcp_[i];
                bestIdx = i;
            }
        }
        if (bestLen == 0) {
            return "";
        }
        return text_.substr(sa_[bestIdx], bestLen);
    }

    std::size_t distinctSubstringCount() const {
        const std::size_t n = text_.size();
        std::size_t total = n * (n + 1) / 2;
        std::size_t sumLcp = 0;
        for (int v : lcp_) {
            sumLcp += static_cast<std::size_t>(v);
        }
        return total - sumLcp;
    }

private:
    int compareToSuffix(const std::string& pattern, int suffixStart) const {
        const int n = static_cast<int>(text_.size());
        const int m = static_cast<int>(pattern.size());
        int i = 0;
        while (i < m && suffixStart + i < n) {
            const unsigned char a = static_cast<unsigned char>(pattern[i]);
            const unsigned char b = static_cast<unsigned char>(text_[suffixStart + i]);
            if (a != b) {
                return a < b ? -1 : 1;
            }
            ++i;
        }
        if (i == m) {
            return 0;
        }
        return 1;
    }

    int lowerBound(const std::string& pattern) const {
        int lo = 0;
        int hi = static_cast<int>(sa_.size());
        while (lo < hi) {
            const int mid = lo + (hi - lo) / 2;
            if (compareToSuffix(pattern, sa_[mid]) > 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }

    int upperBound(const std::string& pattern) const {
        int lo = 0;
        int hi = static_cast<int>(sa_.size());
        while (lo < hi) {
            const int mid = lo + (hi - lo) / 2;
            if (compareToSuffix(pattern, sa_[mid]) >= 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }

    std::string text_;
    std::vector<int> sa_;
    std::vector<int> lcp_;
};

inline std::string longestCommonSubstring(const std::string& a, const std::string& b) {
    const char sep1 = static_cast<char>(1);
    const char sep2 = static_cast<char>(2);

    std::string combined;
    combined.reserve(a.size() + b.size() + 2);
    combined += a;
    combined += sep1;
    combined += b;
    combined += sep2;

    const auto sa = buildSuffixArray(combined);
    const auto lcp = buildLcpArray(combined, sa);
    const std::size_t bStart = a.size() + 1;
    const std::size_t bEnd = combined.size() - 1;

    std::string best;
    for (std::size_t i = 1; i < sa.size(); ++i) {
        const std::size_t x = static_cast<std::size_t>(sa[i - 1]);
        const std::size_t y = static_cast<std::size_t>(sa[i]);
        const bool xInA = x < a.size();
        const bool yInA = y < a.size();
        const bool xInB = x >= bStart && x < bEnd;
        const bool yInB = y >= bStart && y < bEnd;
        const bool crossesSources = (xInA && yInB) || (xInB && yInA);
        if (crossesSources && lcp[i] > static_cast<int>(best.size())) {
            best = combined.substr(sa[i], static_cast<std::size_t>(lcp[i]));
        }
    }
    return best;
}

}
