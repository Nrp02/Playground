"""
Recursive-descent parser for the "mini" scripting language.

Consumes the token stream produced by lexer.Lexer and builds an abstract
syntax tree (AST) out of small dataclasses. Expression parsing uses the
standard precedence-climbing technique so that operator precedence
(`or` < `and` < equality < comparison < `+ -` < `* / %` < unary) and
parenthesized grouping come out correct without an explicit precedence
table.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Union

from lexer import Lexer, Token, TokenType


# ---------------------------------------------------------------------------
# AST node definitions
# ---------------------------------------------------------------------------

@dataclass
class Node:
    line: int
    col: int


# -- Expressions --------------------------------------------------------

@dataclass
class NumberLit(Node):
    value: Union[int, float]


@dataclass
class StringLit(Node):
    value: str


@dataclass
class BoolLit(Node):
    value: bool


@dataclass
class Var(Node):
    name: str


@dataclass
class UnaryOp(Node):
    op: str
    operand: "Expr"


@dataclass
class BinOp(Node):
    op: str
    left: "Expr"
    right: "Expr"


Expr = Union[NumberLit, StringLit, BoolLit, Var, UnaryOp, BinOp]


# -- Statements -----------------------------------------------------------

@dataclass
class LetStmt(Node):
    name: str
    expr: Expr


@dataclass
class AssignStmt(Node):
    name: str
    expr: Expr


@dataclass
class PrintStmt(Node):
    expr: Expr


@dataclass
class Block(Node):
    statements: list


@dataclass
class IfStmt(Node):
    cond: Expr
    then_block: Block
    else_block: Optional[object]  # Block | IfStmt | None


@dataclass
class WhileStmt(Node):
    cond: Expr
    body: Block


@dataclass
class Program(Node):
    statements: list


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

class ParseError(Exception):
    def __init__(self, message: str, line: int, col: int):
        super().__init__(f"Parse error at {line}:{col}: {message}")
        self.message = message
        self.line = line
        self.col = col


_COMPARISON_TYPES = (TokenType.LT, TokenType.LTE, TokenType.GT, TokenType.GTE)
_EQUALITY_TYPES = (TokenType.EQ, TokenType.NEQ)
_TERM_TYPES = (TokenType.PLUS, TokenType.MINUS)
_FACTOR_TYPES = (TokenType.STAR, TokenType.SLASH, TokenType.PERCENT)


class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.pos = 0

    # -- token stream helpers ------------------------------------------------

    def _peek(self, offset: int = 0) -> Token:
        idx = min(self.pos + offset, len(self.tokens) - 1)
        return self.tokens[idx]

    def _advance(self) -> Token:
        tok = self.tokens[self.pos]
        if self.pos < len(self.tokens) - 1:
            self.pos += 1
        return tok

    def _check(self, type_: TokenType) -> bool:
        return self._peek().type == type_

    def _match(self, *types: TokenType) -> bool:
        if self._peek().type in types:
            self._advance()
            return True
        return False

    def _expect(self, type_: TokenType, message: Optional[str] = None) -> Token:
        if self._check(type_):
            return self._advance()
        tok = self._peek()
        default = f"expected {type_.name} but found {tok.type.name}"
        raise ParseError(message or default, tok.line, tok.col)

    # -- entry point ----------------------------------------------------------

    def parse(self) -> Program:
        start = self._peek()
        statements = []
        while not self._check(TokenType.EOF):
            statements.append(self._statement())
        return Program(line=start.line, col=start.col, statements=statements)

    # -- statements -------------------------------------------------------------

    def _statement(self):
        tok = self._peek()

        if tok.type == TokenType.LET:
            return self._let_stmt()
        if tok.type == TokenType.PRINT:
            return self._print_stmt()
        if tok.type == TokenType.IF:
            return self._if_stmt()
        if tok.type == TokenType.WHILE:
            return self._while_stmt()
        if tok.type == TokenType.LBRACE:
            return self._block()
        if tok.type == TokenType.IDENT and self._peek(1).type == TokenType.ASSIGN:
            return self._assign_stmt()

        raise ParseError(
            f"unexpected token {tok.type.name} ({tok.value!r}); expected a statement",
            tok.line,
            tok.col,
        )

    def _let_stmt(self) -> LetStmt:
        kw = self._advance()  # 'let'
        name_tok = self._expect(TokenType.IDENT, "expected a variable name after 'let'")
        self._expect(TokenType.ASSIGN, "expected '=' after variable name in let statement")
        expr = self._expression()
        self._expect(TokenType.SEMI, "expected ';' after let statement")
        return LetStmt(line=kw.line, col=kw.col, name=name_tok.value, expr=expr)

    def _assign_stmt(self) -> AssignStmt:
        name_tok = self._advance()  # IDENT
        self._expect(TokenType.ASSIGN, "expected '=' in assignment")
        expr = self._expression()
        self._expect(TokenType.SEMI, "expected ';' after assignment")
        return AssignStmt(line=name_tok.line, col=name_tok.col, name=name_tok.value, expr=expr)

    def _print_stmt(self) -> PrintStmt:
        kw = self._advance()  # 'print'
        expr = self._expression()
        self._expect(TokenType.SEMI, "expected ';' after print statement")
        return PrintStmt(line=kw.line, col=kw.col, expr=expr)

    def _block(self) -> Block:
        brace = self._expect(TokenType.LBRACE, "expected '{' to start a block")
        statements = []
        while not self._check(TokenType.RBRACE) and not self._check(TokenType.EOF):
            statements.append(self._statement())
        self._expect(TokenType.RBRACE, "expected '}' to close block")
        return Block(line=brace.line, col=brace.col, statements=statements)

    def _if_stmt(self) -> IfStmt:
        kw = self._advance()  # 'if'
        self._expect(TokenType.LPAREN, "expected '(' after 'if'")
        cond = self._expression()
        self._expect(TokenType.RPAREN, "expected ')' after if condition")
        then_block = self._block()

        else_block = None
        if self._match(TokenType.ELSE):
            if self._check(TokenType.IF):
                else_block = self._if_stmt()  # else-if chaining
            else:
                else_block = self._block()

        return IfStmt(line=kw.line, col=kw.col, cond=cond, then_block=then_block, else_block=else_block)

    def _while_stmt(self) -> WhileStmt:
        kw = self._advance()  # 'while'
        self._expect(TokenType.LPAREN, "expected '(' after 'while'")
        cond = self._expression()
        self._expect(TokenType.RPAREN, "expected ')' after while condition")
        body = self._block()
        return WhileStmt(line=kw.line, col=kw.col, cond=cond, body=body)

    # -- expressions (precedence climbing, low to high) ------------------------

    def _expression(self) -> Expr:
        return self._or_expr()

    def _or_expr(self) -> Expr:
        left = self._and_expr()
        while self._check(TokenType.OR):
            tok = self._advance()
            right = self._and_expr()
            left = BinOp(line=tok.line, col=tok.col, op="or", left=left, right=right)
        return left

    def _and_expr(self) -> Expr:
        left = self._equality()
        while self._check(TokenType.AND):
            tok = self._advance()
            right = self._equality()
            left = BinOp(line=tok.line, col=tok.col, op="and", left=left, right=right)
        return left

    def _equality(self) -> Expr:
        left = self._comparison()
        while self._peek().type in _EQUALITY_TYPES:
            tok = self._advance()
            right = self._comparison()
            left = BinOp(line=tok.line, col=tok.col, op=tok.value, left=left, right=right)
        return left

    def _comparison(self) -> Expr:
        left = self._term()
        while self._peek().type in _COMPARISON_TYPES:
            tok = self._advance()
            right = self._term()
            left = BinOp(line=tok.line, col=tok.col, op=tok.value, left=left, right=right)
        return left

    def _term(self) -> Expr:
        left = self._factor()
        while self._peek().type in _TERM_TYPES:
            tok = self._advance()
            right = self._factor()
            left = BinOp(line=tok.line, col=tok.col, op=tok.value, left=left, right=right)
        return left

    def _factor(self) -> Expr:
        left = self._unary()
        while self._peek().type in _FACTOR_TYPES:
            tok = self._advance()
            right = self._unary()
            left = BinOp(line=tok.line, col=tok.col, op=tok.value, left=left, right=right)
        return left

    def _unary(self) -> Expr:
        tok = self._peek()
        if tok.type == TokenType.MINUS:
            self._advance()
            operand = self._unary()
            return UnaryOp(line=tok.line, col=tok.col, op="-", operand=operand)
        if tok.type == TokenType.NOT:
            self._advance()
            operand = self._unary()
            return UnaryOp(line=tok.line, col=tok.col, op="not", operand=operand)
        return self._primary()

    def _primary(self) -> Expr:
        tok = self._peek()

        if tok.type == TokenType.NUMBER:
            self._advance()
            return NumberLit(line=tok.line, col=tok.col, value=tok.value)
        if tok.type == TokenType.STRING:
            self._advance()
            return StringLit(line=tok.line, col=tok.col, value=tok.value)
        if tok.type == TokenType.TRUE:
            self._advance()
            return BoolLit(line=tok.line, col=tok.col, value=True)
        if tok.type == TokenType.FALSE:
            self._advance()
            return BoolLit(line=tok.line, col=tok.col, value=False)
        if tok.type == TokenType.IDENT:
            self._advance()
            return Var(line=tok.line, col=tok.col, name=tok.value)
        if tok.type == TokenType.LPAREN:
            self._advance()
            expr = self._expression()
            self._expect(TokenType.RPAREN, "expected ')' after expression")
            return expr

        raise ParseError(f"unexpected token {tok.type.name} ({tok.value!r}) in expression", tok.line, tok.col)


def parse_source(source: str) -> Program:
    """Convenience helper: lex + parse a source string in one call."""
    tokens = Lexer(source).tokenize()
    return Parser(tokens).parse()
