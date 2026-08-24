from __future__ import annotations

from dataclasses import dataclass, field

from lexer import Token


class ParseError(Exception):
    pass


TYPE_TOKENS = {"INT", "FLOAT", "BOOL", "STRING", "VOID"}
TYPE_NAMES = {"int", "float", "bool", "string", "void"}


@dataclass
class Node:
    line: int


@dataclass
class NumberLit(Node):
    value: float
    is_float: bool


@dataclass
class StringLit(Node):
    value: str


@dataclass
class BoolLit(Node):
    value: bool


@dataclass
class Variable(Node):
    name: str


@dataclass
class Assign(Node):
    name: str
    value: Node


@dataclass
class Binary(Node):
    op: str
    left: Node
    right: Node


@dataclass
class Logical(Node):
    op: str
    left: Node
    right: Node


@dataclass
class Unary(Node):
    op: str
    right: Node


@dataclass
class Call(Node):
    callee: str
    args: list[Node]


@dataclass
class ExprStmt(Node):
    expr: Node


@dataclass
class Param:
    name: str
    type_name: str


@dataclass
class VarDecl(Node):
    name: str
    type_name: str
    value: Node


@dataclass
class FunDecl(Node):
    name: str
    params: list[Param]
    return_type: str
    body: list[Node]


@dataclass
class Print(Node):
    expr: Node


@dataclass
class If(Node):
    cond: Node
    then_branch: Node
    else_branch: Node | None


@dataclass
class While(Node):
    cond: Node
    body: Node


@dataclass
class Return(Node):
    value: Node | None


@dataclass
class Block(Node):
    statements: list[Node] = field(default_factory=list)


class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.pos = 0

    def peek(self) -> Token:
        return self.tokens[self.pos]

    def previous(self) -> Token:
        return self.tokens[self.pos - 1]

    def at_end(self) -> bool:
        return self.peek().type == "EOF"

    def advance(self) -> Token:
        if not self.at_end():
            self.pos += 1
        return self.previous()

    def check(self, type_: str) -> bool:
        return not self.at_end() and self.peek().type == type_

    def match(self, *types: str) -> bool:
        if self.peek().type in types:
            self.advance()
            return True
        return False

    def consume(self, type_: str, message: str) -> Token:
        if self.check(type_):
            return self.advance()
        tok = self.peek()
        raise ParseError(f"Line {tok.line}: {message} (got {tok.type!r})")

    def parse_program(self) -> list[Node]:
        statements = []
        while not self.at_end():
            statements.append(self.declaration())
        return statements

    def type_name(self) -> str:
        tok = self.peek()
        if tok.type in TYPE_TOKENS:
            self.advance()
            return tok.value
        raise ParseError(f"Line {tok.line}: expected type name, got {tok.type!r}")

    def declaration(self) -> Node:
        if self.match("VAR"):
            return self.var_decl()
        if self.match("DEF"):
            return self.fun_decl()
        return self.statement()

    def var_decl(self) -> Node:
        line = self.previous().line
        name = self.consume("IDENT", "expected variable name").value
        self.consume("COLON", "expected ':' after variable name")
        type_name = self.type_name()
        self.consume("EQ", "expected '=' in variable declaration")
        value = self.expression()
        self.consume("SEMI", "expected ';' after variable declaration")
        return VarDecl(line, name, type_name, value)

    def fun_decl(self) -> Node:
        line = self.previous().line
        name = self.consume("IDENT", "expected function name").value
        self.consume("LPAREN", "expected '(' after function name")
        params: list[Param] = []
        if not self.check("RPAREN"):
            while True:
                pname = self.consume("IDENT", "expected parameter name").value
                self.consume("COLON", "expected ':' after parameter name")
                ptype = self.type_name()
                params.append(Param(pname, ptype))
                if not self.match("COMMA"):
                    break
        self.consume("RPAREN", "expected ')' after parameters")
        self.consume("ARROW", "expected '->' before return type")
        return_type = self.type_name()
        self.consume("LBRACE", "expected '{' before function body")
        body = self.block_statements()
        return FunDecl(line, name, params, return_type, body)

    def statement(self) -> Node:
        if self.match("PRINT"):
            return self.print_statement()
        if self.match("IF"):
            return self.if_statement()
        if self.match("WHILE"):
            return self.while_statement()
        if self.match("RETURN"):
            return self.return_statement()
        if self.match("LBRACE"):
            line = self.previous().line
            return Block(line, self.block_statements())
        return self.expr_statement()

    def block_statements(self) -> list[Node]:
        statements = []
        while not self.check("RBRACE") and not self.at_end():
            statements.append(self.declaration())
        self.consume("RBRACE", "expected '}' after block")
        return statements

    def print_statement(self) -> Node:
        line = self.previous().line
        value = self.expression()
        self.consume("SEMI", "expected ';' after print statement")
        return Print(line, value)

    def if_statement(self) -> Node:
        line = self.previous().line
        self.consume("LPAREN", "expected '(' after 'if'")
        cond = self.expression()
        self.consume("RPAREN", "expected ')' after condition")
        then_branch = self.statement()
        else_branch = self.statement() if self.match("ELSE") else None
        return If(line, cond, then_branch, else_branch)

    def while_statement(self) -> Node:
        line = self.previous().line
        self.consume("LPAREN", "expected '(' after 'while'")
        cond = self.expression()
        self.consume("RPAREN", "expected ')' after condition")
        body = self.statement()
        return While(line, cond, body)

    def return_statement(self) -> Node:
        line = self.previous().line
        value = None if self.check("SEMI") else self.expression()
        self.consume("SEMI", "expected ';' after return statement")
        return Return(line, value)

    def expr_statement(self) -> Node:
        expr = self.expression()
        line = expr.line
        self.consume("SEMI", "expected ';' after expression")
        return ExprStmt(line, expr)

    def expression(self) -> Node:
        return self.assignment()

    def assignment(self) -> Node:
        expr = self.logic_or()
        if self.match("EQ"):
            line = self.previous().line
            value = self.assignment()
            if isinstance(expr, Variable):
                return Assign(line, expr.name, value)
            raise ParseError(f"Line {line}: invalid assignment target")
        return expr

    def logic_or(self) -> Node:
        expr = self.logic_and()
        while self.match("OR"):
            line = self.previous().line
            expr = Logical(line, "or", expr, self.logic_and())
        return expr

    def logic_and(self) -> Node:
        expr = self.equality()
        while self.match("AND"):
            line = self.previous().line
            expr = Logical(line, "and", expr, self.equality())
        return expr

    def equality(self) -> Node:
        expr = self.comparison()
        while self.match("EQEQ", "NEQ"):
            op = self.previous().value
            line = self.previous().line
            expr = Binary(line, op, expr, self.comparison())
        return expr

    def comparison(self) -> Node:
        expr = self.term()
        while self.match("LT", "LE", "GT", "GE"):
            op = self.previous().value
            line = self.previous().line
            expr = Binary(line, op, expr, self.term())
        return expr

    def term(self) -> Node:
        expr = self.factor()
        while self.match("PLUS", "MINUS"):
            op = self.previous().value
            line = self.previous().line
            expr = Binary(line, op, expr, self.factor())
        return expr

    def factor(self) -> Node:
        expr = self.unary()
        while self.match("STAR", "SLASH"):
            op = self.previous().value
            line = self.previous().line
            expr = Binary(line, op, expr, self.unary())
        return expr

    def unary(self) -> Node:
        if self.match("MINUS", "NOT"):
            op = self.previous().value
            line = self.previous().line
            return Unary(line, op, self.unary())
        return self.call()

    def call(self) -> Node:
        expr = self.primary()
        if self.match("LPAREN"):
            line = self.previous().line
            if not isinstance(expr, Variable):
                raise ParseError(f"Line {line}: can only call named functions")
            args = []
            if not self.check("RPAREN"):
                args.append(self.expression())
                while self.match("COMMA"):
                    args.append(self.expression())
            self.consume("RPAREN", "expected ')' after arguments")
            return Call(line, expr.name, args)
        return expr

    def primary(self) -> Node:
        tok = self.peek()
        if self.match("INT"):
            return NumberLit(tok.line, int(tok.value), False)
        if self.match("FLOAT"):
            return NumberLit(tok.line, float(tok.value), True)
        if self.match("STRING"):
            return StringLit(tok.line, tok.value)
        if self.match("TRUE"):
            return BoolLit(tok.line, True)
        if self.match("FALSE"):
            return BoolLit(tok.line, False)
        if self.match("IDENT"):
            return Variable(tok.line, tok.value)
        if self.match("LPAREN"):
            expr = self.expression()
            self.consume("RPAREN", "expected ')' after expression")
            return expr
        raise ParseError(f"Line {tok.line}: expected expression, got {tok.type!r}")
