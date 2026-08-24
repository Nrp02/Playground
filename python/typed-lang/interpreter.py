from __future__ import annotations

import parser as ast


class ReturnSignal(Exception):
    def __init__(self, value):
        self.value = value


class Environment:
    def __init__(self, parent: "Environment | None" = None):
        self.parent = parent
        self.values: dict[str, object] = {}

    def define(self, name: str, value: object) -> None:
        self.values[name] = value

    def get(self, name: str) -> object:
        env: Environment | None = self
        while env is not None:
            if name in env.values:
                return env.values[name]
            env = env.parent
        raise RuntimeError(f"undefined variable '{name}'")

    def set(self, name: str, value: object) -> None:
        env: Environment | None = self
        while env is not None:
            if name in env.values:
                env.values[name] = value
                return
            env = env.parent
        raise RuntimeError(f"undefined variable '{name}'")


def coerce(value: object, type_name: str) -> object:
    if type_name == "float" and isinstance(value, int) and not isinstance(value, bool):
        return float(value)
    return value


def stringify(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float) and value.is_integer():
        return str(value)
    return str(value)


class Interpreter:
    def __init__(self, functions: dict[str, ast.FunDecl]):
        self.functions = functions
        self.globals = Environment()

    def run(self, program: list[ast.Node]) -> None:
        for stmt in program:
            if not isinstance(stmt, ast.FunDecl):
                self.exec_stmt(stmt, self.globals)

    def exec_stmt(self, stmt: ast.Node, env: Environment) -> None:
        if isinstance(stmt, ast.VarDecl):
            env.define(stmt.name, coerce(self.eval_expr(stmt.value, env), stmt.type_name))

        elif isinstance(stmt, ast.FunDecl):
            return

        elif isinstance(stmt, ast.Print):
            print(stringify(self.eval_expr(stmt.expr, env)))

        elif isinstance(stmt, ast.Block):
            inner = Environment(env)
            for s in stmt.statements:
                self.exec_stmt(s, inner)

        elif isinstance(stmt, ast.If):
            if self.eval_expr(stmt.cond, env):
                self.exec_stmt(stmt.then_branch, env)
            elif stmt.else_branch is not None:
                self.exec_stmt(stmt.else_branch, env)

        elif isinstance(stmt, ast.While):
            while self.eval_expr(stmt.cond, env):
                self.exec_stmt(stmt.body, env)

        elif isinstance(stmt, ast.Return):
            value = self.eval_expr(stmt.value, env) if stmt.value is not None else None
            raise ReturnSignal(value)

        elif isinstance(stmt, ast.ExprStmt):
            self.eval_expr(stmt.expr, env)

        else:
            raise RuntimeError(f"unhandled statement node: {type(stmt).__name__}")

    def eval_expr(self, expr: ast.Node, env: Environment) -> object:
        if isinstance(expr, ast.NumberLit):
            return expr.value

        if isinstance(expr, ast.StringLit):
            return expr.value

        if isinstance(expr, ast.BoolLit):
            return expr.value

        if isinstance(expr, ast.Variable):
            return env.get(expr.name)

        if isinstance(expr, ast.Assign):
            value = self.eval_expr(expr.value, env)
            env.set(expr.name, value)
            return value

        if isinstance(expr, ast.Binary):
            return self._eval_binary(expr, env)

        if isinstance(expr, ast.Logical):
            left = self.eval_expr(expr.left, env)
            if expr.op == "and":
                return left if not left else self.eval_expr(expr.right, env)
            return left if left else self.eval_expr(expr.right, env)

        if isinstance(expr, ast.Unary):
            right = self.eval_expr(expr.right, env)
            if expr.op == "-":
                return -right
            return not right

        if isinstance(expr, ast.Call):
            return self._eval_call(expr, env)

        raise RuntimeError(f"unhandled expression node: {type(expr).__name__}")

    def _eval_binary(self, expr: ast.Binary, env: Environment) -> object:
        left = self.eval_expr(expr.left, env)
        right = self.eval_expr(expr.right, env)

        if expr.op == "+":
            return left + right
        if expr.op == "-":
            return left - right
        if expr.op == "*":
            return left * right
        if expr.op == "/":
            return left / right
        if expr.op == "<":
            return left < right
        if expr.op == "<=":
            return left <= right
        if expr.op == ">":
            return left > right
        if expr.op == ">=":
            return left >= right
        if expr.op == "==":
            return left == right
        if expr.op == "!=":
            return left != right
        raise RuntimeError(f"unhandled binary op: {expr.op}")

    def _eval_call(self, expr: ast.Call, env: Environment) -> object:
        func = self.functions[expr.callee]
        call_env = Environment(self.globals)
        args = [self.eval_expr(a, env) for a in expr.args]
        for param, value in zip(func.params, args):
            call_env.define(param.name, coerce(value, param.type_name))
        try:
            for stmt in func.body:
                self.exec_stmt(stmt, call_env)
        except ReturnSignal as ret:
            return coerce(ret.value, func.return_type)
        return None
