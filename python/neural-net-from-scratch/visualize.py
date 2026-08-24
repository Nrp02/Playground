"""
Terminal ASCII visualization of a trained 2D binary classifier's decision
surface -- lets you *see* what the network learned without pulling in a
plotting library. Evaluates the network over a grid spanning the data's
bounding box, shades each cell by predicted probability, then stamps the
actual data points on top by their true label so the learned boundary can
be compared against reality at a glance.
"""

from __future__ import annotations

import numpy as np

# Shading ramp from "confidently class 0" to "confidently class 1".
_SHADE_CHARS = " .:-=+*#%@"


def _shade_for_probability(p: float) -> str:
    idx = int(np.clip(p, 0.0, 1.0) * (len(_SHADE_CHARS) - 1))
    return _SHADE_CHARS[idx]


def _to_cell_index(value: float, lo: float, hi: float, n: int) -> int:
    """Map `value` in [lo, hi] to an integer cell index in [0, n - 1]."""
    frac = (value - lo) / (hi - lo) if hi > lo else 0.5
    return int(np.clip(round(frac * (n - 1)), 0, n - 1))


def render_decision_boundary(net, x: np.ndarray, y: np.ndarray,
                               width: int = 61, height: int = 25) -> str:
    """
    Render the network's decision surface as an ASCII grid over the
    bounding box of `x` (with a small margin), with the real data points
    overlaid as '0' / '1' by their true label.

    `net` must expose `.predict(x) -> (n, 1)` probabilities, matching
    network.Network. Assumes 2 input features.
    """
    mins = x.min(axis=0) - 0.5
    maxs = x.max(axis=0) + 0.5
    x_min, y_min = mins[0], mins[1]
    x_max, y_max = maxs[0], maxs[1]

    xs = np.linspace(x_min, x_max, width)
    ys = np.linspace(y_max, y_min, height)  # row 0 = top = highest y

    grid_points = np.array([[gx, gy] for gy in ys for gx in xs])
    probs = np.asarray(net.predict(grid_points)).reshape(height, width)

    grid = [[_shade_for_probability(p) for p in row] for row in probs]

    for point, label in zip(x, np.asarray(y).ravel()):
        col = _to_cell_index(point[0], x_min, x_max, width)
        row = (height - 1) - _to_cell_index(point[1], y_min, y_max, height)
        grid[row][col] = "1" if label >= 0.5 else "0"

    lines = ["".join(row) for row in grid]
    legend = (
        f"x in [{x_min:.2f}, {x_max:.2f}]   y in [{y_min:.2f}, {y_max:.2f}]\n"
        f"background '{_SHADE_CHARS[0]}'..'{_SHADE_CHARS[-1]}' = predicted P(class 1) low..high   "
        f"'0'/'1' = a real data point of that true class"
    )
    return "\n".join(lines) + "\n" + legend
