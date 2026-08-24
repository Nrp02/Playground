from lexer import tokenize


class ColumnRef:
    def __init__(self, table, name):
        self.table = table
        self.name = name

    def __repr__(self):
        return f"{self.table}.{self.name}" if self.table else self.name


class Comparison:
    def __init__(self, left, op, right):
        self.left = left
        self.op = op
        self.right = right


class JoinClause:
    def __init__(self, table, left_col, right_col):
        self.table = table
        self.left_col = left_col
        self.right_col = right_col


class OrderBy:
    def __init__(self, col, descending):
        self.col = col
        self.descending = descending


class SelectStatement:
    def __init__(self, columns, star, from_table, join, where, order_by):
        self.columns = columns
        self.star = star
        self.from_table = from_table
        self.join = join
        self.where = where
        self.order_by = order_by


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def peek(self):
        return self.tokens[self.pos]

    def advance(self):
        tok = self.tokens[self.pos]
        self.pos += 1
        return tok

    def expect(self, kind):
        tok = self.peek()
        if tok.kind != kind:
            raise SyntaxError(f"expected {kind}, got {tok.kind} ({tok.value!r})")
        return self.advance()

    def parse_column_ref(self):
        first = self.expect("IDENT").value
        if self.peek().kind == ".":
            self.advance()
            name = self.expect("IDENT").value
            return ColumnRef(first, name)
        return ColumnRef(None, first)

    def parse_select(self):
        self.expect("SELECT")

        star = False
        columns = []
        if self.peek().kind == "*":
            self.advance()
            star = True
        else:
            columns.append(self.parse_column_ref())
            while self.peek().kind == ",":
                self.advance()
                columns.append(self.parse_column_ref())

        self.expect("FROM")
        from_table = self.expect("IDENT").value

        join = None
        if self.peek().kind == "JOIN":
            self.advance()
            join_table = self.expect("IDENT").value
            self.expect("ON")
            left_col = self.parse_column_ref()
            self.expect("=")
            right_col = self.parse_column_ref()
            join = JoinClause(join_table, left_col, right_col)

        where = None
        if self.peek().kind == "WHERE":
            self.advance()
            where = self.parse_where()

        order_by = None
        if self.peek().kind == "ORDER":
            self.advance()
            self.expect("BY")
            col = self.parse_column_ref()
            descending = False
            if self.peek().kind == "DESC":
                self.advance()
                descending = True
            elif self.peek().kind == "ASC":
                self.advance()

            order_by = OrderBy(col, descending)

        if self.peek().kind == ";":
            self.advance()

        return SelectStatement(columns, star, from_table, join, where, order_by)

    def parse_where(self):
        conditions = [self.parse_comparison()]
        while self.peek().kind == "AND":
            self.advance()
            conditions.append(self.parse_comparison())
        return conditions

    def parse_comparison(self):
        left = self.parse_column_ref()
        op_tok = self.advance()
        if op_tok.kind not in ("=", "!=", "<", ">", "<=", ">="):
            raise SyntaxError(f"expected comparison operator, got {op_tok.kind}")
        right = self.parse_value()
        return Comparison(left, op_tok.kind, right)

    def parse_value(self):
        tok = self.peek()
        if tok.kind == "NUMBER":
            self.advance()
            return float(tok.value) if "." in tok.value else int(tok.value)
        if tok.kind == "STRING":
            self.advance()
            return tok.value
        if tok.kind == "IDENT":
            return self.parse_column_ref()
        raise SyntaxError(f"expected value, got {tok.kind}")


def parse_script(text):
    statements = []
    for chunk in text.split(";"):
        chunk = chunk.strip()
        if not chunk:
            continue
        tokens = tokenize(chunk + ";")
        parser = Parser(tokens)
        statements.append(parser.parse_select())
    return statements
