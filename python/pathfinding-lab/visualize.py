from __future__ import annotations

from grid import Grid, EAST, SOUTH


def render(grid: Grid, path=None) -> str:
    path_set = set(path) if path else set()
    cw = grid.width * 2 + 1
    ch = grid.height * 2 + 1
    canvas = [["#"] * cw for _ in range(ch)]

    for x, y in grid.all_cells():
        cx, cy = x * 2 + 1, y * 2 + 1
        canvas[cy][cx] = "." if (x, y) in path_set else " "
        if not grid.has_wall((x, y), EAST):
            nx = x + 1
            if grid.in_bounds((nx, y)):
                both_path = (x, y) in path_set and (nx, y) in path_set
                canvas[cy][cx + 1] = "." if both_path else " "
        if not grid.has_wall((x, y), SOUTH):
            ny = y + 1
            if grid.in_bounds((x, ny)):
                both_path = (x, y) in path_set and (x, ny) in path_set
                canvas[cy + 1][cx] = "." if both_path else " "

    if path:
        sx, sy = path[0]
        canvas[sy * 2 + 1][sx * 2 + 1] = "S"
        gx, gy = path[-1]
        canvas[gy * 2 + 1][gx * 2 + 1] = "G"

    return "\n".join("".join(row) for row in canvas)


def render_table(results) -> str:
    header = f"{'algorithm':<10}{'path_len':>10}{'expanded':>10}"
    lines = [header, "-" * len(header)]
    for r in results:
        lines.append(f"{r.name:<10}{r.path_length:>10}{r.expanded:>10}")
    return "\n".join(lines)
