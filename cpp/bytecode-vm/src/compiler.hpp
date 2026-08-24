#pragma once

#include <memory>
#include <vector>

#include "parser.hpp"
#include "vm.hpp"

class Compiler {
public:
    std::shared_ptr<ObjFunction> compile(const std::vector<StmtPtr>& program);

private:
    struct Local {
        std::string name;
        int depth;
    };

    struct CompilerState {
        std::shared_ptr<ObjFunction> function;
        std::vector<Local> locals;
        int scopeDepth = 0;
    };

    std::vector<CompilerState> states_;

    CompilerState& current();
    Chunk& chunk();

    void compileStmt(const StmtPtr& stmt);
    void compileExpr(const ExprPtr& expr);
    void beginScope();
    void endScope(int line);
    void declareLocal(const std::string& name);
    int resolveLocal(const std::string& name);
    size_t emitJump(OpCode op, int line);
    void patchJump(size_t offset);
    void emitLoop(size_t loopStart, int line);
    void compileFunction(const std::string& name, const std::vector<std::string>& params,
                          const std::vector<StmtPtr>& body, int line);
    size_t identifierConstant(const std::string& name);
};
