import time

from satsolver import CNF, Solver, Stats, random_3sat, pigeonhole
from satsolver.encoders.nqueens import solve_nqueens
from satsolver.encoders.graph_coloring import solve_coloring
from satsolver.encoders.sudoku import solve_sudoku


def print_stats(stats: Stats) -> None:
    print(
        f"  decisions={stats.decisions} propagations={stats.propagations} "
        f"conflicts={stats.conflicts} learned={stats.learned_clauses} "
        f"restarts={stats.restarts}"
    )


def verify_model(cnf: CNF, model) -> bool:
    for clause in cnf.clauses:
        satisfied = False
        for lit in clause:
            value = model[abs(lit)]
            if (lit > 0 and value) or (lit < 0 and not value):
                satisfied = True
                break
        if not satisfied:
            return False
    return True


def demo_random_sat() -> None:
    print("Random 3-SAT near the hard ratio (60 vars, ratio 4.2)")
    cnf = random_3sat(60, 4.2, seed=1)
    start = time.time()
    solver = Solver(cnf)
    model = solver.solve()
    elapsed = time.time() - start
    if model is None:
        print("  result=UNSAT")
    else:
        ok = verify_model(cnf, model)
        print(f"  result=SAT model_verified={ok} time={elapsed:.3f}s")
    print_stats(solver.stats)


def demo_pigeonhole() -> None:
    print("Pigeonhole principle (7 pigeons into 6 holes)")
    cnf = pigeonhole(7, 6)
    start = time.time()
    solver = Solver(cnf)
    model = solver.solve()
    elapsed = time.time() - start
    print(f"  result={'SAT' if model is not None else 'UNSAT'} time={elapsed:.3f}s")
    print_stats(solver.stats)


def print_board(board, n) -> None:
    for row in range(n):
        line = "".join("Q" if board[row][col] else "." for col in range(n))
        print(f"  {line}")


def demo_nqueens() -> None:
    n = 12
    print(f"N-Queens (n={n})")
    start = time.time()
    board, stats = solve_nqueens(n)
    elapsed = time.time() - start
    if board is None:
        print("  result=UNSAT")
    else:
        print_board(board, n)
        print(f"  solved in {elapsed:.3f}s")
    print_stats(stats)


def print_grid(grid) -> None:
    for r in range(9):
        if r % 3 == 0 and r != 0:
            print("  " + "-" * 21)
        row_cells = []
        for c in range(9):
            if c % 3 == 0 and c != 0:
                row_cells.append("|")
            row_cells.append(str(grid[r][c]))
        print("  " + " ".join(row_cells))


def demo_sudoku() -> None:
    print("Sudoku via CNF encoding")
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
    start = time.time()
    grid, stats = solve_sudoku(givens)
    elapsed = time.time() - start
    if grid is None:
        print("  result=UNSAT")
    else:
        print_grid(grid)
        print(f"  solved in {elapsed:.3f}s")
    print_stats(stats)


def demo_graph_coloring() -> None:
    print("Graph colouring (Petersen graph, 3 colours)")
    edges = [
        (0, 1), (1, 2), (2, 3), (3, 4), (4, 0),
        (0, 5), (1, 6), (2, 7), (3, 8), (4, 9),
        (5, 7), (7, 9), (9, 6), (6, 8), (8, 5),
    ]
    start = time.time()
    coloring, stats = solve_coloring(10, edges, 3)
    elapsed = time.time() - start
    if coloring is None:
        print("  result=UNSAT")
    else:
        print(f"  coloring={coloring} time={elapsed:.3f}s")
    print_stats(stats)


def main() -> None:
    demo_random_sat()
    print()
    demo_pigeonhole()
    print()
    demo_nqueens()
    print()
    demo_sudoku()
    print()
    demo_graph_coloring()


if __name__ == "__main__":
    main()
