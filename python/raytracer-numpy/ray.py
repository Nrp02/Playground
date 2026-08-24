from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass
class Rays:
    origin: np.ndarray
    direction: np.ndarray

    def at(self, t: np.ndarray) -> np.ndarray:
        return self.origin + t[..., np.newaxis] * self.direction

    def __len__(self) -> int:
        return self.origin.shape[0]
