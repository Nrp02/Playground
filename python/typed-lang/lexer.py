from dataclasses import dataclass


KEYWORDS = {
    "var", "if", "else", "while", "def", "return",
    "print", "true", "false", "and", "or", "not",
    "int", "float", "bool", "string", "void",
}


@dataclass
class Token:
    type: str
    value: str
    line: int


class LexError(Exception):
    pass


SIMPLE_TOKENS = {
    "(": "LPAREN", ")": "RPAREN", "{": "LBRACE", "}": "RBRACE",
    ",": "COMMA", ";": "SEMI", ":": "COLON",
    "+": "PLUS", "-": "MINUS", "*": "STAR", "/": "SLASH",
}


class Lexer:
    def __init__(self, source: str):
        self.source = source
        self.pos = 0
        self.line = 1

    def peek(self, offset: int = 0) -> str:
        idx = self.pos + offset
        return self.source[idx] if idx < len(self.source) else "\0"

    def advance(self) -> str:
        ch = self.source[self.pos]
        self.pos += 1
        if ch == "\n":
            self.line += 1
        return ch

    def tokenize(self) -> list[Token]:
        tokens: list[Token] = []
        while self.pos < len(self.source):
            ch = self.peek()

            if ch in " \r\t\n":
                self.advance()
                continue

            if ch == "/" and self.peek(1) == "/":
                while self.peek() != "\n" and self.pos < len(self.source):
                    self.advance()
                continue

            if ch.isdigit():
                tokens.append(self._number())
                continue

            if ch.isalpha() or ch == "_":
                tokens.append(self._identifier())
                continue

            if ch == '"':
                tokens.append(self._string())
                continue

            line = self.line
            if ch == "=" and self.peek(1) == "=":
                self.advance(); self.advance()
                tokens.append(Token("EQEQ", "==", line))
                continue
            if ch == "!" and self.peek(1) == "=":
                self.advance(); self.advance()
                tokens.append(Token("NEQ", "!=", line))
                continue
            if ch == "<" and self.peek(1) == "=":
                self.advance(); self.advance()
                tokens.append(Token("LE", "<=", line))
                continue
            if ch == ">" and self.peek(1) == "=":
                self.advance(); self.advance()
                tokens.append(Token("GE", ">=", line))
                continue
            if ch == "-" and self.peek(1) == ">":
                self.advance(); self.advance()
                tokens.append(Token("ARROW", "->", line))
                continue
            if ch == "=":
                self.advance()
                tokens.append(Token("EQ", "=", line))
                continue
            if ch == "<":
                self.advance()
                tokens.append(Token("LT", "<", line))
                continue
            if ch == ">":
                self.advance()
                tokens.append(Token("GT", ">", line))
                continue

            if ch in SIMPLE_TOKENS:
                self.advance()
                tokens.append(Token(SIMPLE_TOKENS[ch], ch, line))
                continue

            raise LexError(f"Unexpected character {ch!r} at line {line}")

        tokens.append(Token("EOF", "", self.line))
        return tokens

    def _number(self) -> Token:
        line = self.line
        start = self.pos
        is_float = False
        while self.peek().isdigit():
            self.advance()
        if self.peek() == "." and self.peek(1).isdigit():
            is_float = True
            self.advance()
            while self.peek().isdigit():
                self.advance()
        text = self.source[start:self.pos]
        return Token("FLOAT" if is_float else "INT", text, line)

    def _identifier(self) -> Token:
        line = self.line
        start = self.pos
        while self.peek().isalnum() or self.peek() == "_":
            self.advance()
        text = self.source[start:self.pos]
        if text in KEYWORDS:
            return Token(text.upper(), text, line)
        return Token("IDENT", text, line)

    def _string(self) -> Token:
        line = self.line
        self.advance()
        start = self.pos
        while self.peek() != '"':
            if self.pos >= len(self.source):
                raise LexError(f"Unterminated string at line {line}")
            self.advance()
        text = self.source[start:self.pos]
        self.advance()
        return Token("STRING", text, line)
