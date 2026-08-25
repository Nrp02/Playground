import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from sudoku.generator import generate_puzzle
from sudoku.solver import count_solutions, has_unique_solution, is_valid_board, solve

KNOWN_PUZZLE = [
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

KNOWN_SOLUTION = [
    [5, 3, 4, 6, 7, 8, 9, 1, 2],
    [6, 7, 2, 1, 9, 5, 3, 4, 8],
    [1, 9, 8, 3, 4, 2, 5, 6, 7],
    [8, 5, 9, 7, 6, 1, 4, 2, 3],
    [4, 2, 6, 8, 5, 3, 7, 9, 1],
    [7, 1, 3, 9, 2, 4, 8, 5, 6],
    [9, 6, 1, 5, 3, 7, 2, 8, 4],
    [2, 8, 7, 4, 1, 9, 6, 3, 5],
    [3, 4, 5, 2, 8, 6, 1, 7, 9],
]


class TestSolve(unittest.TestCase):
    def test_solves_known_puzzle(self):
        result = solve(KNOWN_PUZZLE)
        self.assertEqual(result, KNOWN_SOLUTION)

    def test_original_board_not_mutated_incorrectly(self):
        original = [row[:] for row in KNOWN_PUZZLE]
        solve(KNOWN_PUZZLE)
        self.assertEqual(KNOWN_PUZZLE, original)

    def test_unsolvable_board_returns_none(self):
        board = [row[:] for row in KNOWN_PUZZLE]
        board[0][2] = 5
        self.assertIsNone(solve(board))

    def test_invalid_board_shape_returns_none(self):
        board = [[0] * 9 for _ in range(8)]
        self.assertIsNone(solve(board))


class TestIsValidBoard(unittest.TestCase):
    def test_valid_partial_board(self):
        self.assertTrue(is_valid_board(KNOWN_PUZZLE))

    def test_valid_full_solution(self):
        self.assertTrue(is_valid_board(KNOWN_SOLUTION))

    def test_duplicate_in_row_invalid(self):
        board = [row[:] for row in KNOWN_SOLUTION]
        board[0][1] = board[0][0]
        self.assertFalse(is_valid_board(board))

    def test_duplicate_in_column_invalid(self):
        board = [row[:] for row in KNOWN_SOLUTION]
        board[1][0] = board[0][0]
        self.assertFalse(is_valid_board(board))

    def test_duplicate_in_box_invalid(self):
        board = [row[:] for row in KNOWN_SOLUTION]
        board[1][1] = board[0][0]
        self.assertFalse(is_valid_board(board))

    def test_out_of_range_value_invalid(self):
        board = [row[:] for row in KNOWN_SOLUTION]
        board[0][0] = 10
        self.assertFalse(is_valid_board(board))


class TestSolutionCounting(unittest.TestCase):
    def test_known_puzzle_has_unique_solution(self):
        self.assertTrue(has_unique_solution(KNOWN_PUZZLE))
        self.assertEqual(count_solutions(KNOWN_PUZZLE, limit=5), 1)

    def test_empty_board_has_many_solutions(self):
        board = [[0] * 9 for _ in range(9)]
        self.assertGreaterEqual(count_solutions(board, limit=2), 2)


class TestGenerator(unittest.TestCase):
    def test_generated_puzzle_has_unique_solution(self):
        puzzle, solution = generate_puzzle(min_givens=30)
        self.assertTrue(is_valid_board(solution))
        self.assertTrue(is_valid_board(puzzle))
        self.assertTrue(has_unique_solution(puzzle))
        solved = solve(puzzle)
        self.assertEqual(solved, solution)

    def test_generated_solution_is_fully_filled(self):
        _, solution = generate_puzzle(min_givens=30)
        for row in solution:
            self.assertTrue(all(1 <= v <= 9 for v in row))

    def test_generated_puzzle_respects_min_givens(self):
        puzzle, _ = generate_puzzle(min_givens=35)
        givens = sum(1 for row in puzzle for v in row if v != 0)
        self.assertGreaterEqual(givens, 35)


if __name__ == "__main__":
    unittest.main()
