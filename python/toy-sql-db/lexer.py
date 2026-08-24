KEYWORDS = {
    "SELECT", "FROM", "WHERE", "JOIN", "ON", "AND",
    "ORDER", "BY", "ASC", "DESC",
}

SYMBOLS = ["<=", ">=", "!=", "=", "<", ">", ",", ".", "*", ";"]


class Token:
    def __init__(self, kind, value):
        self.kind = kind
        self.value = value

    def __repr__(self):
        return f"Token({self.kind!r}, {self.value!r})"


def tokenize(text):
    tokens = []
    i = 0
    n = len(text)

    while i < n:
        c = text[i]

        if c.isspace():
            i += 1
            continue

        if c == "'" or c == '"':
            quote = c
            j = i + 1
            buf = []
            while j < n and text[j] != quote:
                buf.append(text[j])
                j += 1
            tokens.append(Token("STRING", "".join(buf)))
            i = j + 1
            continue

        if c.isdigit():
            j = i
            while j < n and (text[j].isdigit() or text[j] == "."):
                j += 1
            tokens.append(Token("NUMBER", text[i:j]))
            i = j
            continue

        if c.isalpha() or c == "_":
            j = i
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            word = text[i:j]
            upper = word.upper()
            if upper in KEYWORDS:
                tokens.append(Token(upper, word))
            else:
                tokens.append(Token("IDENT", word))
            i = j
            continue

        matched = False
        for sym in SYMBOLS:
            if text.startswith(sym, i):
                tokens.append(Token(sym, sym))
                i += len(sym)
                matched = True
                break
        if matched:
            continue

        raise SyntaxError(f"unexpected character {c!r} at position {i}")

    tokens.append(Token("EOF", None))
    return tokens
