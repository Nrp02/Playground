"""Unit tests for lexer.py."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from lexer import Lexer, LexError, TokenType


def token_types(source: str) -> list[TokenType]:
    return [tok.type for tok in Lexer(source).tokenize()]


class TestNumbers(unittest.TestCase):
    def test_integer(self):
        tokens = Lexer("42").tokenize()
        self.assertEqual(tokens[0].type, TokenType.NUMBER)
        self.assertEqual(tokens[0].value, 42)
        self.assertIsInstance(tokens[0].value, int)

    def test_float(self):
        tokens = Lexer("3.14").tokenize()
        self.assertEqual(tokens[0].value, 3.14)
        self.assertIsInstance(tokens[0].value, float)

    def test_leading_dot_float(self):
        tokens = Lexer(".5").tokenize()
        self.assertEqual(tokens[0].value, 0.5)


class TestStrings(unittest.TestCase):
    def test_basic_string(self):
        tokens = Lexer('"hello"').tokenize()
        self.assertEqual(tokens[0].type, TokenType.STRING)
        self.assertEqual(tokens[0].value, "hello")

    def test_escapes(self):
        tokens = Lexer(r'"a\tb\nc\"d\\e"').tokenize()
        self.assertEqual(tokens[0].value, 'a\tb\nc"d\\e')

    def test_unterminated_string_raises(self):
        with self.assertRaises(LexError):
            Lexer('"unterminated').tokenize()

    def test_unknown_escape_raises(self):
        with self.assertRaises(LexError):
            Lexer(r'"bad \q escape"').tokenize()

    def test_empty_string(self):
        tokens = Lexer('""').tokenize()
        self.assertEqual(tokens[0].value, "")


class TestIdentifiersAndKeywords(unittest.TestCase):
    def test_identifier(self):
        tokens = Lexer("my_var2").tokenize()
        self.assertEqual(tokens[0].type, TokenType.IDENT)
        self.assertEqual(tokens[0].value, "my_var2")

    def test_keywords(self):
        types = token_types("let if else while print true false and or not")
        expected = [
            TokenType.LET, TokenType.IF, TokenType.ELSE, TokenType.WHILE,
            TokenType.PRINT, TokenType.TRUE, TokenType.FALSE,
            TokenType.AND, TokenType.OR, TokenType.NOT, TokenType.EOF,
        ]
        self.assertEqual(types, expected)

    def test_keyword_prefix_is_still_identifier(self):
        # "iffy" must not be lexed as IF + "fy".
        tokens = Lexer("iffy").tokenize()
        self.assertEqual(tokens[0].type, TokenType.IDENT)
        self.assertEqual(tokens[0].value, "iffy")


class TestOperators(unittest.TestCase):
    def test_single_char_operators(self):
        types = token_types("+ - * / % ( ) { } ; ,")
        expected = [
            TokenType.PLUS, TokenType.MINUS, TokenType.STAR, TokenType.SLASH,
            TokenType.PERCENT, TokenType.LPAREN, TokenType.RPAREN,
            TokenType.LBRACE, TokenType.RBRACE, TokenType.SEMI, TokenType.COMMA,
            TokenType.EOF,
        ]
        self.assertEqual(types, expected)

    def test_two_char_operators_not_confused_with_one_char(self):
        types = token_types("== != <= >= = < >")
        expected = [
            TokenType.EQ, TokenType.NEQ, TokenType.LTE, TokenType.GTE,
            TokenType.ASSIGN, TokenType.LT, TokenType.GT, TokenType.EOF,
        ]
        self.assertEqual(types, expected)

    def test_unexpected_character_raises(self):
        with self.assertRaises(LexError):
            Lexer("let x = 5 @ 3;").tokenize()


class TestCommentsAndWhitespace(unittest.TestCase):
    def test_hash_comment_skipped(self):
        types = token_types("1 # this is a comment\n2")
        self.assertEqual(types, [TokenType.NUMBER, TokenType.NUMBER, TokenType.EOF])

    def test_slash_slash_comment_skipped(self):
        types = token_types("1 // comment\n2")
        self.assertEqual(types, [TokenType.NUMBER, TokenType.NUMBER, TokenType.EOF])

    def test_whitespace_ignored(self):
        types = token_types("  1\t\n\n  +   2  ")
        self.assertEqual(types, [TokenType.NUMBER, TokenType.PLUS, TokenType.NUMBER, TokenType.EOF])

    def test_comment_only_source_yields_only_eof(self):
        types = token_types("# just a comment")
        self.assertEqual(types, [TokenType.EOF])


class TestLineColTracking(unittest.TestCase):
    def test_line_and_col_advance_correctly(self):
        tokens = Lexer("let x\n= 5;").tokenize()
        by_value = {(t.type, t.value): (t.line, t.col) for t in tokens}
        self.assertEqual(by_value[(TokenType.LET, "let")], (1, 1))
        self.assertEqual(by_value[(TokenType.IDENT, "x")], (1, 5))
        self.assertEqual(by_value[(TokenType.ASSIGN, "=")], (2, 1))
        self.assertEqual(by_value[(TokenType.NUMBER, 5)], (2, 3))

    def test_lex_error_reports_correct_position(self):
        try:
            Lexer("let x = 1;\nlet y = @;").tokenize()
            self.fail("expected LexError")
        except LexError as exc:
            self.assertEqual(exc.line, 2)
            self.assertEqual(exc.col, 9)


class TestFullProgramTokenization(unittest.TestCase):
    def test_realistic_snippet(self):
        source = 'let x = 1 + 2 * 3;\nif (x >= 7) { print "big"; }'
        tokens = Lexer(source).tokenize()
        self.assertEqual(tokens[-1].type, TokenType.EOF)
        # Spot check a handful of tokens rather than the whole stream.
        types = [t.type for t in tokens]
        self.assertIn(TokenType.IF, types)
        self.assertIn(TokenType.GTE, types)
        self.assertIn(TokenType.STRING, types)


if __name__ == "__main__":
    unittest.main()
