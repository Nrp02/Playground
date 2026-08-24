#pragma once

#include <string>
#include <vector>

enum class TokenType {
    Number, String, Identifier,
    Plus, Minus, Star, Slash,
    Equal, EqualEqual, BangEqual, Bang,
    Less, LessEqual, Greater, GreaterEqual,
    And, Or,
    LParen, RParen, LBrace, RBrace,
    Comma, Semicolon,
    Var, If, Else, While, Fun, Return, Print, True, False, Nil,
    Eof, Error
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
};

class Lexer {
public:
    explicit Lexer(std::string source);
    std::vector<Token> tokenize();

private:
    std::string source_;
    size_t start_ = 0;
    size_t current_ = 0;
    int line_ = 1;

    bool isAtEnd() const;
    char advance();
    char peek() const;
    char peekNext() const;
    bool match(char expected);
    Token makeToken(TokenType type) const;
    Token errorToken(const std::string& message) const;
    void skipWhitespace();
    Token scanToken();
    Token scanString();
    Token scanNumber();
    Token scanIdentifier();
};
