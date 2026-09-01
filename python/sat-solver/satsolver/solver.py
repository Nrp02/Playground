from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
from typing import Dict, List, Optional

from .cnf import CNF, simplify


class Clause:
    __slots__ = ("lits", "learned", "activity")

    def __init__(self, lits: List[int], learned: bool):
        self.lits = lits
        self.learned = learned
        self.activity = 0.0


@dataclass
class Stats:
    decisions: int = 0
    propagations: int = 0
    conflicts: int = 0
    learned_clauses: int = 0
    restarts: int = 0


class Solver:
    def __init__(self, cnf: CNF):
        self.num_vars = cnf.num_vars
        self.assign: List[int] = [0] * (self.num_vars + 1)
        self.level: List[int] = [0] * (self.num_vars + 1)
        self.reason: List[Optional[Clause]] = [None] * (self.num_vars + 1)
        self.trail: List[int] = []
        self.trail_lim: List[int] = []
        self.watches: Dict[int, List[Clause]] = defaultdict(list)
        self.learned_clauses: List[Clause] = []
        self.activity: List[float] = [0.0] * (self.num_vars + 1)
        self.var_inc = 1.0
        self.var_decay = 0.95
        self.clause_inc = 1.0
        self.clause_decay = 0.999
        self.qhead = 0
        self.ok = True
        self.stats = Stats()
        self._restart_index = 1
        self._restart_threshold = self._luby(self._restart_index) * 48
        self._conflicts_since_restart = 0
        self._conflicts_since_reduce = 0
        self._reduce_threshold = 512

        clauses, forced, trivially_unsat = simplify(cnf)
        if trivially_unsat:
            self.ok = False
        for lit in forced:
            if not self._enqueue(lit, None):
                self.ok = False
        for lits in clauses:
            if len(lits) == 0:
                self.ok = False
                continue
            clause = self._add_clause(lits, learned=False)
            if len(lits) == 1:
                if not self._enqueue(lits[0], clause):
                    self.ok = False

    def solve(self) -> Optional[Dict[int, bool]]:
        if not self.ok:
            return None
        while True:
            conflict = self._propagate()
            if conflict is not None:
                self.stats.conflicts += 1
                self._conflicts_since_restart += 1
                self._conflicts_since_reduce += 1
                if self.decision_level() == 0:
                    return None
                learned_lits, backtrack_level = self._analyze(conflict)
                self._backtrack(backtrack_level)
                clause = self._add_clause(learned_lits, learned=True)
                self._enqueue(learned_lits[0], clause)
                self._decay_var_activity()
                self._decay_clause_activity()
                if self._conflicts_since_reduce >= self._reduce_threshold:
                    self._reduce_learned()
                    self._conflicts_since_reduce = 0
                    self._reduce_threshold += 512
                continue
            if len(self.trail) == self.num_vars:
                return self._extract_model()
            if self._conflicts_since_restart >= self._restart_threshold:
                self.stats.restarts += 1
                self._backtrack(0)
                self._restart_index += 1
                self._restart_threshold = self._luby(self._restart_index) * 48
                self._conflicts_since_restart = 0
                continue
            var = self._pick_branch_var()
            if var is None:
                return self._extract_model()
            self.stats.decisions += 1
            self.trail_lim.append(len(self.trail))
            self._enqueue(var, None)

    def decision_level(self) -> int:
        return len(self.trail_lim)

    def _is_true(self, lit: int) -> bool:
        v = abs(lit)
        a = self.assign[v]
        return a != 0 and (a > 0) == (lit > 0)

    def _is_false(self, lit: int) -> bool:
        v = abs(lit)
        a = self.assign[v]
        return a != 0 and (a > 0) != (lit > 0)

    def _enqueue(self, lit: int, reason: Optional[Clause]) -> bool:
        v = abs(lit)
        val = 1 if lit > 0 else -1
        if self.assign[v] != 0:
            return self.assign[v] == val
        self.assign[v] = val
        self.level[v] = self.decision_level()
        self.reason[v] = reason
        self.trail.append(lit)
        return True

    def _add_clause(self, lits: List[int], learned: bool) -> Clause:
        clause = Clause(list(lits), learned)
        if learned:
            self.learned_clauses.append(clause)
            self.stats.learned_clauses += 1
        if len(clause.lits) >= 2:
            self.watches[clause.lits[0]].append(clause)
            self.watches[clause.lits[1]].append(clause)
        return clause

    def _propagate(self) -> Optional[Clause]:
        while self.qhead < len(self.trail):
            lit = self.trail[self.qhead]
            self.qhead += 1
            self.stats.propagations += 1
            neg = -lit
            watch_list = self.watches.get(neg, [])
            self.watches[neg] = []
            n = len(watch_list)
            i = 0
            keep: List[Clause] = []
            conflict: Optional[Clause] = None
            while i < n:
                clause = watch_list[i]
                i += 1
                lits = clause.lits
                if lits[0] == neg:
                    lits[0], lits[1] = lits[1], lits[0]
                other = lits[0]
                if self._is_true(other):
                    keep.append(clause)
                    continue
                found = False
                for k in range(2, len(lits)):
                    candidate = lits[k]
                    if not self._is_false(candidate):
                        lits[1], lits[k] = lits[k], lits[1]
                        self.watches[lits[1]].append(clause)
                        found = True
                        break
                if found:
                    continue
                keep.append(clause)
                if self._is_false(other):
                    conflict = clause
                    keep.extend(watch_list[i:])
                    break
                self._enqueue(other, clause)
            self.watches[neg] = keep
            if conflict is not None:
                return conflict
        return None

    def _backtrack(self, level: int) -> None:
        if self.decision_level() <= level:
            return
        start = self.trail_lim[level]
        for i in range(len(self.trail) - 1, start - 1, -1):
            v = abs(self.trail[i])
            self.assign[v] = 0
            self.reason[v] = None
            self.level[v] = 0
        del self.trail[start:]
        del self.trail_lim[level:]
        self.qhead = len(self.trail)

    def _pick_branch_var(self) -> Optional[int]:
        best_var = None
        best_activity = -1.0
        for v in range(1, self.num_vars + 1):
            if self.assign[v] == 0 and self.activity[v] > best_activity:
                best_activity = self.activity[v]
                best_var = v
        return best_var

    def _bump_var_activity(self, v: int) -> None:
        self.activity[v] += self.var_inc
        if self.activity[v] > 1e100:
            for i in range(1, self.num_vars + 1):
                self.activity[i] *= 1e-100
            self.var_inc *= 1e-100

    def _decay_var_activity(self) -> None:
        self.var_inc /= self.var_decay

    def _bump_clause_activity(self, clause: Clause) -> None:
        if not clause.learned:
            return
        clause.activity += self.clause_inc
        if clause.activity > 1e100:
            for c in self.learned_clauses:
                c.activity *= 1e-100
            self.clause_inc *= 1e-100

    def _decay_clause_activity(self) -> None:
        self.clause_inc /= self.clause_decay

    def _analyze(self, conflict: Clause):
        seen = [False] * (self.num_vars + 1)
        learned = [0]
        counter = 0
        idx = len(self.trail) - 1
        p: Optional[int] = None
        p_reason = conflict
        current_level = self.decision_level()
        while True:
            self._bump_clause_activity(p_reason)
            for lit in p_reason.lits:
                v = abs(lit)
                if p is not None and v == abs(p):
                    continue
                if seen[v]:
                    continue
                seen[v] = True
                self._bump_var_activity(v)
                if self.level[v] == current_level:
                    counter += 1
                elif self.level[v] > 0:
                    learned.append(lit)
            while not seen[abs(self.trail[idx])]:
                idx -= 1
            p = self.trail[idx]
            idx -= 1
            v = abs(p)
            seen[v] = False
            counter -= 1
            if counter == 0:
                break
            p_reason = self.reason[v]
        learned[0] = -p
        backtrack_level = 0
        if len(learned) > 1:
            max_idx = 1
            max_level = self.level[abs(learned[1])]
            for i in range(2, len(learned)):
                lvl = self.level[abs(learned[i])]
                if lvl > max_level:
                    max_level = lvl
                    max_idx = i
            learned[1], learned[max_idx] = learned[max_idx], learned[1]
            backtrack_level = max_level
        return learned, backtrack_level

    def _remove_clause_watches(self, clause: Clause) -> None:
        lits = clause.lits
        if len(lits) < 2:
            return
        for lit in (lits[0], lits[1]):
            lst = self.watches.get(lit)
            if not lst:
                continue
            for i, c in enumerate(lst):
                if c is clause:
                    del lst[i]
                    break

    def _reduce_learned(self) -> None:
        locked = set()
        for v in range(1, self.num_vars + 1):
            if self.assign[v] != 0 and self.reason[v] is not None:
                locked.add(id(self.reason[v]))
        candidates = [
            c for c in self.learned_clauses
            if id(c) not in locked and len(c.lits) > 2
        ]
        candidates.sort(key=lambda c: c.activity)
        remove_count = len(candidates) // 2
        if remove_count == 0:
            return
        to_remove = set(id(c) for c in candidates[:remove_count])
        new_learned = []
        for c in self.learned_clauses:
            if id(c) in to_remove:
                self._remove_clause_watches(c)
            else:
                new_learned.append(c)
        self.learned_clauses = new_learned

    def _extract_model(self) -> Dict[int, bool]:
        return {v: self.assign[v] > 0 for v in range(1, self.num_vars + 1)}

    def _luby(self, i: int) -> int:
        k = 1
        while (1 << k) - 1 < i:
            k += 1
        if i == (1 << k) - 1:
            return 1 << (k - 1)
        return self._luby(i - (1 << (k - 1)) + 1)
