"""End-to-end tests: lex + parse + execute a source string and check
either its printed output or the runtime error it raises."""

import io
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from interpreter import Environment, Interpreter, InterpreterError
from parser import parse_source


def run(source: str) -> str:
    """Run a mini-language program and return everything it printed."""
    program = parse_source(source)
    buf = io.StringIO()
    with redirect_stdout(buf):
        Interpreter().run(program)
    return buf.getvalue()


class TestArithmetic(unittest.TestCase):
    def test_operator_precedence(self):
        self.assertEqual(run("print 2 + 3 * 4;"), "14\n")

    def test_parens(self):
        self.assertEqual(run("print (2 + 3) * 4;"), "20\n")

    def test_integer_division_stays_int_when_exact(self):
        self.assertEqual(run("print 10 / 5;"), "2\n")

    def test_division_produces_float_when_inexact(self):
        self.assertEqual(run("print 1 / 4;"), "0.25\n")

    def test_modulo(self):
        self.assertEqual(run("print 7 % 3;"), "1\n")

    def test_unary_minus(self):
        self.assertEqual(run("print -5 + 10;"), "5\n")

    def test_division_by_zero_raises(self):
        with self.assertRaises(InterpreterError):
            run("print 1 / 0;")

    def test_modulo_by_zero_raises(self):
        with self.assertRaises(InterpreterError):
            run("print 1 % 0;")


class TestComparisonsAndLogic(unittest.TestCase):
    def test_equality(self):
        self.assertEqual(run("print 1 + 1 == 2;"), "true\n")

    def test_and_short_circuits(self):
        # If `and` evaluated both sides unconditionally this would raise
        # a divide-by-zero error instead of printing "false".
        self.assertEqual(run("print false and (1 / 0 == 0);"), "false\n")

    def test_or_short_circuits(self):
        self.assertEqual(run("print true or (1 / 0 == 0);"), "true\n")

    def test_not(self):
        self.assertEqual(run("print not (3 > 5);"), "true\n")

    def test_comparison_operators(self):
        self.assertEqual(run("print 3 >= 3;"), "true\n")
        self.assertEqual(run("print 3 > 3;"), "false\n")
        self.assertEqual(run("print 2 <= 1;"), "false\n")

    def test_comparison_requires_numbers(self):
        with self.assertRaises(InterpreterError):
            run('print "a" > 1;')


class TestStrings(unittest.TestCase):
    def test_concatenation(self):
        self.assertEqual(run('print "foo" + "bar";'), "foobar\n")

    def test_number_concatenated_with_string(self):
        self.assertEqual(run('print "count = " + 3;'), "count = 3\n")

    def test_string_equality(self):
        self.assertEqual(run('print "a" == "a";'), "true\n")
        self.assertEqual(run('print "a" == "b";'), "false\n")


class TestVariablesAndScope(unittest.TestCase):
    def test_let_and_reassignment(self):
        self.assertEqual(run("let x = 1; x = x + 1; x = x + 1; print x;"), "3\n")

    def test_block_scoped_let_shadows_outer(self):
        out = run("let x = 1; if (true) { let x = 99; print x; } print x;")
        self.assertEqual(out, "99\n1\n")

    def test_assignment_mutates_outer_scope(self):
        out = run("let x = 1; if (true) { x = 99; } print x;")
        self.assertEqual(out, "99\n")

    def test_undefined_variable_raises(self):
        with self.assertRaises(InterpreterError):
            run("print y;")

    def test_assign_to_undeclared_raises(self):
        with self.assertRaises(InterpreterError):
            run("x = 1;")

    def test_while_loop_accumulator(self):
        out = run(
            "let i = 0; let total = 0; "
            "while (i < 5) { total = total + i; i = i + 1; } "
            "print total;"
        )
        self.assertEqual(out, "10\n")

    def test_each_loop_iteration_gets_a_fresh_block_scope(self):
        # `doubled` is `let`-declared inside the loop body every pass; it
        # must not leak or accumulate state across iterations.
        out = run(
            "let i = 0; "
            "while (i < 3) { let doubled = i * 2; print doubled; i = i + 1; }"
        )
        self.assertEqual(out, "0\n2\n4\n")


class TestControlFlow(unittest.TestCase):
    def test_if_else(self):
        self.assertEqual(run('if (1 > 2) { print "a"; } else { print "b"; }'), "b\n")

    def test_else_if_chain(self):
        source = (
            'let x = 5;'
            'if (x > 10) { print "big"; }'
            'else if (x > 3) { print "mid"; }'
            'else { print "small"; }'
        )
        self.assertEqual(run(source), "mid\n")

    def test_fizzbuzz_small_range(self):
        source = (
            "let i = 1;"
            "while (i <= 5) {"
            '  if (i % 3 == 0) { print "Fizz"; }'
            "  else { print i; }"
            "  i = i + 1;"
            "}"
        )
        self.assertEqual(run(source), "1\n2\nFizz\n4\n5\n")

    def test_nested_while_loops(self):
        source = (
            "let i = 1;"
            "while (i <= 3) {"
            "  let j = 1;"
            "  while (j <= i) {"
            "    print i * 10 + j;"
            "    j = j + 1;"
            "  }"
            "  i = i + 1;"
            "}"
        )
        self.assertEqual(run(source), "11\n21\n22\n31\n32\n33\n")


class TestEnvironment(unittest.TestCase):
    """Direct unit tests of the Environment scope-chain object."""

    def test_get_walks_parent_chain(self):
        parent = Environment()
        parent.define("a", 1)
        child = Environment(parent=parent)
        self.assertEqual(child.get("a"), 1)

    def test_define_shadows_in_child_only(self):
        parent = Environment()
        parent.define("a", 1)
        child = Environment(parent=parent)
        child.define("a", 2)
        self.assertEqual(child.get("a"), 2)
        self.assertEqual(parent.get("a"), 1)

    def test_assign_mutates_owning_scope_not_child(self):
        parent = Environment()
        parent.define("a", 1)
        child = Environment(parent=parent)
        child.assign("a", 2)
        self.assertEqual(parent.get("a"), 2)
        self.assertNotIn("a", child.values)

    def test_get_missing_raises(self):
        env = Environment()
        with self.assertRaises(InterpreterError):
            env.get("missing")

    def test_assign_missing_raises(self):
        env = Environment()
        with self.assertRaises(InterpreterError):
            env.assign("missing", 1)


if __name__ == "__main__":
    unittest.main()
