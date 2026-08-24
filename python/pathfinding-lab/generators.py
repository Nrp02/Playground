from __future__ import annotations

import random

from grid import Grid


def generate_dfs(width: int, height: int, seed: int | None = None) -> Grid:
    rng = random.Random(seed)
    grid = Grid(width, height)
    start = (0, 0)
    visited = {start}
    stack = [start]
    while stack:
        cur = stack[-1]
        candidates = []
        for d in ((0, -1), (0, 1), (1, 0), (-1, 0)):
            n = (cur[0] + d[0], cur[1] + d[1])
            if grid.in_bounds(n) and n not in visited:
                candidates.append(n)
        if not candidates:
            stack.pop()
            continue
        nxt = rng.choice(candidates)
        grid.carve(cur, nxt)
        visited.add(nxt)
        stack.append(nxt)
    return grid


class DisjointSet:
    def __init__(self, items):
        self.parent = {item: item for item in items}
        self.rank = {item: 0 for item in items}

    def find(self, item):
        root = item
        while self.parent[root] != root:
            root = self.parent[root]
        while self.parent[item] != root:
            self.parent[item], item = root, self.parent[item]
        return root

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra == rb:
            return False
        if self.rank[ra] < self.rank[rb]:
            ra, rb = rb, ra
        self.parent[rb] = ra
        if self.rank[ra] == self.rank[rb]:
            self.rank[ra] += 1
        return True


def generate_kruskal(width: int, height: int, seed: int | None = None) -> Grid:
    rng = random.Random(seed)
    grid = Grid(width, height)
    cells = list(grid.all_cells())
    dsu = DisjointSet(cells)
    edges = list(grid.all_adjacent_pairs())
    rng.shuffle(edges)
    for a, b in edges:
        if dsu.union(a, b):
            grid.carve(a, b)
    return grid


GENERATORS = {
    "dfs": generate_dfs,
    "kruskal": generate_kruskal,
}
