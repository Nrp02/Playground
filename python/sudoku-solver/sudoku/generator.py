import random
from typing import List, Tuple

from .solver import Board, SIZE, has_unique_solution


def _shuffled_values() -> List[int]:
    values = list(range(1, 10))
    random.shuffle(values)
    return values


def _fill_full_grid() -> Board:
    board: Board = [[0] * SIZE for _ in range(SIZE)]

    def backtrack(pos: int) -> bool:
        if pos == SIZE * SIZE:
            return True
        row, col = divmod(pos, SIZE)
        for value in _shuffled_values():
            if _is_placement_valid(board, row, col, value):
                board[row][col] = value
                if backtrack(pos + 1):
                    return True
                board[row][col] = 0
        return False

    backtrack(0)
    return board


def _is_placement_valid(board: Board, row: int, col: int, value: int) -> bool:
    for i in range(SIZE):
        if board[row][i] == value or board[i][col] == value:
            return False
    box_row = (row // 3) * 3
    box_col = (col // 3) * 3
    for r in range(box_row, box_row + 3):
        for c in range(box_col, box_col + 3):
            if board[r][c] == value:
                return False
    return True


def generate_puzzle(min_givens: int = 25) -> Tuple[Board, Board]:
    solution = _fill_full_grid()
    puzzle = [row[:] for row in solution]
    cells = [(r, c) for r in range(SIZE) for c in range(SIZE)]
    random.shuffle(cells)
    givens = SIZE * SIZE

    for row, col in cells:
        if givens <= min_givens:
            break
        removed_value = puzzle[row][col]
        if removed_value == 0:
            continue
        puzzle[row][col] = 0
        if has_unique_solution(puzzle):
            givens -= 1
        else:
            puzzle[row][col] = removed_value

    return puzzle, solution
