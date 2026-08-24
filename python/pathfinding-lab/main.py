from __future__ import annotations

import argparse

from generators import GENERATORS
from solvers import SOLVERS
from visualize import render, render_table


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--generator", choices=list(GENERATORS), default="dfs")
    p.add_argument("--size", nargs=2, type=int, metavar=("WIDTH", "HEIGHT"), default=[21, 11])
    p.add_argument("--solve", choices=list(SOLVERS) + ["all"], default="all")
    p.add_argument("--seed", type=int, default=None)
    return p.parse_args()


def main():
    args = parse_args()
    width, height = args.size
    grid = GENERATORS[args.generator](width, height, args.seed)

    assert grid.is_connected(), "generated maze is not fully connected"

    start = (0, 0)
    goal = (width - 1, height - 1)

    print(f"generator={args.generator} size={width}x{height} seed={args.seed}")
    print()
    print(render(grid))
    print()

    names = list(SOLVERS) if args.solve == "all" else [args.solve]
    results = [SOLVERS[name](grid, start, goal) for name in names]
    print(render_table(results))
    print()
    print(render(grid, results[0].path))


if __name__ == "__main__":
    main()
