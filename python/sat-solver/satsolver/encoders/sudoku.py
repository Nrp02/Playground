from __future__ import annotations

from typing import List, Optional, Tuple

from ..cnf import CNF
from ..solver import Solver, Stats

N = 9
B = 3


def var(row: int, col: int, digit: int) -> int:
    return row * N * N + col * N + digit + 1


def encode(givens: List[List[int]]) -> CNF:
    cnf = CNF(N * N * N)
    for row in range(N):
        for col in range(N):
            cnf.add_clause([var(row, col, d) for d in range(N)])
            for d1 in range(N):
                for d2 in range(d1 + 1, N):
                    cnf.add_clause([-var(row, col, d1), -var(row, col, d2)])
    for row in range(N):
        for d in range(N):
            for c1 in range(N):
                for c2 in range(c1 + 1, N):
                    cnf.add_clause([-var(row, c1, d), -var(row, c2, d)])
    for col in range(N):
        for d in range(N):
            for r1 in range(N):
                for r2 in range(r1 + 1, N):
                    cnf.add_clause([-var(r1, col, d), -var(r2, col, d)])
    for br in range(B):
        for bc in range(B):
            cells = [(br * B + dr, bc * B + dc) for dr in range(B) for dc in range(B)]
            for d in range(N):
                for i in range(len(cells)):
                    for j in range(i + 1, len(cells)):
                        r1, c1 = cells[i]
                        r2, c2 = cells[j]
                        cnf.add_clause([-var(r1, c1, d), -var(r2, c2, d)])
    for row in range(N):
        for col in range(N):
            value = givens[row][col]
            if value != 0:
                cnf.add_clause([var(row, col, value - 1)])
    return cnf


def solve_sudoku(givens: List[List[int]]) -> Tuple[Optional[List[List[int]]], Stats]:
    cnf = encode(givens)
    solver = Solver(cnf)
    model = solver.solve()
    if model is None:
        return None, solver.stats
    grid = [[0] * N for _ in range(N)]
    for row in range(N):
        for col in range(N):
            for d in range(N):
                if model[var(row, col, d)]:
                    grid[row][col] = d + 1
    return grid, solver.stats
