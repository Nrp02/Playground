from .nqueens import encode as encode_nqueens, solve_nqueens
from .graph_coloring import encode as encode_coloring, solve_coloring
from .sudoku import encode as encode_sudoku, solve_sudoku

__all__ = [
    "encode_nqueens",
    "solve_nqueens",
    "encode_coloring",
    "solve_coloring",
    "encode_sudoku",
    "solve_sudoku",
]
