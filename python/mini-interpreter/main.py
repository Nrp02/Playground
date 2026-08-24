#!/usr/bin/env python3
"""
Command-line entry point for the mini scripting language.

Usage:
    python3 main.py path/to/script.mini

Reads the given `.mini` source file, lexes + parses it into an AST, then
executes it with the tree-walking interpreter. Lexer/parser/runtime errors
are caught and reported as short, single-line diagnostics (file:line:col)
instead of raw Python tracebacks.
"""

from __future__ import annotations

import sys
from pathlib import Path

from lexer import Lexer, LexError
from parser import Parser, ParseError
from interpreter import Interpreter, InterpreterError


def run_file(path: Path) -> int:
    try:
        source = path.read_text()
    except OSError as exc:
        print(f"error: could not read '{path}': {exc.strerror}", file=sys.stderr)
        return 1

    try:
        tokens = Lexer(source).tokenize()
        program = Parser(tokens).parse()
    except LexError as exc:
        print(f"{path}: {exc}", file=sys.stderr)
        return 1
    except ParseError as exc:
        print(f"{path}: {exc}", file=sys.stderr)
        return 1

    try:
        Interpreter().run(program)
    except InterpreterError as exc:
        print(f"{path}: {exc}", file=sys.stderr)
        return 1
    except RecursionError:
        print(f"{path}: Runtime error: recursion / loop nesting too deep", file=sys.stderr)
        return 1

    return 0


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: main.py <script.mini>", file=sys.stderr)
        return 1

    script_path = Path(argv[1])
    if not script_path.exists():
        print(f"error: no such file: '{script_path}'", file=sys.stderr)
        return 1

    return run_file(script_path)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
