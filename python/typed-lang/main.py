import sys

import parser as ast
from interpreter import Interpreter
from lexer import LexError, Lexer
from parser import ParseError, Parser
from typechecker import TypeChecker


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: python3 main.py <script.tl>", file=sys.stderr)
        return 64

    path = sys.argv[1]
    with open(path, "r") as f:
        source = f.read()

    try:
        tokens = Lexer(source).tokenize()
        program = Parser(tokens).parse_program()
    except (LexError, ParseError) as e:
        print(f"Syntax error: {e}", file=sys.stderr)
        return 65

    checker = TypeChecker()
    errors = checker.check(program)
    if errors:
        print(f"Type checking failed with {len(errors)} error(s):")
        for err in errors:
            print(f"  {err}")
        return 65

    functions = {stmt.name: stmt for stmt in program if isinstance(stmt, ast.FunDecl)}
    Interpreter(functions).run(program)
    return 0


if __name__ == "__main__":
    sys.exit(main())
