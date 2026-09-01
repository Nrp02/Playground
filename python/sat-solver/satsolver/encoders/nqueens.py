from __future__ import annotations

from typing import List, Optional, Tuple

from ..cnf import CNF
from ..solver import Solver, Stats


def var(n: int, row: int, col: int) -> int:
    return row * n + col + 1


def encode(n: int) -> CNF:
    cnf = CNF(n * n)
    for row in range(n):
        cnf.add_clause([var(n, row, col) for col in range(n)])
    for row in range(n):
        for c1 in range(n):
            for c2 in range(c1 + 1, n):
                cnf.add_clause([-var(n, row, c1), -var(n, row, c2)])
    for col in range(n):
        for r1 in range(n):
            for r2 in range(r1 + 1, n):
                cnf.add_clause([-var(n, r1, col), -var(n, r2, col)])
    for r1 in range(n):
        for c1 in range(n):
            for r2 in range(r1 + 1, n):
                d = r2 - r1
                c2a = c1 + d
                c2b = c1 - d
                if c2a < n:
                    cnf.add_clause([-var(n, r1, c1), -var(n, r2, c2a)])
                if c2b >= 0:
                    cnf.add_clause([-var(n, r1, c1), -var(n, r2, c2b)])
    return cnf


def solve_nqueens(n: int) -> Tuple[Optional[List[List[bool]]], Stats]:
    cnf = encode(n)
    solver = Solver(cnf)
    model = solver.solve()
    if model is None:
        return None, solver.stats
    board = [[False] * n for _ in range(n)]
    for row in range(n):
        for col in range(n):
            if model[var(n, row, col)]:
                board[row][col] = True
    return board, solver.stats
