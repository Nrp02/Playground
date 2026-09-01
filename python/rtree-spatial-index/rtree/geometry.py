from typing import Tuple

BBox = Tuple[float, float, float, float]
Point = Tuple[float, float]


def union(a: BBox, b: BBox) -> BBox:
    return (
        min(a[0], b[0]),
        min(a[1], b[1]),
        max(a[2], b[2]),
        max(a[3], b[3]),
    )


def union_many(boxes: Tuple[BBox, ...]) -> BBox:
    result = boxes[0]
    for b in boxes[1:]:
        result = union(result, b)
    return result


def area(b: BBox) -> float:
    return max(0.0, b[2] - b[0]) * max(0.0, b[3] - b[1])


def enlargement(b: BBox, other: BBox) -> float:
    return area(union(b, other)) - area(b)


def intersects(a: BBox, b: BBox) -> bool:
    return a[0] <= b[2] and b[0] <= a[2] and a[1] <= b[3] and b[1] <= a[3]


def contains(outer: BBox, inner: BBox) -> bool:
    return (
        outer[0] <= inner[0]
        and outer[1] <= inner[1]
        and outer[2] >= inner[2]
        and outer[3] >= inner[3]
    )


def min_dist_point_bbox(point: Point, b: BBox) -> float:
    px, py = point
    dx = 0.0
    if px < b[0]:
        dx = b[0] - px
    elif px > b[2]:
        dx = px - b[2]
    dy = 0.0
    if py < b[1]:
        dy = b[1] - py
    elif py > b[3]:
        dy = py - b[3]
    return dx * dx + dy * dy
