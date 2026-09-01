from __future__ import annotations

from typing import Dict, List, Optional, Tuple

from ..cnf import CNF
from ..solver import Solver, Stats


def var(num_colors: int, node: int, color: int) -> int:
    return node * num_colors + color + 1


def encode(num_nodes: int, edges: List[Tuple[int, int]], num_colors: int) -> CNF:
    cnf = CNF(num_nodes * num_colors)
    for node in range(num_nodes):
        cnf.add_clause([var(num_colors, node, c) for c in range(num_colors)])
        for c1 in range(num_colors):
            for c2 in range(c1 + 1, num_colors):
                cnf.add_clause([-var(num_colors, node, c1), -var(num_colors, node, c2)])
    for a, b in edges:
        for c in range(num_colors):
            cnf.add_clause([-var(num_colors, a, c), -var(num_colors, b, c)])
    return cnf


def solve_coloring(
    num_nodes: int, edges: List[Tuple[int, int]], num_colors: int
) -> Tuple[Optional[Dict[int, int]], Stats]:
    cnf = encode(num_nodes, edges, num_colors)
    solver = Solver(cnf)
    model = solver.solve()
    if model is None:
        return None, solver.stats
    coloring: Dict[int, int] = {}
    for node in range(num_nodes):
        for c in range(num_colors):
            if model[var(num_colors, node, c)]:
                coloring[node] = c
    return coloring, solver.stats
