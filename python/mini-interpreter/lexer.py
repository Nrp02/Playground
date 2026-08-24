"""
Lexer for the "mini" scripting language.

Turns raw source text into a flat stream of Token objects that the parser
consumes. The lexer is hand-written (no regex-based tokenizer table) so that
error positions (line/column) can be tracked precisely for diagnostics.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto


class TokenType(Enum):
    # Literals
    NUMBER = auto()
    STRING = auto()
    IDENT = auto()

    # Keywords
    LET = auto()
    IF = auto()
    ELSE = auto()
    WHILE = auto()
    PRINT = auto()
    TRUE = auto()
    FALSE = auto()
    AND = auto()
    OR = auto()
    NOT = auto()

    # Operators / punctuation
    PLUS = auto()
    MINUS = auto()
    STAR = auto()
    SLASH = auto()
    PERCENT = auto()
    ASSIGN = auto()
    EQ = auto()
    NEQ = auto()
    LT = auto()
    LTE = auto()
    GT = auto()
    GTE = auto()
    LPAREN = auto()
    RPAREN = auto()
    LBRACE = auto()
    RBRACE = auto()
    SEMI = auto()
    COMMA = auto()

    EOF = auto()


KEYWORDS = {
    "let": TokenType.LET,
    "if": TokenType.IF,
    "else": TokenType.ELSE,
    "while": TokenType.WHILE,
    "print": TokenType.PRINT,
    "true": TokenType.TRUE,
    "false": TokenType.FALSE,
    "and": TokenType.AND,
    "or": TokenType.OR,
    "not": TokenType.NOT,
}


@dataclass
class Token:
    type: TokenType
    value: object
    line: int
    col: int

    def __repr__(self) -> str:  # pragma: no cover - debugging helper
        return f"Token({self.type.name}, {self.value!r}, {self.line}:{self.col})"


class LexError(Exception):
    def __init__(self, message: str, line: int, col: int):
        super().__init__(f"Lex error at {line}:{col}: {message}")
        self.message = message
        self.line = line
        self.col = col


_SINGLE_CHAR_TOKENS = {
    "+": TokenType.PLUS,
    "-": TokenType.MINUS,
    "*": TokenType.STAR,
    "/": TokenType.SLASH,
    "%": TokenType.PERCENT,
    "(": TokenType.LPAREN,
    ")": TokenType.RPAREN,
    "{": TokenType.LBRACE,
    "}": TokenType.RBRACE,
    ";": TokenType.SEMI,
    ",": TokenType.COMMA,
}


class Lexer:
    def __init__(self, source: str):
        self.source = source
        self.pos = 0
        self.line = 1
        self.col = 1

    def _peek(self, offset: int = 0) -> str:
        idx = self.pos + offset
        if idx >= len(self.source):
            return "\0"
        return self.source[idx]

    def _advance(self) -> str:
        ch = self.source[self.pos]
        self.pos += 1
        if ch == "\n":
            self.line += 1
            self.col = 1
        else:
            self.col += 1
        return ch

    def _match(self, expected: str) -> bool:
        if self._peek() == expected:
            self._advance()
            return True
        return False

    def tokenize(self) -> list[Token]:
        tokens: list[Token] = []
        while True:
            self._skip_whitespace_and_comments()
            if self.pos >= len(self.source):
                tokens.append(Token(TokenType.EOF, None, self.line, self.col))
                break

            start_line, start_col = self.line, self.col
            ch = self._peek()

            if ch.isdigit() or (ch == "." and self._peek(1).isdigit()):
                tokens.append(self._read_number(start_line, start_col))
                continue

            if ch.isalpha() or ch == "_":
                tokens.append(self._read_ident(start_line, start_col))
                continue

            if ch == '"':
                tokens.append(self._read_string(start_line, start_col))
                continue

            # Two-character operators first.
            if ch == "=" and self._peek(1) == "=":
                self._advance()
                self._advance()
                tokens.append(Token(TokenType.EQ, "==", start_line, start_col))
                continue
            if ch == "!" and self._peek(1) == "=":
                self._advance()
                self._advance()
                tokens.append(Token(TokenType.NEQ, "!=", start_line, start_col))
                continue
            if ch == "<" and self._peek(1) == "=":
                self._advance()
                self._advance()
                tokens.append(Token(TokenType.LTE, "<=", start_line, start_col))
                continue
            if ch == ">" and self._peek(1) == "=":
                self._advance()
                self._advance()
                tokens.append(Token(TokenType.GTE, ">=", start_line, start_col))
                continue

            if ch == "=":
                self._advance()
                tokens.append(Token(TokenType.ASSIGN, "=", start_line, start_col))
                continue
            if ch == "<":
                self._advance()
                tokens.append(Token(TokenType.LT, "<", start_line, start_col))
                continue
            if ch == ">":
                self._advance()
                tokens.append(Token(TokenType.GT, ">", start_line, start_col))
                continue

            if ch in _SINGLE_CHAR_TOKENS:
                self._advance()
                tokens.append(Token(_SINGLE_CHAR_TOKENS[ch], ch, start_line, start_col))
                continue

            raise LexError(f"unexpected character {ch!r}", start_line, start_col)

        return tokens

    def _skip_whitespace_and_comments(self) -> None:
        while True:
            ch = self._peek()
            if ch in (" ", "\t", "\r", "\n"):
                self._advance()
            elif ch == "#":
                while self._peek() not in ("\n", "\0"):
                    self._advance()
            elif ch == "/" and self._peek(1) == "/":
                while self._peek() not in ("\n", "\0"):
                    self._advance()
            else:
                break

    def _read_number(self, line: int, col: int) -> Token:
        start = self.pos
        seen_dot = False
        while self._peek().isdigit() or (self._peek() == "." and not seen_dot):
            if self._peek() == ".":
                seen_dot = True
            self._advance()
        text = self.source[start:self.pos]
        value = float(text) if seen_dot else int(text)
        return Token(TokenType.NUMBER, value, line, col)

    def _read_ident(self, line: int, col: int) -> Token:
        start = self.pos
        while self._peek().isalnum() or self._peek() == "_":
            self._advance()
        text = self.source[start:self.pos]
        if text in KEYWORDS:
            return Token(KEYWORDS[text], text, line, col)
        return Token(TokenType.IDENT, text, line, col)

    def _read_string(self, line: int, col: int) -> Token:
        self._advance()  # consume opening quote
        chars: list[str] = []
        while True:
            ch = self._peek()
            if ch == "\0":
                raise LexError("unterminated string literal", line, col)
            if ch == '"':
                self._advance()
                break
            if ch == "\\":
                self._advance()
                esc = self._advance()
                mapping = {"n": "\n", "t": "\t", '"': '"', "\\": "\\"}
                if esc not in mapping:
                    raise LexError(f"unknown escape sequence '\\{esc}'", line, col)
                chars.append(mapping[esc])
            else:
                chars.append(self._advance())
        return Token(TokenType.STRING, "".join(chars), line, col)
