#include "lexer.hpp"

#include <cctype>
#include <unordered_map>

namespace {
const std::unordered_map<std::string, TokenType> kKeywords = {
    {"var", TokenType::Var}, {"if", TokenType::If}, {"else", TokenType::Else},
    {"while", TokenType::While}, {"fun", TokenType::Fun}, {"return", TokenType::Return},
    {"print", TokenType::Print}, {"true", TokenType::True}, {"false", TokenType::False},
    {"nil", TokenType::Nil}, {"and", TokenType::And}, {"or", TokenType::Or},
};
}

Lexer::Lexer(std::string source) : source_(std::move(source)) {}

bool Lexer::isAtEnd() const { return current_ >= source_.size(); }

char Lexer::advance() { return source_[current_++]; }

char Lexer::peek() const { return isAtEnd() ? '\0' : source_[current_]; }

char Lexer::peekNext() const {
    if (current_ + 1 >= source_.size()) return '\0';
    return source_[current_ + 1];
}

bool Lexer::match(char expected) {
    if (isAtEnd() || source_[current_] != expected) return false;
    current_++;
    return true;
}

Token Lexer::makeToken(TokenType type) const {
    return Token{type, source_.substr(start_, current_ - start_), line_};
}

Token Lexer::errorToken(const std::string& message) const {
    return Token{TokenType::Error, message, line_};
}

void Lexer::skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                line_++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

Token Lexer::scanString() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') line_++;
        advance();
    }
    if (isAtEnd()) return errorToken("Unterminated string.");
    advance();
    Token t = makeToken(TokenType::String);
    t.lexeme = source_.substr(start_ + 1, (current_ - start_) - 2);
    return t;
}

Token Lexer::scanNumber() {
    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    return makeToken(TokenType::Number);
}

Token Lexer::scanIdentifier() {
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
    std::string text = source_.substr(start_, current_ - start_);
    auto it = kKeywords.find(text);
    if (it != kKeywords.end()) return makeToken(it->second);
    return makeToken(TokenType::Identifier);
}

Token Lexer::scanToken() {
    skipWhitespace();
    start_ = current_;
    if (isAtEnd()) return makeToken(TokenType::Eof);

    char c = advance();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') return scanIdentifier();
    if (std::isdigit(static_cast<unsigned char>(c))) return scanNumber();

    switch (c) {
        case '(': return makeToken(TokenType::LParen);
        case ')': return makeToken(TokenType::RParen);
        case '{': return makeToken(TokenType::LBrace);
        case '}': return makeToken(TokenType::RBrace);
        case ',': return makeToken(TokenType::Comma);
        case ';': return makeToken(TokenType::Semicolon);
        case '+': return makeToken(TokenType::Plus);
        case '-': return makeToken(TokenType::Minus);
        case '*': return makeToken(TokenType::Star);
        case '/': return makeToken(TokenType::Slash);
        case '!': return makeToken(match('=') ? TokenType::BangEqual : TokenType::Bang);
        case '=': return makeToken(match('=') ? TokenType::EqualEqual : TokenType::Equal);
        case '<': return makeToken(match('=') ? TokenType::LessEqual : TokenType::Less);
        case '>': return makeToken(match('=') ? TokenType::GreaterEqual : TokenType::Greater);
        case '"': return scanString();
    }
    return errorToken("Unexpected character.");
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    for (;;) {
        Token t = scanToken();
        tokens.push_back(t);
        if (t.type == TokenType::Eof || t.type == TokenType::Error) break;
    }
    return tokens;
}
