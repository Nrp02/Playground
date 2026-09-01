import itertools
import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from satsolver import CNF, Solver, parse_dimacs, write_dimacs, random_3sat, pigeonhole
from satsolver.encoders.nqueens import solve_nqueens
from satsolver.encoders.graph_coloring import solve_coloring
from satsolver.encoders.sudoku import solve_sudoku


def clause_satisfied(clause, model):
    for lit in clause:
        value = model[abs(lit)]
        if (lit > 0 and value) or (lit < 0 and not value):
            return True
    return False


def model_satisfies_cnf(cnf, model):
    return all(clause_satisfied(c, model) for c in cnf.clauses)


def brute_force_sat(cnf):
    if any(len(c) == 0 for c in cnf.clauses):
        return False
    variables = list(range(1, cnf.num_vars + 1))
    if not variables:
        return True
    for bits in itertools.product([False, True], repeat=len(variables)):
        model = {v: b for v, b in zip(variables, bits)}
        if model_satisfies_cnf(cnf, model):
            return True
    return False


class TestDimacs(unittest.TestCase):
    def test_round_trip(self):
        cnf = CNF(3, [[1, -2, 3], [-1, 2], [3]])
        text = write_dimacs(cnf)
        parsed = parse_dimacs(text)
        self.assertEqual(parsed.num_vars, cnf.num_vars)
        self.assertEqual(parsed.clauses, cnf.clauses)

    def test_parse_ignores_comments(self):
        text = "c a comment\np cnf 2 2\n1 -2 0\nc another\n-1 2 0\n"
        cnf = parse_dimacs(text)
        self.assertEqual(cnf.num_vars, 2)
        self.assertEqual(cnf.clauses, [[1, -2], [-1, 2]])


class TestTrivialCases(unittest.TestCase):
    def test_empty_clause_set_is_sat(self):
        cnf = CNF(0, [])
        model = Solver(cnf).solve()
        self.assertEqual(model, {})

    def test_empty_clause_is_unsat(self):
        cnf = CNF(1, [[]])
        model = Solver(cnf).solve()
        self.assertIsNone(model)

    def test_simple_contradiction(self):
        cnf = CNF(1, [[1], [-1]])
        model = Solver(cnf).solve()
        self.assertIsNone(model)


class TestPigeonhole(unittest.TestCase):
    def test_pigeonhole_n_plus_one(self):
        for pigeons, holes in [(3, 2), (4, 3), (5, 4)]:
            cnf = pigeonhole(pigeons, holes)
            model = Solver(cnf).solve()
            self.assertIsNone(model, f"expected UNSAT for {pigeons} into {holes}")

    def test_pigeonhole_fits(self):
        cnf = pigeonhole(3, 3)
        model = Solver(cnf).solve()
        self.assertIsNotNone(model)
        self.assertTrue(model_satisfies_cnf(cnf, model))


class TestRandomInstancesAgainstBruteForce(unittest.TestCase):
    def test_small_random_instances(self):
        rng = random.Random(1234)
        for trial in range(60):
            num_vars = rng.randint(1, 7)
            num_clauses = rng.randint(0, 10)
            cnf = CNF(num_vars, [])
            for _ in range(num_clauses):
                length = rng.randint(1, min(3, num_vars))
                vars_chosen = rng.sample(range(1, num_vars + 1), length)
                clause = [v if rng.random() < 0.5 else -v for v in vars_chosen]
                cnf.add_clause(clause)
            expected_sat = brute_force_sat(cnf)
            model = Solver(cnf.copy()).solve()
            if expected_sat:
                self.assertIsNotNone(model, f"trial {trial}: expected SAT")
                self.assertTrue(model_satisfies_cnf(cnf, model), f"trial {trial}: bad model")
            else:
                self.assertIsNone(model, f"trial {trial}: expected UNSAT")

    def test_random_3sat_models_are_valid(self):
        for seed in range(1, 15):
            cnf = random_3sat(25, 3.5, seed=seed)
            model = Solver(cnf.copy()).solve()
            if model is not None:
                self.assertTrue(model_satisfies_cnf(cnf, model))


class TestUnitPropagation(unittest.TestCase):
    def test_chain_propagation(self):
        cnf = CNF(4, [[1], [-1, 2], [-2, 3], [-3, 4]])
        model = Solver(cnf).solve()
        self.assertIsNotNone(model)
        self.assertTrue(model[1] and model[2] and model[3] and model[4])

    def test_conflicting_unit_clauses(self):
        cnf = CNF(2, [[1], [-1, 2], [-2]])
        model = Solver(cnf).solve()
        self.assertIsNone(model)


class TestConflictAnalysis(unittest.TestCase):
    def test_learned_clause_is_asserting_and_falsified(self):
        cnf = pigeonhole(4, 3)
        solver = Solver(cnf)
        original_analyze = solver._analyze
        captured = []

        def spy(conflict):
            learned, backtrack_level = original_analyze(conflict)
            trail_levels = {abs(lit): solver.level[abs(lit)] for lit in learned}
            captured.append((list(learned), backtrack_level, dict(trail_levels), solver.decision_level()))
            return learned, backtrack_level

        solver._analyze = spy
        model = solver.solve()
        self.assertIsNone(model)
        self.assertTrue(len(captured) > 0)
        for learned, backtrack_level, trail_levels, conflict_level in captured:
            self.assertGreaterEqual(len(learned), 1)
            levels_in_clause = [trail_levels[abs(lit)] for lit in learned]
            max_level = max(levels_in_clause)
            count_at_max = sum(1 for lvl in levels_in_clause if lvl == max_level)
            self.assertEqual(count_at_max, 1)
            self.assertLessEqual(backtrack_level, max_level)


class TestEncoders(unittest.TestCase):
    def test_nqueens_no_attacks(self):
        for n in [4, 6, 8]:
            board, stats = solve_nqueens(n)
            self.assertIsNotNone(board)
            positions = [
                (r, c) for r in range(n) for c in range(n) if board[r][c]
            ]
            self.assertEqual(len(positions), n)
            rows = [p[0] for p in positions]
            cols = [p[1] for p in positions]
            self.assertEqual(len(set(rows)), n)
            self.assertEqual(len(set(cols)), n)
            for (r1, c1), (r2, c2) in itertools.combinations(positions, 2):
                self.assertNotEqual(abs(r1 - r2), abs(c1 - c2))

    def test_graph_coloring_no_monochromatic_edge(self):
        edges = [(0, 1), (1, 2), (2, 0), (2, 3)]
        coloring, stats = solve_coloring(4, edges, 3)
        self.assertIsNotNone(coloring)
        for a, b in edges:
            self.assertNotEqual(coloring[a], coloring[b])

    def test_graph_coloring_unsat_when_too_few_colors(self):
        edges = [(0, 1), (1, 2), (2, 0)]
        coloring, stats = solve_coloring(3, edges, 2)
        self.assertIsNone(coloring)

    def test_sudoku_valid_grid_matches_givens(self):
        givens = [
            [5, 3, 0, 0, 7, 0, 0, 0, 0],
            [6, 0, 0, 1, 9, 5, 0, 0, 0],
            [0, 9, 8, 0, 0, 0, 0, 6, 0],
            [8, 0, 0, 0, 6, 0, 0, 0, 3],
            [4, 0, 0, 8, 0, 3, 0, 0, 1],
            [7, 0, 0, 0, 2, 0, 0, 0, 6],
            [0, 6, 0, 0, 0, 0, 2, 8, 0],
            [0, 0, 0, 4, 1, 9, 0, 0, 5],
            [0, 0, 0, 0, 8, 0, 0, 7, 9],
        ]
        grid, stats = solve_sudoku(givens)
        self.assertIsNotNone(grid)
        for r in range(9):
            for c in range(9):
                if givens[r][c] != 0:
                    self.assertEqual(grid[r][c], givens[r][c])
        for r in range(9):
            self.assertEqual(sorted(grid[r]), list(range(1, 10)))
        for c in range(9):
            column = [grid[r][c] for r in range(9)]
            self.assertEqual(sorted(column), list(range(1, 10)))
        for br in range(3):
            for bc in range(3):
                block = [
                    grid[br * 3 + dr][bc * 3 + dc]
                    for dr in range(3)
                    for dc in range(3)
                ]
                self.assertEqual(sorted(block), list(range(1, 10)))


class TestBackjump(unittest.TestCase):
    def test_backtrack_level_never_exceeds_conflict_level(self):
        cnf = random_3sat(20, 4.5, seed=99)
        solver = Solver(cnf)
        original_backtrack = solver._backtrack
        levels_seen = []

        def spy(level):
            levels_seen.append((level, solver.decision_level()))
            original_backtrack(level)

        solver._backtrack = spy
        solver.solve()
        for level, before in levels_seen:
            self.assertLessEqual(level, before)


if __name__ == "__main__":
    unittest.main()
