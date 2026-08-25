from typing import List, Optional, Set, Tuple

Board = List[List[int]]

SIZE = 9
BOX = 3


def _box_index(row: int, col: int) -> int:
    return (row // BOX) * BOX + (col // BOX)


def is_valid_board(board: Board) -> bool:
    if len(board) != SIZE or any(len(row) != SIZE for row in board):
        return False
    rows: List[Set[int]] = [set() for _ in range(SIZE)]
    cols: List[Set[int]] = [set() for _ in range(SIZE)]
    boxes: List[Set[int]] = [set() for _ in range(SIZE)]
    for r in range(SIZE):
        for c in range(SIZE):
            value = board[r][c]
            if value == 0:
                continue
            if not (1 <= value <= 9):
                return False
            b = _box_index(r, c)
            if value in rows[r] or value in cols[c] or value in boxes[b]:
                return False
            rows[r].add(value)
            cols[c].add(value)
            boxes[b].add(value)
    return True


def _build_candidate_sets(board: Board) -> Tuple[List[Set[int]], List[Set[int]], List[Set[int]]]:
    rows: List[Set[int]] = [set() for _ in range(SIZE)]
    cols: List[Set[int]] = [set() for _ in range(SIZE)]
    boxes: List[Set[int]] = [set() for _ in range(SIZE)]
    for r in range(SIZE):
        for c in range(SIZE):
            value = board[r][c]
            if value == 0:
                continue
            b = _box_index(r, c)
            rows[r].add(value)
            cols[c].add(value)
            boxes[b].add(value)
    return rows, cols, boxes


def _candidates(rows: List[Set[int]], cols: List[Set[int]], boxes: List[Set[int]], row: int, col: int) -> Set[int]:
    b = _box_index(row, col)
    used = rows[row] | cols[col] | boxes[b]
    return {v for v in range(1, 10) if v not in used}


def _find_best_empty_cell(
    board: Board,
    rows: List[Set[int]],
    cols: List[Set[int]],
    boxes: List[Set[int]],
) -> Optional[Tuple[int, int, Set[int]]]:
    best: Optional[Tuple[int, int, Set[int]]] = None
    for r in range(SIZE):
        for c in range(SIZE):
            if board[r][c] != 0:
                continue
            candidates = _candidates(rows, cols, boxes, r, c)
            if len(candidates) == 0:
                return (r, c, candidates)
            if best is None or len(candidates) < len(best[2]):
                best = (r, c, candidates)
                if len(candidates) == 1:
                    return best
    return best


def _solve_recursive(
    board: Board,
    rows: List[Set[int]],
    cols: List[Set[int]],
    boxes: List[Set[int]],
    count_solutions: bool,
    solutions_found: List[int],
    solution_limit: int,
) -> bool:
    cell = _find_best_empty_cell(board, rows, cols, boxes)
    if cell is None:
        solutions_found[0] += 1
        return True
    r, c, candidates = cell
    if not candidates:
        return False
    b = _box_index(r, c)
    for value in candidates:
        board[r][c] = value
        rows[r].add(value)
        cols[c].add(value)
        boxes[b].add(value)
        solved = _solve_recursive(board, rows, cols, boxes, count_solutions, solutions_found, solution_limit)
        if solved and not count_solutions:
            return True
        if count_solutions and solutions_found[0] >= solution_limit:
            board[r][c] = 0
            rows[r].discard(value)
            cols[c].discard(value)
            boxes[b].discard(value)
            return True
        board[r][c] = 0
        rows[r].discard(value)
        cols[c].discard(value)
        boxes[b].discard(value)
    return False


def solve(board: Board) -> Optional[Board]:
    if not is_valid_board(board):
        return None
    working = [row[:] for row in board]
    rows, cols, boxes = _build_candidate_sets(working)
    solutions_found = [0]
    solved = _solve_recursive(working, rows, cols, boxes, False, solutions_found, 1)
    if not solved:
        return None
    return working


def count_solutions(board: Board, limit: int = 2) -> int:
    if not is_valid_board(board):
        return 0
    working = [row[:] for row in board]
    rows, cols, boxes = _build_candidate_sets(working)
    solutions_found = [0]
    _solve_recursive(working, rows, cols, boxes, True, solutions_found, limit)
    return solutions_found[0]


def has_unique_solution(board: Board) -> bool:
    return count_solutions(board, limit=2) == 1
