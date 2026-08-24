from __future__ import annotations

import heapq
from collections import deque
from dataclasses import dataclass

from grid import Grid


@dataclass
class SolveResult:
    name: str
    path: list
    expanded: int

    @property
    def path_length(self):
        return len(self.path) - 1 if self.path else -1


def reconstruct(came_from, start, goal):
    if goal not in came_from and goal != start:
        return []
    path = [goal]
    cur = goal
    while cur != start:
        cur = came_from[cur]
        path.append(cur)
    path.reverse()
    return path


def solve_bfs(grid: Grid, start, goal) -> SolveResult:
    frontier = deque([start])
    came_from = {}
    seen = {start}
    expanded = 0
    while frontier:
        cur = frontier.popleft()
        expanded += 1
        if cur == goal:
            break
        for n in grid.open_neighbors(cur):
            if n not in seen:
                seen.add(n)
                came_from[n] = cur
                frontier.append(n)
    return SolveResult("BFS", reconstruct(came_from, start, goal), expanded)


def solve_dfs(grid: Grid, start, goal) -> SolveResult:
    stack = [start]
    came_from = {}
    seen = {start}
    expanded = 0
    while stack:
        cur = stack.pop()
        expanded += 1
        if cur == goal:
            break
        for n in grid.open_neighbors(cur):
            if n not in seen:
                seen.add(n)
                came_from[n] = cur
                stack.append(n)
    return SolveResult("DFS", reconstruct(came_from, start, goal), expanded)


def solve_dijkstra(grid: Grid, start, goal) -> SolveResult:
    dist = {start: 0}
    came_from = {}
    pq = [(0, start)]
    visited = set()
    expanded = 0
    while pq:
        d, cur = heapq.heappop(pq)
        if cur in visited:
            continue
        visited.add(cur)
        expanded += 1
        if cur == goal:
            break
        for n in grid.open_neighbors(cur):
            nd = d + 1
            if n not in dist or nd < dist[n]:
                dist[n] = nd
                came_from[n] = cur
                heapq.heappush(pq, (nd, n))
    return SolveResult("Dijkstra", reconstruct(came_from, start, goal), expanded)


def manhattan(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def solve_astar(grid: Grid, start, goal) -> SolveResult:
    g_score = {start: 0}
    came_from = {}
    pq = [(manhattan(start, goal), start)]
    visited = set()
    expanded = 0
    while pq:
        _, cur = heapq.heappop(pq)
        if cur in visited:
            continue
        visited.add(cur)
        expanded += 1
        if cur == goal:
            break
        for n in grid.open_neighbors(cur):
            ng = g_score[cur] + 1
            if n not in g_score or ng < g_score[n]:
                g_score[n] = ng
                came_from[n] = cur
                heapq.heappush(pq, (ng + manhattan(n, goal), n))
    return SolveResult("A*", reconstruct(came_from, start, goal), expanded)


SOLVERS = {
    "bfs": solve_bfs,
    "dfs": solve_dfs,
    "dijkstra": solve_dijkstra,
    "astar": solve_astar,
}
