"""Tests for repl.py: the buffering heuristic and a scripted session."""

import io
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from repl import _is_buffer_complete, run_repl


class TestBufferComplete(unittest.TestCase):
    def test_simple_statement_is_complete(self):
        self.assertTrue(_is_buffer_complete("let x = 1;"))

    def test_statement_missing_semicolon_is_incomplete(self):
        self.assertFalse(_is_buffer_complete("let x = 1"))

    def test_empty_buffer_is_incomplete(self):
        self.assertFalse(_is_buffer_complete(""))

    def test_open_brace_is_incomplete(self):
        self.assertFalse(_is_buffer_complete("if (true) {"))

    def test_balanced_braces_is_complete(self):
        self.assertTrue(_is_buffer_complete("if (true) { print 1; }"))

    def test_nested_braces_track_depth(self):
        self.assertFalse(_is_buffer_complete("if (true) { if (true) {"))
        self.assertTrue(_is_buffer_complete("if (true) { if (true) { print 1; } }"))

    def test_brace_inside_string_literal_is_ignored(self):
        # The '{' here is data, not a block delimiter -- must not be
        # counted by the brace-depth heuristic.
        self.assertTrue(_is_buffer_complete('print "{ not a block }";'))

    def test_lex_error_reported_immediately_not_buffered_forever(self):
        # An unterminated string can never be "completed" by more input
        # in the way the REPL buffers, so treat it as done right away.
        self.assertTrue(_is_buffer_complete('print "oops'))


class TestReplSession(unittest.TestCase):
    def _run_session(self, lines: list[str]) -> str:
        buf = io.StringIO()
        with patch("builtins.input", side_effect=[*lines, EOFError()]):
            with redirect_stdout(buf):
                exit_code = run_repl()
        self.assertEqual(exit_code, 0)
        return buf.getvalue()

    def test_variable_persists_across_inputs(self):
        output = self._run_session(["let x = 5;", "print x + 1;"])
        self.assertIn("6", output)

    def test_multiline_if_block(self):
        output = self._run_session([
            "let x = 10;",
            "if (x > 5) {",
            '  print "big";',
            "} else {",
            '  print "small";',
            "}",
        ])
        self.assertIn("big", output)
        self.assertNotIn("small", output)

    def test_env_command_lists_variables(self):
        output = self._run_session(["let a = 1;", "let b = 2;", ":env"])
        self.assertIn("a = 1", output)
        self.assertIn("b = 2", output)

    def test_quit_command_exits_cleanly(self):
        buf = io.StringIO()
        with patch("builtins.input", side_effect=["let x = 1;", ":quit", EOFError()]):
            with redirect_stdout(buf):
                exit_code = run_repl()
        self.assertEqual(exit_code, 0)

    def test_runtime_error_does_not_kill_session(self):
        # A division-by-zero on one line should print an error but let
        # the REPL keep running and preserve prior state.
        output = self._run_session([
            "let x = 42;",
            "print 1 / 0;",
            "print x;",
        ])
        self.assertIn("division by zero", output)
        self.assertIn("42", output)

    def test_parse_error_does_not_kill_session(self):
        output = self._run_session([
            "let x = 1;",
            "this is not valid;",
            "print x;",
        ])
        self.assertIn("Parse error", output)
        self.assertIn("1", output)

    def test_help_command_prints_help(self):
        output = self._run_session([":help"])
        self.assertIn("Special commands", output)


if __name__ == "__main__":
    unittest.main()
