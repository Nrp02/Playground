#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace ac {

struct Match {
    int patternIndex;
    std::size_t endPosition;
};

class AhoCorasick {
public:
    explicit AhoCorasick(bool caseInsensitive = false) : caseInsensitive_(caseInsensitive) {
        nodes_.emplace_back();
    }

    int addPattern(const std::string& pattern) {
        std::string normalized = normalize(pattern);
        int node = 0;
        for (char rawChar : normalized) {
            unsigned char childKey = static_cast<unsigned char>(rawChar);
            auto it = nodes_[node].children.find(childKey);
            if (it == nodes_[node].children.end()) {
                nodes_.emplace_back();
                int newIndex = static_cast<int>(nodes_.size()) - 1;
                nodes_[node].children.emplace(childKey, newIndex);
                node = newIndex;
            } else {
                node = it->second;
            }
        }
        int patternIndex = static_cast<int>(patternLengths_.size());
        patternLengths_.push_back(normalized.size());
        nodes_[node].patternsEndingHere.push_back(patternIndex);
        built_ = false;
        return patternIndex;
    }

    void build() {
        std::deque<int> queue;
        for (auto& kv : nodes_[0].children) {
            nodes_[kv.second].fail = 0;
            nodes_[kv.second].output = nodes_[kv.second].patternsEndingHere;
            queue.push_back(kv.second);
        }
        while (!queue.empty()) {
            int current = queue.front();
            queue.pop_front();
            for (auto& kv : nodes_[current].children) {
                unsigned char symbol = kv.first;
                int child = kv.second;
                int fallback = nodes_[current].fail;
                while (fallback != 0 && nodes_[fallback].children.find(symbol) == nodes_[fallback].children.end()) {
                    fallback = nodes_[fallback].fail;
                }
                auto it = nodes_[fallback].children.find(symbol);
                if (it != nodes_[fallback].children.end() && it->second != child) {
                    nodes_[child].fail = it->second;
                } else {
                    nodes_[child].fail = 0;
                }
                Node& childNode = nodes_[child];
                Node& failNode = nodes_[childNode.fail];
                childNode.output = childNode.patternsEndingHere;
                childNode.output.insert(childNode.output.end(), failNode.output.begin(), failNode.output.end());
                queue.push_back(child);
            }
        }
        built_ = true;
    }

    std::vector<Match> scan(const std::string& text) {
        if (!built_) {
            build();
        }
        int state = 0;
        std::vector<Match> matches;
        std::string normalized = normalize(text);
        for (std::size_t position = 0; position < normalized.size(); ++position) {
            state = step(state, static_cast<unsigned char>(normalized[position]));
            for (int patternIndex : nodes_[state].output) {
                matches.push_back(Match{patternIndex, position});
            }
        }
        return matches;
    }

    class StreamingScanner {
    public:
        explicit StreamingScanner(AhoCorasick& automaton) : automaton_(automaton) {
            if (!automaton_.built_) {
                automaton_.build();
            }
        }

        std::vector<Match> feed(const std::string& chunk) {
            std::vector<Match> matches;
            std::string normalized = automaton_.normalize(chunk);
            for (char rawChar : normalized) {
                state_ = automaton_.step(state_, static_cast<unsigned char>(rawChar));
                ++consumed_;
                for (int patternIndex : automaton_.nodes_[state_].output) {
                    matches.push_back(Match{patternIndex, consumed_ - 1});
                }
            }
            return matches;
        }

    private:
        AhoCorasick& automaton_;
        int state_ = 0;
        std::size_t consumed_ = 0;
    };

    StreamingScanner streamingScanner() {
        return StreamingScanner(*this);
    }

    std::size_t patternLength(int patternIndex) const {
        return patternLengths_[patternIndex];
    }

    std::size_t patternCount() const {
        return patternLengths_.size();
    }

    std::string replaceAll(const std::string& text, const std::string& replacement) const {
        AhoCorasick& self = const_cast<AhoCorasick&>(*this);
        std::vector<Match> matches = self.scan(text);
        std::vector<bool> covered(text.size(), false);
        std::sort(matches.begin(), matches.end(), [this](const Match& lhs, const Match& rhs) {
            std::size_t lhsStart = lhs.endPosition + 1 - patternLength(lhs.patternIndex);
            std::size_t rhsStart = rhs.endPosition + 1 - patternLength(rhs.patternIndex);
            if (lhsStart != rhsStart) {
                return lhsStart < rhsStart;
            }
            return patternLength(lhs.patternIndex) > patternLength(rhs.patternIndex);
        });
        std::string result;
        std::size_t cursor = 0;
        for (const Match& match : matches) {
            std::size_t length = patternLength(match.patternIndex);
            std::size_t start = match.endPosition + 1 - length;
            if (start < cursor) {
                continue;
            }
            result.append(text, cursor, start - cursor);
            result += replacement;
            cursor = start + length;
        }
        result.append(text, cursor, text.size() - cursor);
        return result;
    }

private:
    struct Node {
        std::unordered_map<unsigned char, int> children;
        int fail = 0;
        std::vector<int> patternsEndingHere;
        std::vector<int> output;
    };

    std::string normalize(const std::string& input) const {
        if (!caseInsensitive_) {
            return input;
        }
        std::string result = input;
        for (char& character : result) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        return result;
    }

    int step(int state, unsigned char symbol) {
        while (state != 0 && nodes_[state].children.find(symbol) == nodes_[state].children.end()) {
            state = nodes_[state].fail;
        }
        auto it = nodes_[state].children.find(symbol);
        if (it != nodes_[state].children.end()) {
            state = it->second;
        }
        return state;
    }

    std::vector<Node> nodes_;
    std::vector<std::size_t> patternLengths_;
    bool caseInsensitive_;
    bool built_ = false;
};

}
