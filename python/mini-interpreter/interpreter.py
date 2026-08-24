"""
Tree-walking interpreter for the "mini" scripting language.

Evaluates the AST produced by parser.py directly (no bytecode compilation
step). Variable storage is modeled with a chain of Environment objects so
that `{ ... }` blocks (if/while bodies) get their own lexical scope for
`let` declarations, while plain assignment (`x = ...`) mutates whichever
enclosing scope actually owns the variable -- the same model JavaScript's
`let` uses relative to block scope.
"""

from __future__ import annotations

from typing import Optional

import parser as ast


class InterpreterError(Exception):
    def __init__(self, message: str, line: Optional[int] = None, col: Optional[int] = None):
        location = f" at {line}:{col}" if line is not None else ""
        super().__init__(f"Runtime error{location}: {message}")
        self.message = message
        self.line = line
        self.col = col


class Environment:
    """A single lexical scope, chained to its enclosing (parent) scope."""

    __slots__ = ("values", "parent")

    def __init__(self, parent: Optional["Environment"] = None):
        self.values: dict[str, object] = {}
        self.parent = parent

    def define(self, name: str, value: object) -> None:
        """Bind `name` in *this* scope (what `let` does)."""
        self.values[name] = value

    def get(self, name: str) -> object:
        env: Optional[Environment] = self
        while env is not None:
            if name in env.values:
                return env.values[name]
            env = env.parent
        raise InterpreterError(f"undefined variable '{name}'")

    def assign(self, name: str, value: object) -> None:
        """Mutate the nearest enclosing scope that already owns `name`."""
        env: Optional[Environment] = self
        while env is not None:
            if name in env.values:
                env.values[name] = value
                return
            env = env.parent
        raise InterpreterError(f"cannot assign to undefined variable '{name}' (declare it first with 'let')")


# ---------------------------------------------------------------------------
# Runtime value helpers
#
# Runtime values are plain Python objects: bool, int, float, str. Booleans
# are kept distinct from numbers even though Python's `bool` is technically
# an `int` subclass, since the mini-language treats them as separate types.
# ---------------------------------------------------------------------------

def is_number(value: object) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def is_truthy(value: object) -> bool:
    if isinstance(value, bool):
        return value
    if is_number(value):
        return value != 0
    if isinstance(value, str):
        return len(value) > 0
    return bool(value)


def values_equal(a: object, b: object) -> bool:
    if isinstance(a, bool) or isinstance(b, bool):
        return isinstance(a, bool) and isinstance(b, bool) and a == b
    if is_number(a) and is_number(b):
        return a == b
    if isinstance(a, str) and isinstance(b, str):
        return a == b
    return False


def type_name(value: object) -> str:
    if isinstance(value, bool):
        return "bool"
    if is_number(value):
        return "number"
    if isinstance(value, str):
        return "string"
    return type(value).__name__


def stringify(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        if value.is_integer():
            return str(int(value))
        return str(value)
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return value
    return str(value)


class Interpreter:
    """Executes a parsed Program against a fresh global environment."""

    def __init__(self):
        self.globals = Environment()

    def run(self, program: ast.Program) -> None:
        env = self.globals
        for stmt in program.statements:
            self._exec(stmt, env)

    # -- statement dispatch -----------------------------------------------------

    def _exec(self, stmt, env: Environment) -> None:
        kind = type(stmt).__name__
        handler = getattr(self, f"_exec_{kind}", None)
        if handler is None:
            raise InterpreterError(f"no handler for statement type {kind}", stmt.line, stmt.col)
        handler(stmt, env)

    def _exec_LetStmt(self, stmt: ast.LetStmt, env: Environment) -> None:
        value = self._eval(stmt.expr, env)
        env.define(stmt.name, value)

    def _exec_AssignStmt(self, stmt: ast.AssignStmt, env: Environment) -> None:
        value = self._eval(stmt.expr, env)
        try:
            env.assign(stmt.name, value)
        except InterpreterError as exc:
            raise InterpreterError(exc.message, stmt.line, stmt.col) from None

    def _exec_PrintStmt(self, stmt: ast.PrintStmt, env: Environment) -> None:
        value = self._eval(stmt.expr, env)
        print(stringify(value))

    def _exec_Block(self, stmt: ast.Block, env: Environment) -> None:
        inner = Environment(parent=env)
        for s in stmt.statements:
            self._exec(s, inner)

    def _exec_IfStmt(self, stmt: ast.IfStmt, env: Environment) -> None:
        if is_truthy(self._eval(stmt.cond, env)):
            self._exec(stmt.then_block, env)
        elif stmt.else_block is not None:
            self._exec(stmt.else_block, env)

    def _exec_WhileStmt(self, stmt: ast.WhileStmt, env: Environment) -> None:
        while is_truthy(self._eval(stmt.cond, env)):
            self._exec(stmt.body, env)

    # -- expression evaluation --------------------------------------------------

    def _eval(self, node, env: Environment):
        kind = type(node).__name__
        handler = getattr(self, f"_eval_{kind}", None)
        if handler is None:
            raise InterpreterError(f"no handler for expression type {kind}", node.line, node.col)
        return handler(node, env)

    def _eval_NumberLit(self, node: ast.NumberLit, env: Environment):
        return node.value

    def _eval_StringLit(self, node: ast.StringLit, env: Environment):
        return node.value

    def _eval_BoolLit(self, node: ast.BoolLit, env: Environment):
        return node.value

    def _eval_Var(self, node: ast.Var, env: Environment):
        try:
            return env.get(node.name)
        except InterpreterError as exc:
            raise InterpreterError(exc.message, node.line, node.col) from None

    def _eval_UnaryOp(self, node: ast.UnaryOp, env: Environment):
        value = self._eval(node.operand, env)
        if node.op == "-":
            if not is_number(value):
                raise InterpreterError(f"unary '-' requires a number, got {type_name(value)}", node.line, node.col)
            return -value
        if node.op == "not":
            return not is_truthy(value)
        raise InterpreterError(f"unknown unary operator '{node.op}'", node.line, node.col)

    def _eval_BinOp(self, node: ast.BinOp, env: Environment):
        op = node.op

        # Short-circuit logical operators evaluate the right side lazily.
        if op == "and":
            left = self._eval(node.left, env)
            return self._eval(node.right, env) if is_truthy(left) else left
        if op == "or":
            left = self._eval(node.left, env)
            return left if is_truthy(left) else self._eval(node.right, env)

        left = self._eval(node.left, env)
        right = self._eval(node.right, env)

        if op == "+":
            if is_number(left) and is_number(right):
                return left + right
            if isinstance(left, str) or isinstance(right, str):
                return stringify(left) + stringify(right)
            raise InterpreterError(
                f"unsupported operand types for '+': {type_name(left)} and {type_name(right)}",
                node.line, node.col,
            )

        if op in ("-", "*", "/", "%"):
            if not (is_number(left) and is_number(right)):
                raise InterpreterError(
                    f"unsupported operand types for '{op}': {type_name(left)} and {type_name(right)}",
                    node.line, node.col,
                )
            if op == "-":
                return left - right
            if op == "*":
                return left * right
            if op == "/":
                if right == 0:
                    raise InterpreterError("division by zero", node.line, node.col)
                if isinstance(left, int) and isinstance(right, int) and left % right == 0:
                    return left // right
                return left / right
            if op == "%":
                if right == 0:
                    raise InterpreterError("modulo by zero", node.line, node.col)
                return left % right

        if op in ("<", "<=", ">", ">="):
            if not (is_number(left) and is_number(right)):
                raise InterpreterError(
                    f"comparison '{op}' requires numbers, got {type_name(left)} and {type_name(right)}",
                    node.line, node.col,
                )
            if op == "<":
                return left < right
            if op == "<=":
                return left <= right
            if op == ">":
                return left > right
            if op == ">=":
                return left >= right

        if op == "==":
            return values_equal(left, right)
        if op == "!=":
            return not values_equal(left, right)

        raise InterpreterError(f"unknown binary operator '{op}'", node.line, node.col)


def run_source(source: str) -> None:
    """Convenience helper: lex + parse + run a source string in one call."""
    program = ast.parse_source(source)
    Interpreter().run(program)
