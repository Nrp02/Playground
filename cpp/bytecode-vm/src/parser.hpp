#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lexer.hpp"

enum class ExprKind { Number, String, Bool, Nil, Variable, Assign, Binary, Unary, Logical, Call };
enum class StmtKind { Expr, Var, Print, Block, If, While, Function, Return };

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Expr {
    ExprKind kind;
    int line = 0;

    double number = 0;
    std::string str;
    bool boolean = false;
    std::string name;
    TokenType op = TokenType::Eof;
    ExprPtr left;
    ExprPtr right;
    std::vector<ExprPtr> args;
};

struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;

struct Stmt {
    StmtKind kind;
    int line = 0;

    ExprPtr expr;
    std::string name;
    std::vector<StmtPtr> body;
    StmtPtr thenBranch;
    StmtPtr elseBranch;
    std::vector<std::string> params;
};

class Parser {
public:
    Parser(std::vector<Token> tokens);
    std::vector<StmtPtr> parseProgram();

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;

    const Token& peek() const;
    const Token& previous() const;
    bool check(TokenType type) const;
    bool isAtEnd() const;
    const Token& advance();
    bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const std::string& message);
    [[noreturn]] void error(const std::string& message) const;

    StmtPtr declaration();
    StmtPtr varDeclaration();
    StmtPtr functionDeclaration();
    StmtPtr statement();
    StmtPtr printStatement();
    StmtPtr ifStatement();
    StmtPtr whileStatement();
    StmtPtr returnStatement();
    StmtPtr block();
    StmtPtr expressionStatement();

    ExprPtr expression();
    ExprPtr assignment();
    ExprPtr orExpr();
    ExprPtr andExpr();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr term();
    ExprPtr factor();
    ExprPtr unary();
    ExprPtr call();
    ExprPtr finishCall(ExprPtr callee);
    ExprPtr primary();
};
