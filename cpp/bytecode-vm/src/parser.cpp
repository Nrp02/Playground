#include "parser.hpp"

#include <stdexcept>

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& Parser::peek() const { return tokens_[pos_]; }
const Token& Parser::previous() const { return tokens_[pos_ - 1]; }
bool Parser::isAtEnd() const { return peek().type == TokenType::Eof; }
bool Parser::check(TokenType type) const { return !isAtEnd() && peek().type == type; }

const Token& Parser::advance() {
    if (!isAtEnd()) pos_++;
    return previous();
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (TokenType t : types) {
        if (check(t)) {
            advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    error(message);
}

void Parser::error(const std::string& message) const {
    throw std::runtime_error("Parse error at line " + std::to_string(peek().line) + ": " + message);
}

std::vector<StmtPtr> Parser::parseProgram() {
    std::vector<StmtPtr> stmts;
    while (!isAtEnd()) stmts.push_back(declaration());
    return stmts;
}

StmtPtr Parser::declaration() {
    if (match({TokenType::Var})) return varDeclaration();
    if (match({TokenType::Fun})) return functionDeclaration();
    return statement();
}

StmtPtr Parser::varDeclaration() {
    auto stmt = std::make_shared<Stmt>();
    stmt->kind = StmtKind::Var;
    stmt->line = previous().line;
    stmt->name = consume(TokenType::Identifier, "Expected variable name.").lexeme;
    consume(TokenType::Equal, "Expected '=' after variable name.");
    stmt->expr = expression();
    consume(TokenType::Semicolon, "Expected ';' after variable declaration.");
    return stmt;
}

StmtPtr Parser::functionDeclaration() {
    auto stmt = std::make_shared<Stmt>();
    stmt->kind = StmtKind::Function;
    stmt->line = previous().line;
    stmt->name = consume(TokenType::Identifier, "Expected function name.").lexeme;
    consume(TokenType::LParen, "Expected '(' after function name.");
    if (!check(TokenType::RParen)) {
        do {
            stmt->params.push_back(consume(TokenType::Identifier, "Expected parameter name.").lexeme);
        } while (match({TokenType::Comma}));
    }
    consume(TokenType::RParen, "Expected ')' after parameters.");
    consume(TokenType::LBrace, "Expected '{' before function body.");
    auto body = block();
    stmt->body = body->body;
    return stmt;
}

StmtPtr Parser::statement() {
    if (match({TokenType::Print})) return printStatement();
    if (match({TokenType::If})) return ifStatement();
    if (match({TokenType::While})) return whileStatement();
    if (match({TokenType::Return})) return returnStatement();
    if (match({TokenType::LBrace})) return block();
    return expressionStatement();
}

StmtPtr Parser::printStatement() {
    auto stmt = std::make_shared<Stmt>();
    stmt->kind = StmtKind::Print;
    stmt->line = previous().line;
    stmt->expr = expression();
    consume(TokenType::Semicolon, "Expected ';' after value.");
    return stmt;
}

StmtPtr Parser::ifStatement() {
    auto stmt = std::make_shared<Stmt>();
    stmt->kind = StmtKind::If;
    stmt->line = previous().line;
    consume(TokenType::LParen, "Expected '(' after 'if'.");
    stmt->expr = expression();
    consume(TokenType::RParen, "Expected ')' after condition.");
    stmt->thenBranch = statement();
    if (match({TokenType::Else})) stmt->elseBranch = statement();
    return stmt;
}

StmtPtr Parser::whileStatement() {
    auto stmt = std::make_shared<Stmt>();
    stmt->kind = StmtKind::While;
    stmt->line = previous().line;
    consume(TokenType::LParen, "Expected '(' after 'while'.");
    stmt->expr = expression();
    consume(TokenType::RParen, "Expected ')' after condition.");
    stmt->thenBranch = statement();
    return stmt;
}

StmtPtr Parser::returnStatement() {
    auto stmt = std::make_shared<Stmt>();
    stmt->kind = StmtKind::Return;
    stmt->line = previous().line;
    if (!check(TokenType::Semicolon)) stmt->expr = expression();
    consume(TokenType::Semicolon, "Expected ';' after return value.");
    return stmt;
}

StmtPtr Parser::block() {
    auto stmt = std::make_shared<Stmt>();
    stmt->kind = StmtKind::Block;
    stmt->line = previous().line;
    while (!check(TokenType::RBrace) && !isAtEnd()) stmt->body.push_back(declaration());
    consume(TokenType::RBrace, "Expected '}' after block.");
    return stmt;
}

StmtPtr Parser::expressionStatement() {
    auto stmt = std::make_shared<Stmt>();
    stmt->kind = StmtKind::Expr;
    stmt->expr = expression();
    stmt->line = stmt->expr->line;
    consume(TokenType::Semicolon, "Expected ';' after expression.");
    return stmt;
}

ExprPtr Parser::expression() { return assignment(); }

ExprPtr Parser::assignment() {
    ExprPtr expr = orExpr();
    if (match({TokenType::Equal})) {
        int line = previous().line;
        ExprPtr value = assignment();
        if (expr->kind == ExprKind::Variable) {
            auto assign = std::make_shared<Expr>();
            assign->kind = ExprKind::Assign;
            assign->line = line;
            assign->name = expr->name;
            assign->right = value;
            return assign;
        }
        error("Invalid assignment target.");
    }
    return expr;
}

ExprPtr Parser::orExpr() {
    ExprPtr expr = andExpr();
    while (match({TokenType::Or})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Logical;
        e->op = TokenType::Or;
        e->line = previous().line;
        e->left = expr;
        e->right = andExpr();
        expr = e;
    }
    return expr;
}

ExprPtr Parser::andExpr() {
    ExprPtr expr = equality();
    while (match({TokenType::And})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Logical;
        e->op = TokenType::And;
        e->line = previous().line;
        e->left = expr;
        e->right = equality();
        expr = e;
    }
    return expr;
}

ExprPtr Parser::equality() {
    ExprPtr expr = comparison();
    while (match({TokenType::EqualEqual, TokenType::BangEqual})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Binary;
        e->op = previous().type;
        e->line = previous().line;
        e->left = expr;
        e->right = comparison();
        expr = e;
    }
    return expr;
}

ExprPtr Parser::comparison() {
    ExprPtr expr = term();
    while (match({TokenType::Less, TokenType::LessEqual, TokenType::Greater, TokenType::GreaterEqual})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Binary;
        e->op = previous().type;
        e->line = previous().line;
        e->left = expr;
        e->right = term();
        expr = e;
    }
    return expr;
}

ExprPtr Parser::term() {
    ExprPtr expr = factor();
    while (match({TokenType::Plus, TokenType::Minus})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Binary;
        e->op = previous().type;
        e->line = previous().line;
        e->left = expr;
        e->right = factor();
        expr = e;
    }
    return expr;
}

ExprPtr Parser::factor() {
    ExprPtr expr = unary();
    while (match({TokenType::Star, TokenType::Slash})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Binary;
        e->op = previous().type;
        e->line = previous().line;
        e->left = expr;
        e->right = unary();
        expr = e;
    }
    return expr;
}

ExprPtr Parser::unary() {
    if (match({TokenType::Bang, TokenType::Minus})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Unary;
        e->op = previous().type;
        e->line = previous().line;
        e->right = unary();
        return e;
    }
    return call();
}

ExprPtr Parser::call() {
    ExprPtr expr = primary();
    while (match({TokenType::LParen})) expr = finishCall(expr);
    return expr;
}

ExprPtr Parser::finishCall(ExprPtr callee) {
    auto e = std::make_shared<Expr>();
    e->kind = ExprKind::Call;
    e->line = previous().line;
    e->left = callee;
    if (!check(TokenType::RParen)) {
        do {
            e->args.push_back(expression());
        } while (match({TokenType::Comma}));
    }
    consume(TokenType::RParen, "Expected ')' after arguments.");
    return e;
}

ExprPtr Parser::primary() {
    if (match({TokenType::False})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Bool;
        e->boolean = false;
        e->line = previous().line;
        return e;
    }
    if (match({TokenType::True})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Bool;
        e->boolean = true;
        e->line = previous().line;
        return e;
    }
    if (match({TokenType::Nil})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Nil;
        e->line = previous().line;
        return e;
    }
    if (match({TokenType::Number})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Number;
        e->number = std::stod(previous().lexeme);
        e->line = previous().line;
        return e;
    }
    if (match({TokenType::String})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::String;
        e->str = previous().lexeme;
        e->line = previous().line;
        return e;
    }
    if (match({TokenType::Identifier})) {
        auto e = std::make_shared<Expr>();
        e->kind = ExprKind::Variable;
        e->name = previous().lexeme;
        e->line = previous().line;
        return e;
    }
    if (match({TokenType::LParen})) {
        ExprPtr expr = expression();
        consume(TokenType::RParen, "Expected ')' after expression.");
        return expr;
    }
    error("Expected expression.");
}
