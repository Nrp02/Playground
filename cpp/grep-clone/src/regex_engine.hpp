#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace rex {

enum class NodeType {
    Literal,
    Any,
    Class,
    Concat,
    Alternate,
    Star,
    Plus,
    Quest,
    AssertStart,
    AssertEnd
};

struct Node {
    NodeType type;
    char ch = 0;
    bool negate = false;
    std::vector<std::pair<char, char>> ranges;
    std::vector<std::unique_ptr<Node>> children;
};

class Parser {
public:
    explicit Parser(const std::string& pattern) : pattern_(pattern), pos_(0) {}

    std::unique_ptr<Node> parse() {
        auto node = parseAlternation();
        if (pos_ != pattern_.size()) {
            throw std::runtime_error("unexpected character in pattern");
        }
        return node;
    }

private:
    const std::string& pattern_;
    std::size_t pos_;

    bool atEnd() const { return pos_ >= pattern_.size(); }
    char peek() const { return pattern_[pos_]; }
    char advance() { return pattern_[pos_++]; }

    std::unique_ptr<Node> parseAlternation() {
        auto left = parseConcat();
        while (!atEnd() && peek() == '|') {
            advance();
            auto right = parseConcat();
            auto node = std::make_unique<Node>();
            node->type = NodeType::Alternate;
            node->children.push_back(std::move(left));
            node->children.push_back(std::move(right));
            left = std::move(node);
        }
        return left;
    }

    std::unique_ptr<Node> parseConcat() {
        std::vector<std::unique_ptr<Node>> parts;
        while (!atEnd() && peek() != '|' && peek() != ')') {
            parts.push_back(parseRepeat());
        }
        if (parts.empty()) {
            auto node = std::make_unique<Node>();
            node->type = NodeType::Concat;
            return node;
        }
        auto result = std::move(parts.front());
        for (std::size_t i = 1; i < parts.size(); ++i) {
            auto node = std::make_unique<Node>();
            node->type = NodeType::Concat;
            node->children.push_back(std::move(result));
            node->children.push_back(std::move(parts[i]));
            result = std::move(node);
        }
        return result;
    }

    std::unique_ptr<Node> parseRepeat() {
        auto atom = parseAtom();
        if (!atEnd() && (peek() == '*' || peek() == '+' || peek() == '?')) {
            char op = advance();
            auto node = std::make_unique<Node>();
            node->type = op == '*' ? NodeType::Star
                        : op == '+' ? NodeType::Plus
                                    : NodeType::Quest;
            node->children.push_back(std::move(atom));
            return node;
        }
        return atom;
    }

    std::unique_ptr<Node> parseAtom() {
        if (atEnd()) {
            throw std::runtime_error("unexpected end of pattern");
        }
        char c = peek();
        if (c == '(') {
            advance();
            auto inner = parseAlternation();
            if (atEnd() || peek() != ')') {
                throw std::runtime_error("unmatched '(' in pattern");
            }
            advance();
            return inner;
        }
        if (c == '.') {
            advance();
            auto node = std::make_unique<Node>();
            node->type = NodeType::Any;
            return node;
        }
        if (c == '^') {
            advance();
            auto node = std::make_unique<Node>();
            node->type = NodeType::AssertStart;
            return node;
        }
        if (c == '$') {
            advance();
            auto node = std::make_unique<Node>();
            node->type = NodeType::AssertEnd;
            return node;
        }
        if (c == '[') {
            return parseClass();
        }
        advance();
        auto node = std::make_unique<Node>();
        node->type = NodeType::Literal;
        node->ch = c;
        return node;
    }

    std::unique_ptr<Node> parseClass() {
        advance();
        auto node = std::make_unique<Node>();
        node->type = NodeType::Class;
        if (!atEnd() && peek() == '^') {
            node->negate = true;
            advance();
        }
        bool first = true;
        while (!atEnd() && (peek() != ']' || first)) {
            first = false;
            char lo = advance();
            if (!atEnd() && peek() == '-' && pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] != ']') {
                advance();
                char hi = advance();
                node->ranges.emplace_back(lo, hi);
            } else {
                node->ranges.emplace_back(lo, lo);
            }
        }
        if (atEnd() || peek() != ']') {
            throw std::runtime_error("unmatched '[' in pattern");
        }
        advance();
        return node;
    }
};

struct State {
    enum class Kind { Char, Any, Class, Split, Match, AssertStart, AssertEnd };
    Kind kind = Kind::Char;
    char ch = 0;
    bool negate = false;
    std::vector<std::pair<char, char>> ranges;
    int out1 = -1;
    int out2 = -1;
};

class Nfa {
public:
    std::vector<State> states;
    int start = -1;

    int addState(State s) {
        states.push_back(s);
        return static_cast<int>(states.size()) - 1;
    }
};

struct Frag {
    int start;
    std::vector<std::pair<int, int>> outs;
};

class NfaBuilder {
public:
    explicit NfaBuilder(Nfa& nfa) : nfa_(nfa) {}

    Frag build(const Node* node) {
        switch (node->type) {
            case NodeType::Literal: {
                State s;
                s.kind = State::Kind::Char;
                s.ch = node->ch;
                int id = nfa_.addState(s);
                return Frag{id, {{id, 1}}};
            }
            case NodeType::Any: {
                State s;
                s.kind = State::Kind::Any;
                int id = nfa_.addState(s);
                return Frag{id, {{id, 1}}};
            }
            case NodeType::Class: {
                State s;
                s.kind = State::Kind::Class;
                s.negate = node->negate;
                s.ranges = node->ranges;
                int id = nfa_.addState(s);
                return Frag{id, {{id, 1}}};
            }
            case NodeType::AssertStart: {
                State s;
                s.kind = State::Kind::AssertStart;
                int id = nfa_.addState(s);
                return Frag{id, {{id, 1}}};
            }
            case NodeType::AssertEnd: {
                State s;
                s.kind = State::Kind::AssertEnd;
                int id = nfa_.addState(s);
                return Frag{id, {{id, 1}}};
            }
            case NodeType::Concat: {
                if (node->children.empty()) {
                    State s;
                    s.kind = State::Kind::Split;
                    int id = nfa_.addState(s);
                    return Frag{id, {{id, 1}}};
                }
                Frag left = build(node->children[0].get());
                Frag right = build(node->children[1].get());
                patch(left.outs, right.start);
                return Frag{left.start, right.outs};
            }
            case NodeType::Alternate: {
                Frag left = build(node->children[0].get());
                Frag right = build(node->children[1].get());
                State s;
                s.kind = State::Kind::Split;
                s.out1 = left.start;
                s.out2 = right.start;
                int id = nfa_.addState(s);
                std::vector<std::pair<int, int>> outs = left.outs;
                outs.insert(outs.end(), right.outs.begin(), right.outs.end());
                return Frag{id, outs};
            }
            case NodeType::Star: {
                Frag body = build(node->children[0].get());
                State s;
                s.kind = State::Kind::Split;
                s.out1 = body.start;
                int id = nfa_.addState(s);
                patch(body.outs, id);
                return Frag{id, {{id, 2}}};
            }
            case NodeType::Plus: {
                Frag body = build(node->children[0].get());
                State s;
                s.kind = State::Kind::Split;
                s.out1 = body.start;
                int id = nfa_.addState(s);
                patch(body.outs, id);
                return Frag{body.start, {{id, 2}}};
            }
            case NodeType::Quest: {
                Frag body = build(node->children[0].get());
                State s;
                s.kind = State::Kind::Split;
                s.out1 = body.start;
                int id = nfa_.addState(s);
                std::vector<std::pair<int, int>> outs = body.outs;
                outs.push_back({id, 2});
                return Frag{id, outs};
            }
        }
        throw std::runtime_error("unknown node type");
    }

private:
    Nfa& nfa_;

    void patch(const std::vector<std::pair<int, int>>& outs, int target) {
        for (const auto& [idx, slot] : outs) {
            if (slot == 1) {
                nfa_.states[idx].out1 = target;
            } else {
                nfa_.states[idx].out2 = target;
            }
        }
    }
};

class Regex {
public:
    explicit Regex(const std::string& pattern) : anchoredStart_(!pattern.empty() && pattern.front() == '^') {
        Parser parser(pattern);
        auto ast = parser.parse();
        NfaBuilder builder(nfa_);
        Frag frag = builder.build(ast.get());
        State matchState;
        matchState.kind = State::Kind::Match;
        int matchId = nfa_.addState(matchState);
        for (const auto& [idx, slot] : frag.outs) {
            if (slot == 1) {
                nfa_.states[idx].out1 = matchId;
            } else {
                nfa_.states[idx].out2 = matchId;
            }
        }
        nfa_.start = frag.start;

        if (anchoredStart_) {
            searchStart_ = nfa_.start;
        } else {
            State anyState;
            anyState.kind = State::Kind::Any;
            int anyId = nfa_.addState(anyState);
            State skipState;
            skipState.kind = State::Kind::Split;
            skipState.out1 = nfa_.start;
            skipState.out2 = anyId;
            int skipId = nfa_.addState(skipState);
            nfa_.states[anyId].out1 = skipId;
            searchStart_ = skipId;
        }
    }

    bool fullMatch(const std::string& s) const { return run(nfa_.start, s, false); }

    bool search(const std::string& s) const { return run(searchStart_, s, true); }

private:
    Nfa nfa_;
    int searchStart_ = -1;
    bool anchoredStart_ = false;

    static bool classMatches(const State& s, char c) {
        bool found = false;
        for (const auto& [lo, hi] : s.ranges) {
            if (c >= lo && c <= hi) {
                found = true;
                break;
            }
        }
        return s.negate ? !found : found;
    }

    void addState(int id, std::vector<int>& list, std::vector<char>& visited, std::size_t pos, std::size_t len) const {
        if (id < 0 || visited[static_cast<std::size_t>(id)]) {
            return;
        }
        visited[static_cast<std::size_t>(id)] = 1;
        const State& st = nfa_.states[static_cast<std::size_t>(id)];
        switch (st.kind) {
            case State::Kind::Split:
                addState(st.out1, list, visited, pos, len);
                addState(st.out2, list, visited, pos, len);
                return;
            case State::Kind::AssertStart:
                if (pos == 0) {
                    addState(st.out1, list, visited, pos, len);
                }
                return;
            case State::Kind::AssertEnd:
                if (pos == len) {
                    addState(st.out1, list, visited, pos, len);
                }
                return;
            default:
                list.push_back(id);
        }
    }

    bool containsMatch(const std::vector<int>& list) const {
        for (int id : list) {
            if (nfa_.states[static_cast<std::size_t>(id)].kind == State::Kind::Match) {
                return true;
            }
        }
        return false;
    }

    bool run(int startId, const std::string& s, bool stopEarly) const {
        std::size_t n = s.size();
        std::size_t stateCount = nfa_.states.size();
        std::vector<char> visited(stateCount, 0);
        std::vector<int> list;
        addState(startId, list, visited, 0, n);

        for (std::size_t pos = 0;; ++pos) {
            if (stopEarly && containsMatch(list)) {
                return true;
            }
            if (pos == n) {
                return containsMatch(list);
            }
            if (list.empty()) {
                return false;
            }
            char c = s[pos];
            std::vector<char> nextVisited(stateCount, 0);
            std::vector<int> nextList;
            for (int id : list) {
                const State& st = nfa_.states[static_cast<std::size_t>(id)];
                bool matches = false;
                if (st.kind == State::Kind::Char) {
                    matches = st.ch == c;
                } else if (st.kind == State::Kind::Any) {
                    matches = true;
                } else if (st.kind == State::Kind::Class) {
                    matches = classMatches(st, c);
                }
                if (matches) {
                    addState(st.out1, nextList, nextVisited, pos + 1, n);
                }
            }
            list = std::move(nextList);
        }
    }
};

}
