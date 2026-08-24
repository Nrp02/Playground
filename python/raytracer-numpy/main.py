from __future__ import annotations

import sys
import time

import numpy as np

from camera import Camera
from render import render
from scene import build_demo_scene


def write_ppm(path: str, image: np.ndarray) -> None:
    height, width, _ = image.shape
    data = (image * 255.999).astype(np.uint8)
    with open(path, "wb") as f:
        f.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        f.write(data.tobytes())


def main() -> None:
    output_path = sys.argv[1] if len(sys.argv) > 1 else "output.ppm"
    width = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    height = int(sys.argv[3]) if len(sys.argv) > 3 else 225
    samples = int(sys.argv[4]) if len(sys.argv) > 4 else 16
    max_depth = int(sys.argv[5]) if len(sys.argv) > 5 else 4

    print("Numpy ray tracer")
    print(f"  output:     {output_path}")
    print(f"  resolution: {width}x{height}")
    print(f"  samples:    {samples}")
    print(f"  max depth:  {max_depth}")

    scene = build_demo_scene()
    aspect_ratio = width / height
    camera = Camera(
        look_from=[0.0, 2.6, 9.0],
        look_at=[0.0, 0.75, 0.0],
        vup=[0.0, 1.0, 0.0],
        vfov_degrees=35.0,
        aspect_ratio=aspect_ratio,
    )

    print("Rendering...", end="", flush=True)
    start = time.perf_counter()
    image = render(scene, camera, width, height, samples, max_depth)
    elapsed = time.perf_counter() - start
    print(f" done in {elapsed:.2f}s")

    write_ppm(output_path, image)
    print(f"Wrote {output_path}")


if __name__ == "__main__":
    main()
