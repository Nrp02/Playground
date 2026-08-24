from __future__ import annotations

import numpy as np

import vec3


class Camera:
    def __init__(self, look_from, look_at, vup, vfov_degrees: float, aspect_ratio: float):
        self.look_from = np.asarray(look_from, dtype=np.float64)
        look_at = np.asarray(look_at, dtype=np.float64)
        vup = np.asarray(vup, dtype=np.float64)

        theta = np.radians(vfov_degrees)
        half_height = np.tan(theta / 2.0)
        half_width = aspect_ratio * half_height

        w = vec3.normalize(self.look_from - look_at)
        u = vec3.normalize(np.cross(vup, w))
        v = np.cross(w, u)

        self.horizontal = 2.0 * half_width * u
        self.vertical = 2.0 * half_height * v
        self.lower_left = self.look_from - half_width * u - half_height * v - w

    def get_rays(self, s: np.ndarray, t: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        n = s.shape[0]
        origin = np.tile(self.look_from, (n, 1))
        target = (self.lower_left
                  + s[:, np.newaxis] * self.horizontal
                  + t[:, np.newaxis] * self.vertical)
        direction = vec3.normalize(target - origin)
        return origin, direction
