from __future__ import annotations

import parser as ast

ERROR = "error"
NUMERIC = {"int", "float"}


def is_numeric(t: str) -> bool:
    return t in NUMERIC


def assignable(target: str, value: str) -> bool:
    if ERROR in (target, value):
        return True
    if target == value:
        return True
    if target == "float" and value == "int":
        return True
    return False


class TypeChecker:
    def __init__(self):
        self.errors: list[str] = []
        self.scopes: list[dict[str, str]] = [{}]
        self.functions: dict[str, tuple[list[str], str]] = {}
        self.return_stack: list[str] = []

    def error(self, line: int, message: str) -> None:
        self.errors.append(f"Line {line}: {message}")

    def push_scope(self) -> None:
        self.scopes.append({})

    def pop_scope(self) -> None:
        self.scopes.pop()

    def declare(self, name: str, type_name: str, line: int) -> None:
        if name in self.scopes[-1]:
            self.error(line, f"variable '{name}' already declared in this scope")
            return
        self.scopes[-1][name] = type_name

    def lookup(self, name: str, line: int) -> str:
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        self.error(line, f"undefined variable '{name}'")
        return ERROR

    def check(self, program: list[ast.Node]) -> list[str]:
        for stmt in program:
            if isinstance(stmt, ast.FunDecl):
                if stmt.name in self.functions:
                    self.error(stmt.line, f"function '{stmt.name}' already defined")
                    continue
                self.functions[stmt.name] = ([p.type_name for p in stmt.params], stmt.return_type)

        for stmt in program:
            self.check_stmt(stmt)
        return self.errors

    def check_stmt(self, stmt: ast.Node) -> None:
        if isinstance(stmt, ast.VarDecl):
            value_t = self.check_expr(stmt.value)
            if not assignable(stmt.type_name, value_t):
                self.error(stmt.line, f"cannot assign {value_t} to variable '{stmt.name}' of type {stmt.type_name}")
            self.declare(stmt.name, stmt.type_name, stmt.line)

        elif isinstance(stmt, ast.FunDecl):
            self.push_scope()
            for param in stmt.params:
                self.declare(param.name, param.type_name, stmt.line)
            self.return_stack.append(stmt.return_type)
            for s in stmt.body:
                self.check_stmt(s)
            self.return_stack.pop()
            self.pop_scope()

        elif isinstance(stmt, ast.Print):
            self.check_expr(stmt.expr)

        elif isinstance(stmt, ast.Block):
            self.push_scope()
            for s in stmt.statements:
                self.check_stmt(s)
            self.pop_scope()

        elif isinstance(stmt, ast.If):
            cond_t = self.check_expr(stmt.cond)
            if cond_t not in ("bool", ERROR):
                self.error(stmt.line, f"if condition must be bool, got {cond_t}")
            self.check_stmt(stmt.then_branch)
            if stmt.else_branch is not None:
                self.check_stmt(stmt.else_branch)

        elif isinstance(stmt, ast.While):
            cond_t = self.check_expr(stmt.cond)
            if cond_t not in ("bool", ERROR):
                self.error(stmt.line, f"while condition must be bool, got {cond_t}")
            self.check_stmt(stmt.body)

        elif isinstance(stmt, ast.Return):
            expected = self.return_stack[-1] if self.return_stack else "void"
            if stmt.value is None:
                if expected != "void":
                    self.error(stmt.line, f"missing return value, expected {expected}")
                return
            value_t = self.check_expr(stmt.value)
            if expected == "void":
                self.error(stmt.line, "void function cannot return a value")
            elif not assignable(expected, value_t):
                self.error(stmt.line, f"cannot return {value_t}, expected {expected}")

        elif isinstance(stmt, ast.ExprStmt):
            self.check_expr(stmt.expr)

        else:
            raise TypeError(f"unhandled statement node: {type(stmt).__name__}")

    def check_expr(self, expr: ast.Node) -> str:
        if isinstance(expr, ast.NumberLit):
            return "float" if expr.is_float else "int"

        if isinstance(expr, ast.StringLit):
            return "string"

        if isinstance(expr, ast.BoolLit):
            return "bool"

        if isinstance(expr, ast.Variable):
            return self.lookup(expr.name, expr.line)

        if isinstance(expr, ast.Assign):
            value_t = self.check_expr(expr.value)
            target_t = self.lookup(expr.name, expr.line)
            if not assignable(target_t, value_t):
                self.error(expr.line, f"cannot assign {value_t} to variable '{expr.name}' of type {target_t}")
            return target_t

        if isinstance(expr, ast.Binary):
            return self._check_binary(expr)

        if isinstance(expr, ast.Logical):
            left_t = self.check_expr(expr.left)
            right_t = self.check_expr(expr.right)
            if left_t not in ("bool", ERROR) or right_t not in ("bool", ERROR):
                self.error(expr.line, f"'{expr.op}' requires bool operands, got {left_t} and {right_t}")
            return "bool"

        if isinstance(expr, ast.Unary):
            right_t = self.check_expr(expr.right)
            if expr.op == "-":
                if not is_numeric(right_t) and right_t != ERROR:
                    self.error(expr.line, f"unary '-' requires numeric operand, got {right_t}")
                    return ERROR
                return right_t
            if expr.op == "not":
                if right_t not in ("bool", ERROR):
                    self.error(expr.line, f"'not' requires bool operand, got {right_t}")
                return "bool"
            raise TypeError(f"unhandled unary op: {expr.op}")

        if isinstance(expr, ast.Call):
            return self._check_call(expr)

        raise TypeError(f"unhandled expression node: {type(expr).__name__}")

    def _check_binary(self, expr: ast.Binary) -> str:
        left_t = self.check_expr(expr.left)
        right_t = self.check_expr(expr.right)

        if expr.op == "+":
            if left_t == "string" and right_t == "string":
                return "string"
            if is_numeric(left_t) and is_numeric(right_t):
                return "float" if "float" in (left_t, right_t) else "int"
            if ERROR in (left_t, right_t):
                return ERROR
            self.error(expr.line, f"'+' requires two numbers or two strings, got {left_t} and {right_t}")
            return ERROR

        if expr.op in ("-", "*", "/"):
            if is_numeric(left_t) and is_numeric(right_t):
                return "float" if "float" in (left_t, right_t) else "int"
            if ERROR in (left_t, right_t):
                return ERROR
            self.error(expr.line, f"'{expr.op}' requires numeric operands, got {left_t} and {right_t}")
            return ERROR

        if expr.op in ("<", "<=", ">", ">="):
            if is_numeric(left_t) and is_numeric(right_t):
                return "bool"
            if ERROR in (left_t, right_t):
                return "bool"
            self.error(expr.line, f"'{expr.op}' requires numeric operands, got {left_t} and {right_t}")
            return "bool"

        if expr.op in ("==", "!="):
            if left_t == right_t or ERROR in (left_t, right_t) or (is_numeric(left_t) and is_numeric(right_t)):
                return "bool"
            self.error(expr.line, f"cannot compare {left_t} and {right_t}")
            return "bool"

        raise TypeError(f"unhandled binary op: {expr.op}")

    def _check_call(self, expr: ast.Call) -> str:
        if expr.callee not in self.functions:
            self.error(expr.line, f"undefined function '{expr.callee}'")
            for arg in expr.args:
                self.check_expr(arg)
            return ERROR

        param_types, return_type = self.functions[expr.callee]
        if len(param_types) != len(expr.args):
            self.error(
                expr.line,
                f"function '{expr.callee}' expects {len(param_types)} argument(s), got {len(expr.args)}",
            )
            for arg in expr.args:
                self.check_expr(arg)
            return return_type

        for arg, expected in zip(expr.args, param_types):
            arg_t = self.check_expr(arg)
            if not assignable(expected, arg_t):
                self.error(arg.line, f"argument to '{expr.callee}' expects {expected}, got {arg_t}")

        return return_type
