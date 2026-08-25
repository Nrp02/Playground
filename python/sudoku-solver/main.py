import time

from sudoku import generate_puzzle, is_valid_board, solve

HARD_PUZZLES = [
    (
        "17-clue puzzle",
        [
            [0, 0, 0, 8, 0, 1, 0, 0, 0],
            [0, 0, 0, 0, 0, 0, 0, 4, 3],
            [5, 0, 0, 0, 0, 0, 0, 0, 0],
            [0, 0, 0, 0, 7, 0, 8, 0, 0],
            [0, 0, 0, 0, 0, 0, 1, 0, 0],
            [0, 2, 0, 0, 3, 0, 0, 0, 0],
            [6, 0, 0, 0, 0, 0, 0, 7, 5],
            [0, 0, 3, 4, 0, 0, 0, 0, 0],
            [0, 0, 0, 2, 0, 0, 6, 0, 0],
        ],
    ),
    (
        "AI Escargot",
        [
            [1, 0, 0, 0, 0, 7, 0, 9, 0],
            [0, 3, 0, 0, 2, 0, 0, 0, 8],
            [0, 0, 9, 6, 0, 0, 5, 0, 0],
            [0, 0, 5, 3, 0, 0, 9, 0, 0],
            [0, 1, 0, 0, 8, 0, 0, 0, 2],
            [6, 0, 0, 0, 0, 4, 0, 0, 0],
            [3, 0, 0, 0, 0, 0, 0, 1, 0],
            [0, 4, 1, 0, 0, 0, 0, 0, 7],
            [0, 0, 7, 0, 0, 0, 3, 0, 0],
        ],
    ),
]


def print_board(board):
    for r, row in enumerate(board):
        if r % 3 == 0 and r != 0:
            print("-" * 21)
        line = []
        for c, value in enumerate(row):
            if c % 3 == 0 and c != 0:
                line.append("|")
            line.append(str(value) if value != 0 else ".")
        print(" ".join(line))


def main() -> None:
    print("Generating a random puzzle...")
    puzzle, solution = generate_puzzle(min_givens=28)
    givens = sum(1 for row in puzzle for v in row if v != 0)
    print(f"Generated puzzle with {givens} givens:")
    print_board(puzzle)

    start = time.perf_counter()
    solved = solve(puzzle)
    elapsed = time.perf_counter() - start
    print(f"\nSolved in {elapsed * 1000:.2f} ms:")
    print_board(solved)
    print(f"Matches known solution: {solved == solution}")

    for name, board in HARD_PUZZLES:
        print(f"\nSolving hand-coded hard puzzle: {name}")
        print_board(board)
        start = time.perf_counter()
        result = solve(board)
        elapsed = time.perf_counter() - start
        if result is None:
            print("No solution found.")
        else:
            print(f"Solved in {elapsed * 1000:.2f} ms:")
            print_board(result)
            print(f"Valid solved board: {is_valid_board(result)}")


if __name__ == "__main__":
    main()
