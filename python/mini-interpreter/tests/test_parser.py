"""Unit tests for parser.py (AST shape and error reporting)."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from parser import (
    AssignStmt, Block, BoolLit, IfStmt, LetStmt, NumberLit, ParseError,
    PrintStmt, StringLit, UnaryOp, Var, WhileStmt, parse_source,
)


def parse_expr(source: str):
    """Parse `print <source>;` and return just the parsed expression."""
    program = parse_source(f"print {source};")
    stmt = program.statements[0]
    assert isinstance(stmt, PrintStmt)
    return stmt.expr


class TestLiterals(unittest.TestCase):
    def test_number_literal(self):
        expr = parse_expr("42")
        self.assertIsInstance(expr, NumberLit)
        self.assertEqual(expr.value, 42)

    def test_string_literal(self):
        expr = parse_expr('"hi"')
        self.assertIsInstance(expr, StringLit)
        self.assertEqual(expr.value, "hi")

    def test_bool_literals(self):
        self.assertIsInstance(parse_expr("true"), BoolLit)
        self.assertIs(parse_expr("true").value, True)
        self.assertIs(parse_expr("false").value, False)

    def test_variable_reference(self):
        expr = parse_expr("x")
        self.assertIsInstance(expr, Var)
        self.assertEqual(expr.name, "x")


class TestPrecedence(unittest.TestCase):
    def test_multiplication_binds_tighter_than_addition(self):
        # 2 + 3 * 4  =>  (+ 2 (* 3 4))
        expr = parse_expr("2 + 3 * 4")
        self.assertEqual(expr.op, "+")
        self.assertEqual(expr.left.value, 2)
        self.assertEqual(expr.right.op, "*")

    def test_parens_override_precedence(self):
        # (2 + 3) * 4  =>  (* (+ 2 3) 4)
        expr = parse_expr("(2 + 3) * 4")
        self.assertEqual(expr.op, "*")
        self.assertEqual(expr.left.op, "+")
        self.assertEqual(expr.right.value, 4)

    def test_left_associativity_of_subtraction(self):
        # 10 - 2 - 3  =>  (- (- 10 2) 3)
        expr = parse_expr("10 - 2 - 3")
        self.assertEqual(expr.op, "-")
        self.assertEqual(expr.left.op, "-")
        self.assertEqual(expr.right.value, 3)

    def test_comparison_binds_looser_than_arithmetic(self):
        # 1 + 1 == 2  =>  (== (+ 1 1) 2)
        expr = parse_expr("1 + 1 == 2")
        self.assertEqual(expr.op, "==")
        self.assertEqual(expr.left.op, "+")

    def test_and_binds_tighter_than_or(self):
        # a or b and c  =>  (or a (and b c))
        expr = parse_expr("a or b and c")
        self.assertEqual(expr.op, "or")
        self.assertEqual(expr.right.op, "and")

    def test_unary_minus_binds_tighter_than_binary_plus(self):
        # -a + b  =>  (+ (- a) b)
        expr = parse_expr("-a + b")
        self.assertEqual(expr.op, "+")
        self.assertIsInstance(expr.left, UnaryOp)
        self.assertEqual(expr.left.op, "-")

    def test_not_unary(self):
        expr = parse_expr("not true")
        self.assertIsInstance(expr, UnaryOp)
        self.assertEqual(expr.op, "not")

    def test_deeply_nested_parens(self):
        expr = parse_expr("((((1 + 2))))")
        self.assertEqual(expr.op, "+")


class TestStatements(unittest.TestCase):
    def test_let_statement(self):
        stmt = parse_source("let x = 5;").statements[0]
        self.assertIsInstance(stmt, LetStmt)
        self.assertEqual(stmt.name, "x")
        self.assertIsInstance(stmt.expr, NumberLit)

    def test_assign_statement(self):
        stmt = parse_source("x = 5;").statements[0]
        self.assertIsInstance(stmt, AssignStmt)
        self.assertEqual(stmt.name, "x")

    def test_block_statement(self):
        stmt = parse_source("{ let x = 1; print x; }").statements[0]
        self.assertIsInstance(stmt, Block)
        self.assertEqual(len(stmt.statements), 2)

    def test_if_without_else(self):
        stmt = parse_source("if (true) { print 1; }").statements[0]
        self.assertIsInstance(stmt, IfStmt)
        self.assertIsNone(stmt.else_block)

    def test_if_else(self):
        stmt = parse_source("if (true) { print 1; } else { print 2; }").statements[0]
        self.assertIsInstance(stmt.else_block, Block)

    def test_else_if_chain_produces_nested_ifstmt(self):
        stmt = parse_source(
            "if (a) { print 1; } else if (b) { print 2; } else { print 3; }"
        ).statements[0]
        self.assertIsInstance(stmt.else_block, IfStmt)
        self.assertIsInstance(stmt.else_block.else_block, Block)

    def test_while_statement(self):
        stmt = parse_source("while (x < 10) { x = x + 1; }").statements[0]
        self.assertIsInstance(stmt, WhileStmt)
        self.assertIsInstance(stmt.body, Block)

    def test_multiple_top_level_statements(self):
        program = parse_source("let x = 1; let y = 2; print x + y;")
        self.assertEqual(len(program.statements), 3)


class TestParseErrors(unittest.TestCase):
    def test_missing_semicolon(self):
        with self.assertRaises(ParseError):
            parse_source("let x = 5")

    def test_missing_equals_in_let(self):
        with self.assertRaises(ParseError):
            parse_source("let x 5;")

    def test_unclosed_paren(self):
        with self.assertRaises(ParseError):
            parse_source("print (1 + 2;")

    def test_unclosed_brace(self):
        with self.assertRaises(ParseError):
            parse_source("if (true) { print 1;")

    def test_unexpected_token_in_expression(self):
        with self.assertRaises(ParseError):
            parse_source("print ;")

    def test_bare_expression_statement_not_supported(self):
        # This language has no expression-statements; a bare identifier
        # with no assignment is not a recognized statement form.
        with self.assertRaises(ParseError):
            parse_source("x;")

    def test_error_reports_line_number(self):
        try:
            parse_source("let x = 1;\nlet y 2;")
            self.fail("expected ParseError")
        except ParseError as exc:
            self.assertEqual(exc.line, 2)


if __name__ == "__main__":
    unittest.main()
