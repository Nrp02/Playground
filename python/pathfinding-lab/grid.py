from __future__ import annotations

NORTH = (0, -1)
SOUTH = (0, 1)
EAST = (1, 0)
WEST = (-1, 0)
DIRECTIONS = (NORTH, SOUTH, EAST, WEST)


class Grid:
    def __init__(self, width: int, height: int):
        self.width = width
        self.height = height
        self.walls = {}
        for y in range(height):
            for x in range(width):
                self.walls[(x, y)] = {d: True for d in DIRECTIONS}

    def in_bounds(self, cell):
        x, y = cell
        return 0 <= x < self.width and 0 <= y < self.height

    def neighbor(self, cell, direction):
        x, y = cell
        dx, dy = direction
        return (x + dx, y + dy)

    def opposite(self, direction):
        dx, dy = direction
        return (-dx, -dy)

    def carve(self, a, b):
        for d in DIRECTIONS:
            if self.neighbor(a, d) == b:
                self.walls[a][d] = False
                self.walls[b][self.opposite(d)] = False
                return
        raise ValueError("cells are not grid-adjacent")

    def has_wall(self, a, direction):
        return self.walls[a][direction]

    def open_neighbors(self, cell):
        result = []
        for d in DIRECTIONS:
            if not self.walls[cell][d]:
                n = self.neighbor(cell, d)
                if self.in_bounds(n):
                    result.append(n)
        return result

    def all_cells(self):
        for y in range(self.height):
            for x in range(self.width):
                yield (x, y)

    def all_adjacent_pairs(self):
        for cell in self.all_cells():
            for d in (EAST, SOUTH):
                n = self.neighbor(cell, d)
                if self.in_bounds(n):
                    yield cell, n

    def is_connected(self):
        start = (0, 0)
        seen = {start}
        stack = [start]
        while stack:
            cur = stack.pop()
            for n in self.open_neighbors(cur):
                if n not in seen:
                    seen.add(n)
                    stack.append(n)
        return len(seen) == self.width * self.height
