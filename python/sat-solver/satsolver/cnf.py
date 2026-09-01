from __future__ import annotations

import random
from dataclasses import dataclass, field
from typing import Dict, List, Tuple


@dataclass
class CNF:
    num_vars: int
    clauses: List[List[int]] = field(default_factory=list)

    def add_clause(self, lits: List[int]) -> None:
        self.clauses.append(list(lits))

    def copy(self) -> "CNF":
        return CNF(self.num_vars, [list(c) for c in self.clauses])


def parse_dimacs(text: str) -> CNF:
    num_vars = 0
    clauses: List[List[int]] = []
    current: List[int] = []
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("c"):
            continue
        if line.startswith("p"):
            parts = line.split()
            num_vars = int(parts[2])
            continue
        for token in line.split():
            value = int(token)
            if value == 0:
                clauses.append(current)
                current = []
            else:
                current.append(value)
    if current:
        clauses.append(current)
    return CNF(num_vars, clauses)


def write_dimacs(cnf: CNF) -> str:
    lines = [f"p cnf {cnf.num_vars} {len(cnf.clauses)}"]
    for clause in cnf.clauses:
        lines.append(" ".join(str(lit) for lit in clause) + " 0")
    return "\n".join(lines) + "\n"


def simplify(cnf: CNF) -> Tuple[List[List[int]], List[int], bool]:
    clauses: List[List[int]] = []
    for clause in cnf.clauses:
        seen_pos = set()
        seen_neg = set()
        deduped: List[int] = []
        tautology = False
        for lit in clause:
            v = abs(lit)
            if lit > 0:
                if v in seen_neg:
                    tautology = True
                    break
                if v not in seen_pos:
                    seen_pos.add(v)
                    deduped.append(lit)
            else:
                if v in seen_pos:
                    tautology = True
                    break
                if v not in seen_neg:
                    seen_neg.add(v)
                    deduped.append(lit)
        if tautology:
            continue
        clauses.append(deduped)

    for clause in clauses:
        if len(clause) == 0:
            return clauses, [], True

    forced: List[int] = []
    changed = True
    while changed:
        changed = False
        polarity: Dict[int, int] = {}
        for clause in clauses:
            for lit in clause:
                v = abs(lit)
                sign = 1 if lit > 0 else -1
                if v not in polarity:
                    polarity[v] = sign
                elif polarity[v] != sign:
                    polarity[v] = 0
        pure_vars = {v: s for v, s in polarity.items() if s != 0}
        if not pure_vars:
            break
        new_clauses = []
        removed_any = False
        for clause in clauses:
            satisfied = False
            for lit in clause:
                v = abs(lit)
                if v in pure_vars and (1 if lit > 0 else -1) == pure_vars[v]:
                    satisfied = True
                    break
            if satisfied:
                removed_any = True
                continue
            new_clauses.append(clause)
        if removed_any:
            for v, s in pure_vars.items():
                forced.append(v * s)
            clauses = new_clauses
            changed = True
    return clauses, forced, False


def random_3sat(num_vars: int, ratio: float, seed: int) -> CNF:
    rng = random.Random(seed)
    num_clauses = int(round(num_vars * ratio))
    cnf = CNF(num_vars)
    for _ in range(num_clauses):
        chosen = rng.sample(range(1, num_vars + 1), 3)
        clause = [v if rng.random() < 0.5 else -v for v in chosen]
        cnf.add_clause(clause)
    return cnf


def pigeonhole(pigeons: int, holes: int) -> CNF:
    def var(p: int, h: int) -> int:
        return p * holes + h + 1

    cnf = CNF(pigeons * holes)
    for p in range(pigeons):
        cnf.add_clause([var(p, h) for h in range(holes)])
    for h in range(holes):
        for p1 in range(pigeons):
            for p2 in range(p1 + 1, pigeons):
                cnf.add_clause([-var(p1, h), -var(p2, h)])
    return cnf
