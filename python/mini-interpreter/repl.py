#!/usr/bin/env python3
"""
Interactive REPL for the mini scripting language.

Usage:
    python3 repl.py

Unlike main.py (which runs one complete .mini script file and exits),
this starts an interactive prompt: each statement you type is executed
immediately against a single persistent environment, so variables carry
over between inputs -- similar to Python's own interactive interpreter.

Since mini-language statements always end with ';' and blocks are
delimited with matching '{' '}', the REPL buffers physical input lines
until brace depth returns to zero and the buffered text ends with a
statement terminator, so multi-line if/while blocks can be typed
naturally across several lines instead of requiring one giant line.
"""

from __future__ import annotations

from interpreter import Interpreter, InterpreterError
from lexer import Lexer, LexError, TokenType
from parser import Parser, ParseError

PROMPT = "mini> "
CONTINUATION_PROMPT = "...   "

_HELP_TEXT = """\
Enter mini-language statements ending in ';'. Multi-line if/while blocks
are supported -- keep typing until the closing '}'.

Special commands:
  :help    show this message
  :env     print all currently defined variables
  :quit    exit the REPL (Ctrl-D also works)
"""


def _is_buffer_complete(source: str) -> bool:
    """
    Decide whether the REPL's buffered input is a complete statement yet:
    tokenize it for real (so a '{' inside a string literal can't fool the
    brace counter), require brace depth back to zero, and require the
    last real token to be ';' or '}'.
    """
    try:
        tokens = Lexer(source).tokenize()
    except LexError:
        # Most commonly an unterminated string -- report it now rather
        # than waiting for more input that will never fix it.
        return True

    depth = 0
    for tok in tokens:
        if tok.type == TokenType.LBRACE:
            depth += 1
        elif tok.type == TokenType.RBRACE:
            depth -= 1
    if depth > 0:
        return False

    real_tokens = [t for t in tokens if t.type != TokenType.EOF]
    if not real_tokens:
        return False
    return real_tokens[-1].type in (TokenType.SEMI, TokenType.RBRACE)


def _print_env(interpreter: Interpreter) -> None:
    values = interpreter.globals.values
    if not values:
        print("(no variables defined yet)")
        return
    for name in sorted(values):
        print(f"  {name} = {values[name]!r}")


def run_repl() -> int:
    print("mini-language REPL -- type :help for help, Ctrl-D to exit.")
    interpreter = Interpreter()
    buffered_lines: list[str] = []

    while True:
        prompt = CONTINUATION_PROMPT if buffered_lines else PROMPT
        try:
            line = input(prompt)
        except EOFError:
            print()
            return 0
        except KeyboardInterrupt:
            print()
            buffered_lines = []
            continue

        if not buffered_lines:
            stripped = line.strip()
            if stripped in (":quit", ":exit"):
                return 0
            if stripped == ":help":
                print(_HELP_TEXT)
                continue
            if stripped == ":env":
                _print_env(interpreter)
                continue
            if not stripped:
                continue

        buffered_lines.append(line)
        source = "\n".join(buffered_lines)
        if not _is_buffer_complete(source):
            continue

        buffered_lines = []
        try:
            tokens = Lexer(source).tokenize()
            program = Parser(tokens).parse()
        except (LexError, ParseError) as exc:
            print(exc)
            continue

        try:
            interpreter.run(program)
        except InterpreterError as exc:
            print(exc)
        except KeyboardInterrupt:
            print("\n(interrupted)")


if __name__ == "__main__":
    raise SystemExit(run_repl())
