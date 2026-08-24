#include "compiler.hpp"

#include <stdexcept>

Compiler::CompilerState& Compiler::current() { return states_.back(); }
Chunk& Compiler::chunk() { return current().function->chunk; }

size_t Compiler::identifierConstant(const std::string& name) {
    return chunk().addConstant(Value::string_(name));
}

void Compiler::beginScope() { current().scopeDepth++; }

void Compiler::endScope(int line) {
    current().scopeDepth--;
    auto& locals = current().locals;
    while (!locals.empty() && locals.back().depth > current().scopeDepth) {
        chunk().writeOp(OpCode::Pop, line);
        locals.pop_back();
    }
}

void Compiler::declareLocal(const std::string& name) {
    current().locals.push_back(Local{name, current().scopeDepth});
}

int Compiler::resolveLocal(const std::string& name) {
    auto& locals = current().locals;
    for (int i = static_cast<int>(locals.size()) - 1; i >= 0; i--) {
        if (locals[i].name == name) return i;
    }
    return -1;
}

size_t Compiler::emitJump(OpCode op, int line) {
    chunk().writeOp(op, line);
    chunk().writeByte(0xff, line);
    chunk().writeByte(0xff, line);
    return chunk().code.size() - 2;
}

void Compiler::patchJump(size_t offset) {
    size_t jump = chunk().code.size() - offset - 2;
    if (jump > 0xffff) throw std::runtime_error("Jump target too far.");
    chunk().code[offset] = static_cast<uint8_t>((jump >> 8) & 0xff);
    chunk().code[offset + 1] = static_cast<uint8_t>(jump & 0xff);
}

void Compiler::emitLoop(size_t loopStart, int line) {
    chunk().writeOp(OpCode::Loop, line);
    size_t offset = chunk().code.size() - loopStart + 2;
    if (offset > 0xffff) throw std::runtime_error("Loop body too large.");
    chunk().writeByte(static_cast<uint8_t>((offset >> 8) & 0xff), line);
    chunk().writeByte(static_cast<uint8_t>(offset & 0xff), line);
}

std::shared_ptr<ObjFunction> Compiler::compile(const std::vector<StmtPtr>& program) {
    auto script = std::make_shared<ObjFunction>();
    script->name = "<script>";
    script->arity = 0;

    CompilerState state;
    state.function = script;
    state.locals.push_back(Local{"", 0});
    states_.push_back(state);

    for (const auto& stmt : program) compileStmt(stmt);
    chunk().writeOp(OpCode::Nil, 0);
    chunk().writeOp(OpCode::Return, 0);

    states_.pop_back();
    return script;
}

void Compiler::compileFunction(const std::string& name, const std::vector<std::string>& params,
                                const std::vector<StmtPtr>& body, int line) {
    auto function = std::make_shared<ObjFunction>();
    function->name = name;
    function->arity = static_cast<int>(params.size());

    CompilerState state;
    state.function = function;
    states_.push_back(state);
    beginScope();
    current().locals.push_back(Local{"", current().scopeDepth});
    for (const auto& param : params) declareLocal(param);

    for (const auto& stmt : body) compileStmt(stmt);
    chunk().writeOp(OpCode::Nil, line);
    chunk().writeOp(OpCode::Return, line);

    states_.pop_back();

    chunk().writeOp(OpCode::Constant, line);
    chunk().writeByte(static_cast<uint8_t>(chunk().addConstant(Value::function_(function))), line);
}

void Compiler::compileStmt(const StmtPtr& stmt) {
    switch (stmt->kind) {
        case StmtKind::Expr:
            compileExpr(stmt->expr);
            chunk().writeOp(OpCode::Pop, stmt->line);
            break;
        case StmtKind::Var: {
            compileExpr(stmt->expr);
            if (current().scopeDepth > 0) {
                declareLocal(stmt->name);
            } else {
                size_t idx = identifierConstant(stmt->name);
                chunk().writeOp(OpCode::DefineGlobal, stmt->line);
                chunk().writeByte(static_cast<uint8_t>(idx), stmt->line);
            }
            break;
        }
        case StmtKind::Print:
            compileExpr(stmt->expr);
            chunk().writeOp(OpCode::Print, stmt->line);
            break;
        case StmtKind::Block:
            beginScope();
            for (const auto& s : stmt->body) compileStmt(s);
            endScope(stmt->line);
            break;
        case StmtKind::If: {
            compileExpr(stmt->expr);
            size_t thenJump = emitJump(OpCode::JumpIfFalse, stmt->line);
            chunk().writeOp(OpCode::Pop, stmt->line);
            compileStmt(stmt->thenBranch);
            size_t elseJump = emitJump(OpCode::Jump, stmt->line);
            patchJump(thenJump);
            chunk().writeOp(OpCode::Pop, stmt->line);
            if (stmt->elseBranch) compileStmt(stmt->elseBranch);
            patchJump(elseJump);
            break;
        }
        case StmtKind::While: {
            size_t loopStart = chunk().code.size();
            compileExpr(stmt->expr);
            size_t exitJump = emitJump(OpCode::JumpIfFalse, stmt->line);
            chunk().writeOp(OpCode::Pop, stmt->line);
            compileStmt(stmt->thenBranch);
            emitLoop(loopStart, stmt->line);
            patchJump(exitJump);
            chunk().writeOp(OpCode::Pop, stmt->line);
            break;
        }
        case StmtKind::Function: {
            compileFunction(stmt->name, stmt->params, stmt->body, stmt->line);
            size_t idx = identifierConstant(stmt->name);
            chunk().writeOp(OpCode::DefineGlobal, stmt->line);
            chunk().writeByte(static_cast<uint8_t>(idx), stmt->line);
            break;
        }
        case StmtKind::Return:
            if (stmt->expr) compileExpr(stmt->expr);
            else chunk().writeOp(OpCode::Nil, stmt->line);
            chunk().writeOp(OpCode::Return, stmt->line);
            break;
    }
}

void Compiler::compileExpr(const ExprPtr& expr) {
    switch (expr->kind) {
        case ExprKind::Number: {
            size_t idx = chunk().addConstant(Value::number_(expr->number));
            chunk().writeOp(OpCode::Constant, expr->line);
            chunk().writeByte(static_cast<uint8_t>(idx), expr->line);
            break;
        }
        case ExprKind::String: {
            size_t idx = chunk().addConstant(Value::string_(expr->str));
            chunk().writeOp(OpCode::Constant, expr->line);
            chunk().writeByte(static_cast<uint8_t>(idx), expr->line);
            break;
        }
        case ExprKind::Bool:
            chunk().writeOp(expr->boolean ? OpCode::True : OpCode::False, expr->line);
            break;
        case ExprKind::Nil:
            chunk().writeOp(OpCode::Nil, expr->line);
            break;
        case ExprKind::Variable: {
            int slot = resolveLocal(expr->name);
            if (slot != -1) {
                chunk().writeOp(OpCode::GetLocal, expr->line);
                chunk().writeByte(static_cast<uint8_t>(slot), expr->line);
            } else {
                size_t idx = identifierConstant(expr->name);
                chunk().writeOp(OpCode::GetGlobal, expr->line);
                chunk().writeByte(static_cast<uint8_t>(idx), expr->line);
            }
            break;
        }
        case ExprKind::Assign: {
            compileExpr(expr->right);
            int slot = resolveLocal(expr->name);
            if (slot != -1) {
                chunk().writeOp(OpCode::SetLocal, expr->line);
                chunk().writeByte(static_cast<uint8_t>(slot), expr->line);
            } else {
                size_t idx = identifierConstant(expr->name);
                chunk().writeOp(OpCode::SetGlobal, expr->line);
                chunk().writeByte(static_cast<uint8_t>(idx), expr->line);
            }
            break;
        }
        case ExprKind::Binary: {
            compileExpr(expr->left);
            compileExpr(expr->right);
            switch (expr->op) {
                case TokenType::Plus: chunk().writeOp(OpCode::Add, expr->line); break;
                case TokenType::Minus: chunk().writeOp(OpCode::Sub, expr->line); break;
                case TokenType::Star: chunk().writeOp(OpCode::Mul, expr->line); break;
                case TokenType::Slash: chunk().writeOp(OpCode::Div, expr->line); break;
                case TokenType::EqualEqual: chunk().writeOp(OpCode::Equal, expr->line); break;
                case TokenType::BangEqual:
                    chunk().writeOp(OpCode::Equal, expr->line);
                    chunk().writeOp(OpCode::Not, expr->line);
                    break;
                case TokenType::Less: chunk().writeOp(OpCode::Less, expr->line); break;
                case TokenType::Greater: chunk().writeOp(OpCode::Greater, expr->line); break;
                case TokenType::LessEqual:
                    chunk().writeOp(OpCode::Greater, expr->line);
                    chunk().writeOp(OpCode::Not, expr->line);
                    break;
                case TokenType::GreaterEqual:
                    chunk().writeOp(OpCode::Less, expr->line);
                    chunk().writeOp(OpCode::Not, expr->line);
                    break;
                default: throw std::runtime_error("Unknown binary operator.");
            }
            break;
        }
        case ExprKind::Unary: {
            compileExpr(expr->right);
            if (expr->op == TokenType::Minus) chunk().writeOp(OpCode::Negate, expr->line);
            else if (expr->op == TokenType::Bang) chunk().writeOp(OpCode::Not, expr->line);
            break;
        }
        case ExprKind::Logical: {
            if (expr->op == TokenType::And) {
                compileExpr(expr->left);
                size_t endJump = emitJump(OpCode::JumpIfFalse, expr->line);
                chunk().writeOp(OpCode::Pop, expr->line);
                compileExpr(expr->right);
                patchJump(endJump);
            } else {
                compileExpr(expr->left);
                size_t elseJump = emitJump(OpCode::JumpIfFalse, expr->line);
                size_t endJump = emitJump(OpCode::Jump, expr->line);
                patchJump(elseJump);
                chunk().writeOp(OpCode::Pop, expr->line);
                compileExpr(expr->right);
                patchJump(endJump);
            }
            break;
        }
        case ExprKind::Call: {
            compileExpr(expr->left);
            for (const auto& arg : expr->args) compileExpr(arg);
            chunk().writeOp(OpCode::Call, expr->line);
            chunk().writeByte(static_cast<uint8_t>(expr->args.size()), expr->line);
            break;
        }
    }
}
